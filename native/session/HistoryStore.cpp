#include "HistoryStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>

#include <algorithm>
#include <initializer_list>

#include "logging/Logger.h"
#include "telemetry/TelemetryUiState.h"

namespace nlsi::session {
namespace {

QString ToQString(const std::wstring& value) {
    return QString::fromStdWString(value);
}

QString FieldText(const telemetry::TelemetryField<std::wstring>& field) {
    return field.available ? ToQString(field.value) : QString();
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

QString JobRoute(
    const telemetry::TelemetryField<std::wstring>& company,
    const telemetry::TelemetryField<std::wstring>& city) {
    const QString company_text = FieldText(company);
    const QString city_text = FieldText(city);
    if (company_text.isEmpty()) {
        return city_text;
    }
    if (city_text.isEmpty() || company_text == city_text) {
        return company_text;
    }
    return company_text + QStringLiteral(" · ") + city_text;
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

void SortNewestFirst(QVector<EventRecord>& events) {
    std::sort(events.begin(), events.end(), [](const EventRecord& left, const EventRecord& right) {
        return left.timestamp > right.timestamp;
    });
}

void SortNewestFirst(QVector<SessionRecord>& sessions) {
    std::sort(sessions.begin(), sessions.end(), [](const SessionRecord& left, const SessionRecord& right) {
        return left.started_at > right.started_at;
    });
}

void SortNewestFirst(QVector<JobRecord>& jobs) {
    std::sort(jobs.begin(), jobs.end(), [](const JobRecord& left, const JobRecord& right) {
        return left.timestamp > right.timestamp;
    });
}

} // namespace

HistoryStore::HistoryStore(nlsi::logging::Logger& logger) : logger_(logger) {
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
    recorded_job_events_.clear();
    const bool events_loaded = LoadEvents();
    const bool sessions_loaded = LoadSessions();
    const bool jobs_loaded = LoadJobs();
    SortNewestFirst(snapshot_.events);
    SortNewestFirst(snapshot_.sessions);
    SortNewestFirst(snapshot_.jobs);
    return events_loaded && sessions_loaded && jobs_loaded;
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
        snapshot_.events.push_back({
            object.value(QStringLiteral("timestamp")).toString(),
            object.value(QStringLiteral("source")).toString(),
            object.value(QStringLiteral("type")).toString(),
            QString::fromUtf8(QJsonDocument(object.value(QStringLiteral("details")).toObject())
                .toJson(QJsonDocument::Compact)),
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
        snapshot_.jobs.push_back({
            object.value(QStringLiteral("identity")).toString(),
            object.value(QStringLiteral("cargo")).toString(),
            object.value(QStringLiteral("source")).toString(),
            object.value(QStringLiteral("destination")).toString(),
            object.value(QStringLiteral("status")).toString(),
            object.value(QStringLiteral("timestamp")).toString(),
            details,
        });
    }
    return true;
}

bool HistoryStore::RecordProviderEvent(const QByteArray& raw_packet) {
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
    const QJsonObject record{
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("source"), source},
        {QStringLiteral("type"), type},
        {QStringLiteral("details"), details},
    };
    if (!AppendJsonLine(events_path_, record)) {
        return false;
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
    if (!logger_.Log(
            (QStringLiteral("[event] %1 %2 %3")
                .arg(timestamp, source, type)).toStdWString(),
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
    const QString cargo = FieldText(job.cargo).isEmpty()
        ? EventText(event_details, {
            QStringLiteral("cargo"), QStringLiteral("cargo_name"), QStringLiteral("cargo.id")})
        : FieldText(job.cargo);
    const QString event_source_company = EventText(event_details, {
        QStringLiteral("source_company"), QStringLiteral("source.company")});
    const QString event_source_city = EventText(event_details, {
        QStringLiteral("source_city"), QStringLiteral("source.city")});
    const QString event_destination_company = EventText(event_details, {
        QStringLiteral("destination_company"), QStringLiteral("destination.company")});
    const QString event_destination_city = EventText(event_details, {
        QStringLiteral("destination_city"), QStringLiteral("destination.city")});
    const QString source = FieldText(job.source_company).isEmpty()
            && FieldText(job.source_city).isEmpty()
        ? ((event_source_company.isEmpty() || event_source_city.isEmpty()
                || event_source_company == event_source_city)
            ? (event_source_company.isEmpty() ? event_source_city : event_source_company)
            : event_source_company + QStringLiteral(" · ") + event_source_city)
        : JobRoute(job.source_company, job.source_city);
    const QString destination = FieldText(job.destination_company).isEmpty()
            && FieldText(job.destination_city).isEmpty()
        ? ((event_destination_company.isEmpty() || event_destination_city.isEmpty()
                || event_destination_company == event_destination_city)
            ? (event_destination_company.isEmpty()
                ? event_destination_city
                : event_destination_company)
            : event_destination_company + QStringLiteral(" · ") + event_destination_city)
        : JobRoute(job.destination_company, job.destination_city);
    const QJsonObject record{
        {QStringLiteral("event_key"), event_key},
        {QStringLiteral("identity"), identity},
        {QStringLiteral("cargo"), cargo},
        {QStringLiteral("source"), source},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("status"), status},
        {QStringLiteral("timestamp"), timestamp},
        {QStringLiteral("details"), event_details},
    };
    if (!AppendJsonLine(jobs_path_, record)) {
        return false;
    }
    recorded_job_events_.insert(event_key);
    snapshot_.jobs.prepend({identity, cargo, source, destination, status, timestamp, event_details});
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
