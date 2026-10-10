#include "TelemetryUiState.h"

#include <algorithm>
#include <sstream>

namespace {

std::wstring JobIdentity(const nlsi::telemetry::TelemetrySnapshot& snapshot) {
    std::wstring key;
    const auto append = [&key](const auto& field) {
        if (field.available && !field.value.empty()) {
            if (!key.empty()) {
                key.push_back(L'\x1f');
            }
            key += field.value;
        }
    };
    append(snapshot.cargo_id);
    append(snapshot.cargo_name);
    append(snapshot.source_company);
    append(snapshot.source_city);
    append(snapshot.destination_company);
    append(snapshot.destination_city);
    append(snapshot.income);
    return key;
}

std::optional<double> ParseNonNegativeNumber(const std::wstring& text) {
    std::wistringstream input(text);
    double value = 0.0;
    input >> value;
    if (!input || value < 0.0) {
        return std::nullopt;
    }
    input >> std::ws;
    return input.eof() ? std::optional<double>(value) : std::nullopt;
}

} // namespace

namespace nlsi::telemetry {

TelemetryUiState MakeTelemetryUiState(
    const TelemetrySnapshot& snapshot,
    const ProviderStatus& providers,
    const std::wstring& previous_job_identity) {
    TelemetryUiState state;
    state.fast.values = snapshot;
    state.providers = providers;
    state.session.timestamp = snapshot.timestamp;

    const bool has_current_job = snapshot.connected
        && snapshot.has_job.available && snapshot.has_job.value;
    const bool keep_stale_job = !snapshot.connected
        && (snapshot.cargo_id.available || snapshot.cargo_name.available);
    if (has_current_job || keep_stale_job) {
        state.job.available = true;
        state.job.identity = JobIdentity(snapshot);
        state.job.cargo = snapshot.cargo_name;
        state.job.cargo_id = snapshot.cargo_id;
        state.job.source_company = snapshot.source_company;
        state.job.source_city = snapshot.source_city;
        state.job.destination_company = snapshot.destination_company;
        state.job.destination_city = snapshot.destination_city;
        state.job.income = snapshot.income;
        state.job.planned_distance = snapshot.planned_distance;
        state.job.loaded = snapshot.loaded;

        if (keep_stale_job) {
            state.job_status = JobStatus::Unknown;
        } else if (!state.job.identity.empty()
            && state.job.identity != previous_job_identity) {
            state.job_status = JobStatus::JobDetected;
        } else if (snapshot.loaded.available && snapshot.loaded.value) {
            state.job_status = snapshot.paused.available && snapshot.paused.value
                ? JobStatus::Loaded
                : (snapshot.driving.available && snapshot.driving.value
                    ? JobStatus::InTransit : JobStatus::Loaded);
        } else if (snapshot.loaded.available) {
            state.job_status = JobStatus::Loading;
        } else if (snapshot.driving.available && snapshot.driving.value && !snapshot.driving.stale) {
            state.job_status = JobStatus::InTransit;
        } else {
            state.job_status = JobStatus::Unknown;
        }

        if (snapshot.connected
            && snapshot.navigation_distance_km.available
            && !snapshot.navigation_distance_km.stale
            && snapshot.navigation_distance_km.value >= 0.0) {
            state.progress.remaining_distance_km = snapshot.navigation_distance_km.value;
            const auto planned_km = snapshot.planned_distance.available && !snapshot.planned_distance.stale
                ? ParseNonNegativeNumber(snapshot.planned_distance.value)
                : std::nullopt;
            if (planned_km && *planned_km > 0.0) {
                state.progress.progress_percent = std::clamp(
                    (1.0 - snapshot.navigation_distance_km.value / *planned_km) * 100.0,
                    0.0,
                    100.0);
            }
        }
        if (snapshot.navigation_time_s.available && !snapshot.navigation_time_s.stale
            && std::isfinite(snapshot.navigation_time_s.value)
            && snapshot.navigation_time_s.value >= 0.0) {
            state.progress.eta_seconds = snapshot.navigation_time_s.value;
        } else if (snapshot.eta_seconds.available && !snapshot.eta_seconds.stale
            && std::isfinite(snapshot.eta_seconds.value)
            && snapshot.eta_seconds.value >= 0.0) {
            state.progress.eta_seconds = snapshot.eta_seconds.value;
        }
    } else if (snapshot.has_job.available && !snapshot.has_job.value) {
        state.job_status = JobStatus::NoJob;
    }

    if (!snapshot.connected) {
        state.session.status = providers.trucksim == ProviderState::Connecting
            ? SessionStatus::Waiting
            : SessionStatus::TelemetryLost;
    } else if (!snapshot.session_active.available || !snapshot.session_active.value) {
        state.session.status = SessionStatus::NoSession;
    } else if (snapshot.paused.available && snapshot.paused.value) {
        state.session.status = SessionStatus::Paused;
    } else if (snapshot.driving.available && snapshot.driving.value) {
        state.session.status = SessionStatus::Driving;
    } else {
        state.session.status = SessionStatus::Waiting;
    }
    return state;
}

std::wstring FormatJobStatus(JobStatus status) {
    switch (status) {
    case JobStatus::NoJob: return L"NO JOB";
    case JobStatus::JobDetected: return L"JOB DETECTED";
    case JobStatus::Loading: return L"LOADING";
    case JobStatus::Loaded: return L"LOADED";
    case JobStatus::InTransit: return L"IN TRANSIT";
    case JobStatus::Delivering: return L"DELIVERING";
    case JobStatus::Completed: return L"COMPLETED";
    case JobStatus::Cancelled: return L"CANCELLED";
    case JobStatus::Abandoned: return L"ABANDONED";
    case JobStatus::Unknown: return L"UNKNOWN";
    }
    return L"UNKNOWN";
}

std::wstring FormatSessionStatus(SessionStatus status) {
    switch (status) {
    case SessionStatus::NoSession: return L"NO ACTIVE SESSION";
    case SessionStatus::Waiting: return L"WAITING";
    case SessionStatus::Driving: return L"DRIVING";
    case SessionStatus::Paused: return L"PAUSED";
    case SessionStatus::TelemetryLost: return L"TELEMETRY LOST";
    }
    return L"UNKNOWN";
}

} // namespace nlsi::telemetry
