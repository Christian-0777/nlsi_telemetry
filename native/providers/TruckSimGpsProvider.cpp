#define WIN32_LEAN_AND_MEAN
#include "TruckSimGpsProvider.h"

#include <windows.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <thread>
#include <utility>

namespace {

constexpr std::size_t kMappingSize = 32 * 1024;
constexpr std::uint32_t kSupportedRevision = 13;
constexpr std::size_t kSdkActiveOffset = 0;
constexpr std::size_t kPausedOffset = 4;
constexpr std::size_t kTimestampOffset = 8;
constexpr std::size_t kSimulationTimestampOffset = 16;
constexpr std::size_t kRenderTimestampOffset = 24;
constexpr std::size_t kRevisionOffset = 40;
constexpr std::size_t kGameOffset = 52;
constexpr std::size_t kPlannedDistanceOffset = 100;
constexpr std::size_t kRetarderLevelOffset = 108;
constexpr std::size_t kSelectedGearOffset = 504;
constexpr std::size_t kSpeedOffset = 948;
constexpr std::size_t kRpmOffset = 952;
constexpr std::size_t kInputThrottleOffset = 960;
constexpr std::size_t kInputBrakeOffset = 964;
constexpr std::size_t kEffectiveThrottleOffset = 976;
constexpr std::size_t kEffectiveBrakeOffset = 980;
constexpr std::size_t kCruiseSpeedOffset = 988;
constexpr std::size_t kCruiseActiveOffset = 1589;
constexpr std::size_t kFuelOffset = 1000;
constexpr std::size_t kFuelRangeOffset = 1008;
constexpr std::size_t kOdometerOffset = 1056;
constexpr std::size_t kNavigationDistanceOffset = 1060;
constexpr std::size_t kNavigationTimeOffset = 1064;
constexpr std::size_t kJobLoadedOffset = 1564;
constexpr std::size_t kJobSpecialOffset = 1565;
constexpr std::size_t kCargoIdOffset = 2556;
constexpr std::size_t kCargoNameOffset = 2620;
constexpr std::size_t kDestinationCityOffset = 2748;
constexpr std::size_t kDestinationCompanyOffset = 2876;
constexpr std::size_t kSourceCityOffset = 3004;
constexpr std::size_t kSourceCompanyOffset = 3132;
constexpr std::size_t kJobMarketOffset = 3404;
constexpr std::size_t kIncomeOffset = 4000;
constexpr std::size_t kOnJobOffset = 4300;
constexpr std::size_t kJobCancelledOffset = 4302;
constexpr std::size_t kJobDeliveredOffset = 4303;

template <typename T>
T ReadValue(const std::uint8_t* data, std::size_t offset) {
    T value{};
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

bool ReadBoolean(const std::uint8_t* data, std::size_t offset) {
    return data[offset] != 0;
}

std::wstring ReadString(const std::uint8_t* data, std::size_t offset, std::size_t length) {
    std::size_t size = 0;
    while (size < length && data[offset + size] != 0) {
        ++size;
    }
    if (size == 0) {
        return {};
    }

    const int wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data + offset),
        static_cast<int>(size), nullptr, 0);
    if (wide_length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(wide_length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char*>(data + offset),
            static_cast<int>(size), result.data(), wide_length) != wide_length) {
        return {};
    }
    return result;
}

std::wstring UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_s(&utc, &time);
    std::wostringstream output;
    output << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%S")
           << L'.' << std::setw(3) << std::setfill(L'0') << milliseconds.count() << L'Z';
    return output.str();
}

std::wstring WindowsErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    std::wstring message = length && buffer
        ? std::wstring(buffer, length)
        : L"Unknown Windows error";
    if (buffer) {
        LocalFree(buffer);
    }
    while (!message.empty()
        && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

std::wstring FailureText(const wchar_t* operation, DWORD code) {
    std::wostringstream output;
    output << operation << L" failed (Win32 " << code << L"): " << WindowsErrorText(code);
    return output.str();
}

void SetNumber(
    nlsi::telemetry::TelemetryField<double>& field,
    float value,
    const std::wstring& timestamp,
    bool non_negative = true) {
    if (std::isfinite(value) && (!non_negative || value >= 0.0f)) {
        field.Set(static_cast<double>(value), L"TruckSim GPS", timestamp);
    }
}

void SetText(
    nlsi::telemetry::TelemetryField<std::wstring>& field,
    std::wstring value,
    const std::wstring& timestamp) {
    if (!value.empty()) {
        field.Set(std::move(value), L"TruckSim GPS", timestamp);
    }
}

QString FieldString(const nlsi::telemetry::TelemetryField<std::wstring>& field) {
    return field.available ? QString::fromStdWString(field.value) : QString();
}

std::string MakeJobEvent(
    const nlsi::telemetry::TelemetrySnapshot& snapshot,
    const wchar_t* event_name,
    const std::wstring& timestamp) {
    QJsonObject details;
    details.insert(QStringLiteral("job_id"), FieldString(snapshot.cargo_id));
    details.insert(QStringLiteral("cargo"), FieldString(snapshot.cargo_name));
    details.insert(QStringLiteral("source_city"), FieldString(snapshot.source_city));
    details.insert(QStringLiteral("source_company"), FieldString(snapshot.source_company));
    details.insert(QStringLiteral("destination_city"), FieldString(snapshot.destination_city));
    details.insert(QStringLiteral("destination_company"), FieldString(snapshot.destination_company));
    details.insert(QStringLiteral("income"), FieldString(snapshot.income));
    details.insert(QStringLiteral("planned_distance_km"), FieldString(snapshot.planned_distance));
    details.insert(QStringLiteral("market"), FieldString(snapshot.market));
    if (snapshot.special_job.available) {
        details.insert(QStringLiteral("special_job"), snapshot.special_job.value == L"true");
    }
    if (snapshot.odometer_km.available && std::isfinite(snapshot.odometer_km.value)) {
        details.insert(QStringLiteral("odometer_km"), snapshot.odometer_km.value);
    }
    if (snapshot.navigation_distance_km.available
        && std::isfinite(snapshot.navigation_distance_km.value)) {
        details.insert(
            QStringLiteral("remaining_navigation_km"), snapshot.navigation_distance_km.value);
    }
    if (snapshot.fuel_liters.available && std::isfinite(snapshot.fuel_liters.value)) {
        details.insert(QStringLiteral("fuel_liters"), snapshot.fuel_liters.value);
    }

    QJsonObject packet;
    packet.insert(QStringLiteral("type"), QStringLiteral("gameplay_event"));
    packet.insert(QStringLiteral("provider"), QStringLiteral("TruckSim GPS"));
    packet.insert(QStringLiteral("event"), QString::fromStdWString(event_name));
    packet.insert(QStringLiteral("timestamp"), QString::fromStdWString(timestamp));
    packet.insert(QStringLiteral("data"), details);
    return QJsonDocument(packet).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

namespace nlsi::providers {

TruckSimGpsProvider::TruckSimGpsProvider() = default;

TruckSimGpsProvider::~TruckSimGpsProvider() {
    Stop();
}

bool TruckSimGpsProvider::Start(UpdateCallback callback, EventCallback event_callback) {
    if (worker_.joinable()) {
        return false;
    }
    callback_ = std::move(callback);
    event_callback_ = std::move(event_callback);
    stopping_ = false;
    state_ = telemetry::ProviderState::Connecting;
    try {
        worker_ = std::thread(&TruckSimGpsProvider::ReadLoop, this);
    } catch (const std::system_error&) {
        state_ = telemetry::ProviderState::Disconnected;
        return false;
    }
    return true;
}

void TruckSimGpsProvider::Stop() {
    stopping_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
    state_ = telemetry::ProviderState::Disconnected;
}

telemetry::ProviderState TruckSimGpsProvider::State() const {
    return state_.load();
}

std::wstring TruckSimGpsProvider::Name() const {
    return L"TruckSim GPS";
}

bool TruckSimGpsProvider::DecodeRevision13(
    const std::uint8_t* data,
    std::size_t size,
    telemetry::TelemetrySnapshot& snapshot,
    std::uint32_t& revision,
    std::wstring& error) {
    revision = 0;
    if (!data || size < kMappingSize) {
        error = L"Shared-memory view is smaller than the 32 KiB TruckSim GPS layout.";
        return false;
    }

    revision = ReadValue<std::uint32_t>(data, kRevisionOffset);
    if (revision != kSupportedRevision) {
        std::wostringstream output;
        output << L"Unsupported TruckSim GPS plugin revision " << revision
               << L"; this build only decodes revision " << kSupportedRevision << L".";
        error = output.str();
        return false;
    }

    const std::wstring timestamp = UtcNow();
    snapshot = {};
    const std::uint32_t game = ReadValue<std::uint32_t>(data, kGameOffset);
    if (game == 1) {
        snapshot.game_id.Set(L"ets2", L"TruckSim GPS", timestamp);
        snapshot.game_name.Set(L"Euro Truck Simulator 2", L"TruckSim GPS", timestamp);
    } else if (game == 2) {
        snapshot.game_id.Set(L"ats", L"TruckSim GPS", timestamp);
        snapshot.game_name.Set(L"American Truck Simulator", L"TruckSim GPS", timestamp);
    }

    snapshot.paused.Set(ReadBoolean(data, kPausedOffset), L"TruckSim GPS", timestamp);
    const float speed_mps = ReadValue<float>(data, kSpeedOffset);
    if (std::isfinite(speed_mps) && speed_mps >= 0.0f) {
        snapshot.speed_kmh.Set(static_cast<double>(speed_mps) * 3.6, L"TruckSim GPS", timestamp);
        snapshot.driving.Set(!snapshot.paused.value && speed_mps > 0.0f, L"TruckSim GPS", timestamp);
    }
    snapshot.gear.Set(
        static_cast<double>(ReadValue<std::int32_t>(data, kSelectedGearOffset)),
        L"TruckSim GPS", timestamp);
    SetNumber(snapshot.rpm, ReadValue<float>(data, kRpmOffset), timestamp);
    SetNumber(snapshot.input_throttle, ReadValue<float>(data, kInputThrottleOffset), timestamp);
    SetNumber(snapshot.effective_throttle, ReadValue<float>(data, kEffectiveThrottleOffset), timestamp);
    SetNumber(snapshot.input_brake, ReadValue<float>(data, kInputBrakeOffset), timestamp);
    SetNumber(snapshot.effective_brake, ReadValue<float>(data, kEffectiveBrakeOffset), timestamp);

    const double retarder = static_cast<double>(
        ReadValue<std::uint32_t>(data, kRetarderLevelOffset));
    snapshot.retarder_level.Set(retarder, L"TruckSim GPS", timestamp);
    snapshot.retarder_active.Set(retarder > 0.0, L"TruckSim GPS", timestamp);
    const float cruise_speed_mps = ReadValue<float>(data, kCruiseSpeedOffset);
    if (std::isfinite(cruise_speed_mps) && cruise_speed_mps >= 0.0f) {
        snapshot.cruise_control_speed.Set(
            static_cast<double>(cruise_speed_mps) * 3.6, L"TruckSim GPS", timestamp);
    }
    snapshot.cruise_control_active.Set(
        ReadBoolean(data, kCruiseActiveOffset), L"TruckSim GPS", timestamp);

    SetNumber(snapshot.fuel_liters, ReadValue<float>(data, kFuelOffset), timestamp);
    SetNumber(snapshot.fuel_range_km, ReadValue<float>(data, kFuelRangeOffset), timestamp);
    SetNumber(snapshot.odometer_km, ReadValue<float>(data, kOdometerOffset), timestamp);
    const float distance_m = ReadValue<float>(data, kNavigationDistanceOffset);
    SetNumber(snapshot.navigation_distance_m, distance_m, timestamp);
    if (snapshot.navigation_distance_m.available) {
        snapshot.navigation_distance_km.Set(
            telemetry::MetersToKilometers(snapshot.navigation_distance_m.value),
            L"TruckSim GPS", timestamp);
    }
    SetNumber(snapshot.navigation_time_s, ReadValue<float>(data, kNavigationTimeOffset), timestamp);
    if (const auto eta = telemetry::CalculateEtaSeconds(
            snapshot.navigation_distance_m, snapshot.speed_kmh)) {
        snapshot.eta_seconds.Set(*eta, L"TruckSim GPS", timestamp);
    }

    SetText(snapshot.cargo_id, ReadString(data, kCargoIdOffset, 64), timestamp);
    SetText(snapshot.cargo_name, ReadString(data, kCargoNameOffset, 64), timestamp);
    SetText(snapshot.source_city, ReadString(data, kSourceCityOffset, 64), timestamp);
    SetText(snapshot.source_company, ReadString(data, kSourceCompanyOffset, 64), timestamp);
    SetText(snapshot.destination_city, ReadString(data, kDestinationCityOffset, 64), timestamp);
    SetText(snapshot.destination_company, ReadString(data, kDestinationCompanyOffset, 64), timestamp);
    const std::uint64_t income = ReadValue<std::uint64_t>(data, kIncomeOffset);
    if (income > 0) {
        snapshot.income.Set(std::to_wstring(income), L"TruckSim GPS", timestamp);
    }
    const std::uint32_t planned_distance =
        ReadValue<std::uint32_t>(data, kPlannedDistanceOffset);
    if (planned_distance > 0) {
        snapshot.planned_distance.Set(
            std::to_wstring(planned_distance), L"TruckSim GPS", timestamp);
    }
    snapshot.loaded.Set(ReadBoolean(data, kJobLoadedOffset), L"TruckSim GPS", timestamp);
    snapshot.special_job.Set(
        ReadBoolean(data, kJobSpecialOffset) ? L"true" : L"false",
        L"TruckSim GPS", timestamp);
    SetText(snapshot.market, ReadString(data, kJobMarketOffset, 32), timestamp);
    snapshot.has_job.Set(ReadBoolean(data, kOnJobOffset), L"TruckSim GPS", timestamp);
    snapshot.session_active.Set(
        ReadBoolean(data, kSdkActiveOffset), L"TruckSim GPS", timestamp);
    snapshot.timestamp = timestamp;
    snapshot.connected = true;
    return true;
}

void TruckSimGpsProvider::Publish(
    const telemetry::TelemetrySnapshot& snapshot,
    telemetry::ProviderState state,
    const telemetry::ProviderStatus& status) {
    state_ = state;
    if (callback_) {
        callback_(snapshot, state, status);
    }
}

void TruckSimGpsProvider::ReadLoop() {
    using namespace std::chrono_literals;
    telemetry::TelemetrySnapshot snapshot;
    telemetry::ProviderStatus status;
    std::uint64_t previous_timestamp = 0;
    auto last_changed = std::chrono::steady_clock::time_point{};
    bool event_baseline_set = false;
    bool previous_cancelled = false;
    bool previous_delivered = false;
    HANDLE mapping = nullptr;
    void* view = nullptr;

    while (!stopping_.load()) {
        if (!mapping) {
            mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\TSGPSTelemetry");
            if (!mapping) {
                const DWORD error = GetLastError();
                status.trucksim_mapping_open = false;
                status.trucksim_view_mapped = false;
                status.trucksim_stage = error == ERROR_FILE_NOT_FOUND
                    ? L"Shared-memory map not found: plugin may be missing or the game is not running"
                    : (error == ERROR_ACCESS_DENIED
                        ? L"Access denied opening TruckSim GPS shared-memory map"
                        : L"TruckSim GPS shared-memory map could not be opened");
                status.trucksim_win32_error = error;
                status.trucksim_error = FailureText(
                    L"OpenFileMappingW(Local\\TSGPSTelemetry)", error);
                snapshot = {};
                Publish(snapshot, telemetry::ProviderState::Disconnected, status);
                std::this_thread::sleep_for(1s);
                continue;
            }
            status.trucksim_mapping_open = true;
            status.trucksim_win32_error = 0;
            status.trucksim_error.clear();
            status.trucksim_stage = L"Mapping opened; shared-memory view not yet validated";
        }

        if (!view) {
            view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, kMappingSize);
            if (!view) {
                const DWORD error = GetLastError();
                status.trucksim_view_mapped = false;
                status.trucksim_stage = L"MapViewOfFile failed";
                status.trucksim_win32_error = error;
                status.trucksim_error = FailureText(L"MapViewOfFile", error);
                CloseHandle(mapping);
                mapping = nullptr;
                status.trucksim_mapping_open = false;
                status.trucksim_view_mapped = false;
                snapshot = {};
                Publish(snapshot, telemetry::ProviderState::Disconnected, status);
                std::this_thread::sleep_for(1s);
                continue;
            }
            status.trucksim_view_mapped = true;
            event_baseline_set = false;
            previous_timestamp = 0;
            last_changed = {};
        }

        std::array<std::uint8_t, kMappingSize> bytes{};
        std::memcpy(bytes.data(), view, bytes.size());
        telemetry::TelemetrySnapshot current;
        std::uint32_t revision = 0;
        std::wstring error;
        if (!DecodeRevision13(bytes.data(), bytes.size(), current, revision, error)) {
            status.trucksim_revision = revision;
            status.trucksim_last_read = UtcNow();
            const bool unsupported_revision =
                error.find(L"Unsupported TruckSim GPS plugin revision") == 0;
            status.trucksim_layout = unsupported_revision
                ? L"Unsupported revision " + std::to_wstring(revision) + L" (layout unverified)"
                : L"Invalid shared-memory layout";
            status.trucksim_data_age = unsupported_revision
                ? L"Unavailable: unsupported layout"
                : L"Unavailable: invalid layout";
            status.trucksim_source_timestamp.clear();
            status.trucksim_stage = unsupported_revision
                ? L"Mapping open, but data was not decoded for this plugin revision"
                : L"Shared-memory read failed layout validation";
            status.trucksim_error = error;
            status.trucksim_win32_error = 0;
            event_baseline_set = false;
            snapshot = {};
            Publish(snapshot, telemetry::ProviderState::Disconnected, status);
            std::this_thread::sleep_for(1s);
            continue;
        }

        status.trucksim_revision = revision;
        status.trucksim_layout = L"TruckSim GPS plugin revision 13 (32 KiB)";
        status.trucksim_last_read = UtcNow();
        const std::uint64_t source_timestamp =
            ReadValue<std::uint64_t>(bytes.data(), kTimestampOffset);
        const std::uint64_t simulation =
            ReadValue<std::uint64_t>(bytes.data(), kSimulationTimestampOffset);
        const std::uint64_t render =
            ReadValue<std::uint64_t>(bytes.data(), kRenderTimestampOffset);
        status.trucksim_source_timestamp =
            L"sample=" + std::to_wstring(source_timestamp)
            + L"; simulation=" + std::to_wstring(simulation)
            + L"; render=" + std::to_wstring(render);

        const auto now = std::chrono::steady_clock::now();
        if (source_timestamp != previous_timestamp
            || last_changed == std::chrono::steady_clock::time_point{}) {
            previous_timestamp = source_timestamp;
            last_changed = now;
        }
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(now - last_changed);
        status.trucksim_data_age = std::to_wstring(age.count()) + L" seconds";
        status.trucksim_error.clear();
        status.trucksim_win32_error = 0;

        const bool sdk_active = ReadBoolean(bytes.data(), kSdkActiveOffset);
        const bool cancelled = ReadBoolean(bytes.data(), kJobCancelledOffset);
        const bool delivered = ReadBoolean(bytes.data(), kJobDeliveredOffset);
        if (event_baseline_set && sdk_active && event_callback_) {
            if (cancelled && !previous_cancelled) {
                event_callback_(MakeJobEvent(current, L"job.cancelled", current.timestamp));
            }
            if (delivered && !previous_delivered) {
                event_callback_(MakeJobEvent(current, L"job.delivered", current.timestamp));
            }
        }
        previous_cancelled = cancelled;
        previous_delivered = delivered;
        event_baseline_set = true;

        snapshot = std::move(current);
        if (!sdk_active) {
            status.trucksim_stage = L"Stale data: SCS SDK reports inactive";
            status.trucksim_error =
                L"TruckSim GPS map is valid, but its SCS SDK active flag is false.";
            Publish(snapshot, telemetry::ProviderState::Stale, status);
        } else if (source_timestamp == 0) {
            status.trucksim_stage = L"Stale data: no plugin sample timestamp";
            status.trucksim_error =
                L"TruckSim GPS map is valid, but its sample timestamp is zero.";
            Publish(snapshot, telemetry::ProviderState::Stale, status);
        } else if (age >= 5s) {
            status.trucksim_stage = L"Stale data: plugin timestamp has stopped changing";
            status.trucksim_error =
                L"TruckSim GPS sample timestamp has not changed for at least 5 seconds.";
            telemetry::MarkSnapshotStale(snapshot);
            Publish(snapshot, telemetry::ProviderState::Stale, status);
        } else {
            status.trucksim_stage = L"Connected; revision-13 telemetry is changing";
            Publish(snapshot, telemetry::ProviderState::Connected, status);
        }
        std::this_thread::sleep_for(250ms);
    }

    if (view) {
        UnmapViewOfFile(view);
    }
    if (mapping) {
        CloseHandle(mapping);
    }
}

} // namespace nlsi::providers
