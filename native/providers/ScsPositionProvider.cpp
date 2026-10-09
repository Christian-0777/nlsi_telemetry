#define WIN32_LEAN_AND_MEAN
#include "ScsPositionProvider.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <system_error>
#include <thread>

#include "ScsPositionIpc.h"

namespace nlsi::providers {

ScsPositionProvider::~ScsPositionProvider() {
    Stop();
}

bool ScsPositionProvider::Start(UpdateCallback callback) {
    if (worker_.joinable()) {
        return false;
    }
    callback_ = std::move(callback);
    stopping_.store(false);
    try {
        worker_ = std::thread(&ScsPositionProvider::ReadLoop, this);
    } catch (const std::system_error&) {
        callback_ = {};
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
}

void ScsPositionProvider::Publish(const ScsPositionSnapshot& snapshot) {
    if (callback_) {
        callback_(snapshot);
    }
}

void ScsPositionProvider::ReadLoop() {
    HANDLE mapping = nullptr;
    const ScsPositionIpcV1* view = nullptr;
    ScsPositionSnapshot last{};
    last.error = L"Position plugin mapping has not been opened.";

    while (!stopping_.load()) {
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
}

} // namespace nlsi::providers
