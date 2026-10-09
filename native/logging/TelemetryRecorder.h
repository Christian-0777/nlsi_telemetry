#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <QJsonObject>
#include <QSet>

namespace nlsi::logging {

class TelemetryRecorder {
public:
    TelemetryRecorder() = default;
    ~TelemetryRecorder();
    TelemetryRecorder(const TelemetryRecorder&) = delete;
    TelemetryRecorder& operator=(const TelemetryRecorder&) = delete;

    bool Start(const std::wstring& user_data_directory, std::wstring* error = nullptr);
    bool Enqueue(const QJsonObject& sample, std::wstring* error = nullptr);
    bool Flush();
    void Stop();
    std::wstring LastError() const;
    std::uint64_t PendingCount() const;

private:
    void WriteLoop();
    bool Recover();
    bool RecoverTelemetryFile(const std::wstring& path);
    bool ReconcileSyncQueue(const QSet<QString>& record_ids);
    bool WriteSample(QJsonObject sample);
    bool AppendPending(const QString& record_id);
    void SetError(const QString& error);

    QString root_;
    QString telemetry_directory_;
    QString sync_queue_path_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<QJsonObject> queue_;
    bool running_ = false;
    bool ready_ = false;
    bool stopping_ = false;
    bool writing_ = false;
    std::thread worker_;
    mutable std::mutex error_mutex_;
    std::wstring last_error_;
    std::atomic<std::uint64_t> pending_count_{0};
    std::uint64_t next_sequence_ = 1;
};

} // namespace nlsi::logging
