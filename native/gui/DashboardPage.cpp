#include "DashboardPage.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QResizeEvent>
#include <QStringList>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "providers/ScsPositionIpc.h"

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

bool HasFreshText(const telemetry::TelemetryField<std::wstring>& field) {
    return field.available && !field.stale && !field.value.empty()
        && !QString::fromStdWString(field.value).trimmed().isEmpty();
}

QString CurrentPositionText(const telemetry::TelemetryUiState& state) {
    QString text = QStringLiteral("Unavailable");
    const auto& position = state.scs_position;
    if (position.available) {
        const QString game = position.game_id == nlsi::providers::kScsPositionGameEts2
            ? QStringLiteral("ETS2")
            : position.game_id == nlsi::providers::kScsPositionGameAts
                ? QStringLiteral("ATS") : QStringLiteral("Game");
        text = QStringLiteral("%1 · X %2 m · Y %3 m · Z %4 m")
            .arg(game,
                FormatNumber(position.x, 2),
                FormatNumber(position.y, 2),
                FormatNumber(position.z, 2));
        if (position.state == nlsi::providers::ScsPositionState::Stale) {
            text += QStringLiteral(" · stale");
        }
    }
    const auto& has_job = state.fast.values.has_job;
    if (state.fast.values.connected && state.job.available
        && has_job.available && !has_job.stale && has_job.value) {
        const QString destination = HasFreshText(state.job.destination_city)
            ? QString::fromStdWString(state.job.destination_city.value).trimmed()
            : QStringLiteral("Unavailable");
        text += QStringLiteral("\n→ Destination: %1").arg(destination);
    }
    return text;
}

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : StatePage(parent) {
    setObjectName(QStringLiteral("dashboardPage"));
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(2, 2, 2, 2);
    page_layout->setSpacing(6);

    grid_ = new QGridLayout();
    grid_->setObjectName(QStringLiteral("dashboardCardGrid"));
    grid_->setSpacing(6);
    AddCard(QStringLiteral("status"), QStringLiteral("CONNECTION / GAME / PROVIDER"), grid_);
    AddCard(QStringLiteral("speed"), QStringLiteral("SPEED"), grid_);
    AddCard(QStringLiteral("engine"), QStringLiteral("RPM / GEAR"), grid_);
    AddCard(QStringLiteral("fuel"), QStringLiteral("FUEL / RANGE / ODOMETER"), grid_);
    AddCard(QStringLiteral("throttle"), QStringLiteral("THROTTLE"), grid_);
    AddCard(QStringLiteral("brake"), QStringLiteral("BRAKE"), grid_);
    AddCard(QStringLiteral("job"), QStringLiteral("CURRENT JOB"), grid_);
    AddCard(QStringLiteral("position"), QStringLiteral("CURRENT POSITION"), grid_);
    AddCard(QStringLiteral("navigation"), QStringLiteral("NAVIGATION"), grid_);
    AddCard(QStringLiteral("controls"), QStringLiteral("VEHICLE CONTROLS"), grid_);
    page_layout->addLayout(grid_);
    page_layout->addStretch(1);
    ReflowCards();
}

QLabel* DashboardPage::AddCard(
    const QString& key,
    const QString& title,
    QGridLayout* grid) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("telemetryCard"));
    card->setMinimumHeight(66);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setMinimumWidth(0);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(2);
    if (key == QStringLiteral("position")) {
        auto* title_row = new QWidget(card);
        auto* title_layout = new QHBoxLayout(title_row);
        title_layout->setContentsMargins(0, 0, 0, 0);
        title_layout->setSpacing(5);
        auto* icon = new QLabel(title_row);
        icon->setObjectName(QStringLiteral("currentPositionIcon"));
        icon->setPixmap(QIcon(QStringLiteral(":/icons/map-pin.svg"))
            .pixmap(14, 14));
        icon->setFixedSize(14, 14);
        title_layout->addWidget(icon);
        auto* title_label = new QLabel(title, title_row);
        title_label->setObjectName(QStringLiteral("cardTitle"));
        title_label->setWordWrap(true);
        title_label->setMinimumWidth(0);
        title_layout->addWidget(title_label, 1);
        layout->addWidget(title_row);
    } else {
        auto* title_label = new QLabel(title, card);
        title_label->setObjectName(QStringLiteral("cardTitle"));
        title_label->setWordWrap(true);
        layout->addWidget(title_label);
    }
    auto* value_label = new QLabel(QStringLiteral("--"), card);
    value_label->setObjectName(QStringLiteral("cardValue"));
    value_label->setWordWrap(true);
    value_label->setMinimumWidth(0);
    value_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(value_label);
    layout->addStretch(1);
    grid->addWidget(card, 0, 0);
    values_.insert(key, value_label);
    cards_.insert(key, card);
    ordered_cards_.push_back(card);
    return value_label;
}

void DashboardPage::resizeEvent(QResizeEvent* event) {
    StatePage::resizeEvent(event);
    ReflowCards();
}

void DashboardPage::ReflowCards() {
    if (!grid_) {
        return;
    }
    const int available_width = contentsRect().width();
    const int columns = available_width < 520 ? 1 : available_width < 850 ? 2 : 3;
    if (columns == grid_columns_) {
        return;
    }
    for (int column = 0; column < 3; ++column) {
        grid_->setColumnStretch(column, column < columns ? 1 : 0);
        grid_->setColumnMinimumWidth(column, 0);
    }
    while (QLayoutItem* item = grid_->takeAt(0)) {
        delete item;
    }
    grid_columns_ = columns;
    for (qsizetype index = 0; index < ordered_cards_.size(); ++index) {
        grid_->addWidget(ordered_cards_[index],
            static_cast<int>(index / columns),
            static_cast<int>(index % columns));
    }
}

void DashboardPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    const QString normalized = value.trimmed().isEmpty()
        ? QStringLiteral("Unavailable") : value;
    if (label && label->text() != normalized) {
        label->setText(normalized);
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
    SetValue(QStringLiteral("position"), CurrentPositionText(state));
    SetValue(QStringLiteral("navigation"), NavigationText(state));
    const QString controls = VehicleControlsText(snapshot);
    SetValue(QStringLiteral("controls"),
        controls.isEmpty() ? QStringLiteral("Unavailable") : controls);
    SetCardVisible(QStringLiteral("controls"), !controls.isEmpty());
}

} // namespace nlsi::gui
