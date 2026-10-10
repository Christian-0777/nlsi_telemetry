#include "HistoryStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "logging/Logger.h"
#include "telemetry/TelemetryUiState.h"
#include "time/ApplicationTime.h"

namespace nlsi::session {
namespace {

QString ToQString(const std::wstring& value) {
    return QString::fromStdWString(value);
}

QString FieldText(const telemetry::TelemetryField<std::wstring>& field) {
    return field.available && !field.stale ? ToQString(field.value) : QString();
}

QString EventText(const QJsonObject& object, std::initializer_list<QString> keys) {
    for (const QString& key : keys) {
        QJsonValue value = object.value(key);
        if (key.contains(QLatin1Char('.'))) {
            const QStringList path = key.split(QLatin1Char('.'));
            value = object.value(path.front());
            for (qsizetype index = 1; index < path.size() && value.isObject(); ++index) {
                value = value.toObject().value(path[index]);
            }
        }
        if (value.isString()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        } else if (value.isDouble()) {
            return value.toVariant().toString();
        }
    }
    return {};
}

QString ProviderEventKey(
    const QString& timestamp,
    const QString& source,
    const QString& type,
    const QJsonObject& details) {
    const QString source_event_id = EventText(details, {
        QStringLiteral("provider_event_id")});
    const QString event_identity = source_event_id.isEmpty()
        ? QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact))
        : source_event_id;
    return source + QLatin1Char('|') + type + QLatin1Char('|') + timestamp
        + QLatin1Char('|') + event_identity;
}

bool ParseRecord(
    const QByteArray& line,
    const QString& path,
    int line_number,
    QJsonObject& object,
    QString& error) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Invalid JSONL record in %1 at line %2: %3")
            .arg(path)
            .arg(line_number)
            .arg(parse_error.errorString());
        return false;
    }
    object = document.object();
    return true;
}

bool TimestampNewer(const QString& left, const QString& right) {
    const QDateTime left_time = nlsi::time::ParseInstant(left);
    const QDateTime right_time = nlsi::time::ParseInstant(right);
    if (left_time.isValid() && right_time.isValid()) {
        return left_time > right_time;
    }
    if (left_time.isValid() != right_time.isValid()) {
        return left_time.isValid();
    }
    return left > right;
}

void SortNewestFirst(QVector<EventRecord>& events) {
    std::sort(events.begin(), events.end(), [](const EventRecord& left, const EventRecord& right) {
        return TimestampNewer(left.timestamp, right.timestamp);
    });
}

void SortNewestFirst(QVector<SessionRecord>& sessions) {
    std::sort(sessions.begin(), sessions.end(), [](const SessionRecord& left, const SessionRecord& right) {
        return TimestampNewer(left.started_at, right.started_at);
    });
}

void SortNewestFirst(QVector<JobRecord>& jobs) {
    std::sort(jobs.begin(), jobs.end(), [](const JobRecord& left, const JobRecord& right) {
        return TimestampNewer(left.timestamp, right.timestamp);
    });
}

} // namespace

HistoryStore::HistoryStore(
    nlsi::logging::Logger& logger,
    std::function<std::uint32_t()> job_id_number_source)
    : logger_(logger),
      job_id_number_source_(std::move(job_id_number_source)) {
}

bool HistoryStore::Initialize(
    const QString& user_data_directory,
    const QString& legacy_application_directory,
    const QString& legacy_user_data_directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    root_ = user_data_directory;
    events_path_ = QDir(root_).filePath(QStringLiteral("logs/events.jsonl"));
    sessions_path_ = QDir(root_).filePath(QStringLiteral("session_logs/sessions.jsonl"));
    jobs_path_ = QDir(root_).filePath(QStringLiteral("session_logs/jobs.jsonl"));
    job_ids_path_ = QDir(root_).filePath(QStringLiteral("session_logs/job_ids.json"));
    if (!QDir().mkpath(QDir(root_).filePath(QStringLiteral("logs")))
        || !QDir().mkpath(QDir(root_).filePath(QStringLiteral("session_logs")))) {
        SetError(QStringLiteral("Could not create logs or session_logs under %1.").arg(root_));
        return false;
    }
    const auto migrate_legacy_root = [this](const QString& legacy_root) {
        if (legacy_root.isEmpty()) {
            return true;
        }
        const bool logs_migrated = MigrateLegacyDirectory(
            QDir(legacy_root).filePath(QStringLiteral("logs")),
            QDir(root_).filePath(QStringLiteral("logs")));
        const bool sessions_migrated = MigrateLegacyDirectory(
            QDir(legacy_root).filePath(QStringLiteral("session_logs")),
            QDir(root_).filePath(QStringLiteral("session_logs")));
        return logs_migrated && sessions_migrated;
    };
    if (!migrate_legacy_root(legacy_application_directory)
        || !migrate_legacy_root(legacy_user_data_directory)) {
        return false;
    }
    return Load();
}

bool HistoryStore::MigrateLegacyDirectory(
    const QString& source,
    const QString& destination) {
    if (!QFileInfo::exists(source)) {
        return true;
    }
    bool succeeded = true;
    QDirIterator files(
        source,
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString source_path = files.next();
        const QString relative_path = QDir(source).relativeFilePath(source_path);
        const QString destination_path = QDir(destination).filePath(relative_path);
        if (QFileInfo::exists(destination_path)) {
            continue;
        }
        if (!QDir().mkpath(QFileInfo(destination_path).absolutePath())) {
            SetError(QStringLiteral("Could not create legacy log destination directory for %1.")
                .arg(destination_path));
            succeeded = false;
            continue;
        }
        if (!QFile::copy(source_path, destination_path)) {
            SetError(QStringLiteral("Could not preserve legacy log file %1 at %2.")
                .arg(source_path, destination_path));
            succeeded = false;
        }
    }
    return succeeded;
}

bool HistoryStore::Load() {
    snapshot_ = {};
    recorded_provider_events_.clear();
    recorded_job_events_.clear();
    used_job_ids_.clear();
    job_ids_.clear();
    const bool events_loaded = LoadEvents();
    const bool sessions_loaded = LoadSessions();
    const bool jobs_loaded = LoadJobs();
    const bool telemetry_jobs_loaded = LoadTelemetryJobs();
    const bool job_ids_loaded = LoadJobIds();
    SortNewestFirst(snapshot_.events);
    SortNewestFirst(snapshot_.sessions);
    SortNewestFirst(snapshot_.jobs);
    return events_loaded && sessions_loaded && jobs_loaded
        && telemetry_jobs_loaded && job_ids_loaded;
}

bool HistoryStore::LoadEvents() {
    QFile file(events_path_);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SetError(QStringLiteral("Could not read %1: %2").arg(events_path_, file.errorString()));
        return false;
    }
    int line_number = 0;
    while (!file.atEnd()) {
        ++line_number;
        QJsonObject object;
        QString error;
        if (!ParseRecord(file.readLine(), events_path_, line_number, object, error)) {
            SetError(error);
            continue;
        }
        const QString timestamp = object.value(QStringLiteral("timestamp")).toString();
        const QString source = object.value(QStringLiteral("source")).toString();
        const QString type = object.value(QStringLiteral("type")).toString();
        const QJsonObject details = object.value(QStringLiteral("details")).toObject();
        const QString event_key = ProviderEventKey(timestamp, source, type, details);
        if (recorded_provider_events_.contains(event_key)) {
            continue;
        }
        recorded_provider_events_.insert(event_key);
        snapshot_.events.push_back({
            timestamp,
            source,
            type,
            QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)),
        });
    }
    return true;
}

bool HistoryStore::LoadSessions() {
    QFile file(sessions_path_);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SetError(QStringLiteral("Could not read %1: %2").arg(sessions_path_, file.errorString()));
        return false;
    }

    QMap<QString, int> rows_by_id;
    int line_number = 0;
    while (!file.atEnd()) {
        ++line_number;
        QJsonObject object;
        QString error;
        if (!ParseRecord(file.readLine(), sessions_path_, line_number, object, error)) {
            SetError(error);
            continue;
        }
        const QString id = object.value(QStringLiteral("session_id")).toString();
        const QString kind = object.value(QStringLiteral("record_type")).toString();
        if (id.isEmpty() || (kind != QStringLiteral("started") && kind != QStringLiteral("ended"))) {
            SetError(QStringLiteral("Invalid session record in %1 at line %2.")
                .arg(sessions_path_)
                .arg(line_number));
            continue;
        }
        int row = rows_by_id.value(id, -1);
        if (kind == QStringLiteral("started")) {
            if (row < 0) {
                row = snapshot_.sessions.size();
                rows_by_id.insert(id, row);
                snapshot_.sessions.push_back({
                    id,
                    object.value(QStringLiteral("game")).toString(),
                    object.value(QStringLiteral("timestamp")).toString(),
                    {},
                    QStringLiteral("Active"),
                });
            } else {
                SetError(QStringLiteral("Duplicate session start record for %1.").arg(id));
            }
        } else if (row >= 0) {
            auto& session = snapshot_.sessions[row];
            session.ended_at = object.value(QStringLiteral("timestamp")).toString();
            session.result = object.value(QStringLiteral("reason")).toString();
        } else {
            SetError(QStringLiteral("Session end has no matching start record for %1.").arg(id));
        }
    }
    return true;
}

bool HistoryStore::LoadJobs() {
    QFile file(jobs_path_);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        SetError(QStringLiteral("Could not read %1: %2").arg(jobs_path_, file.errorString()));
        return false;
    }
    int line_number = 0;
    while (!file.atEnd()) {
        ++line_number;
        QJsonObject object;
        QString error;
        if (!ParseRecord(file.readLine(), jobs_path_, line_number, object, error)) {
            SetError(error);
            continue;
        }
        const QString key = object.value(QStringLiteral("event_key")).toString();
        const QJsonObject details = object.value(QStringLiteral("details")).toObject();
        const QString job_id = EventText(details, {
            QStringLiteral("job_id"), QStringLiteral("job.id")});
        if (!job_id.isEmpty()) {
            recorded_job_events_.insert(QStringLiteral("job_id:%1").arg(job_id));
        } else if (!key.isEmpty()) {
            recorded_job_events_.insert(key);
        }
        const QString identity = object.value(QStringLiteral("identity")).toString();
        const QString nlsi_job_id = object.value(QStringLiteral("nlsi_job_id")).toString();
        if (!identity.isEmpty()) {
            used_job_ids_.insert(identity);
        }
        if (!nlsi_job_id.isEmpty()) {
            used_job_ids_.insert(nlsi_job_id);
        }
        snapshot_.jobs.push_back({
            identity,
            object.value(QStringLiteral("cargo")).toString(),
            object.value(QStringLiteral("source")).toString(),
            object.value(QStringLiteral("destination")).toString(),
            object.value(QStringLiteral("status")).toString(),
            object.value(QStringLiteral("timestamp")).toString(),
            details,
            nlsi_job_id,
        });
    }
    return true;
}

bool HistoryStore::LoadTelemetryJobs() {
    const QString telemetry_directory =
        QDir(root_).filePath(QStringLiteral("logs/telemetry"));
    if (!QFileInfo::exists(telemetry_directory)) {
        return true;
    }
    QDirIterator files(
        telemetry_directory,
        {QStringLiteral("*.nlsi")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            SetError(QStringLiteral("Could not read completed-job telemetry in %1: %2")
                .arg(path, file.errorString()));
            return false;
        }
        int line_number = 0;
        while (!file.atEnd()) {
            ++line_number;
            QJsonObject object;
            QString error;
            if (!ParseRecord(file.readLine(), path, line_number, object, error)) {
                SetError(error);
                continue;
            }
            if (object.value(QStringLiteral("record_type")).toString()
                    != QStringLiteral("provider_event")) {
                continue;
            }
            const QString event_type = object.value(QStringLiteral("event")).toString();
            const bool delivered = event_type == QStringLiteral("job.delivered");
            const bool cancelled = event_type == QStringLiteral("job.cancelled");
            if (!delivered && !cancelled) {
                continue;
            }
            QJsonObject details = object.value(QStringLiteral("data")).toObject();
            const QString game_job_id = EventText(details, {
                QStringLiteral("job_id"), QStringLiteral("job.id"),
                QStringLiteral("cargo_id"), QStringLiteral("cargo.id")});
            const QString status = delivered
                ? QStringLiteral("Delivered") : QStringLiteral("Cancelled");
            const QString timestamp = object.value(QStringLiteral("timestamp_utc")).toString();
            const QString event_key = !game_job_id.isEmpty()
                ? QStringLiteral("job_id:%1").arg(game_job_id)
                : QStringLiteral("telemetry:%1|%2|%3")
                    .arg(EventText(details, {QStringLiteral("provider_event_id")}),
                        status, timestamp);
            if (recorded_job_events_.contains(event_key)) {
                continue;
            }
            const QString identity = game_job_id.isEmpty()
                ? event_key : game_job_id;
            const QString cargo = EventText(details, {
                QStringLiteral("cargo"), QStringLiteral("cargo_name"),
                QStringLiteral("cargo.name"), QStringLiteral("cargo.id")});
            const QString source_company = EventText(details, {
                QStringLiteral("source_company"), QStringLiteral("source.company")});
            const QString source_city = EventText(details, {
                QStringLiteral("source_city"), QStringLiteral("source.city")});
            const QString destination_company = EventText(details, {
                QStringLiteral("destination_company"),
                QStringLiteral("destination.company")});
            const QString destination_city = EventText(details, {
                QStringLiteral("destination_city"), QStringLiteral("destination.city")});
            const auto route = [](const QString& company, const QString& city) {
                if (company.isEmpty()) return city;
                if (city.isEmpty() || company == city) return company;
                return company + QStringLiteral(" · ") + city;
            };
            const QString source = route(source_company, source_city);
            const QString destination = route(destination_company, destination_city);
            snapshot_.jobs.push_back({
                identity,
                cargo,
                source,
                destination,
                status,
                timestamp,
                details,
                details.value(QStringLiteral("nlsi_job_id")).toString(),
            });
            recorded_job_events_.insert(event_key);
        }
    }
    return true;
}

bool HistoryStore::LoadJobIds() {
    QFile file(job_ids_path_);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        SetError(QStringLiteral("Could not read %1: %2").arg(job_ids_path_, file.errorString()));
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        SetError(QStringLiteral("Invalid NLSI job ID mapping in %1: %2")
            .arg(job_ids_path_, parse_error.errorString()));
        return false;
    }
    const QJsonObject mappings = document.object();
    for (auto it = mappings.constBegin(); it != mappings.constEnd(); ++it) {
        const QString id = it.value().toString();
        if (it.key().isEmpty() || !id.startsWith(QStringLiteral("JOB-NLSI-"))
            || job_ids_.values().contains(id)) {
            SetError(QStringLiteral("Invalid or duplicate NLSI job ID mapping in %1.")
                .arg(job_ids_path_));
            return false;
        }
        job_ids_.insert(it.key(), id);
        used_job_ids_.insert(id);
    }
    return true;
}

bool HistoryStore::SaveJobIds() {
    QJsonObject mappings;
    for (auto it = job_ids_.constBegin(); it != job_ids_.constEnd(); ++it) {
        mappings.insert(it.key(), it.value());
    }
    QSaveFile file(job_ids_path_);
    const QByteArray contents = QJsonDocument(mappings).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(contents) != contents.size()
        || !file.commit()) {
        SetError(QStringLiteral("Could not persist NLSI job ID mappings in %1: %2")
            .arg(job_ids_path_, file.errorString()));
        return false;
    }
    return true;
}

QString HistoryStore::AllocateNlsiJobId() {
    for (int attempt = 0; attempt < 10000; ++attempt) {
        const std::uint32_t number = job_id_number_source_
            ? job_id_number_source_()
            : QRandomGenerator::global()->bounded(10000U);
        const QString candidate = QStringLiteral("JOB-NLSI-%1")
            .arg(number % 10000U, 4, 10, QLatin1Char('0'));
        if (!used_job_ids_.contains(candidate)) {
            return candidate;
        }
    }
    return {};
}

QString HistoryStore::EnsureNlsiJobId(const QString& game_job_identity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (game_job_identity.isEmpty()) {
        return {};
    }
    const auto existing = job_ids_.constFind(game_job_identity);
    if (existing != job_ids_.cend()) {
        return existing.value();
    }
    const QString nlsi_job_id = AllocateNlsiJobId();
    if (nlsi_job_id.isEmpty()) {
        SetError(QStringLiteral("Could not allocate a unique NLSI job ID; all four-digit IDs are in use."));
        return {};
    }
    job_ids_.insert(game_job_identity, nlsi_job_id);
    used_job_ids_.insert(nlsi_job_id);
    if (!SaveJobIds()) {
        job_ids_.remove(game_job_identity);
        used_job_ids_.remove(nlsi_job_id);
        return {};
    }
    ++snapshot_.revision;
    snapshot_.error.clear();
    return nlsi_job_id;
}

bool HistoryStore::RecordProviderEvent(
    const QByteArray& raw_packet,
    bool* newly_recorded) {
    if (newly_recorded) {
        *newly_recorded = false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(raw_packet, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        SetError(QStringLiteral("Could not persist provider event: %1").arg(parse_error.errorString()));
        return false;
    }
    const QJsonObject packet = document.object();
    QString type = packet.value(QStringLiteral("event")).toString();
    if (type.isEmpty()) {
        type = packet.value(QStringLiteral("type")).toString();
    }
    if (type == QStringLiteral("lifecycle")) {
        type = packet.value(QStringLiteral("state")).toString() == QStringLiteral("paused")
            ? QStringLiteral("DRIVING_PAUSED")
            : QStringLiteral("DRIVING_RESUMED");
    }
    if (type.isEmpty()) {
        SetError(QStringLiteral("Provider event did not contain an event type."));
        return false;
    }

    QJsonObject details = packet;
    details.remove(QStringLiteral("type"));
    details.remove(QStringLiteral("timestamp"));
    const QString timestamp = packet.value(QStringLiteral("timestamp")).toString();
    const QString source = packet.value(QStringLiteral("provider")).toString(QStringLiteral("NLSI"));
    const QString event_key = ProviderEventKey(timestamp, source, type, details);
    if (recorded_provider_events_.contains(event_key)) {
        return true;
    }
    const QJsonObject record{
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("source"), source},
        {QStringLiteral("type"), type},
        {QStringLiteral("details"), details},
    };
    if (!AppendJsonLine(events_path_, record)) {
        return false;
    }

    recorded_provider_events_.insert(event_key);
    if (newly_recorded) {
        *newly_recorded = true;
    }
    snapshot_.events.prepend({
        timestamp,
        source,
        type,
        QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)),
    });
    ++snapshot_.revision;
    snapshot_.error.clear();
    std::wstring log_error;
    const QString data_text = QString::fromUtf8(
        QJsonDocument(details.value(QStringLiteral("data")).toObject())
            .toJson(QJsonDocument::Compact));
    if (!logger_.Log(
            (QStringLiteral("[event] %1 %2 %3")
                .arg(timestamp, source, type)
                + (data_text == QStringLiteral("{}")
                    ? QString() : QStringLiteral(" | data=%1").arg(data_text)))
                .toStdWString(),
            &log_error)) {
        snapshot_.error = QString::fromStdWString(log_error);
    }
    return snapshot_.error.isEmpty();
}

bool HistoryStore::StartSession(
    const std::wstring& id,
    const std::wstring& game,
    const std::wstring& timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString session_id = ToQString(id);
    for (const auto& existing : snapshot_.sessions) {
        if (existing.id == session_id) {
            return true;
        }
    }
    const QString start_time = ToQString(timestamp);
    if (!AppendJsonLine(sessions_path_, {
            {QStringLiteral("record_type"), QStringLiteral("started")},
            {QStringLiteral("session_id"), session_id},
            {QStringLiteral("game"), ToQString(game)},
            {QStringLiteral("timestamp"), start_time},
        })) {
        return false;
    }
    snapshot_.sessions.prepend({
        session_id,
        ToQString(game),
        start_time,
        {},
        QStringLiteral("Active"),
    });
    ++snapshot_.revision;
    snapshot_.error.clear();
    const QString path = QDir(root_).filePath(
        QStringLiteral("session_logs/%1.txt").arg(session_id));
    if (!AppendTextLine(path, QStringLiteral("Session started: %1 | Game: %2")
            .arg(start_time, ToQString(game)))) {
        return false;
    }
    std::wstring log_error;
    if (!logger_.Log(
            QStringLiteral("[session] started %1 game=%2")
                .arg(session_id, ToQString(game)).toStdWString(),
            &log_error)) {
        SetError(QString::fromStdWString(log_error));
        return false;
    }
    return true;
}

bool HistoryStore::EndSession(
    const std::wstring& id,
    const std::wstring& timestamp,
    const std::wstring& reason,
    double duration_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString session_id = ToQString(id);
    auto session = std::find_if(snapshot_.sessions.begin(), snapshot_.sessions.end(),
        [&session_id](const SessionRecord& record) { return record.id == session_id; });
    if (session == snapshot_.sessions.end() || !session->ended_at.isEmpty()) {
        return true;
    }

    const QString end_time = ToQString(timestamp);
    const QString result = ToQString(reason);
    if (!AppendJsonLine(sessions_path_, {
            {QStringLiteral("record_type"), QStringLiteral("ended")},
            {QStringLiteral("session_id"), session_id},
            {QStringLiteral("timestamp"), end_time},
            {QStringLiteral("reason"), result},
            {QStringLiteral("duration_seconds"), duration_seconds},
        })) {
        return false;
    }
    session->ended_at = end_time;
    session->result = result;
    ++snapshot_.revision;
    snapshot_.error.clear();
    const QString path = QDir(root_).filePath(
        QStringLiteral("session_logs/%1.txt").arg(session_id));
    if (!AppendTextLine(path, QStringLiteral("Session ended: %1 | Reason: %2 | Duration: %3 seconds")
            .arg(end_time, result)
            .arg(duration_seconds, 0, 'f', 1))) {
        return false;
    }
    std::wstring log_error;
    if (!logger_.Log(
            QStringLiteral("[session] ended %1 reason=%2")
                .arg(session_id, result).toStdWString(),
            &log_error)) {
        SetError(QString::fromStdWString(log_error));
        return false;
    }
    return true;
}

bool HistoryStore::RecordJob(
    const telemetry::JobSnapshot& job,
    const QString& event_type,
    const QString& timestamp,
    const QJsonObject& event_details) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool delivered = event_type == QStringLiteral("job.delivered");
    const bool cancelled = event_type == QStringLiteral("job.cancelled");
    if (!delivered && !cancelled) {
        return true;
    }
    const QString status = delivered ? QStringLiteral("Delivered") : QStringLiteral("Cancelled");
    const QString job_id = EventText(event_details, {
        QStringLiteral("job_id"), QStringLiteral("job.id")});
    const QString event_identity = EventText(event_details, {
        QStringLiteral("cargo_id"), QStringLiteral("cargo.id")});
    const QString identity = !job_id.isEmpty()
        ? job_id
        : (job.identity.empty() ? event_identity : ToQString(job.identity));
    if (identity.isEmpty()) {
        return true;
    }
    const QString detail_key = QString::fromUtf8(
        QJsonDocument(event_details).toJson(QJsonDocument::Compact));
    const QString event_key = !job_id.isEmpty()
        ? QStringLiteral("job_id:%1").arg(job_id)
        : identity + QLatin1Char('|') + status
            + QLatin1Char('|') + timestamp + QLatin1Char('|') + detail_key;
    if (recorded_job_events_.contains(event_key)) {
        return true;
    }
    QString nlsi_job_id = job_ids_.value(identity);
    if (nlsi_job_id.isEmpty()) {
        nlsi_job_id = AllocateNlsiJobId();
        if (!nlsi_job_id.isEmpty()) {
            job_ids_.insert(identity, nlsi_job_id);
            used_job_ids_.insert(nlsi_job_id);
            if (!SaveJobIds()) {
                job_ids_.remove(identity);
                used_job_ids_.remove(nlsi_job_id);
                return false;
            }
        }
    }
    if (nlsi_job_id.isEmpty()) {
        SetError(QStringLiteral("Could not allocate a unique NLSI job ID; all four-digit IDs are in use."));
        return false;
    }
    const QString snapshot_cargo_id = FieldText(job.cargo_id);
    const QString event_job_identity = !job_id.isEmpty() ? job_id : event_identity;
    const bool snapshot_matches_event = !snapshot_cargo_id.isEmpty()
        && snapshot_cargo_id == event_job_identity;
    const auto snapshot_field = [snapshot_matches_event](const auto& field) {
        return snapshot_matches_event ? FieldText(field) : QString();
    };
    const QString event_cargo = EventText(event_details, {
        QStringLiteral("cargo"), QStringLiteral("cargo_name"), QStringLiteral("cargo.id")});
    const QString cargo = event_cargo.isEmpty() ? snapshot_field(job.cargo) : event_cargo;
    const QString event_source_company = EventText(event_details, {
        QStringLiteral("source_company"), QStringLiteral("source.company")});
    const QString event_source_city = EventText(event_details, {
        QStringLiteral("source_city"), QStringLiteral("source.city")});
    const QString event_destination_company = EventText(event_details, {
        QStringLiteral("destination_company"), QStringLiteral("destination.company")});
    const QString event_destination_city = EventText(event_details, {
        QStringLiteral("destination_city"), QStringLiteral("destination.city")});
    const QString source_company = event_source_company.isEmpty()
        ? snapshot_field(job.source_company) : event_source_company;
    const QString source_city = event_source_city.isEmpty()
        ? snapshot_field(job.source_city) : event_source_city;
    const QString destination_company = event_destination_company.isEmpty()
        ? snapshot_field(job.destination_company) : event_destination_company;
    const QString destination_city = event_destination_city.isEmpty()
        ? snapshot_field(job.destination_city) : event_destination_city;
    const QString source = source_company.isEmpty() && source_city.isEmpty()
        ? ((event_source_company.isEmpty() || event_source_city.isEmpty()
                || event_source_company == event_source_city)
            ? (event_source_company.isEmpty() ? event_source_city : event_source_company)
            : event_source_company + QStringLiteral(" · ") + event_source_city)
        : (source_company.isEmpty() || source_city.isEmpty()
                || source_company == source_city
            ? (source_company.isEmpty() ? source_city : source_company)
            : source_company + QStringLiteral(" · ") + source_city);
    const QString destination = destination_company.isEmpty() && destination_city.isEmpty()
        ? ((event_destination_company.isEmpty() || event_destination_city.isEmpty()
                || event_destination_company == event_destination_city)
            ? (event_destination_company.isEmpty()
                ? event_destination_city
                : event_destination_company)
            : event_destination_company + QStringLiteral(" · ") + event_destination_city)
        : (destination_company.isEmpty() || destination_city.isEmpty()
                || destination_company == destination_city
            ? (destination_company.isEmpty() ? destination_city : destination_company)
            : destination_company + QStringLiteral(" · ") + destination_city);
    QJsonObject persisted_details = event_details;
    const auto persist_snapshot_field = [&persisted_details, &event_details](
                                            const QString& name,
                                            const QString& value,
                                            std::initializer_list<QString> event_keys) {
        if (!value.isEmpty() && EventText(event_details, event_keys).isEmpty()) {
            persisted_details.insert(name, value);
        }
    };
    persist_snapshot_field(QStringLiteral("cargo"), snapshot_field(job.cargo), {
        QStringLiteral("cargo"), QStringLiteral("cargo_name"), QStringLiteral("cargo.id")});
    persist_snapshot_field(QStringLiteral("cargo_id"),
        snapshot_matches_event ? snapshot_cargo_id : QString(), {
        QStringLiteral("cargo_id"), QStringLiteral("cargo.id")});
    persist_snapshot_field(QStringLiteral("source_company"), source_company, {
        QStringLiteral("source_company"), QStringLiteral("source.company")});
    persist_snapshot_field(QStringLiteral("source_city"), source_city, {
        QStringLiteral("source_city"), QStringLiteral("source.city")});
    persist_snapshot_field(QStringLiteral("destination_company"),
        destination_company, {
            QStringLiteral("destination_company"), QStringLiteral("destination.company")});
    persist_snapshot_field(QStringLiteral("destination_city"),
        destination_city, {
            QStringLiteral("destination_city"), QStringLiteral("destination.city")});
    persist_snapshot_field(QStringLiteral("income"), snapshot_field(job.income), {
        QStringLiteral("income")});
    persist_snapshot_field(QStringLiteral("planned_distance"),
        snapshot_field(job.planned_distance), {
            QStringLiteral("planned_distance"), QStringLiteral("planned_distance_km")});
    persisted_details.insert(QStringLiteral("nlsi_job_id"), nlsi_job_id);
    const QJsonObject record{
        {QStringLiteral("event_key"), event_key},
        {QStringLiteral("identity"), identity},
        {QStringLiteral("nlsi_job_id"), nlsi_job_id},
        {QStringLiteral("cargo"), cargo},
        {QStringLiteral("source"), source},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("status"), status},
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("details"), persisted_details},
    };
    if (!AppendJsonLine(jobs_path_, record)) {
        return false;
    }
    recorded_job_events_.insert(event_key);
    used_job_ids_.insert(identity);
    used_job_ids_.insert(nlsi_job_id);
    snapshot_.jobs.prepend({
        identity, cargo, source, destination, status, timestamp, persisted_details, nlsi_job_id});
    ++snapshot_.revision;
    snapshot_.error.clear();
    std::wstring log_error;
    if (!logger_.Log(
            QStringLiteral("[job] %1 %2 cargo=%3")
                .arg(timestamp, status, cargo).toStdWString(),
            &log_error)) {
        SetError(QString::fromStdWString(log_error));
        return false;
    }
    return true;
}

HistorySnapshot HistoryStore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool HistoryStore::AppendJsonLine(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        SetError(QStringLiteral("Could not append to %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size() || !file.flush()) {
        SetError(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

bool HistoryStore::AppendTextLine(const QString& path, const QString& line) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        SetError(QStringLiteral("Could not append to %1: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray bytes = line.toUtf8() + '\n';
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        SetError(QStringLiteral("Could not write %1: %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

void HistoryStore::SetError(const QString& error) {
    snapshot_.error = error;
    ++snapshot_.revision;
    std::wstring log_error;
    logger_.Log((QStringLiteral("[history error] ") + error).toStdWString(), &log_error);
}

} // namespace nlsi::session
