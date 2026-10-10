#define WIN32_LEAN_AND_MEAN
#include "ScsPositionProvider.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <system_error>
#include <thread>

#include "ScsPositionIpc.h"
#include "ScsTelemetryEventIpc.h"

namespace nlsi::providers {

ScsPositionProvider::~ScsPositionProvider() {
    Stop();
}

bool ScsPositionProvider::Start(
    UpdateCallback callback,
    EventCallback event_callback) {
    if (worker_.joinable()) {
        return false;
    }
    callback_ = std::move(callback);
    event_callback_ = std::move(event_callback);
    stopping_.store(false);
    try {
        worker_ = std::thread(&ScsPositionProvider::ReadLoop, this);
    } catch (const std::system_error&) {
        callback_ = {};
        event_callback_ = {};
        return false;
    }
    return true;
}

void ScsPositionProvider::Stop() {
    stopping_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    callback_ = {};
    event_callback_ = {};
}

void ScsPositionProvider::Publish(const ScsPositionSnapshot& snapshot) {
    if (callback_) {
        callback_(snapshot);
    }
}

void ScsPositionProvider::PublishEvent(const std::string& packet) {
    if (event_callback_) {
        event_callback_(packet);
    }
}

void ScsPositionProvider::ReadLoop() {
    HANDLE mapping = nullptr;
    const ScsPositionIpcV1* view = nullptr;
    HANDLE event_mapping = nullptr;
    const ScsTelemetryEventIpcV1* event_view = nullptr;
    std::uint64_t last_event_id = 0;
    ScsPositionSnapshot last{};
    last.error = L"Position plugin mapping has not been opened.";

    while (!stopping_.load()) {
        if (!event_mapping) {
            event_mapping = OpenFileMappingW(
                FILE_MAP_READ, FALSE, kScsTelemetryEventMappingName);
            if (event_mapping) {
                event_view = static_cast<const ScsTelemetryEventIpcV1*>(
                    MapViewOfFile(
                        event_mapping,
                        FILE_MAP_READ,
                        0,
                        0,
                        sizeof(ScsTelemetryEventIpcV1)));
                if (!event_view) {
                    CloseHandle(event_mapping);
                    event_mapping = nullptr;
                }
            }
        }

        if (event_view
            && event_view->magic == kScsTelemetryEventIpcMagic
            && event_view->version == kScsTelemetryEventIpcVersion
            && event_view->struct_size == sizeof(ScsTelemetryEventIpcV1)
            && event_view->slot_count == kScsTelemetryEventCapacity) {
            const auto* latest_address =
                reinterpret_cast<const volatile LONG64*>(&event_view->latest_event_id);
            const std::uint64_t latest_event_id = static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    const_cast<volatile LONG64*>(latest_address), 0, 0));
            if (latest_event_id > last_event_id) {
                if (latest_event_id - last_event_id > kScsTelemetryEventCapacity) {
                    last_event_id = latest_event_id - kScsTelemetryEventCapacity;
                }
                while (last_event_id < latest_event_id) {
                    const std::uint64_t expected_event_id = last_event_id + 1;
                    const auto& slot = event_view->entries[
                        (expected_event_id - 1) % kScsTelemetryEventCapacity];
                    const auto* sequence_address =
                        reinterpret_cast<const volatile LONG*>(&slot.sequence);
                    const LONG sequence_before = InterlockedCompareExchange(
                        const_cast<volatile LONG*>(sequence_address), 0, 0);
                    if ((sequence_before & 1) != 0) {
                        break;
                    }
                    ScsTelemetryEventSlotV1 copy{};
                    std::memcpy(&copy, &slot, sizeof(copy));
                    MemoryBarrier();
                    const LONG sequence_after = InterlockedCompareExchange(
                        const_cast<volatile LONG*>(sequence_address), 0, 0);
                    std::string packet;
                    if (sequence_before != sequence_after
                        || (sequence_after & 1) != 0
                        || copy.sequence != static_cast<std::uint32_t>(sequence_after)
                        || !DecodeScsTelemetryEventSlot(
                            copy, expected_event_id, packet)) {
                        break;
                    }
                    PublishEvent(packet);
                    last_event_id = expected_event_id;
                }
            }
        } else if (event_view) {
            UnmapViewOfFile(event_view);
            event_view = nullptr;
            CloseHandle(event_mapping);
            event_mapping = nullptr;
        }

        if (!mapping) {
            mapping = OpenFileMappingW(
                FILE_MAP_READ, FALSE, kScsPositionMappingName);
            if (mapping) {
                view = static_cast<const ScsPositionIpcV1*>(
                    MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(ScsPositionIpcV1)));
                if (!view) {
                    const DWORD error = GetLastError();
                    CloseHandle(mapping);
                    mapping = nullptr;
                    last = {};
                    last.error = L"Could not map the SCS position interface (Windows error "
                        + std::to_wstring(error) + L").";
                }
            } else {
                last = {};
                last.error = L"SCS position plugin or game is not connected.";
            }
        }

        if (view) {
            const auto* sequence = reinterpret_cast<const volatile LONG*>(
                &view->sequence);
            MemoryBarrier();
            const LONG sequence_before = *sequence;
            MemoryBarrier();
            ScsPositionIpcV1 copy{};
            std::memcpy(&copy, view, sizeof(copy));
            MemoryBarrier();
            const LONG sequence_after = *sequence;
            MemoryBarrier();
            if (sequence_before == sequence_after
                && (sequence_before & 1) == 0
                && copy.sequence == static_cast<std::uint32_t>(sequence_before)) {
                ScsPositionFrame frame;
                std::wstring error;
                if (DecodeScsPositionMapping(
                        reinterpret_cast<const std::uint8_t*>(&copy),
                        sizeof(copy),
                        GetTickCount64(),
                        frame,
                        error)) {
                    last.state = frame.stale
                        ? ScsPositionState::Stale : ScsPositionState::Connected;
                    last.available = true;
                    last.x = frame.x;
                    last.y = frame.y;
                    last.z = frame.z;
                    last.game_id = frame.game_id;
                    last.age_ms = frame.age_ms;
                    last.error = std::move(error);
                } else {
                    last = {};
                    last.error = std::move(error);
                }
            } else {
                last = {};
                last.error = L"Position sample changed while it was being read.";
            }
        }

        Publish(last);
        for (int elapsed = 0; elapsed < 250 && !stopping_.load(); elapsed += 10) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (view) {
        UnmapViewOfFile(view);
    }
    if (mapping) {
        CloseHandle(mapping);
    }
    if (event_view) {
        UnmapViewOfFile(event_view);
    }
    if (event_mapping) {
        CloseHandle(event_mapping);
    }
}

} // namespace nlsi::providers
