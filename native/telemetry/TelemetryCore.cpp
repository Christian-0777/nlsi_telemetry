#include "TelemetryCore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace {

std::wstring UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::wostringstream output;
    output << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%S")
           << L'.' << std::setw(3) << std::setfill(L'0') << milliseconds.count() << L'Z';
    return output.str();
}

} // namespace

namespace nlsi::telemetry {

TelemetryCore::TelemetryCore() = default;

TelemetryCore::~TelemetryCore() {
    Shutdown();
}

bool TelemetryCore::Initialize(const std::wstring& user_data_directory) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        status_.trucksim = ProviderState::Connecting;
        status_.combined = CombinedProviderState::Disconnected;
        status_.telemetry_freshness = L"Waiting for a telemetry sample";
        status_.trucksim_stage = L"Waiting for Local\\TSGPSTelemetry";
        status_.trucksim_layout = L"Not detected";
        snapshot_ = {};
        trucksim_snapshot_ = {};
        ui_state_ = {};
        previous_job_identity_.clear();
        trucksim_state_ = ProviderState::Connecting;
        logged_trucksim_message_.clear();
        if (!user_data_directory.empty()) {
            const QString root = QString::fromStdWString(user_data_directory);
            logger_ = std::make_unique<logging::Logger>(
                QDir(root).filePath(QStringLiteral("logs/NLSI-Exclusive-Logbook.txt")).toStdWString());
            history_store_ = std::make_unique<session::HistoryStore>(*logger_);
            const QString legacy_user_data = QDir(QFileInfo(root).absolutePath())
                .filePath(QStringLiteral("NLSI Exclusive Logbook"));
            if (!history_store_->Initialize(
                    root, QCoreApplication::applicationDirPath(), legacy_user_data)) {
                status_.storage_error = history_store_->Snapshot().error.toStdWString();
            }
            std::wstring log_error;
            if (!logger_->Log(L"[startup] NLSI Exclusive Logbook v1.3.8 Alpha", &log_error)) {
                status_.storage_error = std::move(log_error);
            }
        }
    }

    const bool trucksim_started = trucksim_provider_.Start(
        [this](const TelemetrySnapshot& snapshot, ProviderState state, const ProviderStatus& diagnostics) {
            OnTruckSimUpdate(snapshot, state, diagnostics);
        },
        [this](const std::string& packet) {
            OnProviderEvent(packet);
        });
    if (!trucksim_started) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.trucksim = ProviderState::Disconnected;
        status_.trucksim_stage = L"Provider thread could not be started";
        status_.trucksim_error = L"Unable to start TruckSim GPS shared-memory reader thread.";
        RebuildStateLocked();
    }
    return trucksim_started;
}

void TelemetryCore::Shutdown() {
    trucksim_provider_.Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_manager_.IsActive()) {
        EndSessionLocked(L"application_shutdown", UtcNow());
    }
    trucksim_state_ = ProviderState::Disconnected;
    status_.trucksim = trucksim_state_;
    status_.combined = CombinedProviderState::Disconnected;
    status_.telemetry_freshness = L"offline";
    status_.last_error.clear();
    trucksim_snapshot_.connected = false;
    RebuildStateLocked();
}

ProviderStatus TelemetryCore::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

TelemetrySnapshot TelemetryCore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

TelemetryUiState TelemetryCore::UiState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ui_state_;
}

session::HistorySnapshot TelemetryCore::History() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_store_ ? history_store_->Snapshot() : session::HistorySnapshot{};
}

bool TelemetryCore::IsFreshEnough() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.combined != CombinedProviderState::Disconnected;
}

void TelemetryCore::OnTruckSimUpdate(
    const TelemetrySnapshot& snapshot,
    ProviderState state,
    const ProviderStatus& diagnostics) {
    std::lock_guard<std::mutex> lock(mutex_);
    trucksim_snapshot_ = snapshot;
    trucksim_state_ = state;
    if (state != ProviderState::Connected) {
        MarkSnapshotStale(trucksim_snapshot_);
    }
    status_.trucksim = state;
    status_.trucksim_stage = diagnostics.trucksim_stage;
    status_.trucksim_layout = diagnostics.trucksim_layout;
    status_.trucksim_mapping_name = diagnostics.trucksim_mapping_name;
    status_.trucksim_last_read = diagnostics.trucksim_last_read;
    status_.trucksim_data_age = diagnostics.trucksim_data_age;
    status_.trucksim_source_timestamp = diagnostics.trucksim_source_timestamp;
    status_.trucksim_error = diagnostics.trucksim_error;
    status_.trucksim_revision = diagnostics.trucksim_revision;
    status_.trucksim_win32_error = diagnostics.trucksim_win32_error;
    status_.trucksim_mapping_open = diagnostics.trucksim_mapping_open;
    status_.trucksim_view_mapped = diagnostics.trucksim_view_mapped;
    if (status_.last_error.empty()) {
        status_.last_error = diagnostics.trucksim_error;
    }
    const std::wstring message = diagnostics.trucksim_error.empty()
        ? diagnostics.trucksim_stage
        : diagnostics.trucksim_error;
    if (message != logged_trucksim_message_) {
        LogProviderTransition(L"TruckSim GPS", state, message);
        logged_trucksim_message_ = message;
    }
    RebuildStateLocked();
    UpdateSessionLifecycleLocked();
}

void TelemetryCore::OnProviderEvent(const std::string& packet_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!history_store_) {
        return;
    }
    const QByteArray raw_packet(packet_bytes.data(), static_cast<qsizetype>(packet_bytes.size()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(raw_packet, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        status_.last_error = L"Could not parse a telemetry provider event for persistence.";
        return;
    }
    const QJsonObject packet = document.object();
    const QString event_type = packet.value(QStringLiteral("event")).toString();
    const QString timestamp = packet.value(QStringLiteral("timestamp")).toString();
    const QJsonObject details = packet.value(QStringLiteral("data")).toObject();
    bool persisted = history_store_->RecordProviderEvent(raw_packet);
    if (!persisted) {
        status_.storage_error = history_store_->Snapshot().error.toStdWString();
    }
    if (event_type == QStringLiteral("job.delivered")
        || event_type == QStringLiteral("job.cancelled")) {
        persisted = history_store_->RecordJob(ui_state_.job, event_type, timestamp, details)
            && persisted;
        if (!persisted) {
            status_.storage_error = history_store_->Snapshot().error.toStdWString();
        }
    }
    if (persisted) {
        status_.storage_error.clear();
    }
}

void TelemetryCore::RebuildStateLocked() {
    snapshot_ = trucksim_snapshot_;
    snapshot_.connected = trucksim_state_ == ProviderState::Connected;
    if (!snapshot_.connected) {
        MarkSnapshotStale(snapshot_);
    }

    if (snapshot_.paused.available && snapshot_.paused.value) {
        snapshot_.driving.Set(false, snapshot_.paused.source, snapshot_.paused.timestamp);
    }
    if (!snapshot_.connected) {
        snapshot_.driving.value = false;
        snapshot_.driving.available = false;
        snapshot_.session_active.value = false;
        snapshot_.session_active.available = false;
    }
    if (trucksim_state_ == ProviderState::Connected && !trucksim_snapshot_.timestamp.empty()) {
        snapshot_.timestamp = trucksim_snapshot_.timestamp;
    }

    status_.trucksim = trucksim_state_;
    status_.combined = snapshot_.connected
        ? CombinedProviderState::Connected
        : CombinedProviderState::Disconnected;
    status_.telemetry_freshness = snapshot_.timestamp.empty()
        ? L"No telemetry sample received"
        : snapshot_.timestamp;
    ui_state_ = MakeTelemetryUiState(snapshot_, status_, previous_job_identity_);
    if (ui_state_.job.available && !ui_state_.job.identity.empty()) {
        previous_job_identity_ = ui_state_.job.identity;
        job_manager_.SetCurrentJob(ui_state_.job.identity);
    } else if (snapshot_.has_job.available && !snapshot_.has_job.value) {
        previous_job_identity_.clear();
        job_manager_.ClearCurrentJob();
    }
}

void TelemetryCore::UpdateSessionLifecycleLocked() {
    const bool active = snapshot_.connected
        && snapshot_.session_active.available
        && snapshot_.session_active.value;
    if (active && !session_manager_.IsActive()) {
        const std::wstring timestamp = snapshot_.timestamp.empty() ? UtcNow() : snapshot_.timestamp;
        const std::wstring game = snapshot_.game_id.available
            ? snapshot_.game_id.value
            : (snapshot_.game_name.available ? snapshot_.game_name.value : L"Unknown");
        session_manager_.Start(timestamp, game);
        session_started_monotonic_ = std::chrono::steady_clock::now();
        if (history_store_) {
            if (!history_store_->StartSession(
                    session_manager_.CurrentId(), session_manager_.Game(), session_manager_.StartedAt())) {
                status_.storage_error = history_store_->Snapshot().error.toStdWString();
            } else {
                status_.storage_error.clear();
            }
        }
    } else if (!active && session_manager_.IsActive()) {
        const std::wstring reason = snapshot_.connected
            ? L"gameplay_ended"
            : L"telemetry_lost";
        EndSessionLocked(reason, snapshot_.timestamp.empty() ? UtcNow() : snapshot_.timestamp);
    }
}

void TelemetryCore::EndSessionLocked(
    const std::wstring& reason,
    const std::wstring& timestamp) {
    if (!session_manager_.IsActive()) {
        return;
    }
    const std::wstring id = session_manager_.CurrentId();
    const double duration = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - session_started_monotonic_).count();
    if (history_store_) {
        if (!history_store_->EndSession(id, timestamp, reason, duration)) {
            status_.storage_error = history_store_->Snapshot().error.toStdWString();
        } else {
            status_.storage_error.clear();
        }
    }
    session_manager_.Stop();
}

void TelemetryCore::LogProviderTransition(
    const std::wstring& provider,
    ProviderState state,
    const std::wstring& detail) {
    if (!logger_) {
        return;
    }
    std::wstring error;
    const std::wstring message = L"[provider] " + provider + L" "
        + FormatStatus(state) + L": " + detail;
    if (!logger_->Log(message, &error)) {
        status_.storage_error = std::move(error);
    }
}

} // namespace nlsi::telemetry
