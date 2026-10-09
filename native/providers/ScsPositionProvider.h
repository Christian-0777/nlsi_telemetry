#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace nlsi::providers {

enum class ScsPositionState {
    Disconnected,
    Connected,
    Stale
};

struct ScsPositionSnapshot {
    ScsPositionState state = ScsPositionState::Disconnected;
    bool available = false;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::uint32_t game_id = 0;
    std::uint64_t age_ms = 0;
    std::wstring error = L"Waiting for the SCS position plugin.";
};

class ScsPositionProvider {
public:
    using UpdateCallback = std::function<void(const ScsPositionSnapshot&)>;

    ScsPositionProvider() = default;
    ~ScsPositionProvider();
    ScsPositionProvider(const ScsPositionProvider&) = delete;
    ScsPositionProvider& operator=(const ScsPositionProvider&) = delete;

    bool Start(UpdateCallback callback);
    void Stop();

private:
    void ReadLoop();
    void Publish(const ScsPositionSnapshot& snapshot);

    std::atomic_bool stopping_{false};
    UpdateCallback callback_;
    std::thread worker_;
};

} // namespace nlsi::providers
