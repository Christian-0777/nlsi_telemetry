#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "telemetry/TelemetryModel.h"

namespace nlsi::providers {

class TruckSimGpsProvider {
public:
    using UpdateCallback = std::function<void(
        const telemetry::TelemetrySnapshot&,
        telemetry::ProviderState,
        const telemetry::ProviderStatus&)>;
    using EventCallback = std::function<void(const std::string&)>;

    TruckSimGpsProvider();
    ~TruckSimGpsProvider();
    TruckSimGpsProvider(const TruckSimGpsProvider&) = delete;
    TruckSimGpsProvider& operator=(const TruckSimGpsProvider&) = delete;

    bool Start(UpdateCallback callback, EventCallback event_callback = {});
    void Stop();
    telemetry::ProviderState State() const;
    std::wstring Name() const;

    static bool DecodeRevision13(
        const std::uint8_t* data,
        std::size_t size,
        telemetry::TelemetrySnapshot& snapshot,
        std::uint32_t& revision,
        std::wstring& error);

private:
    void ReadLoop();
    void Publish(
        const telemetry::TelemetrySnapshot& snapshot,
        telemetry::ProviderState state,
        const telemetry::ProviderStatus& status);

    std::atomic_bool stopping_{false};
    std::atomic<telemetry::ProviderState> state_{telemetry::ProviderState::Disconnected};
    UpdateCallback callback_;
    EventCallback event_callback_;
    std::thread worker_;
};

} // namespace nlsi::providers
