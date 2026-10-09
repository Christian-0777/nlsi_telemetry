#include "DashboardPage.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QStringList>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

QString RouteText(
    const telemetry::TelemetryField<std::wstring>& company,
    const telemetry::TelemetryField<std::wstring>& city) {
    const QString company_text = FieldText(company);
    const QString city_text = FieldText(city);
    if (company_text == QStringLiteral("--")) {
        return city_text;
    }
    if (city_text == QStringLiteral("--") || company_text == city_text) {
        return company_text;
    }
    return company_text + QStringLiteral(" · ") + city_text;
}

QString PercentText(const telemetry::TelemetryField<double>& field) {
    if (!field.available || !std::isfinite(field.value)) {
        return QStringLiteral("--");
    }
    return FormatNumber(field.value * 100.0, 1) + QLatin1Char('%')
        + (field.stale ? QStringLiteral(" · stale") : QString());
}

QString VehicleControlsText(const telemetry::TelemetrySnapshot& snapshot) {
    QStringList controls;
    if (snapshot.connected && snapshot.cruise_control_active.available
        && !snapshot.cruise_control_active.stale) {
        QString cruise = QStringLiteral("Cruise: %1")
            .arg(snapshot.cruise_control_active.value
                ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
        if (snapshot.cruise_control_speed.available
            && !snapshot.cruise_control_speed.stale
            && std::isfinite(snapshot.cruise_control_speed.value)) {
            cruise += QStringLiteral(" · %1")
                .arg(NumberText(snapshot.cruise_control_speed, 1, QStringLiteral(" km/h")));
        }
        controls.push_back(cruise);
    }
    if (snapshot.connected && snapshot.retarder_level.available
        && !snapshot.retarder_level.stale
        && std::isfinite(snapshot.retarder_level.value)
        && snapshot.retarder_level.value >= 0.0
        && snapshot.retarder_level.value
            <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())
        && std::floor(snapshot.retarder_level.value) == snapshot.retarder_level.value) {
        const auto level = static_cast<std::uint32_t>(snapshot.retarder_level.value);
        controls.push_back(QStringLiteral("Retarder: %1 · level %2")
            .arg(level > 0 ? QStringLiteral("Active") : QStringLiteral("Inactive"))
            .arg(level));
    }
    return controls.join(QLatin1Char('\n'));
}

QString NavigationText(const telemetry::TelemetryUiState& state) {
    QStringList values;
    if (state.progress.remaining_distance_km
        && std::isfinite(*state.progress.remaining_distance_km)
        && *state.progress.remaining_distance_km >= 0.0) {
        values.push_back(QStringLiteral("Remaining: %1")
            .arg(OptionalNumberText(
                state.progress.remaining_distance_km, 2, QStringLiteral(" km"))));
    }
    const auto& navigation_time = state.fast.values.navigation_time_s;
    if (state.fast.values.connected && navigation_time.available
        && !navigation_time.stale && std::isfinite(navigation_time.value)
        && navigation_time.value >= 0.0) {
        values.push_back(QStringLiteral("Navigation time: %1")
            .arg(DurationText(navigation_time.value)));
    }
    if (state.progress.eta_seconds && std::isfinite(*state.progress.eta_seconds)
        && *state.progress.eta_seconds >= 0.0) {
        values.push_back(QStringLiteral("ETA: %1")
            .arg(DurationText(state.progress.eta_seconds)));
    }
    return values.isEmpty() ? QStringLiteral("Unavailable") : values.join(QLatin1Char('\n'));
}

QString CurrentJobText(const telemetry::TelemetryUiState& state) {
    if (!state.job.available) {
        return state.job_status == telemetry::JobStatus::NoJob
            ? QStringLiteral("No active delivery")
            : QStringLiteral("Unavailable");
    }
    QStringList lines;
    lines.push_back(QStringLiteral("NLSI job ID: %1")
        .arg(state.job.nlsi_job_id.empty()
            ? QStringLiteral("Unavailable")
            : QString::fromStdWString(state.job.nlsi_job_id)));
    lines.push_back(QStringLiteral("Cargo: %1").arg(FieldText(state.job.cargo)));
    lines.push_back(QStringLiteral("Source: %1")
        .arg(RouteText(state.job.source_company, state.job.source_city)));
    lines.push_back(QStringLiteral("Destination: %1")
        .arg(RouteText(state.job.destination_company, state.job.destination_city)));
    lines.push_back(QStringLiteral("Status: %1")
        .arg(QString::fromStdWString(telemetry::FormatJobStatus(state.job_status))));
    const auto& special_job = state.fast.values.special_job;
    if (special_job.available && !special_job.stale) {
        const QString value = QString::fromStdWString(special_job.value).trimmed().toLower();
        if (value == QStringLiteral("true") || value == QStringLiteral("false")) {
            lines.push_back(value == QStringLiteral("true")
                ? QStringLiteral("Special job: Yes")
                : QStringLiteral("Special job: No"));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : StatePage(parent) {
    setObjectName(QStringLiteral("dashboardPage"));
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(2, 2, 2, 2);
    page_layout->setSpacing(6);

    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    AddCard(QStringLiteral("status"), QStringLiteral("CONNECTION / GAME / PROVIDER"),
        0, 0, grid, 3);
    AddCard(QStringLiteral("speed"), QStringLiteral("SPEED"), 1, 0, grid);
    AddCard(QStringLiteral("engine"), QStringLiteral("RPM / GEAR"), 1, 1, grid);
    AddCard(QStringLiteral("fuel"), QStringLiteral("FUEL / RANGE / ODOMETER"), 1, 2, grid);
    AddCard(QStringLiteral("throttle"), QStringLiteral("THROTTLE"), 2, 0, grid);
    AddCard(QStringLiteral("brake"), QStringLiteral("BRAKE"), 2, 1, grid);
    AddCard(QStringLiteral("job"), QStringLiteral("CURRENT JOB"), 3, 0, grid, 3);
    AddCard(QStringLiteral("navigation"), QStringLiteral("NAVIGATION"), 4, 0, grid, 2);
    AddCard(QStringLiteral("controls"), QStringLiteral("VEHICLE CONTROLS"), 4, 2, grid);
    page_layout->addLayout(grid);
    page_layout->addStretch(1);
}

QLabel* DashboardPage::AddCard(
    const QString& key,
    const QString& title,
    int row,
    int column,
    QGridLayout* grid,
    int column_span) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("telemetryCard"));
    card->setMinimumHeight(66);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);
    auto* title_label = new QLabel(title, card);
    title_label->setObjectName(QStringLiteral("cardTitle"));
    title_label->setWordWrap(false);
    auto* value_label = new QLabel(QStringLiteral("--"), card);
    value_label->setObjectName(QStringLiteral("cardValue"));
    value_label->setWordWrap(key == QStringLiteral("job")
        || key == QStringLiteral("navigation") || key == QStringLiteral("controls"));
    value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(title_label);
    layout->addWidget(value_label);
    layout->addStretch(1);
    grid->addWidget(card, row, column, 1, column_span);
    values_.insert(key, value_label);
    cards_.insert(key, card);
    return value_label;
}

void DashboardPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    if (label && label->text() != value) {
        label->setText(value);
    }
}

void DashboardPage::SetCardVisible(const QString& key, bool visible) {
    if (QFrame* card = cards_.value(key, nullptr)) {
        card->setVisible(visible);
    }
}

void DashboardPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& snapshot = state.fast.values;
    const QString provider = QString::fromStdWString(
        telemetry::FormatStatus(state.providers.trucksim)).toUpper();
    SetValue(QStringLiteral("status"),
        QStringLiteral("%1 · %2 · %3")
            .arg(snapshot.connected ? QStringLiteral("CONNECTED")
                                   : QStringLiteral("NOT CONNECTED"),
                FieldText(snapshot.game_name), provider));
    SetValue(QStringLiteral("speed"),
        NumberText(snapshot.speed_kmh, 2, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("engine"),
        NumberText(snapshot.rpm, 0) + QStringLiteral(" RPM · Gear ")
            + NumberText(snapshot.gear, 0));
    SetValue(QStringLiteral("fuel"),
        QStringLiteral("%1 · Range: %2\nOdometer: %3")
            .arg(NumberText(snapshot.fuel_liters, 2, QStringLiteral(" L")),
                NumberText(snapshot.fuel_range_km, 2, QStringLiteral(" km")),
                NumberText(snapshot.odometer_km, 2, QStringLiteral(" km"))));
    SetValue(QStringLiteral("throttle"), PercentText(snapshot.effective_throttle));
    SetValue(QStringLiteral("brake"), PercentText(snapshot.effective_brake));
    SetValue(QStringLiteral("job"), CurrentJobText(state));
    SetValue(QStringLiteral("navigation"), NavigationText(state));
    const QString controls = VehicleControlsText(snapshot);
    SetValue(QStringLiteral("controls"),
        controls.isEmpty() ? QStringLiteral("Unavailable") : controls);
    SetCardVisible(QStringLiteral("controls"), !controls.isEmpty());
}

} // namespace nlsi::gui
