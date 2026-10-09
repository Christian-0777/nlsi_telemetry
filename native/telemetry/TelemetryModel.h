#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace nlsi::telemetry {

enum class ProviderState {
    Disconnected,
    Connecting,
    Connected,
    Stale
};

enum class CombinedProviderState {
    Disconnected,
    Partial,
    Connected
};

template <typename T>
struct TelemetryField {
    T value{};
    bool available = false;
    std::wstring source;
    std::wstring timestamp;
    bool stale = false;

    void Set(T new_value, const std::wstring& field_source, const std::wstring& field_timestamp) {
        value = std::move(new_value);
        available = true;
        source = field_source;
        timestamp = field_timestamp;
        stale = false;
    }

    void MarkStale() {
        if (available) {
            stale = true;
        }
    }
};

struct TelemetrySnapshot {
    bool connected = false;
    std::wstring timestamp;
    TelemetryField<std::wstring> game_id;
    TelemetryField<std::wstring> game_name;

    TelemetryField<bool> paused;
    TelemetryField<bool> driving;
    TelemetryField<bool> session_active;
    TelemetryField<bool> has_job;
    TelemetryField<double> speed_kmh;
    TelemetryField<double> rpm;
    TelemetryField<double> gear;
    TelemetryField<double> input_throttle;
    TelemetryField<double> effective_throttle;
    TelemetryField<double> input_brake;
    TelemetryField<double> effective_brake;
    TelemetryField<double> retarder_level;
    TelemetryField<bool> retarder_active;
    TelemetryField<double> cruise_control_speed;
    TelemetryField<bool> cruise_control_active;
    TelemetryField<double> fuel_liters;
    TelemetryField<double> fuel_range_km;
    TelemetryField<double> odometer_km;

    TelemetryField<double> navigation_distance_m;
    TelemetryField<double> navigation_distance_km;
    TelemetryField<double> navigation_time_s;
    TelemetryField<double> eta_seconds;

    TelemetryField<std::wstring> cargo_id;
    TelemetryField<std::wstring> cargo_name;
    TelemetryField<std::wstring> source_company;
    TelemetryField<std::wstring> source_city;
    TelemetryField<std::wstring> destination_company;
    TelemetryField<std::wstring> destination_city;
    TelemetryField<std::wstring> income;
    TelemetryField<std::wstring> planned_distance;
    TelemetryField<std::wstring> delivery_time;
    TelemetryField<bool> loaded;
    TelemetryField<std::wstring> market;
    TelemetryField<std::wstring> special_job;
};

struct ProviderStatus {
    ProviderState trucksim = ProviderState::Disconnected;
    CombinedProviderState combined = CombinedProviderState::Disconnected;
    std::wstring telemetry_freshness;
    std::wstring last_error;
    std::wstring storage_error;
    std::wstring sync_state = L"Offline; no authenticated API configured";
    std::wstring trucksim_stage = L"Not checked";
    std::wstring trucksim_layout = L"Unknown";
    std::wstring trucksim_mapping_name = L"Local\\TSGPSTelemetry";
    std::wstring trucksim_last_read;
    std::wstring trucksim_data_age = L"Unknown";
    std::wstring trucksim_source_timestamp;
    std::wstring trucksim_error;
    std::uint32_t trucksim_revision = 0;
    std::uint32_t trucksim_win32_error = 0;
    bool trucksim_mapping_open = false;
    bool trucksim_view_mapped = false;
};

double MetersToKilometers(double meters);
std::optional<double> CalculateEtaSeconds(
    const TelemetryField<double>& distance_m,
    const TelemetryField<double>& speed_kmh);
bool IsTelemetryFreshFor(std::chrono::milliseconds age);
void MarkSnapshotStale(TelemetrySnapshot& snapshot);
std::wstring FormatStatus(ProviderState state);
std::wstring FormatCombinedStatus(CombinedProviderState state);
bool IsPaused(bool paused_value);
bool IsDrivingState(bool paused_value, bool connected, bool has_speed);
std::wstring NormalizeJobType(const std::wstring& raw_type);

} // namespace nlsi::telemetry
