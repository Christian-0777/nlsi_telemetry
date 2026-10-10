#include <windows.h>
#include <psapi.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "logging/TelemetryRecorder.h"

namespace {

using Clock = std::chrono::steady_clock;

QJsonObject MakeSample(int index, const QString& mapping_base64) {
    const QDateTime timestamp(
        QDate(2026, 10, 7), QTime(16, 30, 0), QTimeZone::UTC);
    return {
        {QStringLiteral("timestamp_utc"),
            timestamp.addMSecs(index).toString(Qt::ISODateWithMs)},
        {QStringLiteral("session_id"), QStringLiteral("benchmark-session")},
        {QStringLiteral("provider"), QStringLiteral("TruckSim GPS")},
        {QStringLiteral("provider_revision"), 13},
        {QStringLiteral("raw_fields"), QJsonObject{
            {QStringLiteral("speed_mps"), 22.5},
            {QStringLiteral("engine_rpm"), 1450.0},
            {QStringLiteral("gear"), 12},
        }},
        {QStringLiteral("raw_availability"), QJsonObject{
            {QStringLiteral("speed_mps"), true},
            {QStringLiteral("engine_rpm"), true},
            {QStringLiteral("gear"), true},
        }},
        {QStringLiteral("normalized_fields"), QJsonObject{
            {QStringLiteral("speed_kmh"), 81.0},
            {QStringLiteral("rpm"), 1450.0},
        }},
        {QStringLiteral("raw_mapping_encoding"), QStringLiteral("qcompress+base64")},
        {QStringLiteral("raw_mapping_uncompressed_bytes"), 32 * 1024},
        {QStringLiteral("raw_mapping_base64"), mapping_base64},
    };
}

bool ParsePositiveInteger(const char* text, int* value) {
    const char* end = text;
    while (*end != '\0') {
        ++end;
    }
    const auto [parsed_end, error] = std::from_chars(text, end, *value);
    return error == std::errc{} && parsed_end == end && *value > 0;
}

std::uint64_t CountPersistedRecords(const QString& telemetry_directory) {
    std::uint64_t count = 0;
    const QFileInfoList files = QDir(telemetry_directory).entryInfoList(
        {QStringLiteral("*.nlsi")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return UINT64_MAX;
        }
        if (!file.readLine().endsWith('\n')) {
            return UINT64_MAX;
        }
        while (!file.atEnd()) {
            const QByteArray line = file.readLine();
            if (!line.endsWith('\n')
                || QJsonDocument::fromJson(line.trimmed()).object()
                    .value(QStringLiteral("record_type")).toString()
                    != QStringLiteral("telemetry_sample")) {
                return UINT64_MAX;
            }
            ++count;
        }
    }
    return count;
}

std::uint64_t PeakPagefileBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(
        GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters))
        ? static_cast<std::uint64_t>(counters.PeakPagefileUsage)
        : 0;
}

std::string ToNarrow(const std::wstring& text) {
    return std::string(text.begin(), text.end());
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    int duration_seconds = 60;
    int records_per_second = 16;
    if ((argc > 1 && !ParsePositiveInteger(argv[1], &duration_seconds))
        || (argc > 2 && !ParsePositiveInteger(argv[2], &records_per_second))
        || argc > 3) {
        std::cerr << "Usage: nlsi_telemetry_benchmark [duration-seconds] [records-per-second]\n";
        return 2;
    }

    QTemporaryDir root;
    if (!root.isValid()) {
        std::cerr << "Could not create temporary benchmark storage.\n";
        return 1;
    }

    QByteArray mapping(32 * 1024, Qt::Uninitialized);
    for (qsizetype index = 0; index < mapping.size(); ++index) {
        mapping[index] = static_cast<char>((index * 31 + index / 7) % 251);
    }
    const QString mapping_base64 = QString::fromLatin1(qCompress(mapping, 9).toBase64());
    const QJsonObject sample = MakeSample(0, mapping_base64);
    const qsizetype sample_bytes = QJsonDocument(sample).toJson(QJsonDocument::Compact).size();

    nlsi::logging::TelemetryRecorder recorder;
    if (!recorder.Start(root.path().toStdWString())) {
        std::cerr << "Could not start the telemetry recorder.\n";
        return 1;
    }

    const int target_records = duration_seconds * records_per_second;
    std::uint64_t maximum_queue_depth = 0;
    std::uint64_t maximum_queued_bytes_estimate = 0;
    std::chrono::milliseconds maximum_oldest_pending_age{0};
    std::chrono::nanoseconds total_enqueue_time{};
    std::chrono::nanoseconds maximum_enqueue_time{};
    const auto producer_start = Clock::now();

    for (int index = 0; index < target_records; ++index) {
        const auto scheduled = producer_start
            + std::chrono::nanoseconds(
                (static_cast<std::int64_t>(index) * 1'000'000'000LL)
                / records_per_second);
        std::this_thread::sleep_until(scheduled);
        const auto enqueue_start = Clock::now();
        if (!recorder.Enqueue(MakeSample(index, mapping_base64))) {
            const std::wstring error = recorder.LastError();
            std::cerr << "Recorder rejected sample " << index << ": "
                << ToNarrow(error) << '\n';
            recorder.Stop();
            return 1;
        }
        const auto enqueue_time = Clock::now() - enqueue_start;
        total_enqueue_time += enqueue_time;
        maximum_enqueue_time = std::max(
            maximum_enqueue_time,
            std::chrono::duration_cast<std::chrono::nanoseconds>(enqueue_time));
        const std::uint64_t queue_depth = recorder.QueuedCount();
        maximum_queue_depth = std::max(maximum_queue_depth, queue_depth);
        maximum_queued_bytes_estimate = std::max(
            maximum_queued_bytes_estimate,
            queue_depth * static_cast<std::uint64_t>(sample_bytes));
        maximum_oldest_pending_age = std::max(
            maximum_oldest_pending_age,
            recorder.GetMetrics().oldest_pending_age);
    }

    const auto producer_end = Clock::now();
    const auto drain_start = producer_end;
    if (!recorder.FlushFor(std::chrono::minutes(2))) {
        std::cerr << "Recorder did not flush: " << ToNarrow(recorder.LastError()) << '\n';
        recorder.Stop();
        return 1;
    }
    const auto drain_end = Clock::now();
    const bool stopped = recorder.StopFor(std::chrono::seconds(10));
    if (!stopped || recorder.IsRunning()) {
        std::cerr << "Recorder did not stop cleanly.\n";
        return 1;
    }

    const QString telemetry_directory = QDir(root.path()).filePath(QStringLiteral("telemetry"));
    const std::uint64_t persisted = CountPersistedRecords(telemetry_directory);
    if (persisted != static_cast<std::uint64_t>(target_records)) {
        std::cerr << "Persisted record count mismatch: accepted " << target_records
            << ", found " << persisted << ".\n";
        return 1;
    }

    const double producer_seconds =
        std::chrono::duration<double>(producer_end - producer_start).count();
    const double total_seconds =
        std::chrono::duration<double>(drain_end - producer_start).count();
    const double drain_seconds =
        std::chrono::duration<double>(drain_end - drain_start).count();
    const double average_enqueue_microseconds =
        std::chrono::duration<double, std::micro>(total_enqueue_time).count() / target_records;
    const nlsi::logging::TelemetryRecorder::Metrics metrics = recorder.GetMetrics();
    const double average_batch_write_microseconds = metrics.batches_written == 0
        ? 0.0
        : std::chrono::duration<double, std::micro>(metrics.total_batch_write_time).count()
            / metrics.batches_written;

    std::cout
        << "workload_seconds=" << duration_seconds
        << " target_records_per_second=" << records_per_second
        << " accepted=" << target_records
        << " persisted=" << persisted
        << " producer_seconds=" << producer_seconds
        << " end_to_end_records_per_second=" << target_records / total_seconds
        << " producer_records_per_second=" << target_records / producer_seconds
        << " drain_seconds=" << drain_seconds
        << " maximum_queue_depth=" << maximum_queue_depth
        << " estimated_maximum_queued_bytes=" << maximum_queued_bytes_estimate
        << " maximum_reported_queue_bytes=" << metrics.maximum_queue_bytes
        << " maximum_oldest_pending_age_ms=" << maximum_oldest_pending_age.count()
        << " sample_json_bytes=" << sample_bytes
        << " average_enqueue_microseconds=" << average_enqueue_microseconds
        << " worst_enqueue_microseconds="
        << std::chrono::duration<double, std::micro>(maximum_enqueue_time).count()
        << " batches_written=" << metrics.batches_written
        << " maximum_batch_size=" << metrics.maximum_batch_size
        << " average_batch_write_microseconds=" << average_batch_write_microseconds
        << " worst_batch_write_microseconds="
        << std::chrono::duration<double, std::micro>(metrics.maximum_batch_write_time).count()
        << " write_failures=" << metrics.write_failures
        << " peak_pagefile_bytes=" << PeakPagefileBytes()
        << '\n';
    return 0;
}
