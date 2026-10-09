#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstring>

#include "scssdk_telemetry.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"
#include "ScsPositionIpc.h"

namespace {

HANDLE mapping_handle = nullptr;
nlsi::providers::ScsPositionIpcV1* shared_position = nullptr;
scs_log_t game_log = nullptr;

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
        init->common.log(SCS_LOG_TYPE_warning, "nlsi supports ETS2 and ATS only.");
        return SCS_RESULT_unsupported;
    }

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
            "Could not create the dedicated NLSI position interface.");
        return SCS_RESULT_generic_error;
    }

    shared_position = static_cast<nlsi::providers::ScsPositionIpcV1*>(
        MapViewOfFile(mapping_handle, FILE_MAP_WRITE, 0, 0,
            sizeof(nlsi::providers::ScsPositionIpcV1)));
    if (!shared_position) {
        ClosePositionMapping();
        init->common.log(SCS_LOG_TYPE_error,
            "Could not map the dedicated NLSI position interface.");
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
                "The dedicated NLSI position interface is incompatible or already active.");
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

    const scs_result_t result = init->register_for_channel(
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
        "nlsi position provider initialized using the dedicated v1 interface.");
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    ClosePositionMapping();
    game_log = nullptr;
}
