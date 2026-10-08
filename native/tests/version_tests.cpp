#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "providers/NLSIProvider.h"
#include "telemetry/TelemetryModel.h"
#include "telemetry/TelemetryUiState.h"
#include "updater/GitHubUpdater.h"

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const std::string kValidPacket = R"json({
  "type":"telemetry",
  "timestamp":"2026-10-07T08:36:46.000Z",
  "game":{"id":"ets2","name":"Euro Truck Simulator 2","version":"1.61.1.1s","telemetry_api_version":"1.01"},
  "state":"driving",
  "truck":{
    "speed_mps":10.0,
    "speed_kmh":36.0,
    "rpm":1500,
    "gear":6,
    "input_throttle":0.85,
    "effective_throttle":0.37,
    "input_brake":0.60,
    "effective_brake":0.14,
    "retarder_level":2,
    "retarder_active":true,
    "cruise_control_speed":67.5,
    "cruise_control_active":true,
    "fuel_liters":623.7,
    "fuel_range_km":812.0,
    "odometer_km":9645.4,
    "navigation_distance_m":191185.671875,
    "navigation_time_s":3661
  },
  "configurations":{
    "job":{
      "job_id":"job-17",
      "cargo":"Furniture",
      "source.company":"Berlin Logistics",
      "source.city":"Berlin",
      "destination.company":"Paris Freight",
      "destination.city":"Paris",
      "income":25000,
      "planned_distance_km":1050,
      "delivery_time":"2026-10-08T12:00:00Z",
      "loaded":true,
      "market":"External",
      "special_job":false
    }
  }
})json";

void TestValidTelemetryAndUnits() {
    nlsi::telemetry::TelemetrySnapshot snapshot;
    std::wstring error;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(kValidPacket, snapshot, &error),
        "valid telemetry packet was rejected");
    Check(snapshot.connected && snapshot.driving.value && snapshot.session_active.value
        && !snapshot.paused.value,
        "driving telemetry state was not normalized");
    Check(snapshot.game_id.available && snapshot.game_id.value == L"ets2"
        && snapshot.game_name.available && snapshot.game_name.value == L"Euro Truck Simulator 2",
        "game identity was not normalized from the telemetry packet");
    Check(snapshot.speed_kmh.available && snapshot.speed_kmh.value == 36.0,
        "speed_kmh did not parse");
    Check(snapshot.rpm.available && snapshot.gear.available, "RPM or gear was unavailable");
    Check(snapshot.navigation_distance_km.available, "normalized navigation distance is unavailable");
    Check(std::abs(snapshot.navigation_distance_km.value - 191.185671875) < 0.000001,
        "navigation meters were not converted to kilometers");
    Check(snapshot.navigation_time_s.available && snapshot.navigation_time_s.value == 3661.0,
        "navigation time did not parse");
    Check(snapshot.eta_seconds.available && snapshot.eta_seconds.value > 19000.0,
        "ETA was not calculated from valid distance and speed");
}

void TestMissingAndInvalidFields() {
    const std::string packet = R"json({
      "type":"telemetry","timestamp":"2026-10-07T08:36:46.000Z",
      "state":"paused","truck":{"speed_mps":2.0,"rpm":null,"navigation_distance_m":null},
      "configurations":{}
    })json";
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(packet, snapshot),
        "valid packet with missing fields was rejected");
    Check(snapshot.speed_kmh.available && std::abs(snapshot.speed_kmh.value - 7.2) < 0.000001,
        "speed_mps fallback was not converted to km/h");
    Check(!snapshot.rpm.available && !snapshot.navigation_distance_m.available,
        "null fields were incorrectly marked available");
    Check(!snapshot.fuel_liters.available && !snapshot.has_job.value,
        "missing fuel or job data was fabricated");

    std::wstring error;
    Check(!nlsi::providers::NLSIProvider::ParseTelemetryPacket(
        R"json({"type":"telemetry","state":"driving","truck":{}})json", snapshot, &error),
        "telemetry without timestamp was accepted");
    Check(!error.empty(), "invalid packet did not provide an error");
    Check(!nlsi::providers::NLSIProvider::ParseTelemetryPacket("{invalid json", snapshot, &error),
        "malformed JSON was accepted");
}

void TestThrottleBrakeCruiseAndJobParsing() {
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(kValidPacket, snapshot),
        "valid telemetry packet was rejected");
    Check(snapshot.input_throttle.available && snapshot.input_throttle.value == 0.85
        && snapshot.effective_throttle.available && snapshot.effective_throttle.value == 0.37,
        "input/effective throttle were not parsed independently");
    Check(snapshot.input_brake.available && snapshot.input_brake.value == 0.60
        && snapshot.effective_brake.available && snapshot.effective_brake.value == 0.14,
        "input/effective brake were not parsed independently");
    Check(snapshot.retarder_active.available && snapshot.retarder_active.value,
        "retarder active state did not parse");
    Check(snapshot.cruise_control_speed.available && snapshot.cruise_control_speed.value == 67.5
        && snapshot.cruise_control_active.available && snapshot.cruise_control_active.value,
        "cruise control state did not parse");
    Check(snapshot.has_job.value && snapshot.cargo_id.value == L"job-17"
        && snapshot.cargo_name.value == L"Furniture", "cargo id/name did not parse");
    Check(snapshot.source_company.value == L"Berlin Logistics" && snapshot.source_city.value == L"Berlin"
        && snapshot.destination_company.value == L"Paris Freight"
        && snapshot.destination_city.value == L"Paris", "job route did not parse");
    Check(snapshot.income.value == L"25000" && snapshot.planned_distance.value == L"1050"
        && snapshot.delivery_time.value == L"2026-10-08T12:00:00Z", "job details did not parse");
    Check(snapshot.loaded.available && snapshot.loaded.value
        && snapshot.market.value == L"External" && snapshot.special_job.value == L"false",
        "job flags or market did not parse");
}

void TestPausedStateAndZeroSpeedEta() {
    const std::string packet = R"json({
      "type":"telemetry","timestamp":"2026-10-07T08:36:46.000Z",
      "state":"paused","truck":{"speed_kmh":0,"navigation_distance_m":1000},
      "configurations":{}
    })json";
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(packet, snapshot),
        "paused telemetry packet was rejected");
    Check(snapshot.paused.value && !snapshot.driving.value, "paused telemetry was marked Driving");
    Check(!snapshot.eta_seconds.available, "zero-speed ETA was fabricated");
}

void TestStaleTelemetryStopsDriving() {
    using namespace std::chrono_literals;
    Check(nlsi::telemetry::IsTelemetryFreshFor(4999ms), "telemetry became stale before timeout");
    Check(!nlsi::telemetry::IsTelemetryFreshFor(5000ms), "telemetry remained fresh at timeout");
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(kValidPacket, snapshot),
        "valid telemetry packet was rejected");
    nlsi::telemetry::MarkSnapshotStale(snapshot);
    Check(!snapshot.connected && !snapshot.driving.available && !snapshot.driving.value
        && !snapshot.session_active.available && !snapshot.session_active.value,
        "stale telemetry did not clear active connection/session state");
    Check(snapshot.speed_kmh.available && snapshot.speed_kmh.stale,
        "stale telemetry did not retain and mark last value");
    Check(!nlsi::telemetry::CalculateEtaSeconds(snapshot.navigation_distance_m, snapshot.speed_kmh),
        "ETA was calculated from stale data");
}

void TestUiJobIdentityProgressAndSessionStates() {
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(kValidPacket, snapshot),
        "valid telemetry packet was rejected");
    nlsi::telemetry::ProviderStatus providers;
    providers.nlsi = nlsi::telemetry::ProviderState::Connected;
    providers.rencloud = nlsi::telemetry::ProviderState::Disconnected;
    providers.combined = nlsi::telemetry::CombinedProviderState::Partial;

    const auto detected = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers);
    Check(detected.job.available && detected.job_status == nlsi::telemetry::JobStatus::JobDetected,
        "new job was not detected independently of its progress");
    Check(detected.progress.remaining_distance_km.has_value()
        && detected.progress.progress_percent.has_value()
        && detected.progress.eta_seconds.has_value(),
        "available navigation data did not produce job progress and ETA");

    const auto in_transit = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers, detected.job.identity);
    Check(in_transit.job.identity == detected.job.identity
        && in_transit.job_status == nlsi::telemetry::JobStatus::InTransit,
        "stable job identity changed while progress was being refreshed");
    const double old_progress = *in_transit.progress.progress_percent;
    snapshot.navigation_distance_km.Set(100.0, L"NLSI", L"new-sample");
    snapshot.navigation_distance_m.Set(100000.0, L"NLSI", L"new-sample");
    snapshot.eta_seconds.Set(10000.0, L"NLSI", L"new-sample");
    const auto progressed = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers, in_transit.job.identity);
    Check(progressed.job.identity == in_transit.job.identity
        && progressed.progress.progress_percent.has_value()
        && *progressed.progress.progress_percent > old_progress,
        "progress update modified job identity or failed to advance");

    snapshot.paused.Set(true, L"NLSI", L"paused");
    snapshot.driving.Set(false, L"NLSI", L"paused");
    const auto paused = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers, progressed.job.identity);
    Check(paused.session.status == nlsi::telemetry::SessionStatus::Paused
        && paused.job_status == nlsi::telemetry::JobStatus::Loaded,
        "paused telemetry was shown as driving or in transit");

    nlsi::telemetry::MarkSnapshotStale(snapshot);
    providers.nlsi = nlsi::telemetry::ProviderState::Stale;
    const auto lost = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers, progressed.job.identity);
    Check(lost.session.status == nlsi::telemetry::SessionStatus::TelemetryLost
        && lost.job.available && lost.job.identity == progressed.job.identity
        && !lost.progress.remaining_distance_km.has_value(),
        "stale provider lost session status, cleared the active job, or kept live progress");
}

void TestVersionComparison() {
    Check(nlsi::updater::GitHubUpdater::CompareVersions(L"1.3.0 Alpha", L"1.3.1") < 0,
        "version comparison failed for newer release");
    Check(nlsi::updater::GitHubUpdater::IsUpdateAvailable(L"1.3.1", L"1.3.0 Alpha") == false,
        "older release was reported as an update");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"valid telemetry and units", TestValidTelemetryAndUnits},
        {"missing and invalid fields", TestMissingAndInvalidFields},
        {"throttle, brake, cruise, and job parsing", TestThrottleBrakeCruiseAndJobParsing},
        {"paused state and zero-speed ETA", TestPausedStateAndZeroSpeedEta},
        {"stale telemetry", TestStaleTelemetryStopsDriving},
        {"UI job identity, progress, and session states", TestUiJobIdentityProgressAndSessionStates},
        {"version comparison", TestVersionComparison},
    };
    try {
        for (const auto& test : tests) {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
    std::cout << tests.size() << " tests passed.\n";
    return 0;
}
