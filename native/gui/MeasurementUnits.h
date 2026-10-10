#pragma once

#include <cmath>
#include <optional>

#include <QSettings>
#include <QString>

namespace nlsi::gui {

enum class MeasurementSystem {
    Metric,
    USCustomary
};

inline MeasurementSystem CurrentMeasurementSystem() {
    QSettings settings(QStringLiteral("NLSI"), QStringLiteral("Exclusive Logbook"));
    return settings.value(QStringLiteral("measurement/system"), QStringLiteral("metric"))
            .toString().compare(QStringLiteral("us"), Qt::CaseInsensitive) == 0
        ? MeasurementSystem::USCustomary
        : MeasurementSystem::Metric;
}

inline void SetMeasurementSystem(MeasurementSystem system) {
    QSettings settings(QStringLiteral("NLSI"), QStringLiteral("Exclusive Logbook"));
    settings.setValue(QStringLiteral("measurement/system"),
        system == MeasurementSystem::USCustomary
            ? QStringLiteral("us") : QStringLiteral("metric"));
}

inline double DisplayDistance(double kilometers, MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? kilometers * 0.621371192237334
        : kilometers;
}

inline double DisplaySpeed(double kilometers_per_hour, MeasurementSystem system) {
    return DisplayDistance(kilometers_per_hour, system);
}

inline double DisplayFuelVolume(double liters, MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? liters / 3.785411784
        : liters;
}

inline std::optional<double> DisplayFuelEconomy(
    double liters_per_100_kilometers,
    MeasurementSystem system) {
    if (system == MeasurementSystem::Metric) {
        return liters_per_100_kilometers;
    }
    if (!std::isfinite(liters_per_100_kilometers)
        || liters_per_100_kilometers <= 0.0) {
        return std::nullopt;
    }
    return 235.214583 / liters_per_100_kilometers;
}

inline double DisplayMassKilograms(double kilograms, MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? kilograms * 2.20462262185
        : kilograms;
}

inline QString DistanceUnit(MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? QStringLiteral("mi") : QStringLiteral("km");
}

inline QString SpeedUnit(MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? QStringLiteral("mph") : QStringLiteral("km/h");
}

inline QString FuelVolumeUnit(MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? QStringLiteral("US gal") : QStringLiteral("L");
}

inline QString FuelEconomyUnit(MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? QStringLiteral("US mpg") : QStringLiteral("L/100 km");
}

inline QString MassUnit(MeasurementSystem system) {
    return system == MeasurementSystem::USCustomary
        ? QStringLiteral("lb") : QStringLiteral("kg");
}

} // namespace nlsi::gui
