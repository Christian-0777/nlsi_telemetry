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
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "logging/Logger.h"
#include "providers/NLSIProvider.h"
#include "providers/TruckSimGpsProvider.h"
#include "session/HistoryStore.h"
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
    std::uint32_t decoded_revision = 0;
    std::wstring error;
    Check(nlsi::providers::TruckSimGpsProvider::DecodeRevision13(
            bytes.data(), bytes.size(), snapshot, decoded_revision, error),
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
    Check(snapshot.has_job.available && snapshot.has_job.value
        && snapshot.loaded.value && snapshot.cargo_id.value == L"job-17"
        && snapshot.cargo_name.value == L"Furniture"
        && snapshot.source_city.value == L"Berlin"
        && snapshot.destination_city.value == L"Paris",
        "TruckSim GPS current-job data was not decoded");

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
    Check(history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:00.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("job-test")},
                {QStringLiteral("cargo"), QStringLiteral("Furniture")},
                {QStringLiteral("source_city"), QStringLiteral("Berlin")},
                {QStringLiteral("destination_city"), QStringLiteral("Paris")},
                {QStringLiteral("income"), QStringLiteral("25000")},
                {QStringLiteral("planned_distance_km"), QStringLiteral("1200")}}),
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
        && snapshot.jobs.front().cargo == QStringLiteral("Furniture")
        && snapshot.jobs.front().source == QStringLiteral("Berlin")
        && snapshot.jobs.front().destination == QStringLiteral("Paris")
        && snapshot.jobs.front().details.value(QStringLiteral("income")).toString()
            == QStringLiteral("25000"),
        "completed job or its event-provided details were missing from history");
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

    nlsi::session::HistoryStore reloaded_history(logger);
    Check(reloaded_history.Initialize(root)
        && reloaded_history.Snapshot().jobs.size() == 1
        && reloaded_history.Snapshot().jobs.front().details
            .value(QStringLiteral("planned_distance_km")).toString() == QStringLiteral("1200"),
        "completed-job history did not survive a reload");
    Check(reloaded_history.RecordJob(
            job,
            QStringLiteral("job.delivered"),
            QStringLiteral("2026-10-07T08:29:03.000Z"),
            {{QStringLiteral("job_id"), QStringLiteral("job-test")}}),
        "persisted duplicate job event could not be handled");
    Check(reloaded_history.Snapshot().jobs.size() == 1,
        "a duplicate completed-job event was recorded after reloading history");
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
        {"TruckSim GPS revision-13 layout and units", TestTruckSimGpsRevision13LayoutAndUnits},
        {"throttle, brake, cruise, and job parsing", TestThrottleBrakeCruiseAndJobParsing},
        {"paused state and zero-speed ETA", TestPausedStateAndZeroSpeedEta},
        {"stale telemetry", TestStaleTelemetryStopsDriving},
        {"UI job identity, progress, and session states", TestUiJobIdentityProgressAndSessionStates},
        {"history and TXT log persistence", TestHistoryAndTxtLogPersistence},
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
