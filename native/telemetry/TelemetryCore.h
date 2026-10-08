#pragma once

#include <mutex>
#include <string>

#include "TelemetryModel.h"
#include "TelemetryUiState.h"
#include "providers/NLSIProvider.h"

namespace nlsi::telemetry {

class TelemetryCore {
public:
    TelemetryCore();
    ~TelemetryCore();
    TelemetryCore(const TelemetryCore&) = delete;
    TelemetryCore& operator=(const TelemetryCore&) = delete;

    bool Initialize();
    void Shutdown();

    ProviderStatus Status() const;
    TelemetrySnapshot Snapshot() const;
    TelemetryUiState UiState() const;
    bool IsFreshEnough() const;

private:
    void OnProviderUpdate(
        const TelemetrySnapshot& snapshot,
        ProviderState state,
        const std::wstring& error);

    mutable std::mutex mutex_;
    ProviderStatus status_;
    TelemetrySnapshot snapshot_;
    TelemetryUiState ui_state_;
    std::wstring previous_job_identity_;
    providers::NLSIProvider nlsi_provider_;
};

} // namespace nlsi::telemetry
