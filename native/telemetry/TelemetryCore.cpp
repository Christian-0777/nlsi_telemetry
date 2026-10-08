#include "TelemetryCore.h"

namespace nlsi::telemetry {

TelemetryCore::TelemetryCore() = default;

TelemetryCore::~TelemetryCore() {
    Shutdown();
}

bool TelemetryCore::Initialize() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        status_.nlsi = ProviderState::Connecting;
        status_.combined = CombinedProviderState::Disconnected;
        status_.telemetry_freshness = L"Waiting for a telemetry sample";
        status_.last_error.clear();
        snapshot_ = {};
        ui_state_ = {};
        previous_job_identity_.clear();
    }
    const bool started = nlsi_provider_.Start(
        [this](const TelemetrySnapshot& snapshot, ProviderState state, const std::wstring& error) {
            OnProviderUpdate(snapshot, state, error);
        });
    if (!started) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.nlsi = ProviderState::Disconnected;
        status_.last_error = L"Telemetry receiver thread could not be started.";
        return false;
    }
    return true;
}

void TelemetryCore::Shutdown() {
    nlsi_provider_.Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    status_.nlsi = ProviderState::Disconnected;
    status_.combined = CombinedProviderState::Disconnected;
    status_.telemetry_freshness = L"offline";
    status_.last_error.clear();
    MarkSnapshotStale(snapshot_);
    ui_state_ = MakeTelemetryUiState(snapshot_, status_, previous_job_identity_);
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

bool TelemetryCore::IsFreshEnough() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.nlsi == ProviderState::Connected;
}

void TelemetryCore::OnProviderUpdate(
    const TelemetrySnapshot& snapshot,
    ProviderState state,
    const std::wstring& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snapshot;
    snapshot_.connected = state == ProviderState::Connected;
    if (!snapshot_.connected) {
        snapshot_.driving.value = false;
        snapshot_.driving.available = false;
        if (state == ProviderState::Stale || state == ProviderState::Disconnected) {
            snapshot_.session_active.value = false;
            snapshot_.session_active.available = false;
        }
    }
    if (snapshot_.paused.available && snapshot_.paused.value) {
        snapshot_.driving.Set(false, snapshot_.paused.source, snapshot_.paused.timestamp);
    }
    status_.nlsi = state;
    status_.rencloud = ProviderState::Disconnected;
    status_.combined = state == ProviderState::Connected
        ? CombinedProviderState::Partial
        : CombinedProviderState::Disconnected;
    status_.telemetry_freshness = snapshot_.timestamp.empty()
        ? L"No telemetry sample received"
        : snapshot_.timestamp;
    status_.last_error = error;
    ui_state_ = MakeTelemetryUiState(snapshot_, status_, previous_job_identity_);
    if (ui_state_.job.available) {
        previous_job_identity_ = ui_state_.job.identity;
    } else if (snapshot_.has_job.available && !snapshot_.has_job.value) {
        previous_job_identity_.clear();
    }
}

} // namespace nlsi::telemetry
