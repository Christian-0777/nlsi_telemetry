#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>

#include "TelemetryModel.h"

namespace nlsi::telemetry {

struct JobFuelResult {
    double fuel_used_liters = 0.0;
    double refueled_liters = 0.0;
    double odometer_distance_km = 0.0;
    std::optional<double> average_consumption_l_per_100km;
    bool average_uses_reported_distance = false;
};

class JobFuelTracker {
public:
    using Clock = std::chrono::steady_clock;

    void Observe(
        const TelemetrySnapshot& snapshot,
        ProviderState state,
        Clock::time_point observed_at = Clock::now()) {
        if (!tracking_) {
            if (state != ProviderState::Connected
                || !snapshot.connected
                || !snapshot.has_job.available
                || snapshot.has_job.stale
                || !snapshot.has_job.value) {
                return;
            }
            Start(snapshot, observed_at);
            return;
        }

        if (state != ProviderState::Connected || !snapshot.connected) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }
        if (!snapshot.has_job.available || snapshot.has_job.stale) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }
        if (!snapshot.has_job.value) {
            job_active_ = false;
            previous_sample_available_ = false;
            completed_ = false;
            return;
        }

        const std::wstring identity = JobIdentity(snapshot);
        if (identity.empty()) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }
        if (completed_ && identity == identity_) {
            return;
        }
        if (!job_active_) {
            Start(snapshot, observed_at);
            return;
        }
        if (identity != identity_) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }
        if (!reliable_) {
            return;
        }

        double fuel = 0.0;
        double odometer = 0.0;
        if (!ReadValidSample(snapshot, fuel, odometer)
            || !snapshot.paused.available || snapshot.paused.stale) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }
        if (!previous_sample_available_) {
            SetBaseline(fuel, odometer, observed_at);
            return;
        }
        if (observed_at <= previous_sample_at_
            || observed_at - previous_sample_at_ > kMaximumSampleGap
            || odometer < previous_odometer_km_ - kValueTolerance) {
            reliable_ = false;
            previous_sample_available_ = false;
            return;
        }

        if (!snapshot.paused.value) {
            const double fuel_delta = fuel - previous_fuel_liters_;
            if (fuel_delta < 0.0) {
                fuel_used_liters_ += -fuel_delta;
            } else {
                refueled_liters_ += fuel_delta;
            }
            odometer_distance_km_ += std::max(0.0, odometer - previous_odometer_km_);
        }
        SetBaseline(fuel, odometer, observed_at);
    }

    std::optional<JobFuelResult> Finish(
        const std::wstring& identity,
        std::optional<double> reported_distance_km) {
        if (!tracking_ || identity.empty() || identity != identity_) {
            return std::nullopt;
        }
        job_active_ = false;
        completed_ = true;
        if (!reliable_ || !previous_sample_available_) {
            return std::nullopt;
        }

        JobFuelResult result;
        result.fuel_used_liters = fuel_used_liters_;
        result.refueled_liters = refueled_liters_;
        result.odometer_distance_km = odometer_distance_km_;
        double distance_km = 0.0;
        if (reported_distance_km && std::isfinite(*reported_distance_km)
            && *reported_distance_km > 0.0) {
            distance_km = *reported_distance_km;
            result.average_uses_reported_distance = true;
        } else {
            distance_km = odometer_distance_km_;
        }
        if (distance_km > 0.0 && std::isfinite(distance_km)) {
            const double average = fuel_used_liters_ / distance_km * 100.0;
            if (std::isfinite(average) && average >= 0.0) {
                result.average_consumption_l_per_100km = average;
            }
        }
        return result;
    }

private:
    static constexpr auto kMaximumSampleGap = std::chrono::seconds(5);
    static constexpr double kValueTolerance = 0.000001;

    static std::wstring JobIdentity(const TelemetrySnapshot& snapshot) {
        if (!snapshot.cargo_id.available || snapshot.cargo_id.stale) {
            return {};
        }
        return snapshot.cargo_id.value;
    }

    static bool ReadValidSample(
        const TelemetrySnapshot& snapshot,
        double& fuel,
        double& odometer) {
        if (!snapshot.fuel_liters.available || snapshot.fuel_liters.stale
            || !snapshot.odometer_km.available || snapshot.odometer_km.stale
            || !std::isfinite(snapshot.fuel_liters.value)
            || !std::isfinite(snapshot.odometer_km.value)
            || snapshot.fuel_liters.value < 0.0
            || snapshot.odometer_km.value < 0.0) {
            return false;
        }
        fuel = snapshot.fuel_liters.value;
        odometer = snapshot.odometer_km.value;
        return true;
    }

    void Start(const TelemetrySnapshot& snapshot, Clock::time_point observed_at) {
        identity_ = JobIdentity(snapshot);
        tracking_ = true;
        job_active_ = true;
        completed_ = false;
        reliable_ = !identity_.empty()
            && snapshot.paused.available
            && !snapshot.paused.stale;
        previous_sample_available_ = false;
        fuel_used_liters_ = 0.0;
        refueled_liters_ = 0.0;
        odometer_distance_km_ = 0.0;

        double fuel = 0.0;
        double odometer = 0.0;
        if (tracking_ && ReadValidSample(snapshot, fuel, odometer)) {
            SetBaseline(fuel, odometer, observed_at);
        } else if (tracking_) {
            reliable_ = false;
        }
    }

    void SetBaseline(double fuel, double odometer, Clock::time_point observed_at) {
        previous_fuel_liters_ = fuel;
        previous_odometer_km_ = odometer;
        previous_sample_at_ = observed_at;
        previous_sample_available_ = true;
    }

    std::wstring identity_;
    bool tracking_ = false;
    bool job_active_ = false;
    bool completed_ = false;
    bool reliable_ = false;
    bool previous_sample_available_ = false;
    double previous_fuel_liters_ = 0.0;
    double previous_odometer_km_ = 0.0;
    double fuel_used_liters_ = 0.0;
    double refueled_liters_ = 0.0;
    double odometer_distance_km_ = 0.0;
    Clock::time_point previous_sample_at_{};
};

} // namespace nlsi::telemetry
