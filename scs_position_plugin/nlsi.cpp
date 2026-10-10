#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "scssdk_telemetry.h"
#include "common/scssdk_telemetry_common_configs.h"
#include "common/scssdk_telemetry_common_gameplay_events.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"
#include "ScsPositionIpc.h"
#include "ScsTelemetryEventIpc.h"

namespace {

HANDLE mapping_handle = nullptr;
nlsi::providers::ScsPositionIpcV1* shared_position = nullptr;
HANDLE event_mapping_handle = nullptr;
nlsi::providers::ScsTelemetryEventIpcV1* shared_events = nullptr;
scs_log_t game_log = nullptr;
std::mutex configuration_mutex;
std::map<std::string, std::string> job_configuration;
std::map<std::string, std::string> truck_configuration;
std::map<std::string, std::string> trailer_configuration;
std::uint32_t active_game_id = 0;

using JsonFields = std::map<std::string, std::string>;

std::string JsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (character < 0x20) {
                char buffer[7]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                escaped += buffer;
            } else {
                escaped.push_back(static_cast<char>(character));
            }
        }
    }
    escaped.push_back('"');
    return escaped;
}

std::string JsonValue(const scs_value_t& value) {
    char buffer[64]{};
    switch (value.type) {
    case SCS_VALUE_TYPE_bool:
        return value.value_bool.value ? "true" : "false";
    case SCS_VALUE_TYPE_s32:
        return std::to_string(value.value_s32.value);
    case SCS_VALUE_TYPE_u32:
        return std::to_string(value.value_u32.value);
    case SCS_VALUE_TYPE_u64:
        return std::to_string(value.value_u64.value);
    case SCS_VALUE_TYPE_s64:
        return std::to_string(value.value_s64.value);
    case SCS_VALUE_TYPE_float:
        if (!std::isfinite(value.value_float.value)) {
            return "null";
        }
        std::snprintf(buffer, sizeof(buffer), "%.9g", value.value_float.value);
        return buffer;
    case SCS_VALUE_TYPE_double:
        if (!std::isfinite(value.value_double.value)) {
            return "null";
        }
        std::snprintf(buffer, sizeof(buffer), "%.17g", value.value_double.value);
        return buffer;
    case SCS_VALUE_TYPE_string:
        return value.value_string.value
            ? JsonString(value.value_string.value) : "null";
    default:
        return "null";
    }
}

JsonFields CaptureAttributes(const scs_named_value_t* attributes) {
    JsonFields fields;
    for (const scs_named_value_t* item = attributes; item && item->name; ++item) {
        fields.insert_or_assign(item->name, JsonValue(item->value));
    }
    return fields;
}

std::string JsonObject(const JsonFields& fields) {
    std::string result = "{";
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) {
            result.push_back(',');
        }
        first = false;
        result += JsonString(key);
        result.push_back(':');
        result += value;
    }
    result.push_back('}');
    return result;
}

void AddAlias(JsonFields& fields, const char* alias, const char* source) {
    const auto value = fields.find(source);
    if (value != fields.end() && value->second != "null") {
        fields.insert_or_assign(alias, value->second);
    }
}

std::string UtcTimestamp() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char buffer[32]{};
    std::snprintf(
        buffer, sizeof(buffer), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute,
        time.wSecond, time.wMilliseconds);
    return buffer;
}

void PublishEventPacket(std::string packet) {
    if (!shared_events || packet.empty()) {
        return;
    }
    if (packet.size() >= nlsi::providers::kScsTelemetryEventPayloadSize) {
        if (game_log) {
            game_log(SCS_LOG_TYPE_warning, "SCS PROVIDER PACKET EXCEEDED THE NLSI EVENT INTERFACE SIZE.");
        }
        return;
    }

    const auto event_id = static_cast<std::uint64_t>(InterlockedIncrement64(
        reinterpret_cast<volatile LONG64*>(&shared_events->latest_event_id)));
    constexpr char event_id_marker[] = "\"provider_event_id\":18446744073709551615";
    const std::size_t marker = packet.find(event_id_marker);
    if (marker != std::string::npos) {
        const std::string replacement =
            "\"provider_event_id\":" + std::to_string(event_id);
        packet.replace(marker, sizeof(event_id_marker) - 1, replacement);
    }
    auto& slot = shared_events->entries[
        (event_id - 1) % nlsi::providers::kScsTelemetryEventCapacity];
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&slot.sequence));
    slot.event_id = event_id;
    slot.payload_size = static_cast<std::uint32_t>(packet.size());
    std::memcpy(slot.payload, packet.data(), packet.size());
    slot.payload[packet.size()] = '\0';
    MemoryBarrier();
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&slot.sequence));
}

void PublishGameplayEvent(const char* event_name, JsonFields fields) {
    if (!event_name) {
        return;
    }
    std::string packet = "{\"type\":\"gameplay_event\",\"provider\":\"SCS SDK\",\"event\":";
    packet += JsonString(event_name);
    packet += ",\"provider_event_id\":18446744073709551615,\"timestamp\":";
    packet += JsonString(UtcTimestamp());
    packet += ",\"data\":";
    packet += JsonObject(fields);
    packet.push_back('}');
    PublishEventPacket(std::move(packet));
}

void PublishConfiguration(const std::string& id, const JsonFields& attributes) {
    JsonFields relevant;
    for (const char* key : {
             "brand", "name", "license.plate", "license_plate",
             "body.type", "chain.type", "version", "game.version",
             "game_version", "game.name", "game_name"}) {
        const auto value = attributes.find(key);
        if (value != attributes.end()) {
            relevant.emplace(value->first, value->second);
        }
    }
    std::string packet = "{\"type\":\"configuration\",\"provider\":\"SCS SDK\",\"id\":";
    packet += JsonString(id);
    packet += ",\"provider_event_id\":18446744073709551615,\"timestamp\":";
    packet += JsonString(UtcTimestamp());
    packet += ",\"game_id\":";
    packet += std::to_string(active_game_id);
    packet += ",\"attributes\":";
    packet += JsonObject(relevant);
    packet.push_back('}');
    PublishEventPacket(std::move(packet));
}

void CloseEventMapping() {
    if (shared_events) {
        UnmapViewOfFile(shared_events);
        shared_events = nullptr;
    }
    if (event_mapping_handle) {
        CloseHandle(event_mapping_handle);
        event_mapping_handle = nullptr;
    }
}

bool OpenEventMapping(const scs_telemetry_init_params_v100_t* init) {
    SetLastError(ERROR_SUCCESS);
    event_mapping_handle = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(nlsi::providers::ScsTelemetryEventIpcV1)),
        nlsi::providers::kScsTelemetryEventMappingName);
    const DWORD mapping_error = GetLastError();
    if (!event_mapping_handle) {
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT CREATE THE NLSI GAMEPLAY EVENT INTERFACE.");
        return false;
    }

    shared_events = static_cast<nlsi::providers::ScsTelemetryEventIpcV1*>(
        MapViewOfFile(
            event_mapping_handle,
            FILE_MAP_WRITE,
            0,
            0,
            sizeof(nlsi::providers::ScsTelemetryEventIpcV1)));
    if (!shared_events) {
        CloseEventMapping();
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT MAP THE NLSI GAMEPLAY EVENT INTERFACE.");
        return false;
    }

    if (mapping_error != ERROR_ALREADY_EXISTS) {
        std::memset(shared_events, 0, sizeof(*shared_events));
        shared_events->magic = nlsi::providers::kScsTelemetryEventIpcMagic;
        shared_events->version = nlsi::providers::kScsTelemetryEventIpcVersion;
        shared_events->struct_size = sizeof(*shared_events);
        shared_events->slot_count =
            static_cast<std::uint32_t>(nlsi::providers::kScsTelemetryEventCapacity);
    } else if (shared_events->magic != nlsi::providers::kScsTelemetryEventIpcMagic
        || shared_events->version != nlsi::providers::kScsTelemetryEventIpcVersion
        || shared_events->struct_size != sizeof(*shared_events)
        || shared_events->slot_count != nlsi::providers::kScsTelemetryEventCapacity) {
        CloseEventMapping();
        init->common.log(SCS_LOG_TYPE_error,
            "THE NLSI GAMEPLAY EVENT INTERFACE HAS AN INCOMPATIBLE VERSION.");
        return false;
    }
    return true;
}

SCSAPI_VOID StoreConfiguration(
    const scs_event_t,
    const void* const event_info,
    const scs_context_t) {
    if (!event_info) {
        return;
    }
    const auto* configuration =
        static_cast<const scs_telemetry_configuration_t*>(event_info);
    if (!configuration->id || !configuration->attributes) {
        return;
    }

    const std::string id(configuration->id);
    JsonFields fields = CaptureAttributes(configuration->attributes);
    {
        std::lock_guard<std::mutex> lock(configuration_mutex);
        if (id == SCS_TELEMETRY_CONFIG_job) {
            job_configuration = fields;
        } else if (id == SCS_TELEMETRY_CONFIG_truck) {
            truck_configuration = fields;
        } else if (id == SCS_TELEMETRY_CONFIG_trailer
            || id.rfind("trailer.", 0) == 0) {
            if (id == SCS_TELEMETRY_CONFIG_trailer || id == "trailer.0") {
                trailer_configuration = fields;
            }
        }
    }
    if (id == SCS_TELEMETRY_CONFIG_truck
        || id == SCS_TELEMETRY_CONFIG_trailer || id == "trailer.0"
        || id == "game") {
        PublishConfiguration(id, fields);
    }
}

SCSAPI_VOID StoreGameplayEvent(
    const scs_event_t,
    const void* const event_info,
    const scs_context_t) {
    if (!event_info) {
        return;
    }
    const auto* gameplay =
        static_cast<const scs_telemetry_gameplay_event_t*>(event_info);
    if (!gameplay->id || !gameplay->attributes) {
        return;
    }

    const char* event_name = gameplay->id;
    const bool delivered =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_job_delivered) == 0;
    const bool cancelled =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_job_cancelled) == 0;
    const bool fined =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_player_fined) == 0;
    const bool toll =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_player_tollgate_paid) == 0;
    const bool ferry =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_player_use_ferry) == 0;
    const bool train =
        std::strcmp(event_name, SCS_TELEMETRY_GAMEPLAY_EVENT_player_use_train) == 0;
    if (!delivered && !cancelled && !fined && !toll && !ferry && !train) {
        return;
    }

    JsonFields fields = CaptureAttributes(gameplay->attributes);
    if (delivered || cancelled) {
        JsonFields job;
        JsonFields truck;
        JsonFields trailer;
        {
            std::lock_guard<std::mutex> lock(configuration_mutex);
            job = job_configuration;
            truck = truck_configuration;
            trailer = trailer_configuration;
        }
        if (!job.empty()) {
            fields.insert_or_assign("job_configuration", JsonObject(job));
            for (const char* key : {
                     "cargo.id", "cargo", "cargo.mass", "source.city",
                     "source.company", "destination.city", "destination.company",
                     "planned_distance.km", "delivery.time"}) {
                const auto value = job.find(key);
                if (value != job.end()) {
                    fields.insert_or_assign(key, value->second);
                }
            }
            AddAlias(fields, "cargo_id", "cargo.id");
            AddAlias(fields, "cargo", "cargo");
            AddAlias(fields, "weight", "cargo.mass");
            AddAlias(fields, "planned_distance_km", "planned_distance.km");
            AddAlias(fields, "delivery_time_game_minutes", "delivery.time");
            AddAlias(fields, "source_city", "source.city");
            AddAlias(fields, "source_company", "source.company");
            AddAlias(fields, "destination_city", "destination.city");
            AddAlias(fields, "destination_company", "destination.company");
        }
        if (!truck.empty()) {
            fields.insert_or_assign("truck_configuration", JsonObject(truck));
            AddAlias(truck, "truck", "brand");
            AddAlias(truck, "truck", "name");
            AddAlias(truck, "truck_license_plate", "license.plate");
            AddAlias(truck, "truck_license_plate_country", "license.plate.country");
            AddAlias(truck, "truck_license_plate_country_id", "license.plate.country.id");
            for (const auto& [key, value] : truck) {
                if (key == "truck" || key == "truck_license_plate"
                    || key == "truck_license_plate_country"
                    || key == "truck_license_plate_country_id") {
                    fields.insert_or_assign(key, value);
                }
            }
        }
        if (!trailer.empty()) {
            fields.insert_or_assign("trailer_configuration", JsonObject(trailer));
            AddAlias(trailer, "trailer", "brand");
            AddAlias(trailer, "trailer", "name");
            AddAlias(trailer, "trailer_license_plate", "license.plate");
            AddAlias(trailer, "trailer_license_plate_country", "license.plate.country");
            AddAlias(trailer, "trailer_license_plate_country_id", "license.plate.country.id");
            for (const auto& [key, value] : trailer) {
                if (key == "trailer" || key == "trailer_license_plate"
                    || key == "trailer_license_plate_country"
                    || key == "trailer_license_plate_country_id") {
                    fields.insert_or_assign(key, value);
                }
            }
        }
        AddAlias(fields, "driven_distance_km", "distance.km");
        AddAlias(fields, "income", "revenue");
        AddAlias(fields, "xp", "earned.xp");
        AddAlias(fields, "damage", "cargo.damage");
        AddAlias(fields, "delivery_time_game_minutes", "delivery.time");
        fields.insert_or_assign("reported_fields", JsonObject(CaptureAttributes(
            gameplay->attributes)));
    } else if (fined) {
        AddAlias(fields, "offences", "fine.offence");
        AddAlias(fields, "fine_amount", "fine.amount");
    } else if (toll || ferry || train) {
        AddAlias(fields, "amount", "pay.amount");
        AddAlias(fields, "source_name", "source.name");
        AddAlias(fields, "target_name", "target.name");
        AddAlias(fields, "source_id", "source.id");
        AddAlias(fields, "target_id", "target.id");
    }

    PublishGameplayEvent(event_name, std::move(fields));
}

void ClosePositionMapping(bool invalidate = true) {
    if (shared_position) {
        if (invalidate) {
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
            shared_position->flags = 0;
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
        }
        UnmapViewOfFile(shared_position);
        shared_position = nullptr;
    }
    if (mapping_handle) {
        CloseHandle(mapping_handle);
        mapping_handle = nullptr;
    }
    CloseEventMapping();
}

SCSAPI_VOID StorePosition(
    const scs_string_t,
    const scs_u32_t,
    const scs_value_t* const value,
    const scs_context_t) {
    if (!shared_position) {
        return;
    }

    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
    if (value && value->type == SCS_VALUE_TYPE_dplacement) {
        const auto& position = value->value_dplacement.position;
        if (std::isfinite(position.x) && std::isfinite(position.y)
            && std::isfinite(position.z)) {
            shared_position->x = position.x;
            shared_position->y = position.y;
            shared_position->z = position.z;
            shared_position->timestamp_ms = GetTickCount64();
            shared_position->flags = nlsi::providers::kScsPositionFlagValid;
        } else {
            shared_position->flags = 0;
        }
    } else {
        shared_position->flags = 0;
    }
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
}

} // namespace

SCSAPI_RESULT scs_telemetry_init(
    const scs_u32_t version,
    const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00 || !params) {
        return SCS_RESULT_unsupported;
    }

    const auto* init = static_cast<const scs_telemetry_init_params_v100_t*>(params);
    std::uint32_t game_id = 0;
    if (std::strcmp(init->common.game_id, SCS_GAME_ID_EUT2) == 0) {
        game_id = nlsi::providers::kScsPositionGameEts2;
    } else if (std::strcmp(init->common.game_id, SCS_GAME_ID_ATS) == 0) {
        game_id = nlsi::providers::kScsPositionGameAts;
    } else {
        init->common.log(SCS_LOG_TYPE_warning, "NLSI SUPPORTS ETS2 AND ATS ONLY.");
        return SCS_RESULT_unsupported;
    }
    active_game_id = game_id;

    SetLastError(ERROR_SUCCESS);
    mapping_handle = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(nlsi::providers::ScsPositionIpcV1)),
        nlsi::providers::kScsPositionMappingName);
    const DWORD mapping_error = GetLastError();
    if (!mapping_handle) {
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT CREATE THE DEDICATED NLSI POSITION INTERFACE.");
        return SCS_RESULT_generic_error;
    }

    shared_position = static_cast<nlsi::providers::ScsPositionIpcV1*>(
        MapViewOfFile(mapping_handle, FILE_MAP_WRITE, 0, 0,
            sizeof(nlsi::providers::ScsPositionIpcV1)));
    if (!shared_position) {
        ClosePositionMapping();
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT MAP THE DEDICATED NLSI POSITION INTERFACE.");
        return SCS_RESULT_generic_error;
    }

    if (mapping_error == ERROR_ALREADY_EXISTS) {
        const std::uint64_t now = GetTickCount64();
        const auto* sequence = reinterpret_cast<const volatile LONG*>(
            &shared_position->sequence);
        MemoryBarrier();
        const LONG sequence_before = *sequence;
        MemoryBarrier();
        nlsi::providers::ScsPositionIpcV1 previous{};
        std::memcpy(&previous, shared_position, sizeof(previous));
        MemoryBarrier();
        const LONG sequence_after = *sequence;
        MemoryBarrier();
        const bool stable_previous_sample = sequence_before == sequence_after
            && (sequence_before & 1) == 0
            && previous.sequence == static_cast<std::uint32_t>(sequence_before);
        const bool previous_sample_is_live =
            stable_previous_sample
            && previous.magic == nlsi::providers::kScsPositionIpcMagic
            && previous.version == nlsi::providers::kScsPositionIpcVersion
            && previous.struct_size == sizeof(*shared_position)
            && (previous.flags & nlsi::providers::kScsPositionFlagValid) != 0
            && previous.timestamp_ms <= now
            && now - previous.timestamp_ms
                <= nlsi::providers::kScsPositionMaxAgeMs;
        if (!stable_previous_sample
            || previous.magic != nlsi::providers::kScsPositionIpcMagic
            || previous.version != nlsi::providers::kScsPositionIpcVersion
            || previous.struct_size != sizeof(*shared_position)
            || previous_sample_is_live) {
            ClosePositionMapping(false);
            init->common.log(SCS_LOG_TYPE_error,
                "THE DEDICATED NLSI POSITION INTERFACE IS INCOMPATIBLE OR ALREADY ACTIVE.");
            return SCS_RESULT_generic_error;
        }
    } else {
        std::memset(shared_position, 0, sizeof(*shared_position));
        shared_position->magic = nlsi::providers::kScsPositionIpcMagic;
        shared_position->version = nlsi::providers::kScsPositionIpcVersion;
        shared_position->struct_size = sizeof(*shared_position);
    }
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
    shared_position->flags = 0;
    shared_position->game_id = game_id;
    shared_position->x = 0.0;
    shared_position->y = 0.0;
    shared_position->z = 0.0;
    shared_position->timestamp_ms = 0;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared_position->sequence));
    game_log = init->common.log;

    {
        std::lock_guard<std::mutex> lock(configuration_mutex);
        job_configuration.clear();
        truck_configuration.clear();
        trailer_configuration.clear();
    }
    if (!OpenEventMapping(init)) {
        ClosePositionMapping();
        game_log = nullptr;
        return SCS_RESULT_generic_error;
    }

    scs_result_t result = init->register_for_event(
        SCS_TELEMETRY_EVENT_configuration, StoreConfiguration, nullptr);
    if (result != SCS_RESULT_ok) {
        ClosePositionMapping();
        game_log = nullptr;
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT REGISTER FOR SCS CONFIGURATION EVENTS.");
        return result;
    }
    result = init->register_for_event(
        SCS_TELEMETRY_EVENT_gameplay, StoreGameplayEvent, nullptr);
    if (result != SCS_RESULT_ok) {
        ClosePositionMapping();
        game_log = nullptr;
        init->common.log(SCS_LOG_TYPE_error,
            "COULD NOT REGISTER FOR SCS GAMEPLAY EVENTS.");
        return result;
    }

    result = init->register_for_channel(
        SCS_TELEMETRY_TRUCK_CHANNEL_world_placement,
        SCS_U32_NIL,
        SCS_VALUE_TYPE_dplacement,
        SCS_TELEMETRY_CHANNEL_FLAG_each_frame | SCS_TELEMETRY_CHANNEL_FLAG_no_value,
        StorePosition,
        nullptr);
    if (result != SCS_RESULT_ok) {
        ClosePositionMapping();
        game_log = nullptr;
        init->common.log(SCS_LOG_TYPE_error,
            "Could not register the truck world-position telemetry channel.");
        return result;
    }

    init->common.log(SCS_LOG_TYPE_message,
        "NLSI TELEMETRY PROVIDER INITIALIZED WITH SCS POSITION AND GAMEPLAY EVENT CAPTURE.");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    ClosePositionMapping();
    game_log = nullptr;
}
