#include "TelemetryCore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <type_traits>

#include "time/ApplicationTime.h"

namespace {

using nlsi::telemetry::TelemetryField;
using nlsi::telemetry::TelemetrySnapshot;

std::wstring UtcNow() {
    return nlsi::time::UtcTimestampNow().toStdWString();
}

template <typename T>
QJsonValue JsonFieldValue(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return value;
    } else if constexpr (std::is_same_v<T, double>) {
        return std::isfinite(value) ? QJsonValue(value) : QJsonValue(QJsonValue::Null);
    } else {
        return QString::fromStdWString(value);
    }
}

template <typename T>
void AddNormalizedField(
    QJsonObject& normalized,
    const char* name,
    const TelemetryField<T>& field) {
    normalized.insert(QString::fromLatin1(name), QJsonObject{
        {QStringLiteral("available"), field.available},
        {QStringLiteral("value"), field.available
            ? JsonFieldValue(field.value)
            : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("source"), QString::fromStdWString(field.source)},
        {QStringLiteral("timestamp_utc"), QString::fromStdWString(field.timestamp)},
        {QStringLiteral("stale"), field.stale},
    });
}

QJsonObject NormalizedFields(const TelemetrySnapshot& snapshot) {
    QJsonObject fields;
    AddNormalizedField(fields, "game_id", snapshot.game_id);
    AddNormalizedField(fields, "game_name", snapshot.game_name);
    AddNormalizedField(fields, "paused", snapshot.paused);
    AddNormalizedField(fields, "driving", snapshot.driving);
    AddNormalizedField(fields, "session_active", snapshot.session_active);
    AddNormalizedField(fields, "has_job", snapshot.has_job);
    AddNormalizedField(fields, "speed_kmh", snapshot.speed_kmh);
    AddNormalizedField(fields, "rpm", snapshot.rpm);
    AddNormalizedField(fields, "gear", snapshot.gear);
    AddNormalizedField(fields, "input_throttle", snapshot.input_throttle);
    AddNormalizedField(fields, "effective_throttle", snapshot.effective_throttle);
    AddNormalizedField(fields, "input_brake", snapshot.input_brake);
    AddNormalizedField(fields, "effective_brake", snapshot.effective_brake);
    AddNormalizedField(fields, "retarder_level", snapshot.retarder_level);
    AddNormalizedField(fields, "retarder_active", snapshot.retarder_active);
    AddNormalizedField(fields, "cruise_control_speed_kmh", snapshot.cruise_control_speed);
    AddNormalizedField(fields, "cruise_control_active", snapshot.cruise_control_active);
    AddNormalizedField(fields, "fuel_liters", snapshot.fuel_liters);
    AddNormalizedField(fields, "fuel_range_km", snapshot.fuel_range_km);
    AddNormalizedField(fields, "odometer_km", snapshot.odometer_km);
    AddNormalizedField(fields, "navigation_distance_m", snapshot.navigation_distance_m);
    AddNormalizedField(fields, "navigation_distance_km", snapshot.navigation_distance_km);
    AddNormalizedField(fields, "navigation_time_s", snapshot.navigation_time_s);
    AddNormalizedField(fields, "eta_seconds", snapshot.eta_seconds);
    AddNormalizedField(fields, "cargo_id", snapshot.cargo_id);
    AddNormalizedField(fields, "cargo_name", snapshot.cargo_name);
    AddNormalizedField(fields, "source_company", snapshot.source_company);
    AddNormalizedField(fields, "source_city", snapshot.source_city);
    AddNormalizedField(fields, "destination_company", snapshot.destination_company);
    AddNormalizedField(fields, "destination_city", snapshot.destination_city);
    AddNormalizedField(fields, "income", snapshot.income);
    AddNormalizedField(fields, "planned_distance", snapshot.planned_distance);
    AddNormalizedField(fields, "delivery_time", snapshot.delivery_time);
    AddNormalizedField(fields, "loaded", snapshot.loaded);
    AddNormalizedField(fields, "market", snapshot.market);
    AddNormalizedField(fields, "special_job", snapshot.special_job);
    return fields;
}

} // namespace

namespace nlsi::telemetry {

TelemetryCore::TelemetryCore() = default;

TelemetryCore::~TelemetryCore() {
    Shutdown();
}

bool TelemetryCore::Initialize(const std::wstring& user_data_directory) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = {};
        status_.trucksim = ProviderState::Connecting;
        status_.combined = CombinedProviderState::Disconnected;
        status_.telemetry_freshness = L"Waiting for a telemetry sample";
        status_.trucksim_stage = L"Waiting for Local\\TSGPSTelemetry";
        status_.trucksim_layout = L"Not detected";
        snapshot_ = {};
        trucksim_snapshot_ = {};
        ui_state_ = {};
        previous_job_identity_.clear();
        shutdown_started_ = false;
        shutdown_error_.clear();
        trucksim_state_ = ProviderState::Connecting;
        logged_trucksim_message_.clear();
        if (!user_data_directory.empty()) {
            const QString root = QString::fromStdWString(user_data_directory);
            logger_ = std::make_unique<logging::Logger>(
                QDir(root).filePath(QStringLiteral("logs/NLSI-Exclusive-Logbook.txt")).toStdWString());
            history_store_ = std::make_unique<session::HistoryStore>(*logger_);
            telemetry_recorder_ = std::make_unique<logging::TelemetryRecorder>();
            const QString legacy_user_data = QDir(QFileInfo(root).absolutePath())
                .filePath(QStringLiteral("NLSI Exclusive Logbook"));
            if (!history_store_->Initialize(
                    root, QCoreApplication::applicationDirPath(), legacy_user_data)) {
                status_.storage_error = history_store_->Snapshot().error.toStdWString();
            }
            std::wstring recorder_error;
            if (!telemetry_recorder_->Start(user_data_directory, &recorder_error)) {
                status_.storage_error = std::move(recorder_error);
            }
            std::wstring log_error;
            const QString startup_message = QStringLiteral("[startup] NLSI Exclusive Logbook %1")
                .arg(QCoreApplication::applicationVersion());
            if (!logger_->Log(startup_message.toStdWString(), &log_error)) {
                status_.storage_error = std::move(log_error);
            }
        }
    }

    const bool trucksim_started = trucksim_provider_.Start(
        [this](const TelemetrySnapshot& snapshot, ProviderState state, const ProviderStatus& diagnostics) {
            OnTruckSimUpdate(snapshot, state, diagnostics);
        },
        [this](const std::string& packet) {
            OnProviderEvent(packet);
        },
        [this](const providers::RawTelemetrySample& sample) {
            OnTruckSimSample(sample);
        });
    const bool scs_position_started = scs_position_provider_.Start(
        [this](const providers::ScsPositionSnapshot& snapshot) {
            OnScsPositionUpdate(snapshot);
        });
    if (!scs_position_started) {
        providers::ScsPositionSnapshot position_status;
        position_status.error = L"Unable to start the SCS position provider thread.";
        OnScsPositionUpdate(position_status);
    }
    if (!trucksim_started) {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.trucksim = ProviderState::Disconnected;
        status_.trucksim_stage = L"Provider thread could not be started";
        status_.trucksim_error = L"Unable to start TruckSim GPS shared-memory reader thread.";
        RebuildStateLocked();
    }
    return trucksim_started;
}

void TelemetryCore::Shutdown() {
    BeginShutdown();
    if (telemetry_recorder_) {
        telemetry_recorder_->StopFor(std::chrono::seconds(5));
    }
}

void TelemetryCore::BeginShutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_started_) {
            return;
        }
        shutdown_started_ = true;
    }
    trucksim_provider_.Stop();
    scs_position_provider_.Stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session_manager_.IsActive()) {
            EndSessionLocked(L"application_shutdown", UtcNow());
        }
        if (history_store_) {
            const std::wstring history_error = history_store_->Snapshot().error.toStdWString();
            if (!history_error.empty()) {
                shutdown_error_ = history_error;
            }
        }
        trucksim_state_ = ProviderState::Disconnected;
        status_.trucksim = trucksim_state_;
        scs_position_snapshot_ = {};
        ui_state_.scs_position = scs_position_snapshot_;
        const std::uint64_t pending_records =
            telemetry_recorder_ ? telemetry_recorder_->PendingCount() : 0;
        const std::wstring recorder_error =
            telemetry_recorder_ ? telemetry_recorder_->LastError() : std::wstring{};
        status_.sync_state = !recorder_error.empty()
            ? L"Error; local records require attention"
            : (!telemetry_recorder_
                ? L"Offline; local telemetry recorder is unavailable"
                : (pending_records == 0
                    ? L"Offline; no authenticated API is configured"
                    : L"Pending (" + std::to_wstring(pending_records)
                        + L" records); authenticated API is not configured"));
        status_.combined = CombinedProviderState::Disconnected;
        status_.telemetry_freshness = L"offline";
        if (shutdown_error_.empty()) {
            status_.last_error.clear();
        } else {
            status_.last_error = shutdown_error_;
        }
        trucksim_snapshot_.connected = false;
        RebuildStateLocked();
        if (telemetry_recorder_) {
            telemetry_recorder_->RequestStop();
        }
    }
}

TelemetryCore::ShutdownProgress TelemetryCore::PollShutdown() {
    BeginShutdown();
    ShutdownProgress progress;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!shutdown_error_.empty()) {
            progress.state = ShutdownState::Failed;
            progress.error = shutdown_error_;
        }
    }
    if (!telemetry_recorder_) {
        if (progress.state != ShutdownState::Failed) {
            progress.state = ShutdownState::Completed;
        }
        return progress;
    }
    const bool drained = telemetry_recorder_->StopFor(std::chrono::milliseconds::zero());
    progress.queued_writes = telemetry_recorder_->QueuedCount();
    progress.pending_records = telemetry_recorder_->PendingCount();
    const std::wstring recorder_error = telemetry_recorder_->LastError();
    if (!recorder_error.empty() && progress.error.empty()) {
        progress.error = recorder_error;
    }
    if (!drained) {
        progress.state = telemetry_recorder_->IsRunning()
            ? ShutdownState::Draining
            : ShutdownState::Failed;
    } else if (progress.state != ShutdownState::Failed) {
        progress.state = ShutdownState::Completed;
    }
    return progress;
}

bool TelemetryCore::IsShuttingDownLocked() const {
    return shutdown_started_;
}

ProviderStatus TelemetryCore::Status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ProviderStatus result = status_;
    if (telemetry_recorder_) {
        const std::wstring recorder_error = telemetry_recorder_->LastError();
        if (!recorder_error.empty()) {
            result.storage_error = recorder_error;
        }
    }
    return result;
}

TelemetrySnapshot TelemetryCore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

TelemetryUiState TelemetryCore::UiState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    TelemetryUiState result = ui_state_;
    if (history_store_ && result.fast.values.connected && result.job.available) {
        const QString game_job_identity = result.job.cargo_id.available
            && !result.job.cargo_id.value.empty()
            ? QString::fromStdWString(result.job.cargo_id.value)
            : QString::fromStdWString(result.job.identity);
        result.job.nlsi_job_id =
            history_store_->EnsureNlsiJobId(game_job_identity).toStdWString();
        if (result.job.nlsi_job_id.empty()) {
            result.providers.storage_error =
                history_store_->Snapshot().error.toStdWString();
        }
    }
    return result;
}

session::HistorySnapshot TelemetryCore::History() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_store_ ? history_store_->Snapshot() : session::HistorySnapshot{};
}

bool TelemetryCore::FlushLocalWrites(std::chrono::milliseconds timeout) const {
    logging::TelemetryRecorder* recorder = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recorder = telemetry_recorder_.get();
    }
    return !recorder || recorder->FlushFor(timeout);
}

bool TelemetryCore::IsFreshEnough() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_.combined != CombinedProviderState::Disconnected;
}

void TelemetryCore::OnTruckSimUpdate(
    const TelemetrySnapshot& snapshot,
    ProviderState state,
    const ProviderStatus& diagnostics) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsShuttingDownLocked()) {
        return;
    }
    trucksim_snapshot_ = snapshot;
    trucksim_state_ = state;
    if (state != ProviderState::Connected) {
        MarkSnapshotStale(trucksim_snapshot_);
    }
    status_.trucksim = state;
    status_.trucksim_stage = diagnostics.trucksim_stage;
    status_.trucksim_layout = diagnostics.trucksim_layout;
    status_.trucksim_mapping_name = diagnostics.trucksim_mapping_name;
    status_.trucksim_last_read = diagnostics.trucksim_last_read;
    status_.trucksim_data_age = diagnostics.trucksim_data_age;
    status_.trucksim_source_timestamp = diagnostics.trucksim_source_timestamp;
    status_.trucksim_error = diagnostics.trucksim_error;
    status_.trucksim_revision = diagnostics.trucksim_revision;
    status_.trucksim_win32_error = diagnostics.trucksim_win32_error;
    status_.trucksim_mapping_open = diagnostics.trucksim_mapping_open;
    status_.trucksim_view_mapped = diagnostics.trucksim_view_mapped;
    if (status_.last_error.empty()) {
        status_.last_error = diagnostics.trucksim_error;
    }
    const std::wstring message = diagnostics.trucksim_error.empty()
        ? diagnostics.trucksim_stage
        : diagnostics.trucksim_error;
    if (message != logged_trucksim_message_) {
        LogProviderTransition(L"TruckSim GPS", state, message);
        logged_trucksim_message_ = message;
    }
    RebuildStateLocked();
    UpdateSessionLifecycleLocked();
}

void TelemetryCore::OnProviderEvent(const std::string& packet_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsShuttingDownLocked() || !history_store_) {
        return;
    }
    const QByteArray raw_packet(packet_bytes.data(), static_cast<qsizetype>(packet_bytes.size()));
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(raw_packet, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        status_.last_error = L"Could not parse a telemetry provider event for persistence.";
        return;
    }
    const QJsonObject packet = document.object();
    const QString event_type = packet.value(QStringLiteral("event")).toString();
    const QString timestamp = packet.value(QStringLiteral("timestamp")).toString();
    const QJsonObject details = packet.value(QStringLiteral("data")).toObject();
    bool persisted = history_store_->RecordProviderEvent(raw_packet);
    if (!persisted) {
        status_.storage_error = history_store_->Snapshot().error.toStdWString();
    }
    if (event_type == QStringLiteral("job.delivered")
        || event_type == QStringLiteral("job.cancelled")) {
        persisted = history_store_->RecordJob(ui_state_.job, event_type, timestamp, details)
            && persisted;
        if (!persisted) {
            status_.storage_error = history_store_->Snapshot().error.toStdWString();
        }
    }
    if (persisted) {
        status_.storage_error.clear();
    }
}

void TelemetryCore::OnTruckSimSample(const providers::RawTelemetrySample& sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsShuttingDownLocked() || !telemetry_recorder_) {
        return;
    }
    const QByteArray compressed_mapping = qCompress(sample.mapping, 9);
    QJsonObject record{
        {QStringLiteral("timestamp_utc"), QString::fromStdWString(sample.timestamp_utc)},
        {QStringLiteral("session_id"), session_manager_.IsActive()
            ? QJsonValue(QString::fromStdWString(session_manager_.CurrentId()))
            : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("provider"), QStringLiteral("TruckSim GPS")},
        {QStringLiteral("provider_revision"), static_cast<int>(sample.revision)},
        {QStringLiteral("mapping_name"), QStringLiteral("Local\\TSGPSTelemetry")},
        {QStringLiteral("source_timestamps"), QJsonObject{
            {QStringLiteral("sample"), QString::number(sample.source_timestamp)},
            {QStringLiteral("simulation"), QString::number(sample.simulation_timestamp)},
            {QStringLiteral("render"), QString::number(sample.render_timestamp)},
        }},
        {QStringLiteral("raw_fields"), sample.source_fields},
        {QStringLiteral("raw_availability"), sample.source_availability},
        {QStringLiteral("raw_mapping_encoding"), QStringLiteral("qcompress+base64")},
        {QStringLiteral("raw_mapping_uncompressed_bytes"), sample.mapping.size()},
        {QStringLiteral("raw_mapping_base64"),
            QString::fromLatin1(compressed_mapping.toBase64(QByteArray::Base64Encoding))},
        {QStringLiteral("normalized_fields"), NormalizedFields(trucksim_snapshot_)},
    };
    std::wstring error;
    if (!telemetry_recorder_->Enqueue(record, &error)) {
        status_.storage_error = std::move(error);
    }
}

void TelemetryCore::OnScsPositionUpdate(
    const providers::ScsPositionSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (IsShuttingDownLocked()) {
        return;
    }
    scs_position_snapshot_ = snapshot;
    ui_state_.scs_position = snapshot;
}

void TelemetryCore::RebuildStateLocked() {
    if (telemetry_recorder_) {
        const std::wstring recorder_error = telemetry_recorder_->LastError();
        if (!recorder_error.empty()) {
            status_.storage_error = recorder_error;
        }
    }
    snapshot_ = trucksim_snapshot_;
    snapshot_.connected = trucksim_state_ == ProviderState::Connected;
    if (!snapshot_.connected) {
        MarkSnapshotStale(snapshot_);
    }

    if (snapshot_.paused.available && snapshot_.paused.value) {
        snapshot_.driving.Set(false, snapshot_.paused.source, snapshot_.paused.timestamp);
    }
    if (!snapshot_.connected) {
        snapshot_.driving.value = false;
        snapshot_.driving.available = false;
        snapshot_.session_active.value = false;
        snapshot_.session_active.available = false;
    }
    if (trucksim_state_ == ProviderState::Connected && !trucksim_snapshot_.timestamp.empty()) {
        snapshot_.timestamp = trucksim_snapshot_.timestamp;
    }

    status_.trucksim = trucksim_state_;
    status_.combined = snapshot_.connected
        ? CombinedProviderState::Connected
        : CombinedProviderState::Disconnected;
    status_.telemetry_freshness = snapshot_.timestamp.empty()
        ? L"No telemetry sample received"
        : snapshot_.timestamp;
    ui_state_ = MakeTelemetryUiState(snapshot_, status_, previous_job_identity_);
    ui_state_.scs_position = scs_position_snapshot_;
    if (ui_state_.job.available && !ui_state_.job.identity.empty()) {
        previous_job_identity_ = ui_state_.job.identity;
        job_manager_.SetCurrentJob(ui_state_.job.identity);
    } else if (snapshot_.has_job.available && !snapshot_.has_job.value) {
        previous_job_identity_.clear();
        job_manager_.ClearCurrentJob();
    }
}

void TelemetryCore::UpdateSessionLifecycleLocked() {
    const bool active = snapshot_.connected
        && snapshot_.session_active.available
        && snapshot_.session_active.value;
    if (active && !session_manager_.IsActive()) {
        const std::wstring timestamp = snapshot_.timestamp.empty() ? UtcNow() : snapshot_.timestamp;
        const std::wstring game = snapshot_.game_id.available
            ? snapshot_.game_id.value
            : (snapshot_.game_name.available ? snapshot_.game_name.value : L"Unknown");
        session_manager_.Start(timestamp, game);
        session_started_monotonic_ = std::chrono::steady_clock::now();
        if (history_store_) {
            if (!history_store_->StartSession(
                    session_manager_.CurrentId(), session_manager_.Game(), session_manager_.StartedAt())) {
                status_.storage_error = history_store_->Snapshot().error.toStdWString();
            } else {
                status_.storage_error.clear();
            }
        }
    } else if (!active && session_manager_.IsActive()) {
        const std::wstring reason = snapshot_.connected
            ? L"gameplay_ended"
            : L"telemetry_lost";
        EndSessionLocked(reason, snapshot_.timestamp.empty() ? UtcNow() : snapshot_.timestamp);
    }
}

void TelemetryCore::EndSessionLocked(
    const std::wstring& reason,
    const std::wstring& timestamp) {
    if (!session_manager_.IsActive()) {
        return;
    }
    const std::wstring id = session_manager_.CurrentId();
    const double duration = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - session_started_monotonic_).count();
    if (history_store_) {
        if (!history_store_->EndSession(id, timestamp, reason, duration)) {
            status_.storage_error = history_store_->Snapshot().error.toStdWString();
        } else {
            status_.storage_error.clear();
        }
    }
    session_manager_.Stop();
}

void TelemetryCore::LogProviderTransition(
    const std::wstring& provider,
    ProviderState state,
    const std::wstring& detail) {
    if (!logger_) {
        return;
    }
    std::wstring error;
    const std::wstring message = L"[provider] " + provider + L" "
        + FormatStatus(state) + L": " + detail;
    if (!logger_->Log(message, &error)) {
        status_.storage_error = std::move(error);
    }
}

} // namespace nlsi::telemetry
