#pragma once

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QFile>
#include <QJsonObject>
#include <QSet>

namespace nlsi::logging {

class TelemetryRecorder {
public:
    struct Metrics {
        std::uint64_t accepted_records = 0;
        std::uint64_t persisted_records = 0;
        std::uint64_t batches_written = 0;
        std::uint64_t maximum_batch_size = 0;
        std::uint64_t write_failures = 0;
        std::uint64_t queued_records = 0;
        std::uint64_t queued_bytes = 0;
        std::uint64_t maximum_queue_depth = 0;
        std::uint64_t maximum_queue_bytes = 0;
        std::chrono::milliseconds oldest_pending_age{0};
        std::chrono::nanoseconds total_batch_write_time{0};
        std::chrono::nanoseconds maximum_batch_write_time{0};
    };

    explicit TelemetryRecorder(std::function<void()> before_write = {});
    ~TelemetryRecorder();
    TelemetryRecorder(const TelemetryRecorder&) = delete;
    TelemetryRecorder& operator=(const TelemetryRecorder&) = delete;

    bool Start(const std::wstring& user_data_directory, std::wstring* error = nullptr);
    bool Enqueue(const QJsonObject& sample, std::wstring* error = nullptr);
    bool Flush();
    bool FlushFor(std::chrono::milliseconds timeout);
    void RequestStop();
    bool StopFor(std::chrono::milliseconds timeout);
    void Stop();
    std::wstring LastError() const;
    std::uint64_t PendingCount() const;
    std::uint64_t QueuedCount() const;
    Metrics GetMetrics() const;
    bool IsRunning() const;

private:
    struct QueuedSample {
        QJsonObject sample;
        QString pending_path;
        std::uint64_t serialized_bytes = 0;
        std::chrono::steady_clock::time_point enqueued_at;
    };

    void WriteLoop();
    bool Recover();
    bool RecoverPendingSamples(const QSet<QString>& record_ids);
    bool RecoverTelemetryFile(const std::wstring& path);
    bool ReconcileSyncQueue(const QSet<QString>& record_ids);
    bool PersistPendingBatch(std::vector<QueuedSample>& samples);
    bool WriteBatch(std::vector<QueuedSample>& samples);
    bool OpenTelemetryFile(const QString& file_path);
    bool AppendPendingBatch(const std::vector<QueuedSample>& samples);
    void CloseFiles();
    void SetError(const QString& error, bool writer_failure = true);

    QString root_;
    QString telemetry_directory_;
    QString sync_queue_path_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<QueuedSample> queue_;
    bool running_ = false;
    bool ready_ = false;
    bool stopping_ = false;
    std::size_t writing_count_ = 0;
    std::uint64_t queued_bytes_ = 0;
    Metrics metrics_;
    std::chrono::steady_clock::time_point writing_oldest_enqueue_at_;
    std::thread worker_;
    mutable std::mutex error_mutex_;
    std::wstring last_error_;
    std::atomic<bool> writer_failed_{false};
    std::atomic<std::uint64_t> pending_count_{0};
    std::function<void()> before_write_;
    QString pending_directory_;
    std::uint64_t next_sequence_ = 1;
    std::uint64_t next_pending_batch_sequence_ = 1;
    QFile telemetry_file_;
    QString telemetry_file_path_;
    qint64 telemetry_file_size_ = 0;
    QFile sync_queue_file_;
};

} // namespace nlsi::logging
