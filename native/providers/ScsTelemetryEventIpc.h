#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace nlsi::providers {

inline constexpr wchar_t kScsTelemetryEventMappingName[] =
    L"Local\\NLSI.SCS.Events.v1";
inline constexpr std::uint32_t kScsTelemetryEventIpcMagic = 0x3145534e; // NSE1
inline constexpr std::uint32_t kScsTelemetryEventIpcVersion = 1;
inline constexpr std::size_t kScsTelemetryEventCapacity = 32;
inline constexpr std::size_t kScsTelemetryEventPayloadSize = 2048;

struct alignas(8) ScsTelemetryEventSlotV1 {
    std::uint32_t sequence = 0;
    std::uint32_t payload_size = 0;
    std::uint64_t event_id = 0;
    char payload[kScsTelemetryEventPayloadSize]{};
};

struct alignas(8) ScsTelemetryEventIpcV1 {
    std::uint32_t magic = kScsTelemetryEventIpcMagic;
    std::uint32_t version = kScsTelemetryEventIpcVersion;
    std::uint32_t struct_size = 0;
    std::uint32_t slot_count = static_cast<std::uint32_t>(kScsTelemetryEventCapacity);
    std::uint64_t latest_event_id = 0;
    ScsTelemetryEventSlotV1 entries[kScsTelemetryEventCapacity]{};
};

static_assert(offsetof(ScsTelemetryEventSlotV1, event_id) == 8);
static_assert(offsetof(ScsTelemetryEventSlotV1, payload) == 16);
static_assert(sizeof(ScsTelemetryEventSlotV1) == 16 + kScsTelemetryEventPayloadSize);
static_assert(offsetof(ScsTelemetryEventIpcV1, latest_event_id) == 16);
static_assert(offsetof(ScsTelemetryEventIpcV1, entries) == 24);

inline bool DecodeScsTelemetryEventSlot(
    const ScsTelemetryEventSlotV1& slot,
    std::uint64_t expected_event_id,
    std::string& payload) {
    if (slot.event_id != expected_event_id
        || slot.payload_size == 0
        || slot.payload_size >= kScsTelemetryEventPayloadSize
        || slot.payload[slot.payload_size] != '\0') {
        return false;
    }
    payload.assign(slot.payload, slot.payload_size);
    return true;
}

} // namespace nlsi::providers
