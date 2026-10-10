#include <cmath>
#include <algorithm>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <array>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include "logging/Logger.h"
#include "logging/TelemetryRecorder.h"
#include "providers/NLSIProvider.h"
#include "providers/TruckSimGpsProvider.h"
#include "providers/ScsPositionIpc.h"
#include "session/HistoryStore.h"
#include "telemetry/TelemetryModel.h"
#include "telemetry/TelemetryUiState.h"
#include "time/ApplicationTime.h"
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

void TestTruckSimGpsRevision13LayoutAndUnits() {
    std::array<std::uint8_t, 32 * 1024> bytes{};
    const auto put = [&bytes](std::size_t offset, const auto& value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    const auto put_text = [&bytes](std::size_t offset, const char* value, std::size_t size) {
        std::memcpy(bytes.data() + offset, value, std::min(std::strlen(value), size));
    };
    const std::uint32_t revision = 13;
    const std::uint32_t game = 2;
    const std::uint32_t planned_distance_km = 1200;
    const std::uint32_t retarder = 2;
    const std::uint64_t income = 25000;
    const float speed_mps = 10.0f;
    const float cruise_speed_mps = 20.0f;
    const float navigation_distance_m = 60000.0f;
    const float navigation_time_s = 3600.0f;
    const bool sdk_active = true;
    const bool paused = false;
    const bool loaded = true;
    const bool on_job = true;
    put(0, sdk_active);
    put(4, paused);
    put(8, std::uint64_t{101});
    put(16, std::uint64_t{202});
    put(24, std::uint64_t{303});
    put(40, revision);
    put(52, game);
    put(100, planned_distance_km);
    put(108, retarder);
    put(504, std::int32_t{6});
    put(948, speed_mps);
    put(952, 1500.0f);
    put(988, cruise_speed_mps);
    put(1000, 500.0f);
    put(1008, 800.0f);
    put(1056, 50000.0f);
    put(1060, navigation_distance_m);
    put(1064, navigation_time_s);
    put(1564, loaded);
    put(2556, "job-17");
    put(2620, "Furniture");
    put(2748, "Paris");
    put(2876, "Paris Freight");
    put(3004, "Berlin");
    put(3132, "Berlin Logistics");
    put(3404, "External");
    put(4000, income);
    put(4300, on_job);

    nlsi::telemetry::TelemetrySnapshot snapshot;
    nlsi::providers::RawTelemetrySample raw_sample;
    std::uint32_t decoded_revision = 0;
    std::wstring error;
    Check(nlsi::providers::TruckSimGpsProvider::DecodeRevision13(
            bytes.data(), bytes.size(), snapshot, decoded_revision, error, &raw_sample),
        "revision-13 TruckSim GPS mapping was rejected");
    Check(decoded_revision == 13 && snapshot.game_id.value == L"ats"
        && snapshot.game_name.value == L"American Truck Simulator",
        "TruckSim GPS revision or game enum was decoded incorrectly");
    Check(snapshot.speed_kmh.available && snapshot.speed_kmh.value == 36.0
        && snapshot.cruise_control_speed.available
        && snapshot.cruise_control_speed.value == 72.0,
        "TruckSim GPS speed fields were not converted from m/s to km/h");
    Check(snapshot.navigation_distance_km.available
        && snapshot.navigation_distance_km.value == 60.0
        && snapshot.planned_distance.value == L"1200",
        "TruckSim GPS navigation or planned distance was not normalized");
    Check(snapshot.retarder_level.available && snapshot.retarder_level.value == 2.0
        && snapshot.retarder_active.available && snapshot.retarder_active.value,
        "revision-13 retarder uint32 at offset 108 was not decoded correctly");
    Check(raw_sample.mapping.size() == static_cast<qsizetype>(bytes.size())
        && raw_sample.revision == 13
        && raw_sample.source_timestamp == 101
        && raw_sample.simulation_timestamp == 202
        && raw_sample.render_timestamp == 303
        && raw_sample.source_fields.value(QStringLiteral("speed_mps")).toDouble() == 10.0
        && raw_sample.source_fields.value(QStringLiteral("cruise_control_speed_mps")).toDouble() == 20.0
        && raw_sample.source_availability.value(QStringLiteral("speed_mps")).toBool(),
        "raw TruckSim values, source timestamps, availability, or full mapping bytes were not retained");
    Check(snapshot.has_job.available && snapshot.has_job.value
        && snapshot.loaded.value && snapshot.cargo_id.value == L"job-17"
        && snapshot.cargo_name.value == L"Furniture"
        && snapshot.source_city.value == L"Berlin"
        && snapshot.destination_city.value == L"Paris",
        "TruckSim GPS current-job data was not decoded");

    const std::uint32_t inactive_retarder = 0;
    put(108, inactive_retarder);
    Check(nlsi::providers::TruckSimGpsProvider::DecodeRevision13(
            bytes.data(), bytes.size(), snapshot, decoded_revision, error)
        && snapshot.retarder_level.available && snapshot.retarder_level.value == 0.0
        && snapshot.retarder_active.available && !snapshot.retarder_active.value,
        "revision-13 retarder active state was not derived from a zero level");

    const std::uint32_t unsupported_revision = 14;
    put(40, unsupported_revision);
    Check(!nlsi::providers::TruckSimGpsProvider::DecodeRevision13(
            bytes.data(), bytes.size(), snapshot, decoded_revision, error)
        && decoded_revision == unsupported_revision
        && error.find(L"14") != std::wstring::npos,
        "unsupported TruckSim GPS revision was accepted or lacked a diagnostic");
    Check(!nlsi::providers::TruckSimGpsProvider::DecodeRevision13(
            bytes.data(), 1024, snapshot, decoded_revision, error)
        && !error.empty(),
        "undersized TruckSim GPS shared-memory view was accepted");
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
    Check(snapshot.retarder_level.available && snapshot.retarder_level.value == 2.0
        && snapshot.retarder_active.available && snapshot.retarder_active.value,
        "retarder level was not authoritative for active state");
    std::string contradictory_retarder = kValidPacket;
    const std::string active_level = "\"retarder_level\":2";
    const std::size_t retarder_level_position = contradictory_retarder.find(active_level);
    Check(retarder_level_position != std::string::npos,
        "retarder level fixture could not be located");
    contradictory_retarder.replace(
        retarder_level_position, active_level.size(), "\"retarder_level\":0");
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(
            contradictory_retarder, snapshot)
        && snapshot.retarder_level.available && snapshot.retarder_level.value == 0.0
        && snapshot.retarder_active.available && !snapshot.retarder_active.value,
        "a separate retarder boolean overrode the reported level");
    Check(snapshot.cruise_control_speed.available && snapshot.cruise_control_speed.value == 67.5
        && snapshot.cruise_control_active.available && snapshot.cruise_control_active.value,
        "cruise control state did not parse");
    const std::string set_speed_only = R"json({
          "type":"telemetry","timestamp":"2026-10-07T08:36:46.000Z","state":"driving",
      "truck":{"cruise_control_speed":67.5},"configurations":{}
    })json";
    nlsi::telemetry::TelemetrySnapshot set_speed_snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(set_speed_only, set_speed_snapshot),
            "set-speed-only telemetry packet was rejected");
    Check(set_speed_snapshot.cruise_control_speed.available
            && set_speed_snapshot.cruise_control_speed.value == 67.5,
            "cruise-control set speed did not parse");
    Check(!set_speed_snapshot.cruise_control_active.available,
            "cruise-control set speed incorrectly confirmed active cruise control");
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

void TestScsPositionIpcDecodingAndFreshness() {
    using namespace nlsi::providers;
    ScsPositionIpcV1 sample{};
    sample.flags = kScsPositionFlagValid;
    sample.game_id = kScsPositionGameEts2;
    sample.x = -1234.5;
    sample.y = 87.25;
    sample.z = 9000.125;
    sample.timestamp_ms = 10000;
    std::array<std::uint8_t, sizeof(sample)> bytes{};
    std::memcpy(bytes.data(), &sample, sizeof(sample));

    ScsPositionFrame frame;
    std::wstring error;
    Check(DecodeScsPositionMapping(bytes.data(), bytes.size(), 12000, frame, error),
        "valid SCS position IPC sample did not decode");
    Check(frame.game_id == kScsPositionGameEts2
        && frame.x == -1234.5 && frame.y == 87.25 && frame.z == 9000.125
        && frame.age_ms == 2000 && !frame.stale,
        "SCS position IPC decoding changed coordinates or freshness");

    Check(DecodeScsPositionMapping(
            bytes.data(), bytes.size(), 14001, frame, error)
        && frame.stale && frame.age_ms == 4001,
        "old SCS position IPC data was not marked stale");
    Check(!DecodeScsPositionMapping(nullptr, 0, 12000, frame, error)
        && error.find(L"missing or truncated") != std::wstring::npos,
        "missing SCS position plugin data was not reported");

    sample.flags = 0;
    std::memcpy(bytes.data(), &sample, sizeof(sample));
    Check(!DecodeScsPositionMapping(bytes.data(), bytes.size(), 12000, frame, error),
        "unavailable game position was accepted as live data");
    sample.flags = kScsPositionFlagValid;
    sample.version = kScsPositionIpcVersion + 1;
    std::memcpy(bytes.data(), &sample, sizeof(sample));
    Check(!DecodeScsPositionMapping(bytes.data(), bytes.size(), 12000, frame, error),
        "unsupported SCS position IPC version was accepted");
}

void TestUiJobIdentityProgressAndSessionStates() {
    nlsi::telemetry::TelemetrySnapshot snapshot;
    Check(nlsi::providers::NLSIProvider::ParseTelemetryPacket(kValidPacket, snapshot),
        "valid telemetry packet was rejected");
    nlsi::telemetry::ProviderStatus providers;
    providers.trucksim = nlsi::telemetry::ProviderState::Connected;
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
    providers.trucksim = nlsi::telemetry::ProviderState::Stale;
    const auto lost = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers, progressed.job.identity);
    Check(lost.session.status == nlsi::telemetry::SessionStatus::TelemetryLost
        && lost.job.available && lost.job.identity == progressed.job.identity
        && !lost.progress.remaining_distance_km.has_value(),
        "stale provider lost session status, cleared the active job, or kept live progress");

    snapshot.connected = true;
    snapshot.has_job.Set(true, L"NLSI", L"sample");
    snapshot.cargo_id = {};
    snapshot.cargo_name = {};
    snapshot.source_company = {};
    snapshot.source_city = {};
    snapshot.destination_company = {};
    snapshot.destination_city = {};
    snapshot.income = {};
    snapshot.loaded.Set(true, L"NLSI", L"sample");
    snapshot.paused.Set(false, L"NLSI", L"sample");
    snapshot.driving.Set(true, L"NLSI", L"sample");
    const auto job_without_identity = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers);
    Check(job_without_identity.job_status == nlsi::telemetry::JobStatus::InTransit,
        "available loaded/driving evidence was discarded because job identity fields were missing");

    snapshot.loaded = {};
    const auto job_without_load_state = nlsi::telemetry::MakeTelemetryUiState(snapshot, providers);
    Check(job_without_load_state.job.available
        && job_without_load_state.job_status == nlsi::telemetry::JobStatus::InTransit,
        "an active job remained Unknown despite fresh driving evidence");
}

void TestHistoryAndTxtLogPersistence() {
    QTemporaryDir temporary_directory;
    Check(temporary_directory.isValid(), "temporary history directory could not be created");
    const QString root = temporary_directory.path();
    const QString legacy_root = QDir(root).filePath(QStringLiteral("legacy-app"));
    Check(QDir().mkpath(QDir(legacy_root).filePath(QStringLiteral("logs")))
        && QDir().mkpath(QDir(legacy_root).filePath(QStringLiteral("session_logs"))),
        "legacy user-data fixture could not be created");
    const QString legacy_user_data = QDir(root).filePath(QStringLiteral("legacy-user-data"));
    Check(QDir().mkpath(QDir(legacy_user_data).filePath(QStringLiteral("logs")))
        && QDir().mkpath(QDir(legacy_user_data).filePath(QStringLiteral("session_logs")))
        && QDir().mkpath(QDir(root).filePath(QStringLiteral("logs"))),
        "legacy LocalAppData fixture could not be created");
    QFile legacy_log(QDir(legacy_root).filePath(QStringLiteral("logs/previous.txt")));
    Check(legacy_log.open(QIODevice::WriteOnly | QIODevice::Text)
        && legacy_log.write("older log") > 0,
        "legacy log fixture could not be written");
    legacy_log.close();
    QFile legacy_user_log(QDir(legacy_user_data).filePath(QStringLiteral("logs/previous-user.txt")));
    Check(legacy_user_log.open(QIODevice::WriteOnly | QIODevice::Text)
        && legacy_user_log.write("older user log") > 0,
        "legacy LocalAppData log fixture could not be written");
    legacy_user_log.close();
    QFile existing_log(QDir(root).filePath(QStringLiteral("logs/keep.txt")));
    Check(existing_log.open(QIODevice::WriteOnly | QIODevice::Text)
        && existing_log.write("current log") > 0,
        "current log fixture could not be written");
    existing_log.close();
    QFile conflicting_legacy_log(QDir(legacy_user_data).filePath(QStringLiteral("logs/keep.txt")));
    Check(conflicting_legacy_log.open(QIODevice::WriteOnly | QIODevice::Text)
        && conflicting_legacy_log.write("older conflicting log") > 0,
        "conflicting legacy log fixture could not be written");
    conflicting_legacy_log.close();
    QFile legacy_session_log(
        QDir(legacy_user_data).filePath(QStringLiteral("session_logs/previous-session.txt")));
    Check(legacy_session_log.open(QIODevice::WriteOnly | QIODevice::Text)
        && legacy_session_log.write("older session history") > 0,
        "legacy LocalAppData session fixture could not be written");
    legacy_session_log.close();
    nlsi::logging::Logger logger(
        QDir(root).filePath(QStringLiteral("logs/application.txt")).toStdWString());
    nlsi::session::HistoryStore history(logger);
    Check(history.Initialize(root, legacy_root, legacy_user_data),
        "history directories and stores could not be initialized");
    Check(QDir(root).exists(QStringLiteral("logs"))
        && QDir(root).exists(QStringLiteral("session_logs")),
        "logs and session_logs were not created under writable application data");
    Check(QFile::exists(QDir(root).filePath(QStringLiteral("logs/previous.txt")))
        && QFile::exists(legacy_log.fileName()),
        "legacy log was not copied into user data or source log was not preserved");
    QFile migrated_user_log(QDir(root).filePath(QStringLiteral("logs/previous-user.txt")));
    QFile preserved_existing_log(QDir(root).filePath(QStringLiteral("logs/keep.txt")));
    Check(migrated_user_log.open(QIODevice::ReadOnly | QIODevice::Text)
        && migrated_user_log.readAll() == "older user log"
        && QFile::exists(legacy_user_log.fileName()),
        "legacy LocalAppData log was not copied or its source was not preserved");
    Check(preserved_existing_log.open(QIODevice::ReadOnly | QIODevice::Text)
        && preserved_existing_log.readAll() == "current log"
        && QFile::exists(conflicting_legacy_log.fileName()),
        "legacy migration overwrote existing destination data");
    Check(QFile::exists(QDir(root).filePath(QStringLiteral("session_logs/previous-session.txt")))
        && QFile::exists(legacy_session_log.fileName()),
        "legacy session history was not copied or its source was not preserved");
    Check(logger.Log(L"test log entry"), "application TXT logger failed to write");
    QStringList native_log_entries;
    Check(nlsi::logging::Logger::ReadNlsiLog(logger.NlsiLogPath(), &native_log_entries)
        && native_log_entries == QStringList{QStringLiteral("test log entry")},
        "versioned .nlsi logger did not safely round-trip its first entry");

    const QString unsupported_log_path = QDir(root).filePath(QStringLiteral("logs/unsupported.nlsi"));
    QFile unsupported_log(unsupported_log_path);
    const QByteArray unsupported_contents =
        "{\"format\":\"nlsi-log\",\"schema_version\":99,\"record_type\":\"header\"}\n";
    Check(unsupported_log.open(QIODevice::WriteOnly)
        && unsupported_log.write(unsupported_contents) == unsupported_contents.size(),
        "unsupported schema fixture could not be created");
    unsupported_log.close();
    nlsi::logging::Logger unsupported_logger(
        QDir(root).filePath(QStringLiteral("logs/unsupported.txt")).toStdWString());
    std::wstring native_log_error;
    Check(!unsupported_logger.Log(L"must not append", &native_log_error)
        && !native_log_error.empty(),
        "unsupported .nlsi schema was overwritten instead of reported");
    QFile unchanged_unsupported_log(unsupported_log_path);
    Check(unchanged_unsupported_log.open(QIODevice::ReadOnly)
        && unchanged_unsupported_log.readAll() == unsupported_contents,
        "unsupported .nlsi file was modified during a rejected write");

    const std::wstring start = L"2026-10-07T08:00:00.000Z";
    const std::wstring end = L"2026-10-07T08:30:00.000Z";
    Check(history.StartSession(L"session-test", L"ets2", start),
        "session start could not be persisted");
    Check(history.EndSession(L"session-test", end, L"gameplay_ended", 1800.0),
        "session completion could not be persisted");

    const QByteArray event = R"json({"type":"gameplay_event","provider":"NLSI","event":"job.delivered","timestamp":"2026-10-07T08:29:00.000Z","data":{"job_id":"job-test","cargo":"Furniture"}})json";
    Check(history.RecordProviderEvent(event), "provider event could not be persisted");
    nlsi::telemetry::JobSnapshot job;
    job.cargo_id.Set(L"job-test", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.cargo.Set(L"Furniture", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.source_company.Set(L"Berlin Logistics", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.source_city.Set(L"Berlin", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.destination_company.Set(L"Paris Freight", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.destination_city.Set(L"Paris", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.income.Set(L"25000", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    job.planned_distance.Set(L"1200", L"TruckSim GPS", L"2026-10-07T08:29:00.000Z");
    Check(history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:00.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("job-test")},
                {QStringLiteral("weight"), QStringLiteral("18000")}}),
        "completed job could not be persisted");
    Check(history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:01.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("job-test")},
                {QStringLiteral("cargo"), QStringLiteral("Furniture")}}),
        "duplicate completed job event was not handled");
    Check(history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:02.000Z"),
            {}),
        "job event without an identity could not be ignored safely");

    const auto snapshot = history.Snapshot();
    Check(snapshot.events.size() == 1 && snapshot.events.front().details.contains(QStringLiteral("job_id")),
        "event details were not retained");
    Check(snapshot.sessions.size() == 1 && snapshot.sessions.front().ended_at == QString::fromStdWString(end),
        "completed session was missing from history");
    Check(snapshot.jobs.size() == 1 && snapshot.jobs.front().status == QStringLiteral("Delivered")
        && snapshot.jobs.front().identity == QStringLiteral("job-test")
        && snapshot.jobs.front().nlsi_job_id.startsWith(QStringLiteral("JOB-NLSI-"))
        && snapshot.jobs.front().nlsi_job_id.size() == 13
        && snapshot.jobs.front().cargo == QStringLiteral("Furniture")
        && snapshot.jobs.front().source == QStringLiteral("Berlin Logistics · Berlin")
        && snapshot.jobs.front().destination == QStringLiteral("Paris Freight · Paris")
        && snapshot.jobs.front().details.value(QStringLiteral("source_city")).toString()
            == QStringLiteral("Berlin")
        && snapshot.jobs.front().details.value(QStringLiteral("destination_city")).toString()
            == QStringLiteral("Paris")
        && snapshot.jobs.front().details.value(QStringLiteral("income")).toString()
            == QStringLiteral("25000")
        && snapshot.jobs.front().details.value(QStringLiteral("planned_distance")).toString()
            == QStringLiteral("1200")
        && snapshot.jobs.front().details.value(QStringLiteral("weight")).toString()
            == QStringLiteral("18000")
        && !snapshot.jobs.front().details.contains(QStringLiteral("driven_distance_km"))
        && !snapshot.jobs.front().details.contains(QStringLiteral("truck")),
        "completed job snapshot fields were not persisted or unavailable data was fabricated");
    const QString session_log = QDir(root).filePath(QStringLiteral("session_logs/session-test.txt"));
    QFile session_file(session_log);
    Check(session_file.open(QIODevice::ReadOnly | QIODevice::Text),
        "per-session TXT log was not created");
    const QByteArray contents = session_file.readAll();
    Check(contents.contains("Session started") && contents.contains("Session ended"),
        "per-session TXT log did not contain session lifecycle entries");
    QFile application_log(QDir(root).filePath(QStringLiteral("logs/application.txt")));
    Check(application_log.open(QIODevice::ReadOnly | QIODevice::Text)
        && application_log.readAll().contains("test log entry"),
        "application TXT log did not contain the written entry");

    Check(history.RecordProviderEvent(QByteArrayLiteral(
            R"json({"type":"gameplay_event","event":"offset_earlier","timestamp":"2026-10-07T23:00:00+14:00","provider":"test"})json"))
        && history.RecordProviderEvent(QByteArrayLiteral(
            R"json({"type":"gameplay_event","event":"offset_later","timestamp":"2026-10-07T20:00:00Z","provider":"test"})json")),
        "offset-bearing events could not be added to history");
    const auto ordered_events = history.Snapshot().events;
    const auto earlier = std::find_if(ordered_events.cbegin(), ordered_events.cend(),
        [](const auto& event_record) { return event_record.type == QStringLiteral("offset_earlier"); });
    const auto later = std::find_if(ordered_events.cbegin(), ordered_events.cend(),
        [](const auto& event_record) { return event_record.type == QStringLiteral("offset_later"); });
    Check(earlier != ordered_events.cend() && later != ordered_events.cend()
        && later < earlier,
        "history sorted offset-bearing timestamps by text instead of their represented instants");

    nlsi::session::HistoryStore reloaded_history(logger);
    Check(reloaded_history.Initialize(root)
        && reloaded_history.Snapshot().jobs.size() == 1
        && reloaded_history.Snapshot().jobs.front().details
            .value(QStringLiteral("planned_distance")).toString() == QStringLiteral("1200"),
        "completed-job history did not survive a reload");
    Check(reloaded_history.Snapshot().jobs.front().nlsi_job_id
            == snapshot.jobs.front().nlsi_job_id,
        "the generated NLSI job ID did not survive a reload");
    Check(reloaded_history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:03.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("job-test")}}),
        "persisted duplicate job event could not be handled");
    Check(reloaded_history.Snapshot().jobs.size() == 1,
        "a duplicate completed-job event was recorded after reloading history");

    QTemporaryDir mismatched_job_directory;
    Check(mismatched_job_directory.isValid(),
        "temporary mismatched-job history directory could not be created");
    nlsi::logging::Logger mismatched_job_logger(
        QDir(mismatched_job_directory.path())
            .filePath(QStringLiteral("logs/application.txt")).toStdWString());
    nlsi::session::HistoryStore mismatched_job_history(mismatched_job_logger);
    Check(mismatched_job_history.Initialize(mismatched_job_directory.path())
        && mismatched_job_history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:04.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("different-job")}}),
        "a terminal record with an unrelated current snapshot could not be recorded");
    const auto mismatched_job_snapshot = mismatched_job_history.Snapshot();
    Check(mismatched_job_snapshot.jobs.size() == 1
        && mismatched_job_snapshot.jobs.front().cargo.isEmpty()
        && !mismatched_job_snapshot.jobs.front().details.contains(QStringLiteral("source_city"))
        && !mismatched_job_snapshot.jobs.front().details.contains(QStringLiteral("income"))
        && !mismatched_job_snapshot.jobs.front().details.contains(
            QStringLiteral("planned_distance")),
        "an unrelated current job snapshot was copied into historical job data");
}

void TestStableNlsiJobIdsAndCollisionHandling() {
    QTemporaryDir temporary_directory;
    Check(temporary_directory.isValid(), "temporary job-ID directory could not be created");
    const QString root = temporary_directory.path();
    const QString jobs_path = QDir(root).filePath(QStringLiteral("session_logs/jobs.jsonl"));
    Check(QDir().mkpath(QFileInfo(jobs_path).absolutePath()),
        "legacy job-history directory could not be created");
    QFile legacy_jobs(jobs_path);
    const QByteArray legacy_record = R"json({"event_key":"legacy","identity":"JOB-NLSI-0007","cargo":"Old cargo","source":"Old source","destination":"Old destination","status":"Delivered","timestamp":"2026-10-01T00:00:00Z","details":{"job_id":"old-game-job"}})json" "\n";
    Check(legacy_jobs.open(QIODevice::WriteOnly | QIODevice::Text)
        && legacy_jobs.write(legacy_record) == legacy_record.size(),
        "legacy job-history record could not be written");
    legacy_jobs.close();

    nlsi::logging::Logger logger(
        QDir(root).filePath(QStringLiteral("logs/application.txt")).toStdWString());
    nlsi::telemetry::JobSnapshot job;
    std::vector<std::uint32_t> initial_numbers{42};
    std::size_t initial_index = 0;
    nlsi::session::HistoryStore initial_history(logger, [&] {
        return initial_numbers.at(initial_index++);
    });
    Check(initial_history.Initialize(root), "initial job history could not be initialized");
    Check(initial_history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:00:00Z"),
            {{QStringLiteral("job_id"), QStringLiteral("game-job-1")}}),
        "first completed job could not be stored");
    const auto first_snapshot = initial_history.Snapshot();
    Check(first_snapshot.jobs.size() == 2
        && first_snapshot.jobs.front().identity == QStringLiteral("game-job-1")
        && first_snapshot.jobs.front().nlsi_job_id == QStringLiteral("JOB-NLSI-0042")
        && first_snapshot.jobs.front().details.value(QStringLiteral("job_id")).toString()
            == QStringLiteral("game-job-1")
        && first_snapshot.jobs.front().details.value(QStringLiteral("nlsi_job_id")).toString()
            == QStringLiteral("JOB-NLSI-0042"),
        "the generated ID was not kept separate from the game-provided job ID");

    std::vector<std::uint32_t> retry_numbers{42, 7, 1234};
    std::size_t retry_index = 0;
    nlsi::session::HistoryStore reloaded_history(logger, [&] {
        return retry_numbers.at(retry_index++);
    });
    Check(reloaded_history.Initialize(root), "persisted job history could not be reloaded");
    Check(reloaded_history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-08T08:00:00Z"),
            {{QStringLiteral("job_id"), QStringLiteral("game-job-2")}}),
        "second completed job could not be stored after collision checks");
    const auto reloaded_snapshot = reloaded_history.Snapshot();
    Check(reloaded_snapshot.jobs.size() == 3
        && reloaded_snapshot.jobs[0].identity == QStringLiteral("game-job-2")
        && reloaded_snapshot.jobs[0].nlsi_job_id == QStringLiteral("JOB-NLSI-1234")
        && reloaded_snapshot.jobs[1].nlsi_job_id == QStringLiteral("JOB-NLSI-0042")
        && reloaded_snapshot.jobs[2].identity == QStringLiteral("JOB-NLSI-0007")
        && reloaded_snapshot.jobs[2].nlsi_job_id.isEmpty(),
        "ID collision retries or historical job IDs were not preserved");

    nlsi::session::HistoryStore restarted_history(logger);
    Check(restarted_history.Initialize(root),
        "job history could not be reopened after a simulated restart");
    const auto restarted_snapshot = restarted_history.Snapshot();
    Check(restarted_snapshot.jobs.size() == 3
        && restarted_snapshot.jobs[0].nlsi_job_id == QStringLiteral("JOB-NLSI-1234")
        && restarted_snapshot.jobs[1].nlsi_job_id == QStringLiteral("JOB-NLSI-0042")
        && restarted_snapshot.jobs[2].identity == QStringLiteral("JOB-NLSI-0007")
        && restarted_snapshot.jobs[2].nlsi_job_id.isEmpty(),
        "generated or historical IDs changed across restart");
}

void TestVersionComparison() {
    using nlsi::updater::GitHubUpdater;
    Check(GitHubUpdater::CompareVersions(QStringLiteral("1.4.2-beta.2"),
              QStringLiteral("1.4.2-beta.10")) < 0,
        "numeric prerelease identifiers were not compared numerically");
    Check(GitHubUpdater::CompareVersions(QStringLiteral("1.4.2-alpha"),
              QStringLiteral("1.4.2-beta")) < 0
        && GitHubUpdater::CompareVersions(QStringLiteral("1.4.2-beta"),
              QStringLiteral("1.4.2")) < 0
        && GitHubUpdater::CompareVersions(QStringLiteral("1.4.2-beta"),
              QStringLiteral("1.4.1-alpha")) > 0,
        "semantic prerelease or core version ordering is incorrect");

    const auto release = [](const QString& tag, bool draft, bool prerelease,
                            const QString& published_at) {
        return QJsonObject{
            {QStringLiteral("tag_name"), tag},
            {QStringLiteral("draft"), draft},
            {QStringLiteral("prerelease"), prerelease},
            {QStringLiteral("published_at"), published_at},
            {QStringLiteral("html_url"),
                QStringLiteral("https://github.com/Christian-0777/nlsi_telemetry/releases/tag/")
                    + tag},
            {QStringLiteral("name"), tag},
            {QStringLiteral("body"), QStringLiteral("Verified release notes.")},
        };
    };
    const QJsonArray releases{
        release(QStringLiteral("v9.0.0"), true, true, QString()),
        release(QStringLiteral("v8.0.0-beta"), false, true, QString()),
        release(QStringLiteral("v1.4.1-alpha"), false, true,
            QStringLiteral("2026-10-07T08:00:00.000Z")),
        release(QStringLiteral("v1.4.2-beta"), false, true,
            QStringLiteral("2026-10-08T08:00:00.000Z")),
    };
    const QByteArray payload = QJsonDocument(releases).toJson(QJsonDocument::Compact);
    nlsi::updater::UpdateCheckResult result;
    QString error;
    Check(GitHubUpdater::ParsePublishedReleases(
              payload, QStringLiteral("1.4.2-beta"), &result, &error)
        && result.succeeded && !result.update_available
        && result.release.version == QStringLiteral("1.4.2-beta")
        && result.release.channel == QStringLiteral("Beta"),
        "drafts, unpublished prereleases, or the current target version were mishandled");
    Check(GitHubUpdater::ParsePublishedReleases(
              payload, QStringLiteral("1.4.1-alpha"), &result, &error)
        && result.update_available
        && result.release.version == QStringLiteral("1.4.2-beta"),
        "a newer published beta was not identified as an update");

    const QJsonArray stable_release{
        release(QStringLiteral("v1.4.2"), false, false,
            QStringLiteral("2026-10-09T08:00:00.000Z")),
    };
    Check(GitHubUpdater::ParsePublishedReleases(
              QJsonDocument(stable_release).toJson(QJsonDocument::Compact),
              QStringLiteral("1.4.2-beta"), &result, &error)
        && result.update_available && result.release.channel == QStringLiteral("Stable"),
        "a stable release was not ranked above the same-core beta");
    Check(GitHubUpdater::ParsePublishedReleases(
              QByteArrayLiteral("[]"), QStringLiteral("1.4.2-beta"), &result, &error)
        && result.succeeded && !result.update_available && result.release.version.isEmpty(),
        "an empty published-release list did not complete gracefully");
    Check(!GitHubUpdater::ParsePublishedReleases(
              QByteArrayLiteral("{not json"), QStringLiteral("1.4.2-beta"), &result, &error)
        && !error.isEmpty(),
        "a malformed GitHub response did not return an explicit parse error");
}

void TestTelemetryRecorderOfflineRecovery() {
    QTemporaryDir root;
    Check(root.isValid(), "telemetry recorder temporary directory could not be created");
    const QJsonObject sample{
        {QStringLiteral("timestamp_utc"), QStringLiteral("2026-10-07T16:30:00.000Z")},
        {QStringLiteral("session_id"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("provider"), QStringLiteral("TruckSim GPS")},
        {QStringLiteral("provider_revision"), 13},
        {QStringLiteral("raw_fields"), QJsonObject{
            {QStringLiteral("speed_mps"), 10.0},
        }},
        {QStringLiteral("raw_availability"), QJsonObject{
            {QStringLiteral("speed_mps"), true},
        }},
        {QStringLiteral("normalized_fields"), QJsonObject{}},
        {QStringLiteral("raw_mapping_encoding"), QStringLiteral("qcompress+base64")},
        {QStringLiteral("raw_mapping_uncompressed_bytes"), 32 * 1024},
        {QStringLiteral("raw_mapping_base64"),
            QString::fromLatin1(qCompress(QByteArray(32 * 1024, '\0'), 9).toBase64())},
    };
    const QJsonObject malformed{
        {QStringLiteral("timestamp_utc"), QStringLiteral("not-a-time")},
        {QStringLiteral("provider_revision"), 99},
        {QStringLiteral("raw_fields"), QJsonObject{}},
        {QStringLiteral("raw_availability"), QJsonObject{}},
        {QStringLiteral("normalized_fields"), QJsonObject{}},
        {QStringLiteral("raw_mapping_encoding"), QStringLiteral("qcompress+base64")},
        {QStringLiteral("raw_mapping_uncompressed_bytes"), 32 * 1024},
        {QStringLiteral("raw_mapping_base64"),
            QString::fromLatin1(qCompress(QByteArray(32 * 1024, '\0'), 9).toBase64())},
    };
    nlsi::logging::TelemetryRecorder malformed_recorder;
    Check(malformed_recorder.Start(root.path().toStdWString()),
        "malformed-record fixture recorder could not start");
    std::wstring error;
    Check(!malformed_recorder.Enqueue(malformed, &error) && !error.empty(),
        "malformed telemetry record was accepted without a validation error");
    malformed_recorder.Stop();
    nlsi::logging::TelemetryRecorder empty_recorder;
    Check(empty_recorder.Start(QDir(root.path()).filePath(QStringLiteral("empty"))
            .toStdWString())
        && empty_recorder.StopFor(std::chrono::seconds(5)),
        "an empty telemetry queue did not shut down cleanly");
    Check(empty_recorder.StopFor(std::chrono::milliseconds::zero()),
        "repeated empty-queue shutdown was not idempotent");

    const QString store = QDir(root.path()).filePath(QStringLiteral("records"));
    nlsi::logging::TelemetryRecorder recorder;
    Check(recorder.Start(store.toStdWString()),
        "offline telemetry recorder could not start");
    Check(recorder.Enqueue(sample) && recorder.Flush(),
        "valid telemetry sample was not durably written offline");
    Check(recorder.PendingCount() == 1,
        "offline sample was not retained in the pending synchronization queue");
    Check(recorder.QueuedCount() == 0 && recorder.IsRunning(),
        "the writer reported queued work or stopped before its flush completed");
    const auto one_record_metrics = recorder.GetMetrics();
    Check(one_record_metrics.accepted_records == 1
        && one_record_metrics.persisted_records == 1
        && one_record_metrics.batches_written == 1
        && one_record_metrics.maximum_batch_size == 1
        && one_record_metrics.maximum_queue_bytes > 0,
        "recorder metrics did not report accepted, persisted, and batched writes accurately");
    Check(recorder.StopFor(std::chrono::seconds(5)) && !recorder.IsRunning()
        && recorder.StopFor(std::chrono::milliseconds::zero()),
        "recorder stop was not complete and idempotent");

    const QString telemetry_file = QDir(store).filePath(
        QStringLiteral("telemetry/2026-10-08.nlsi"));
    QFile interrupted(telemetry_file);
    Check(interrupted.open(QIODevice::WriteOnly | QIODevice::Append)
        && interrupted.write("{\"record_type\":\"telemetry_sample\"")
            == static_cast<qint64>(sizeof("{\"record_type\":\"telemetry_sample\"") - 1),
        "interrupted telemetry tail fixture could not be written");
    interrupted.close();

    nlsi::logging::TelemetryRecorder recovered;
    Check(recovered.Start(store.toStdWString()) && recovered.Flush(),
        "telemetry recorder did not recover after restart");
    Check(recovered.PendingCount() == 1,
        "restart recovery duplicated or lost the pending telemetry sample");
    Check(QFile::exists(telemetry_file + QStringLiteral(".recovery")),
        "incomplete trailing bytes were not preserved for recovery");
    QFile recovered_file(telemetry_file);
    Check(recovered_file.open(QIODevice::ReadOnly | QIODevice::Text),
        "recovered telemetry file could not be inspected");
    const QList<QByteArray> lines = recovered_file.readAll().split('\n');
    Check(lines.size() == 3 && lines.at(1).contains("\"record_id\"")
        && lines.at(1).contains("\"sequence\":1"),
        "recovery did not retain exactly one complete sample with a stable ID and sequence");
    const QJsonObject stored_sample = QJsonDocument::fromJson(lines.at(1)).object();
    Check(stored_sample.value(QStringLiteral("timestamp_utc")).toString()
            == sample.value(QStringLiteral("timestamp_utc")).toString()
        && stored_sample.value(QStringLiteral("raw_fields"))
            == sample.value(QStringLiteral("raw_fields"))
        && stored_sample.value(QStringLiteral("raw_availability"))
            == sample.value(QStringLiteral("raw_availability"))
        && stored_sample.value(QStringLiteral("normalized_fields"))
            == sample.value(QStringLiteral("normalized_fields"))
        && stored_sample.value(QStringLiteral("raw_mapping_base64"))
            == sample.value(QStringLiteral("raw_mapping_base64")),
        "telemetry round-trip changed timestamp, field values, or mapping data");
    QFile sync_queue(QDir(store).filePath(QStringLiteral("sync/queue.jsonl")));
    Check(sync_queue.open(QIODevice::ReadOnly | QIODevice::Text)
        && sync_queue.readAll().count('\n') == 1,
        "restart recovery duplicated the durable pending-queue entry");
    Check(recovered.StopFor(std::chrono::seconds(5)),
        "recovered telemetry writer did not stop cleanly");

    QTemporaryDir concurrent_root;
    Check(concurrent_root.isValid(), "concurrent-writer temporary directory could not be created");
    nlsi::logging::TelemetryRecorder concurrent_recorder;
    Check(concurrent_recorder.Start(concurrent_root.path().toStdWString()),
        "concurrent telemetry recorder could not start");
    constexpr int producer_count = 4;
    constexpr int samples_per_producer = 64;
    std::array<bool, producer_count> producer_results{};
    std::vector<std::thread> producers;
    for (int producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            producer_results[static_cast<std::size_t>(producer)] = true;
            for (int index = 0; index < samples_per_producer; ++index) {
                QJsonObject concurrent_sample = sample;
                QJsonObject raw_fields = concurrent_sample
                    .value(QStringLiteral("raw_fields")).toObject();
                raw_fields.insert(QStringLiteral("producer"), producer);
                raw_fields.insert(QStringLiteral("producer_index"), index);
                concurrent_sample.insert(QStringLiteral("raw_fields"), raw_fields);
                if (!concurrent_recorder.Enqueue(concurrent_sample)) {
                    producer_results[static_cast<std::size_t>(producer)] = false;
                    return;
                }
            }
        });
    }
    for (std::thread& producer : producers) {
        producer.join();
    }
    Check(std::all_of(producer_results.cbegin(), producer_results.cend(), [](bool accepted) {
            return accepted;
        }),
        "a concurrent telemetry producer was rejected unexpectedly");
    Check(concurrent_recorder.FlushFor(std::chrono::seconds(30))
        && concurrent_recorder.StopFor(std::chrono::seconds(5)),
        "concurrent telemetry writes did not flush and stop cleanly");
    const auto concurrent_metrics = concurrent_recorder.GetMetrics();
    Check(concurrent_metrics.accepted_records == producer_count * samples_per_producer
        && concurrent_metrics.persisted_records == concurrent_metrics.accepted_records
        && concurrent_metrics.maximum_batch_size <= 128
        && concurrent_metrics.batches_written < concurrent_metrics.persisted_records
        && concurrent_metrics.maximum_queue_bytes <= 64ULL * 1024 * 1024,
        "concurrent queue metrics violated record, batch, or byte bounds");
    const QString concurrent_file_path = QDir(concurrent_root.path())
        .filePath(QStringLiteral("telemetry/2026-10-08.nlsi"));
    QFile concurrent_file(concurrent_file_path);
    Check(concurrent_file.open(QIODevice::ReadOnly | QIODevice::Text),
        "concurrent telemetry output could not be opened");
    QSet<QString> concurrent_ids;
    std::array<int, producer_count> next_producer_index{};
    std::uint64_t expected_sequence = 1;
    bool header_line = true;
    while (!concurrent_file.atEnd()) {
        const QByteArray line = concurrent_file.readLine();
        if (header_line) {
            header_line = false;
            continue;
        }
        const QJsonObject record = QJsonDocument::fromJson(line.trimmed()).object();
        const QJsonObject raw_fields = record.value(QStringLiteral("raw_fields")).toObject();
        const int producer = raw_fields.value(QStringLiteral("producer")).toInt(-1);
        const int producer_index = raw_fields.value(QStringLiteral("producer_index")).toInt(-1);
        const QString record_id = record.value(QStringLiteral("record_id")).toString();
        Check(record.value(QStringLiteral("sequence")).toVariant().toULongLong()
                == expected_sequence
            && producer >= 0 && producer < producer_count
            && producer_index == next_producer_index[static_cast<std::size_t>(producer)]++
            && !QUuid(record_id).isNull() && !concurrent_ids.contains(record_id),
            "concurrent writes were reordered, duplicated, or assigned invalid sequences");
        concurrent_ids.insert(record_id);
        ++expected_sequence;
    }
    Check(expected_sequence == producer_count * samples_per_producer + 1
        && concurrent_ids.size() == producer_count * samples_per_producer,
        "concurrent telemetry persisted a different number of unique records");

    QTemporaryDir legacy_pending_root;
    Check(legacy_pending_root.isValid(),
        "legacy-pending temporary directory could not be created");
    const QString legacy_pending_directory = QDir(legacy_pending_root.path())
        .filePath(QStringLiteral("telemetry/pending"));
    Check(QDir().mkpath(legacy_pending_directory),
        "legacy-pending directory could not be created");
    QJsonObject legacy_sample = sample;
    legacy_sample.insert(QStringLiteral("record_id"),
        QStringLiteral("00000000-0000-4000-8000-000000000001"));
    const QJsonObject legacy_pending_record{
        {QStringLiteral("format"), QStringLiteral("nlsi-pending-sample")},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("sample"), legacy_sample},
    };
    const QString legacy_pending_path = QDir(legacy_pending_directory)
        .filePath(QStringLiteral("00000000-0000-4000-8000-000000000001.json"));
    QFile legacy_pending_file(legacy_pending_path);
    const QByteArray legacy_pending_bytes =
        QJsonDocument(legacy_pending_record).toJson(QJsonDocument::Compact) + '\n';
    Check(legacy_pending_file.open(QIODevice::WriteOnly)
        && legacy_pending_file.write(legacy_pending_bytes) == legacy_pending_bytes.size(),
        "legacy pending recovery fixture could not be written");
    legacy_pending_file.close();
    nlsi::logging::TelemetryRecorder legacy_pending_recorder;
    Check(legacy_pending_recorder.Start(legacy_pending_root.path().toStdWString())
        && legacy_pending_recorder.Flush()
        && legacy_pending_recorder.PendingCount() == 1
        && !QFile::exists(legacy_pending_path)
        && legacy_pending_recorder.StopFor(std::chrono::seconds(5)),
        "schema-v1 pending telemetry was not recovered compatibly");

    QTemporaryDir slow_root;
    Check(slow_root.isValid(), "slow-writer temporary directory could not be created");
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool write_started = false;
    bool release_write = false;
    nlsi::logging::TelemetryRecorder slow_recorder([&] {
        std::unique_lock<std::mutex> lock(gate_mutex);
        write_started = true;
        gate.notify_all();
        gate.wait(lock, [&] { return release_write; });
    });
    Check(slow_recorder.Start(slow_root.path().toStdWString())
        && slow_recorder.Enqueue(sample),
        "slow-writer sample was not accepted");
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        Check(gate.wait_for(lock, std::chrono::seconds(5), [&] { return write_started; }),
            "slow writer did not enter its controlled delay");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto slow_metrics = slow_recorder.GetMetrics();
    Check(slow_metrics.queued_records == 1
        && slow_metrics.queued_bytes > 0
        && slow_metrics.oldest_pending_age >= std::chrono::milliseconds(10),
        "queue depth, memory, or oldest-record-age metrics missed a blocked write");
    Check(!slow_recorder.StopFor(std::chrono::milliseconds(10))
        && slow_recorder.QueuedCount() == 1,
        "a slow in-flight write was reported complete or lost during a bounded stop");
    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_write = true;
    }
    gate.notify_all();
    Check(slow_recorder.StopFor(std::chrono::seconds(5))
        && !slow_recorder.IsRunning(),
        "the writer did not drain after the slow write completed");

    QTemporaryDir capacity_root;
    Check(capacity_root.isValid(), "queue-capacity temporary directory could not be created");
    std::mutex capacity_gate_mutex;
    std::condition_variable capacity_gate;
    bool capacity_write_started = false;
    bool release_capacity_write = false;
    nlsi::logging::TelemetryRecorder capacity_recorder([&] {
        std::unique_lock<std::mutex> lock(capacity_gate_mutex);
        capacity_write_started = true;
        capacity_gate.notify_all();
        capacity_gate.wait(lock, [&] { return release_capacity_write; });
    });
    Check(capacity_recorder.Start(capacity_root.path().toStdWString())
        && capacity_recorder.Enqueue(sample),
        "bounded-queue recorder did not accept its initial sample");
    {
        std::unique_lock<std::mutex> lock(capacity_gate_mutex);
        Check(capacity_gate.wait_for(lock, std::chrono::seconds(5), [&] {
                return capacity_write_started;
            }),
            "bounded-queue writer did not enter its controlled delay");
    }
    std::uint64_t capacity_accepted = 1;
    std::wstring overload_error;
    while (capacity_accepted < 2050 && capacity_recorder.Enqueue(sample, &overload_error)) {
        ++capacity_accepted;
    }
    Check(capacity_accepted == 2048 && !overload_error.empty()
        && capacity_recorder.GetMetrics().queued_records == 2048
        && capacity_recorder.GetMetrics().queued_bytes <= 64ULL * 1024 * 1024,
        "queue overload was not bounded and explicitly reported");
    {
        std::lock_guard<std::mutex> lock(capacity_gate_mutex);
        release_capacity_write = true;
    }
    capacity_gate.notify_all();
    Check(capacity_recorder.StopFor(std::chrono::seconds(60))
        && !capacity_recorder.IsRunning()
        && capacity_recorder.GetMetrics().persisted_records == capacity_accepted
        && capacity_recorder.GetMetrics().maximum_queue_depth == 2048
        && !capacity_recorder.LastError().empty(),
        "queue overload lost accepted records, hid rejection, or falsified drain status");

    QTemporaryDir failure_root;
    Check(failure_root.isValid(), "write-failure temporary directory could not be created");
    const QString failure_store = QDir(failure_root.path()).filePath(QStringLiteral("records"));
    nlsi::logging::TelemetryRecorder failing_recorder;
    Check(failing_recorder.Start(failure_store.toStdWString()),
        "write-failure recorder could not start");
    const QString failed_target = QDir(failure_store).filePath(
        QStringLiteral("telemetry/2026-10-08.nlsi"));
    Check(QDir().mkpath(failed_target), "telemetry write-failure fixture could not be created");
    Check(failing_recorder.Enqueue(sample)
        && failing_recorder.Enqueue(sample)
        && failing_recorder.Enqueue(sample),
        "samples were not accepted into the durable recovery queue");
    Check(!failing_recorder.StopFor(std::chrono::seconds(5))
        && !failing_recorder.LastError().empty()
        && failing_recorder.QueuedCount() == 3
        && failing_recorder.GetMetrics().write_failures == 1,
        "a disk write failure was not surfaced with its accepted sample retained");
    const QString pending_directory = QDir(failure_store)
        .filePath(QStringLiteral("telemetry/pending"));
    Check(QDir(pending_directory).entryList({QStringLiteral("*.json")}).size() == 1,
        "failed telemetry was not retained in a recovery file");
    Check(QDir(failed_target).removeRecursively(),
        "failed telemetry fixture could not be removed for recovery");
    nlsi::logging::TelemetryRecorder failure_recovery;
    Check(failure_recovery.Start(failure_store.toStdWString())
        && failure_recovery.Flush()
        && failure_recovery.PendingCount() == 3
        && failure_recovery.GetMetrics().persisted_records == 3
        && QDir(pending_directory).entryList({QStringLiteral("*.json")}).isEmpty(),
        "a retained failed write did not recover exactly once after restart");
    Check(failure_recovery.StopFor(std::chrono::seconds(5)),
        "recovered failed writer did not stop cleanly");

    QTemporaryDir malformed_root;
    Check(malformed_root.isValid(), "malformed-file temporary directory could not be created");
    const QString malformed_path = QDir(malformed_root.path())
        .filePath(QStringLiteral("telemetry/2026-10-08.nlsi"));
    Check(QDir().mkpath(QFileInfo(malformed_path).absolutePath()),
        "malformed-file telemetry directory could not be created");
    QFile malformed_file(malformed_path);
    const QByteArray malformed_contents =
        "{\"format\":\"nlsi-telemetry\",\"schema_version\":2,\"record_type\":\"header\"}\n"
        "not-json\n";
    Check(malformed_file.open(QIODevice::WriteOnly)
        && malformed_file.write(malformed_contents) == malformed_contents.size(),
        "malformed complete telemetry fixture could not be written");
    malformed_file.close();
    nlsi::logging::TelemetryRecorder malformed_recovery;
    Check(malformed_recovery.Start(malformed_root.path().toStdWString())
        && !malformed_recovery.FlushFor(std::chrono::seconds(5))
        && !malformed_recovery.LastError().empty(),
        "a malformed complete telemetry record was not rejected");
    QFile preserved_malformed(malformed_path);
    Check(preserved_malformed.open(QIODevice::ReadOnly)
        && preserved_malformed.readAll() == malformed_contents,
        "malformed telemetry data was modified instead of preserved");
    malformed_recovery.Stop();
}

void TestAsiaManilaTimeZone() {
    using nlsi::time::ParseInstant;
    using nlsi::time::Zone;
    Check(Zone().isValid() && Zone().id() == QByteArrayLiteral("Asia/Manila"),
        "the configured IANA Asia/Manila time zone is unavailable");
    const QDateTime midnight = ParseInstant(QStringLiteral("2026-10-07T16:30:00.000Z"));
    Check(midnight.isValid()
        && midnight.toString(Qt::ISODateWithMs)
            == QStringLiteral("2026-10-08T00:30:00.000+08:00"),
        "UTC-to-Manila conversion did not cross the midnight date boundary correctly");
    const QDateTime offset_timestamp =
        ParseInstant(QStringLiteral("2026-10-07T23:30:00-05:00"));
    Check(offset_timestamp.isValid()
        && offset_timestamp.toString(Qt::ISODate)
            == QStringLiteral("2026-10-08T12:30:00+08:00"),
        "an explicit source offset did not preserve the instant in Manila time");
    Check(!ParseInstant(QStringLiteral("2026-10-07T16:30:00.000")).isValid()
        && !ParseInstant(QStringLiteral("not-a-time")).isValid(),
        "missing-zone or invalid timestamps were silently reinterpreted");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests = {
        {"valid telemetry and units", TestValidTelemetryAndUnits},
        {"missing and invalid fields", TestMissingAndInvalidFields},
        {"TruckSim GPS revision-13 layout and units", TestTruckSimGpsRevision13LayoutAndUnits},
        {"throttle, brake, cruise, and job parsing", TestThrottleBrakeCruiseAndJobParsing},
        {"paused state and zero-speed ETA", TestPausedStateAndZeroSpeedEta},
        {"stale telemetry", TestStaleTelemetryStopsDriving},
        {"SCS position IPC decoding and stale data", TestScsPositionIpcDecodingAndFreshness},
        {"UI job identity, progress, and session states", TestUiJobIdentityProgressAndSessionStates},
        {"history and TXT log persistence", TestHistoryAndTxtLogPersistence},
        {"stable NLSI job IDs and collision handling", TestStableNlsiJobIdsAndCollisionHandling},
        {"offline telemetry queue and interrupted-write recovery", TestTelemetryRecorderOfflineRecovery},
        {"Asia/Manila timestamp conversion", TestAsiaManilaTimeZone},
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
