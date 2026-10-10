#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "TelemetryModel.h"
#include "JobFuelTracker.h"
#include "TelemetryUiState.h"
#include "logging/Logger.h"
#include "logging/TelemetryRecorder.h"
#include "providers/NLSIProvider.h"
#include "providers/TruckSimGpsProvider.h"
#include "providers/ScsPositionProvider.h"
#include "session/HistoryStore.h"
#include "session/JobManager.h"
#include "session/SessionManager.h"

namespace nlsi::telemetry {

class TelemetryCore {
public:
    enum class ShutdownState {
        Draining,
        Completed,
        Failed,
    };

    struct ShutdownProgress {
        ShutdownState state = ShutdownState::Draining;
        std::uint64_t queued_writes = 0;
        std::uint64_t pending_records = 0;
        std::wstring error;
    };

    TelemetryCore();
    ~TelemetryCore();
    TelemetryCore(const TelemetryCore&) = delete;
    TelemetryCore& operator=(const TelemetryCore&) = delete;

    bool Initialize(const std::wstring& user_data_directory = {});
    void BeginShutdown();
    ShutdownProgress PollShutdown();
    void Shutdown();

    ProviderStatus Status() const;
    TelemetrySnapshot Snapshot() const;
    TelemetryUiState UiState() const;
    session::HistorySnapshot History() const;
    std::vector<std::wstring> TakeNewlyCompletedJobNotifications();
    bool FlushLocalWrites(std::chrono::milliseconds timeout) const;
    bool IsFreshEnough() const;

private:
    void OnTruckSimUpdate(
        const TelemetrySnapshot& snapshot,
        ProviderState state,
        const ProviderStatus& diagnostics);
    void OnNLSIUpdate(
        const TelemetrySnapshot& snapshot,
        ProviderState state,
        const std::wstring& error);
    void OnProviderEvent(const std::string& packet);
    void OnTruckSimSample(const providers::RawTelemetrySample& sample);
    void OnScsPositionUpdate(const providers::ScsPositionSnapshot& snapshot);
    void RebuildStateLocked();
    bool IsShuttingDownLocked() const;
    void UpdateSessionLifecycleLocked();
    void EndSessionLocked(const std::wstring& reason, const std::wstring& timestamp);
    void LogProviderTransition(
        const std::wstring& provider,
        ProviderState state,
        const std::wstring& detail);

    mutable std::mutex mutex_;
    ProviderStatus status_;
    TelemetrySnapshot snapshot_;
    TelemetrySnapshot trucksim_snapshot_;
    TelemetrySnapshot nlsi_snapshot_;
    TelemetrySnapshot scs_configuration_snapshot_;
    TelemetryUiState ui_state_;
    std::wstring previous_job_identity_;
    ProviderState trucksim_state_ = ProviderState::Disconnected;
    ProviderState nlsi_state_ = ProviderState::Disconnected;
    std::uint32_t scs_configuration_game_id_ = 0;
    std::chrono::steady_clock::time_point session_started_monotonic_;
    std::wstring logged_trucksim_message_;
    bool shutdown_started_ = false;
    std::wstring shutdown_error_;
    std::unique_ptr<logging::Logger> logger_;
    std::unique_ptr<logging::TelemetryRecorder> telemetry_recorder_;
    std::unique_ptr<session::HistoryStore> history_store_;
    std::vector<std::wstring> newly_completed_job_notifications_;
    JobFuelTracker job_fuel_tracker_;
    session::SessionManager session_manager_;
    session::JobManager job_manager_;
    providers::TruckSimGpsProvider trucksim_provider_;
    providers::NLSIProvider nlsi_provider_;
    providers::ScsPositionProvider scs_position_provider_;
    providers::ScsPositionSnapshot scs_position_snapshot_;
};

} // namespace nlsi::telemetry
