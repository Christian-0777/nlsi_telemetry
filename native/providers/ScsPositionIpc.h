#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>

namespace nlsi::providers {

inline constexpr wchar_t kScsPositionMappingName[] = L"Local\\NLSI.SCS.Position.v1";
inline constexpr std::uint32_t kScsPositionIpcMagic = 0x3150534e; // NSP1
inline constexpr std::uint32_t kScsPositionIpcVersion = 1;
inline constexpr std::uint32_t kScsPositionFlagValid = 1;
inline constexpr std::uint32_t kScsPositionGameEts2 = 1;
inline constexpr std::uint32_t kScsPositionGameAts = 2;
inline constexpr std::uint64_t kScsPositionMaxAgeMs = 3000;
inline constexpr std::uint32_t kScsPositionIpcSize = 56;

struct alignas(8) ScsPositionIpcV1 {
    std::uint32_t magic = kScsPositionIpcMagic;
    std::uint32_t version = kScsPositionIpcVersion;
    std::uint32_t struct_size = kScsPositionIpcSize;
    std::uint32_t sequence = 0;
    std::uint32_t flags = 0;
    std::uint32_t game_id = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::uint64_t timestamp_ms = 0;
};

static_assert(offsetof(ScsPositionIpcV1, sequence) == 12);
static_assert(offsetof(ScsPositionIpcV1, x) == 24);
static_assert(offsetof(ScsPositionIpcV1, timestamp_ms) == 48);
static_assert(sizeof(ScsPositionIpcV1) == 56);

struct ScsPositionFrame {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::uint32_t game_id = 0;
    std::uint64_t timestamp_ms = 0;
    std::uint64_t age_ms = 0;
    bool stale = false;
};

inline bool DecodeScsPositionMapping(
    const std::uint8_t* bytes,
    std::size_t size,
    std::uint64_t now_ms,
    ScsPositionFrame& frame,
    std::wstring& error) {
    if (!bytes || size < sizeof(ScsPositionIpcV1)) {
        error = L"Position mapping is missing or truncated.";
        return false;
    }

    ScsPositionIpcV1 sample{};
    std::memcpy(&sample, bytes, sizeof(sample));
    if (sample.magic != kScsPositionIpcMagic
        || sample.version != kScsPositionIpcVersion
        || sample.struct_size != sizeof(ScsPositionIpcV1)) {
        error = L"Position mapping has an unsupported signature or version.";
        return false;
    }
    if ((sample.sequence & 1u) != 0) {
        error = L"Position sample was being updated.";
        return false;
    }
    if ((sample.flags & kScsPositionFlagValid) == 0) {
        error = L"Position is not currently available from the game.";
        return false;
    }
    if (sample.game_id != kScsPositionGameEts2
        && sample.game_id != kScsPositionGameAts) {
        error = L"Position mapping identifies an unsupported game.";
        return false;
    }
    if (!std::isfinite(sample.x) || !std::isfinite(sample.y)
        || !std::isfinite(sample.z)) {
        error = L"Position sample contains non-finite coordinates.";
        return false;
    }
    if (sample.timestamp_ms > now_ms) {
        error = L"Position sample timestamp is in the future.";
        return false;
    }

    frame.x = sample.x;
    frame.y = sample.y;
    frame.z = sample.z;
    frame.game_id = sample.game_id;
    frame.timestamp_ms = sample.timestamp_ms;
    frame.age_ms = now_ms - sample.timestamp_ms;
    frame.stale = frame.age_ms > kScsPositionMaxAgeMs;
    error = frame.stale ? L"Position sample is stale." : std::wstring{};
    return true;
}

} // namespace nlsi::providers
