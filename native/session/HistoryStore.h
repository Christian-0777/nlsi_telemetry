#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QSet>
#include <QVector>

#include <cstdint>
#include <mutex>
#include <string>

namespace nlsi::logging {
class Logger;
}

namespace nlsi::telemetry {
struct JobSnapshot;
}

namespace nlsi::session {

struct EventRecord {
    QString timestamp;
    QString source;
    QString type;
    QString details;
};

struct SessionRecord {
    QString id;
    QString game;
    QString started_at;
    QString ended_at;
    QString result;
};

struct JobRecord {
    QString identity;
    QString cargo;
    QString source;
    QString destination;
    QString status;
    QString timestamp;
    QJsonObject details;
};

struct HistorySnapshot {
    QVector<EventRecord> events;
    QVector<SessionRecord> sessions;
    QVector<JobRecord> jobs;
    QString error;
    std::uint64_t revision = 0;
};

class HistoryStore {
public:
    explicit HistoryStore(nlsi::logging::Logger& logger);

    bool Initialize(
        const QString& user_data_directory,
        const QString& legacy_application_directory = {},
        const QString& legacy_user_data_directory = {});
    bool RecordProviderEvent(const QByteArray& raw_packet);
    bool StartSession(
        const std::wstring& id,
        const std::wstring& game,
        const std::wstring& timestamp);
    bool EndSession(
        const std::wstring& id,
        const std::wstring& timestamp,
        const std::wstring& reason,
        double duration_seconds);
    bool RecordJob(
        const telemetry::JobSnapshot& job,
        const QString& event_type,
        const QString& timestamp,
        const QJsonObject& event_details);
    HistorySnapshot Snapshot() const;

private:
    bool Load();
    bool MigrateLegacyDirectory(const QString& source, const QString& destination);
    bool LoadEvents();
    bool LoadSessions();
    bool LoadJobs();
    bool AppendJsonLine(const QString& path, const QJsonObject& object);
    bool AppendTextLine(const QString& path, const QString& line);
    void SetError(const QString& error);

    nlsi::logging::Logger& logger_;
    QString root_;
    QString events_path_;
    QString sessions_path_;
    QString jobs_path_;
    mutable std::mutex mutex_;
    HistorySnapshot snapshot_;
    QSet<QString> recorded_job_events_;
};

} // namespace nlsi::session
