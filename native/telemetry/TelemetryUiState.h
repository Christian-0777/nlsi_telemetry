#pragma once

#include <optional>
#include <string>

#include "TelemetryModel.h"
#include "providers/ScsPositionProvider.h"

namespace nlsi::telemetry {

struct FastTelemetryState {
    TelemetrySnapshot values;
};

struct JobSnapshot {
    bool available = false;
    std::wstring identity;
    std::wstring nlsi_job_id;
    TelemetryField<std::wstring> cargo;
    TelemetryField<std::wstring> cargo_id;
    TelemetryField<std::wstring> source_company;
    TelemetryField<std::wstring> source_city;
    TelemetryField<std::wstring> destination_company;
    TelemetryField<std::wstring> destination_city;
    TelemetryField<std::wstring> income;
    TelemetryField<std::wstring> planned_distance;
    TelemetryField<bool> loaded;
};

enum class JobStatus {
    NoJob,
    JobDetected,
    Loading,
    Loaded,
    InTransit,
    Delivering,
    Completed,
    Cancelled,
    Abandoned,
    Unknown
};

struct JobProgress {
    std::optional<double> remaining_distance_km;
    std::optional<double> progress_percent;
    std::optional<double> eta_seconds;
};

enum class SessionStatus {
    NoSession,
    Waiting,
    Driving,
    Paused,
    TelemetryLost
};

struct SessionState {
    SessionStatus status = SessionStatus::NoSession;
    std::wstring timestamp;
};

struct TelemetryUiState {
    FastTelemetryState fast;
    providers::ScsPositionSnapshot scs_position;
    JobSnapshot job;
    JobStatus job_status = JobStatus::Unknown;
    JobProgress progress;
    SessionState session;
    ProviderStatus providers;
};

TelemetryUiState MakeTelemetryUiState(
    const TelemetrySnapshot& snapshot,
    const ProviderStatus& providers,
    const std::wstring& previous_job_identity = {});

std::wstring FormatJobStatus(JobStatus status);
std::wstring FormatSessionStatus(SessionStatus status);

} // namespace nlsi::telemetry
