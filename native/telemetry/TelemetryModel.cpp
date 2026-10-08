#include "TelemetryModel.h"

#include <cmath>

namespace nlsi::telemetry {

double MetersToKilometers(double meters) {
    return meters / 1000.0;
}

std::optional<double> CalculateEtaSeconds(
    const TelemetryField<double>& distance_m,
    const TelemetryField<double>& speed_kmh) {
    if (!distance_m.available || distance_m.stale || !speed_kmh.available || speed_kmh.stale
        || !std::isfinite(distance_m.value) || !std::isfinite(speed_kmh.value)
        || distance_m.value < 0.0 || speed_kmh.value <= 0.0) {
        return std::nullopt;
    }
    return distance_m.value / (speed_kmh.value / 3.6);
}

bool IsTelemetryFreshFor(std::chrono::milliseconds age) {
    return age >= std::chrono::milliseconds::zero() && age < std::chrono::seconds(5);
}

void MarkSnapshotStale(TelemetrySnapshot& snapshot) {
    snapshot.connected = false;
    snapshot.paused.MarkStale();
    snapshot.paused.available = false;
    snapshot.paused.value = false;
    snapshot.driving.MarkStale();
    snapshot.driving.available = false;
    snapshot.driving.value = false;
    snapshot.session_active.MarkStale();
    snapshot.session_active.available = false;
    snapshot.session_active.value = false;
    snapshot.has_job.MarkStale();
    snapshot.has_job.available = false;
    snapshot.has_job.value = false;
#define NLSI_MARK_STALE(field) snapshot.field.MarkStale()
    NLSI_MARK_STALE(game_id);
    NLSI_MARK_STALE(game_name);
    NLSI_MARK_STALE(speed_kmh);
    NLSI_MARK_STALE(rpm);
    NLSI_MARK_STALE(gear);
    NLSI_MARK_STALE(input_throttle);
    NLSI_MARK_STALE(effective_throttle);
    NLSI_MARK_STALE(input_brake);
    NLSI_MARK_STALE(effective_brake);
    NLSI_MARK_STALE(retarder_level);
    NLSI_MARK_STALE(retarder_active);
    NLSI_MARK_STALE(cruise_control_speed);
    NLSI_MARK_STALE(cruise_control_active);
    NLSI_MARK_STALE(fuel_liters);
    NLSI_MARK_STALE(fuel_range_km);
    NLSI_MARK_STALE(odometer_km);
    NLSI_MARK_STALE(navigation_distance_m);
    NLSI_MARK_STALE(navigation_distance_km);
    NLSI_MARK_STALE(navigation_time_s);
    NLSI_MARK_STALE(eta_seconds);
    snapshot.eta_seconds.available = false;
    NLSI_MARK_STALE(cargo_id);
    NLSI_MARK_STALE(cargo_name);
    NLSI_MARK_STALE(source_company);
    NLSI_MARK_STALE(source_city);
    NLSI_MARK_STALE(destination_company);
    NLSI_MARK_STALE(destination_city);
    NLSI_MARK_STALE(income);
    NLSI_MARK_STALE(planned_distance);
    NLSI_MARK_STALE(delivery_time);
    NLSI_MARK_STALE(loaded);
    NLSI_MARK_STALE(market);
    NLSI_MARK_STALE(special_job);
#undef NLSI_MARK_STALE
}

std::wstring FormatStatus(ProviderState state) {
    switch (state) {
    case ProviderState::Disconnected: return L"DISCONNECTED";
    case ProviderState::Connecting: return L"CONNECTING";
    case ProviderState::Connected: return L"CONNECTED";
    case ProviderState::Stale: return L"STALE";
    }
    return L"DISCONNECTED";
}

std::wstring FormatCombinedStatus(CombinedProviderState state) {
    switch (state) {
    case CombinedProviderState::Disconnected: return L"DISCONNECTED";
    case CombinedProviderState::Partial: return L"PARTIAL";
    case CombinedProviderState::Connected: return L"CONNECTED";
    }
    return L"DISCONNECTED";
}

bool IsPaused(bool paused_value) {
    return paused_value;
}

bool IsDrivingState(bool paused_value, bool connected, bool has_speed) {
    return connected && !paused_value && has_speed;
}

std::wstring NormalizeJobType(const std::wstring& raw_type) {
    return raw_type.empty() ? L"Unavailable" : raw_type;
}

} // namespace nlsi::telemetry
