#include "TelemetryRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>
#include <QByteArray>
#include <QtEndian>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "time/ApplicationTime.h"

namespace {

constexpr qsizetype kMaximumSampleBytes = 1024 * 1024;
constexpr qsizetype kMaximumQueueLineBytes = 4096;
constexpr qint64 kMaximumFileBytes = 128LL * 1024 * 1024;
constexpr qint64 kMaximumPendingFileBytes =
    65LL * 1024 * 1024;
constexpr std::size_t kMaximumQueuedSamples = 2048;
constexpr std::uint64_t kMaximumQueuedBytes = 64ULL * 1024 * 1024;
constexpr std::size_t kMaximumBatchSamples = 128;
constexpr auto kMaximumBatchDelay = std::chrono::milliseconds(250);

QJsonObject Header() {
    return {
        {QStringLiteral("format"), QStringLiteral("nlsi-telemetry")},
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("record_type"), QStringLiteral("header")},
        {QStringLiteral("provider"), QStringLiteral("TruckSim GPS")},
    };
}

bool ParseObject(const QByteArray& line, QJsonObject* object) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    *object = document.object();
    return true;
}

bool IsTimestamp(const QString& timestamp) {
    const QDateTime parsed = QDateTime::fromString(timestamp, Qt::ISODateWithMs);
    return parsed.isValid() && parsed.offsetFromUtc() == 0 && timestamp.endsWith(QLatin1Char('Z'));
}

bool IsRawMappingValid(const QJsonObject& sample) {
    if (sample.value(QStringLiteral("raw_mapping_encoding")).toString()
            != QStringLiteral("qcompress+base64")
        || sample.value(QStringLiteral("raw_mapping_uncompressed_bytes")).toInt() != 32 * 1024) {
        return false;
    }
    const QByteArray encoded = sample.value(QStringLiteral("raw_mapping_base64"))
        .toString().toLatin1();
    const QByteArray compressed = QByteArray::fromBase64(
        encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (compressed.size() < 4 || compressed.size() > 64 * 1024
        || qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(compressed.constData())) != 32 * 1024) {
        return false;
    }
    return qUncompress(compressed).size() == 32 * 1024;
}

bool IsRawSampleValid(const QJsonObject& sample, QByteArray* serialized = nullptr) {
    const QByteArray encoded = QJsonDocument(sample).toJson(QJsonDocument::Compact);
    const bool valid = !encoded.isEmpty() && encoded.size() <= kMaximumSampleBytes
        && IsTimestamp(sample.value(QStringLiteral("timestamp_utc")).toString())
        && sample.value(QStringLiteral("provider")).toString()
            == QStringLiteral("TruckSim GPS")
        && sample.value(QStringLiteral("provider_revision")).toInt() == 13
        && !sample.value(QStringLiteral("raw_fields")).toObject().isEmpty()
        && sample.value(QStringLiteral("raw_availability")).isObject()
        && sample.value(QStringLiteral("normalized_fields")).isObject()
        && IsRawMappingValid(sample);
    if (valid && serialized) {
        *serialized = encoded;
    }
    return valid;
}

bool IsStoredSampleValid(const QJsonObject& record) {
    const QString record_id = record.value(QStringLiteral("record_id")).toString();
    return record.value(QStringLiteral("record_type")).toString()
            == QStringLiteral("telemetry_sample")
        && record.value(QStringLiteral("schema_version")).toInt() == 2
        && !QUuid(record_id).isNull()
        && record.value(QStringLiteral("sequence")).toVariant().toULongLong() > 0
        && IsTimestamp(record.value(QStringLiteral("timestamp_utc")).toString())
        && record.value(QStringLiteral("provider")).toString()
            == QStringLiteral("TruckSim GPS")
        && record.value(QStringLiteral("provider_revision")).toInt() == 13
        && !record.value(QStringLiteral("raw_fields")).toObject().isEmpty()
        && record.value(QStringLiteral("raw_availability")).isObject()
        && record.value(QStringLiteral("normalized_fields")).isObject()
        && IsRawMappingValid(record);
}

} // namespace

namespace nlsi::logging {

TelemetryRecorder::TelemetryRecorder(std::function<void()> before_write)
    : before_write_(std::move(before_write)) {
}

TelemetryRecorder::~TelemetryRecorder() {
    Stop();
}

bool TelemetryRecorder::Start(
    const std::wstring& user_data_directory,
    std::wstring* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    if (worker_.joinable()) {
        const QString reason = QStringLiteral(
            "The previous local telemetry writer has not been joined.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason);
        return false;
    }
    root_ = QString::fromStdWString(user_data_directory);
    telemetry_directory_ = QDir(root_).filePath(QStringLiteral("telemetry"));
    pending_directory_ = QDir(telemetry_directory_).filePath(QStringLiteral("pending"));
    sync_queue_path_ = QDir(root_).filePath(QStringLiteral("sync/queue.jsonl"));
    if (!QDir().mkpath(telemetry_directory_)
        || !QDir().mkpath(pending_directory_)
        || !QDir().mkpath(QFileInfo(sync_queue_path_).absolutePath())) {
        const QString reason = QStringLiteral("Could not create local telemetry storage under %1.")
            .arg(root_);
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason);
        return false;
    }
    stopping_ = false;
    ready_ = false;
    queue_.clear();
    queued_bytes_ = 0;
    writing_count_ = 0;
    metrics_ = {};
    pending_count_ = 0;
    next_sequence_ = 1;
    next_pending_batch_sequence_ = 1;
    writer_failed_ = false;
    {
        std::lock_guard<std::mutex> error_lock(error_mutex_);
        last_error_.clear();
    }
    running_ = true;
    worker_ = std::thread(&TelemetryRecorder::WriteLoop, this);
    return true;
}

bool TelemetryRecorder::Enqueue(const QJsonObject& sample, std::wstring* error) {
    QJsonObject queued_sample = sample;
    const QString record_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    queued_sample.insert(QStringLiteral("record_id"), record_id);
    QByteArray serialized_sample;
    if (!IsRawSampleValid(queued_sample, &serialized_sample)) {
        const QString reason = QStringLiteral("Rejected malformed or oversized raw telemetry sample.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason, false);
        return false;
    }
    if (serialized_sample.size() > kMaximumSampleBytes - 128) {
        const QString reason = QStringLiteral(
            "Telemetry sample leaves insufficient space for required storage metadata.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason, false);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        const QString reason = QStringLiteral("Local telemetry recorder is not running.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason, false);
        return false;
    }
    const std::uint64_t serialized_bytes =
        static_cast<std::uint64_t>(serialized_sample.size());
    const std::uint64_t queue_count =
        static_cast<std::uint64_t>(queue_.size() + writing_count_);
    if (queue_count >= kMaximumQueuedSamples
        || serialized_bytes > kMaximumQueuedBytes
        || queued_bytes_ > kMaximumQueuedBytes - serialized_bytes) {
        const QString reason = QStringLiteral(
            "Local telemetry write queue reached its 2,048-record or 64 MiB limit; "
            "the sample was not accepted.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason, false);
        return false;
    }
    const QString pending_path;
    queue_.push_back({
        std::move(queued_sample),
        pending_path,
        serialized_bytes,
        std::chrono::steady_clock::now(),
    });
    queued_bytes_ += serialized_bytes;
    ++metrics_.accepted_records;
    metrics_.queued_records = queue_.size() + writing_count_;
    metrics_.queued_bytes = queued_bytes_;
    metrics_.maximum_queue_depth =
        std::max(metrics_.maximum_queue_depth, metrics_.queued_records);
    metrics_.maximum_queue_bytes =
        std::max(metrics_.maximum_queue_bytes, metrics_.queued_bytes);
    condition_.notify_one();
    return true;
}

bool TelemetryRecorder::Flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] {
        return (ready_ && queue_.empty() && writing_count_ == 0) || !running_;
    });
    return !writer_failed_;
}

bool TelemetryRecorder::FlushFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool drained = condition_.wait_for(lock, timeout, [this] {
        return (ready_ && queue_.empty() && writing_count_ == 0) || !running_;
    });
    lock.unlock();
    return drained && !writer_failed_;
}

void TelemetryRecorder::RequestStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    condition_.notify_all();
}

bool TelemetryRecorder::StopFor(std::chrono::milliseconds timeout) {
    RequestStop();
    std::unique_lock<std::mutex> lock(mutex_);
    const bool stopped = condition_.wait_for(lock, timeout, [this] {
        return !running_;
    });
    lock.unlock();
    if (stopped && worker_.joinable()) {
        worker_.join();
    }
    return stopped && !writer_failed_;
}

void TelemetryRecorder::Stop() {
    RequestStop();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}

std::wstring TelemetryRecorder::LastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

std::uint64_t TelemetryRecorder::PendingCount() const {
    return pending_count_.load();
}

std::uint64_t TelemetryRecorder::QueuedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::uint64_t>(queue_.size() + writing_count_);
}

TelemetryRecorder::Metrics TelemetryRecorder::GetMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Metrics result = metrics_;
    result.queued_records = static_cast<std::uint64_t>(queue_.size() + writing_count_);
    result.queued_bytes = queued_bytes_;
    if (result.queued_records > 0) {
        auto oldest = writing_count_ > 0
            ? writing_oldest_enqueue_at_
            : std::chrono::steady_clock::time_point::max();
        if (!queue_.empty()) {
            oldest = std::min(oldest, queue_.front().enqueued_at);
        }
        result.oldest_pending_age = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - oldest);
    }
    return result;
}

bool TelemetryRecorder::IsRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void TelemetryRecorder::WriteLoop() {
    if (!Recover()) {
        CloseFiles();
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        running_ = false;
        condition_.notify_all();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = true;
        condition_.notify_all();
    }

    while (true) {
        std::vector<QueuedSample> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty() && stopping_) {
                break;
            }
            const auto deadline = std::chrono::steady_clock::now() + kMaximumBatchDelay;
            condition_.wait_until(lock, deadline, [this] {
                return stopping_ || queue_.size() >= kMaximumBatchSamples;
            });
            const std::size_t batch_size =
                std::min(queue_.size(), kMaximumBatchSamples);
            batch.reserve(batch_size);
            for (std::size_t index = 0; index < batch_size; ++index) {
                batch.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
            writing_count_ = batch.size();
            writing_oldest_enqueue_at_ = batch.front().enqueued_at;
            metrics_.queued_records = queue_.size() + writing_count_;
        }
        if (before_write_) {
            before_write_();
        }
        const auto write_started = std::chrono::steady_clock::now();
        bool written = PersistPendingBatch(batch) && WriteBatch(batch);
        if (written) {
            QSet<QString> recovery_paths;
            for (const QueuedSample& sample : batch) {
                if (!sample.pending_path.isEmpty()) {
                    recovery_paths.insert(sample.pending_path);
                }
            }
            for (const QString& recovery_path : recovery_paths) {
                if (!QFile::remove(recovery_path)) {
                    SetError(QStringLiteral(
                        "Telemetry was appended, but its local recovery copy could not be "
                        "removed: %1").arg(recovery_path));
                    written = false;
                    break;
                }
            }
        }
        const auto write_duration = std::chrono::steady_clock::now() - write_started;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            writing_count_ = 0;
            if (!written) {
                for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                    queue_.push_front(std::move(*it));
                }
                ++metrics_.write_failures;
                stopping_ = true;
            } else {
                std::uint64_t batch_bytes = 0;
                for (const QueuedSample& sample : batch) {
                    batch_bytes += sample.serialized_bytes;
                }
                queued_bytes_ -= batch_bytes;
                metrics_.persisted_records += batch.size();
                ++metrics_.batches_written;
                metrics_.maximum_batch_size =
                    std::max(metrics_.maximum_batch_size, batch.size());
                metrics_.total_batch_write_time +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(write_duration);
                metrics_.maximum_batch_write_time = std::max(
                    metrics_.maximum_batch_write_time,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(write_duration));
            }
            metrics_.queued_records = queue_.size();
            metrics_.queued_bytes = queued_bytes_;
            condition_.notify_all();
        }
        if (!written) {
            break;
        }
    }
    CloseFiles();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    condition_.notify_all();
}

bool TelemetryRecorder::Recover() {
    QDir directory(telemetry_directory_);
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.nlsi")}, QDir::Files, QDir::Name);
    QSet<QString> record_ids;
    for (const QFileInfo& file : files) {
        if (!RecoverTelemetryFile(file.absoluteFilePath().toStdWString())) {
            return false;
        }
        QFile input(file.absoluteFilePath());
        if (!input.open(QIODevice::ReadOnly)) {
            SetError(QStringLiteral("Could not scan local telemetry file %1: %2")
                .arg(file.fileName(), input.errorString()));
            return false;
        }
        input.readLine();
        while (!input.atEnd()) {
            QJsonObject record;
            if (!ParseObject(input.readLine().trimmed(), &record)) {
                SetError(QStringLiteral("Could not parse recovered telemetry file %1.")
                    .arg(file.fileName()));
                return false;
            }
            if (record.value(QStringLiteral("record_type")).toString()
                    == QStringLiteral("telemetry_sample")) {
                const std::uint64_t sequence = record.value(QStringLiteral("sequence")).toVariant()
                    .toULongLong();
                next_sequence_ = std::max(next_sequence_, sequence + 1);
                const QString record_id = record.value(QStringLiteral("record_id")).toString();
                if (record_id.isEmpty()) {
                    SetError(QStringLiteral("A telemetry record has no stable identifier in %1.")
                        .arg(file.fileName()));
                    return false;
                }
                record_ids.insert(record_id);
            }
        }
    }
    return ReconcileSyncQueue(record_ids) && RecoverPendingSamples(record_ids);
}

bool TelemetryRecorder::PersistPendingBatch(std::vector<QueuedSample>& samples) {
    QJsonArray pending_samples;
    for (const QueuedSample& sample : samples) {
        if (sample.pending_path.isEmpty()) {
            pending_samples.append(sample.sample);
        }
    }
    if (pending_samples.isEmpty()) {
        return true;
    }

    const std::uint64_t batch_sequence = next_pending_batch_sequence_;
    if (batch_sequence == 0
        || batch_sequence >= static_cast<std::uint64_t>(
            std::numeric_limits<qint64>::max())) {
        SetError(QStringLiteral("Local pending telemetry batch sequence is exhausted."));
        return false;
    }
    const QString recovery_path = QDir(pending_directory_).filePath(
        QStringLiteral("z-batch-%1-%2.json")
            .arg(static_cast<qulonglong>(batch_sequence), 20, 10, QLatin1Char('0'))
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QSaveFile file(recovery_path);
    const QJsonObject pending_record{
        {QStringLiteral("format"), QStringLiteral("nlsi-pending-samples")},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("batch_sequence"), static_cast<qint64>(batch_sequence)},
        {QStringLiteral("samples"), pending_samples},
    };
    const QByteArray bytes = QJsonDocument(pending_record).toJson(QJsonDocument::Compact) + '\n';
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size()
        || !file.commit()) {
        SetError(QStringLiteral(
            "Could not atomically preserve accepted telemetry batch in %1: %2")
            .arg(recovery_path, file.errorString()));
        return false;
    }
    ++next_pending_batch_sequence_;
    for (QueuedSample& sample : samples) {
        if (sample.pending_path.isEmpty()) {
            sample.pending_path = recovery_path;
        }
    }
    return true;
}

bool TelemetryRecorder::RecoverPendingSamples(const QSet<QString>& record_ids) {
    QDir directory(pending_directory_);
    const QFileInfoList files = directory.entryInfoList(
        {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    std::deque<QueuedSample> recovered;
    for (const QFileInfo& file_info : files) {
        if (file_info.size() > kMaximumPendingFileBytes) {
            SetError(QStringLiteral(
                "Pending telemetry recovery file %1 exceeds the 65 MiB recovery limit; "
                "it was preserved.")
                .arg(file_info.absoluteFilePath()));
            return false;
        }
        QFile file(file_info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            SetError(QStringLiteral("Could not read pending telemetry recovery file %1: %2")
                .arg(file_info.absoluteFilePath(), file.errorString()));
            return false;
        }
        QJsonObject object;
        if (!ParseObject(file.readAll().trimmed(), &object)
            || object.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
            SetError(QStringLiteral("Malformed pending telemetry recovery file %1; it was preserved.")
                .arg(file_info.absoluteFilePath()));
            return false;
        }
        const QString format = object.value(QStringLiteral("format")).toString();
        QJsonArray samples;
        if (format == QStringLiteral("nlsi-pending-sample")
            && object.value(QStringLiteral("sample")).isObject()) {
            samples.append(object.value(QStringLiteral("sample")));
        } else if (format == QStringLiteral("nlsi-pending-samples")
            && object.value(QStringLiteral("samples")).isArray()) {
            const std::uint64_t batch_sequence = object.value(
                QStringLiteral("batch_sequence")).toVariant().toULongLong();
            if (batch_sequence == 0
                || batch_sequence >= static_cast<std::uint64_t>(
                    std::numeric_limits<qint64>::max())) {
                SetError(QStringLiteral(
                    "Invalid pending telemetry batch order in %1; it was preserved.")
                    .arg(file_info.absoluteFilePath()));
                return false;
            }
            next_pending_batch_sequence_ = std::max(
                next_pending_batch_sequence_, batch_sequence + 1);
            samples = object.value(QStringLiteral("samples")).toArray();
        } else {
            SetError(QStringLiteral("Malformed pending telemetry recovery file %1; it was preserved.")
                .arg(file_info.absoluteFilePath()));
            return false;
        }
        if (samples.isEmpty()) {
            SetError(QStringLiteral("Empty pending telemetry recovery file %1; it was preserved.")
                .arg(file_info.absoluteFilePath()));
            return false;
        }

        std::uint64_t recovered_bytes = 0;
        QSet<QString> batch_record_ids;
        for (const QJsonValue& sample_value : samples) {
            if (!sample_value.isObject()) {
                SetError(QStringLiteral(
                    "Invalid pending telemetry recovery file %1; it was preserved.")
                    .arg(file_info.absoluteFilePath()));
                return false;
            }
            QJsonObject sample = sample_value.toObject();
            const QString record_id = sample.value(QStringLiteral("record_id")).toString();
            sample.remove(QStringLiteral("record_id"));
            if (QUuid(record_id).isNull() || !IsRawSampleValid(sample)) {
                SetError(QStringLiteral(
                    "Invalid pending telemetry recovery file %1; it was preserved.")
                    .arg(file_info.absoluteFilePath()));
                return false;
            }
            if (batch_record_ids.contains(record_id)) {
                SetError(QStringLiteral(
                    "Duplicate record ID in pending telemetry recovery file %1; it was preserved.")
                    .arg(file_info.absoluteFilePath()));
                return false;
            }
            batch_record_ids.insert(record_id);
            sample.insert(QStringLiteral("record_id"), record_id);
            if (record_ids.contains(record_id)) {
                continue;
            }
            const std::uint64_t serialized_bytes = static_cast<std::uint64_t>(
                QJsonDocument(sample).toJson(QJsonDocument::Compact).size());
            if (recovered.size() >= kMaximumQueuedSamples
                || serialized_bytes > kMaximumQueuedBytes
                || recovered_bytes > kMaximumQueuedBytes - serialized_bytes) {
                SetError(QStringLiteral(
                    "Pending telemetry exceeds the 2,048-record or 64 MiB recovery capacity; "
                    "recovery files were preserved."));
                return false;
            }
            recovered_bytes += serialized_bytes;
            recovered.push_back({
                std::move(sample),
                file_info.absoluteFilePath(),
                serialized_bytes,
                std::chrono::steady_clock::now(),
            });
        }
        if (recovered_bytes == 0 && !QFile::remove(file_info.absoluteFilePath())) {
            SetError(QStringLiteral(
                "A recovered telemetry record is already persisted, but its recovery file "
                "could not be removed: %1").arg(file_info.absoluteFilePath()));
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t recovered_bytes = 0;
    for (const QueuedSample& sample : recovered) {
        recovered_bytes += sample.serialized_bytes;
    }
    if (queue_.size() + recovered.size() > kMaximumQueuedSamples
        || recovered_bytes > kMaximumQueuedBytes
        || queued_bytes_ > kMaximumQueuedBytes - recovered_bytes) {
        SetError(QStringLiteral(
            "The local telemetry recovery queue exceeds its 2,048-record or 64 MiB capacity; "
            "recovery files were preserved."));
        return false;
    }
    while (!recovered.empty()) {
        queue_.push_front(std::move(recovered.back()));
        recovered.pop_back();
    }
    queued_bytes_ += recovered_bytes;
    metrics_.queued_records = queue_.size();
    metrics_.queued_bytes = queued_bytes_;
    metrics_.maximum_queue_depth =
        std::max(metrics_.maximum_queue_depth, metrics_.queued_records);
    metrics_.maximum_queue_bytes =
        std::max(metrics_.maximum_queue_bytes, metrics_.queued_bytes);
    condition_.notify_all();
    return true;
}

bool TelemetryRecorder::RecoverTelemetryFile(const std::wstring& path) {
    const QString file_path = QString::fromStdWString(path);
    QFile file(file_path);
    if (!file.open(QIODevice::ReadWrite)) {
        SetError(QStringLiteral("Could not open local telemetry file %1: %2")
            .arg(file_path, file.errorString()));
        return false;
    }
    if (file.size() == 0 || !file.seek(0)) {
        SetError(QStringLiteral("Local telemetry file %1 is empty; it was preserved.")
            .arg(file_path));
        return false;
    }
    const QByteArray header_line = file.readLine();
    QJsonObject header;
    if (!header_line.endsWith('\n') || !ParseObject(header_line.trimmed(), &header)
        || header.value(QStringLiteral("format")).toString() != QStringLiteral("nlsi-telemetry")
        || header.value(QStringLiteral("schema_version")).toInt(-1) != 2
        || header.value(QStringLiteral("record_type")).toString() != QStringLiteral("header")) {
        SetError(QStringLiteral("Unsupported or malformed telemetry header in %1.")
            .arg(file_path));
        return false;
    }

    qsizetype line_number = 1;
    while (!file.atEnd()) {
        const qint64 line_start = file.pos();
        const QByteArray line = file.readLine(kMaximumSampleBytes + 2);
        if (!line.endsWith('\n')) {
            if (!file.atEnd()) {
                SetError(QStringLiteral("Oversized telemetry record in %1; the file was preserved.")
                    .arg(file_path));
                return false;
            }
            QFile recovery(file_path + QStringLiteral(".recovery"));
            if (!recovery.open(QIODevice::WriteOnly | QIODevice::Append)
                || recovery.write(line) != line.size() || !recovery.flush()) {
                SetError(QStringLiteral("Could not preserve incomplete telemetry tail from %1.")
                    .arg(file_path));
                return false;
            }
            if (!file.resize(line_start)) {
                SetError(QStringLiteral("Could not recover incomplete telemetry tail in %1.")
                    .arg(file_path));
                return false;
            }
            break;
        }
        ++line_number;
        QJsonObject record;
        if (line.size() > kMaximumSampleBytes
            || !ParseObject(line.trimmed(), &record)
            || !IsStoredSampleValid(record)) {
            SetError(QStringLiteral("Malformed telemetry record %1 in %2; the file was preserved.")
                .arg(line_number).arg(file_path));
            return false;
        }
    }
    return true;
}

bool TelemetryRecorder::ReconcileSyncQueue(const QSet<QString>& record_ids) {
    QSet<QString> known_ids;
    QFile queue(sync_queue_path_);
    if (queue.exists()) {
        if (!queue.open(QIODevice::ReadWrite)) {
            SetError(QStringLiteral("Could not read local sync queue %1: %2")
                .arg(sync_queue_path_, queue.errorString()));
            return false;
        }
        while (!queue.atEnd()) {
            const qint64 line_start = queue.pos();
            const QByteArray line = queue.readLine(kMaximumQueueLineBytes + 2);
            if (!line.endsWith('\n')) {
                if (!queue.atEnd()) {
                    SetError(QStringLiteral("Oversized sync queue record in %1; the queue was preserved.")
                        .arg(sync_queue_path_));
                    return false;
                }
                QFile recovery(sync_queue_path_ + QStringLiteral(".recovery"));
                if (!recovery.open(QIODevice::WriteOnly | QIODevice::Append)
                    || recovery.write(line) != line.size() || !recovery.flush()) {
                    SetError(QStringLiteral("Could not preserve incomplete local sync queue tail."));
                    return false;
                }
                if (!queue.resize(line_start)) {
                    SetError(QStringLiteral("Could not recover local sync queue %1.")
                        .arg(sync_queue_path_));
                    return false;
                }
                break;
            }
            QJsonObject object;
            if (!ParseObject(line.trimmed(), &object)) {
                SetError(QStringLiteral("Malformed record in local sync queue %1.")
                    .arg(sync_queue_path_));
                return false;
            }
            const QString record_id = object.value(QStringLiteral("record_id")).toString();
            const QString state = object.value(QStringLiteral("state")).toString();
            if (QUuid(record_id).isNull()
                || (state != QStringLiteral("pending")
                    && state != QStringLiteral("syncing")
                    && state != QStringLiteral("synced")
                    && state != QStringLiteral("error"))) {
                SetError(QStringLiteral("Invalid record in local sync queue %1.")
                    .arg(sync_queue_path_));
                return false;
            }
            known_ids.insert(record_id);
        }
        queue.close();
    }

    QFile append_queue(sync_queue_path_);
    QByteArray pending_lines;
    if (!record_ids.isEmpty()
        && !append_queue.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        SetError(QStringLiteral("Could not open local synchronization queue %1: %2")
            .arg(sync_queue_path_, append_queue.errorString()));
        return false;
    }
    std::uint64_t pending = 0;
    for (const QString& record_id : record_ids) {
        if (!known_ids.contains(record_id)) {
            const QJsonObject item{
                {QStringLiteral("record_id"), record_id},
                {QStringLiteral("state"), QStringLiteral("pending")},
            };
            pending_lines.append(QJsonDocument(item).toJson(QJsonDocument::Compact));
            pending_lines.append('\n');
            if (pending_lines.size() >= 64 * 1024
                && append_queue.write(pending_lines) != pending_lines.size()) {
                SetError(QStringLiteral("Could not repair local synchronization queue %1: %2")
                    .arg(sync_queue_path_, append_queue.errorString()));
                return false;
            }
            if (pending_lines.size() >= 64 * 1024) {
                pending_lines.clear();
            }
            known_ids.insert(record_id);
        }
        ++pending;
    }
    if (append_queue.isOpen()
        && (append_queue.write(pending_lines) != pending_lines.size()
            || !append_queue.flush())) {
        SetError(QStringLiteral("Could not repair local synchronization queue %1: %2")
            .arg(sync_queue_path_, append_queue.errorString()));
        return false;
    }
    pending_count_ = pending;
    return true;
}

bool TelemetryRecorder::WriteBatch(std::vector<QueuedSample>& samples) {
    const QTimeZone manila = nlsi::time::Zone();
    if (!manila.isValid()) {
        SetError(QStringLiteral(
            "IANA time-zone data for Asia/Manila is unavailable; telemetry was not written."));
        return false;
    }

    for (QueuedSample& queued_sample : samples) {
        QJsonObject& sample = queued_sample.sample;
        const QDateTime date_time = QDateTime::fromString(
            sample.value(QStringLiteral("timestamp_utc")).toString(),
            Qt::ISODateWithMs).toUTC();
        if (!date_time.isValid()) {
            SetError(QStringLiteral("Telemetry sample contains an invalid UTC timestamp."));
            return false;
        }
        const QString base_name = date_time.toTimeZone(manila)
            .toString(QStringLiteral("yyyy-MM-dd"));
        QString file_path = QDir(telemetry_directory_)
            .filePath(base_name + QStringLiteral(".nlsi"));
        int rotation = 0;
        while (true) {
            const qint64 file_size = telemetry_file_path_ == file_path
                ? telemetry_file_size_
                : QFileInfo(file_path).size();
            if (!QFileInfo::exists(file_path) || file_size < kMaximumFileBytes) {
                break;
            }
            ++rotation;
            file_path = QDir(telemetry_directory_).filePath(
                QStringLiteral("%1-%2.nlsi")
                    .arg(base_name)
                    .arg(rotation, 3, 10, QLatin1Char('0')));
        }
        if (!OpenTelemetryFile(file_path)) {
            return false;
        }

        const QString record_id = sample.value(QStringLiteral("record_id")).toString();
        if (QUuid(record_id).isNull()) {
            SetError(QStringLiteral("Telemetry recovery record has an invalid stable identifier."));
            return false;
        }
        if (next_sequence_ == 0
            || next_sequence_ >= static_cast<std::uint64_t>(
                std::numeric_limits<qint64>::max())) {
            SetError(QStringLiteral("Local telemetry sequence is exhausted."));
            return false;
        }
        sample.insert(QStringLiteral("record_type"), QStringLiteral("telemetry_sample"));
        sample.insert(QStringLiteral("schema_version"), 2);
        sample.insert(QStringLiteral("sequence"), static_cast<qint64>(next_sequence_++));
        const QByteArray line = QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n';
        if (line.size() > kMaximumSampleBytes
            || telemetry_file_.write(line) != line.size()) {
            SetError(QStringLiteral("Could not append telemetry batch to %1: %2")
                .arg(file_path, telemetry_file_.errorString()));
            return false;
        }
        telemetry_file_size_ += line.size();
    }

    if (telemetry_file_.isOpen() && !telemetry_file_.flush()) {
        SetError(QStringLiteral("Could not flush telemetry batch to %1: %2")
            .arg(telemetry_file_path_, telemetry_file_.errorString()));
        return false;
    }
    if (!AppendPendingBatch(samples)) {
        return false;
    }
    pending_count_ += samples.size();
    return true;
}

bool TelemetryRecorder::OpenTelemetryFile(const QString& file_path) {
    if (telemetry_file_.isOpen() && telemetry_file_path_ == file_path) {
        return true;
    }
    if (telemetry_file_.isOpen()) {
        if (!telemetry_file_.flush()) {
            SetError(QStringLiteral("Could not flush telemetry file %1: %2")
                .arg(telemetry_file_path_, telemetry_file_.errorString()));
            return false;
        }
        telemetry_file_.close();
    }

    QFileInfo info(file_path);
    if (!info.exists() || info.size() == 0) {
        QSaveFile initial_file(file_path);
        const QByteArray header = QJsonDocument(Header()).toJson(QJsonDocument::Compact) + '\n';
        if (!initial_file.open(QIODevice::WriteOnly)
            || initial_file.write(header) != header.size()
            || !initial_file.commit()) {
            SetError(QStringLiteral("Could not create telemetry schema header %1: %2")
                .arg(file_path, initial_file.errorString()));
            return false;
        }
    }

    telemetry_file_.setFileName(file_path);
    if (!telemetry_file_.open(QIODevice::ReadWrite | QIODevice::Append)) {
        SetError(QStringLiteral("Could not open local telemetry file %1: %2")
            .arg(file_path, telemetry_file_.errorString()));
        return false;
    }
    telemetry_file_path_ = file_path;
    telemetry_file_size_ = telemetry_file_.size();
    if (telemetry_file_size_ == 0
        || !telemetry_file_.seek(telemetry_file_size_ - 1)
        || telemetry_file_.read(1) != QByteArray("\n")) {
        SetError(QStringLiteral("Telemetry file %1 has an incomplete tail; it was not appended.")
            .arg(file_path));
        telemetry_file_.close();
        telemetry_file_path_.clear();
        telemetry_file_size_ = 0;
        return false;
    }
    return true;
}

bool TelemetryRecorder::AppendPendingBatch(const std::vector<QueuedSample>& samples) {
    if (!sync_queue_file_.isOpen()) {
        sync_queue_file_.setFileName(sync_queue_path_);
        if (!sync_queue_file_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            SetError(QStringLiteral("Could not open local synchronization queue %1: %2")
                .arg(sync_queue_path_, sync_queue_file_.errorString()));
            return false;
        }
    }

    QByteArray lines;
    lines.reserve(static_cast<qsizetype>(samples.size() * 80));
    for (const QueuedSample& sample : samples) {
        const QJsonObject item{
            {QStringLiteral("record_id"),
                sample.sample.value(QStringLiteral("record_id")).toString()},
            {QStringLiteral("state"), QStringLiteral("pending")},
        };
        lines.append(QJsonDocument(item).toJson(QJsonDocument::Compact));
        lines.append('\n');
    }
    if (sync_queue_file_.write(lines) != lines.size() || !sync_queue_file_.flush()) {
        SetError(QStringLiteral("Could not append telemetry batch to synchronization queue %1: %2")
            .arg(sync_queue_path_, sync_queue_file_.errorString()));
        return false;
    }
    return true;
}

void TelemetryRecorder::CloseFiles() {
    if (telemetry_file_.isOpen()) {
        if (!telemetry_file_.flush()) {
            SetError(QStringLiteral("Could not flush telemetry file %1 during shutdown: %2")
                .arg(telemetry_file_path_, telemetry_file_.errorString()));
        }
        telemetry_file_.close();
    }
    if (sync_queue_file_.isOpen()) {
        if (!sync_queue_file_.flush()) {
            SetError(QStringLiteral("Could not flush synchronization queue %1 during shutdown: %2")
                .arg(sync_queue_path_, sync_queue_file_.errorString()));
        }
        sync_queue_file_.close();
    }
    telemetry_file_path_.clear();
    telemetry_file_size_ = 0;
}

void TelemetryRecorder::SetError(const QString& error, bool writer_failure) {
    if (writer_failure) {
        writer_failed_ = true;
    }
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error.toStdWString();
}

} // namespace nlsi::logging
