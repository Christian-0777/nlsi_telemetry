#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "telemetry/TelemetryModel.h"

namespace nlsi::providers {

class NLSIProvider {
public:
    using UpdateCallback = std::function<void(
        const telemetry::TelemetrySnapshot&,
        telemetry::ProviderState,
        const std::wstring&)>;

    static constexpr unsigned short kUdpPort = 28745;

    NLSIProvider();
    ~NLSIProvider();
    NLSIProvider(const NLSIProvider&) = delete;
    NLSIProvider& operator=(const NLSIProvider&) = delete;

    bool Start(UpdateCallback callback);
    void Stop();
    bool IsRunning() const;
    telemetry::ProviderState State() const;
    std::wstring Name() const;

    static bool ParseTelemetryPacket(
        const std::string& packet,
        telemetry::TelemetrySnapshot& snapshot,
        std::wstring* error = nullptr);

private:
    void ReceiveLoop();
    void Publish(
        const telemetry::TelemetrySnapshot& snapshot,
        telemetry::ProviderState state,
        const std::wstring& error);

    std::atomic_bool stopping_{false};
    std::atomic_bool running_{false};
    std::atomic<telemetry::ProviderState> state_{telemetry::ProviderState::Disconnected};
    UpdateCallback callback_;
    std::thread worker_;
};

} // namespace nlsi::providers
