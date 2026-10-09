#include "TelemetryRecorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <QByteArray>
#include <QtEndian>
#include <QUuid>

#include <algorithm>

namespace {

constexpr qsizetype kMaximumSampleBytes = 1024 * 1024;
constexpr qsizetype kMaximumQueueLineBytes = 4096;
constexpr qint64 kMaximumFileBytes = 128LL * 1024 * 1024;
constexpr std::size_t kMaximumQueuedSamples = 2048;

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
    root_ = QString::fromStdWString(user_data_directory);
    telemetry_directory_ = QDir(root_).filePath(QStringLiteral("telemetry"));
    sync_queue_path_ = QDir(root_).filePath(QStringLiteral("sync/queue.jsonl"));
    if (!QDir().mkpath(telemetry_directory_)
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
    running_ = true;
    worker_ = std::thread(&TelemetryRecorder::WriteLoop, this);
    return true;
}

bool TelemetryRecorder::Enqueue(const QJsonObject& sample, std::wstring* error) {
    const QByteArray encoded = QJsonDocument(sample).toJson(QJsonDocument::Compact);
    if (encoded.isEmpty() || encoded.size() > kMaximumSampleBytes
        || !IsTimestamp(sample.value(QStringLiteral("timestamp_utc")).toString())
        || sample.value(QStringLiteral("provider")).toString()
            != QStringLiteral("TruckSim GPS")
        || sample.value(QStringLiteral("provider_revision")).toInt() != 13
        || sample.value(QStringLiteral("raw_fields")).toObject().isEmpty()
        || !sample.value(QStringLiteral("raw_availability")).isObject()
        || !sample.value(QStringLiteral("normalized_fields")).isObject()
        || !IsRawMappingValid(sample)) {
        const QString reason = QStringLiteral("Rejected malformed or oversized raw telemetry sample.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stopping_) {
        const QString reason = QStringLiteral("Local telemetry recorder is not running.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason);
        return false;
    }
    if (queue_.size() >= kMaximumQueuedSamples) {
        const QString reason = QStringLiteral(
            "Local telemetry write queue is full; the sample was not accepted.");
        if (error) {
            *error = reason.toStdWString();
        }
        SetError(reason);
        return false;
    }
    queue_.push_back(sample);
    condition_.notify_one();
    return true;
}

bool TelemetryRecorder::Flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] {
        return (ready_ && queue_.empty() && !writing_) || !running_;
    });
    return LastError().empty();
}

bool TelemetryRecorder::FlushFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool drained = condition_.wait_for(lock, timeout, [this] {
        return (ready_ && queue_.empty() && !writing_) || !running_;
    });
    lock.unlock();
    return drained && LastError().empty();
}

void TelemetryRecorder::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        condition_.notify_all();
    }
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

void TelemetryRecorder::WriteLoop() {
    if (!Recover()) {
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
        QJsonObject sample;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty() && stopping_) {
                break;
            }
            sample = std::move(queue_.front());
            queue_.pop_front();
            writing_ = true;
        }
        const bool written = WriteSample(std::move(sample));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            writing_ = false;
            if (!written) {
                stopping_ = true;
            }
            condition_.notify_all();
        }
        if (!written) {
            break;
        }
    }
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
    return ReconcileSyncQueue(record_ids);
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

    std::uint64_t pending = 0;
    for (const QString& record_id : record_ids) {
        if (!known_ids.contains(record_id)) {
            if (!AppendPending(record_id)) {
                return false;
            }
            known_ids.insert(record_id);
        }
        ++pending;
    }
    pending_count_ = pending;
    return true;
}

bool TelemetryRecorder::WriteSample(QJsonObject sample) {
    const QString timestamp = sample.value(QStringLiteral("timestamp_utc")).toString();
    const QDateTime date_time = QDateTime::fromString(timestamp, Qt::ISODateWithMs).toUTC();
    if (!date_time.isValid()) {
        SetError(QStringLiteral("Telemetry sample contains an invalid UTC timestamp."));
        return false;
    }
    const QString base_name = date_time.toString(QStringLiteral("yyyy-MM-dd"));
    QString file_path = QDir(telemetry_directory_).filePath(base_name + QStringLiteral(".nlsi"));
    int rotation = 0;
    while (QFileInfo::exists(file_path) && QFileInfo(file_path).size() >= kMaximumFileBytes) {
        ++rotation;
        file_path = QDir(telemetry_directory_).filePath(
            QStringLiteral("%1-%2.nlsi").arg(base_name).arg(rotation, 3, 10, QLatin1Char('0')));
    }

    QFile file(file_path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append)) {
        SetError(QStringLiteral("Could not open local telemetry file %1: %2")
            .arg(file_path, file.errorString()));
        return false;
    }
    if (file.size() == 0) {
        const QByteArray header = QJsonDocument(Header()).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(header) != header.size() || !file.flush()) {
            SetError(QStringLiteral("Could not write telemetry schema header to %1: %2")
                .arg(file_path, file.errorString()));
            return false;
        }
    } else if (!file.seek(file.size() - 1) || file.read(1) != QByteArray("\n")) {
        SetError(QStringLiteral("Telemetry file %1 has an incomplete tail; it was not appended.")
            .arg(file_path));
        return false;
    }
    const QString record_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sample.insert(QStringLiteral("record_type"), QStringLiteral("telemetry_sample"));
    sample.insert(QStringLiteral("schema_version"), 2);
    sample.insert(QStringLiteral("record_id"), record_id);
    sample.insert(QStringLiteral("sequence"), static_cast<qint64>(next_sequence_++));
    const QByteArray line = QJsonDocument(sample).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > kMaximumSampleBytes || file.write(line) != line.size() || !file.flush()) {
        SetError(QStringLiteral("Could not durably append telemetry sample to %1: %2")
            .arg(file_path, file.errorString()));
        return false;
    }
    if (!AppendPending(record_id)) {
        return false;
    }
    ++pending_count_;
    return true;
}

bool TelemetryRecorder::AppendPending(const QString& record_id) {
    QFile file(sync_queue_path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        SetError(QStringLiteral("Could not open local synchronization queue %1: %2")
            .arg(sync_queue_path_, file.errorString()));
        return false;
    }
    const QJsonObject item{
        {QStringLiteral("record_id"), record_id},
        {QStringLiteral("state"), QStringLiteral("pending")},
    };
    const QByteArray line = QJsonDocument(item).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size() || !file.flush()) {
        SetError(QStringLiteral("Could not append to local synchronization queue %1: %2")
            .arg(sync_queue_path_, file.errorString()));
        return false;
    }
    return true;
}

void TelemetryRecorder::SetError(const QString& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error.toStdWString();
}

} // namespace nlsi::logging
