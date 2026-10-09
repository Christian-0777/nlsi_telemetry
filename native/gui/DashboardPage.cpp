#include "DashboardPage.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

QString ConnectionText(const telemetry::TelemetryUiState& state) {
    return state.fast.values.connected
        ? QStringLiteral("CONNECTED")
        : QStringLiteral("NOT CONNECTED");
}

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

QString JobSummary(const telemetry::TelemetryUiState& state) {
    const QString cargo = FieldText(state.job.cargo);
    if (!state.job.available) {
        return state.job_status == telemetry::JobStatus::NoJob
            ? QStringLiteral("No active delivery")
            : QStringLiteral("--");
    }
    const QString status = QString::fromStdWString(telemetry::FormatJobStatus(state.job_status));
    return cargo == QStringLiteral("--")
        ? status
        : cargo + QStringLiteral("\n") + status;
}

QString SessionSummary(const telemetry::TelemetryUiState& state) {
    QString summary = QString::fromStdWString(telemetry::FormatSessionStatus(state.session.status));
    if (!state.session.timestamp.empty()) {
        summary += QStringLiteral("\n") + QString::fromStdWString(state.session.timestamp);
    }
    return summary;
}

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : StatePage(parent) {
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(4, 4, 4, 4);
    page_layout->setSpacing(14);

    auto* grid = new QGridLayout();
    grid->setSpacing(14);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    AddCard(QStringLiteral("game"), QStringLiteral("GAME"), 0, 0, grid);
    AddCard(QStringLiteral("connection"), QStringLiteral("CONNECTION"), 0, 1, grid);
    AddCard(QStringLiteral("speed"), QStringLiteral("SPEED"), 0, 2, grid);
    AddCard(QStringLiteral("engine"), QStringLiteral("RPM / GEAR"), 1, 0, grid);
    AddCard(QStringLiteral("fuel"), QStringLiteral("FUEL / RANGE"), 1, 1, grid);
    AddCard(QStringLiteral("cargo"), QStringLiteral("CURRENT JOB"), 1, 2, grid);
    AddCard(QStringLiteral("route"), QStringLiteral("ROUTE"), 2, 0, grid);
    AddCard(QStringLiteral("session"), QStringLiteral("SESSION SUMMARY"), 2, 1, grid);
    page_layout->addLayout(grid);
    page_layout->addStretch(1);
}

QLabel* DashboardPage::AddCard(
    const QString& key,
    const QString& title,
    int row,
    int column,
    QGridLayout* grid) {
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("telemetryCard"));
    card->setMinimumHeight(108);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(8);
    auto* title_label = new QLabel(title, card);
    title_label->setObjectName(QStringLiteral("cardTitle"));
    auto* value_label = new QLabel(QStringLiteral("--"), card);
    value_label->setObjectName(QStringLiteral("cardValue"));
    value_label->setWordWrap(true);
    value_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(title_label);
    layout->addWidget(value_label);
    layout->addStretch(1);
    grid->addWidget(card, row, column);
    values_.insert(key, value_label);
    return value_label;
}

void DashboardPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    if (label && label->text() != value) {
        label->setText(value);
    }
}

void DashboardPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& snapshot = state.fast.values;
    SetValue(QStringLiteral("game"), FieldText(snapshot.game_name));
    SetValue(QStringLiteral("connection"), ConnectionText(state));
    SetValue(QStringLiteral("speed"), NumberText(snapshot.speed_kmh, 2, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("engine"),
        NumberText(snapshot.rpm, 0) + QStringLiteral(" RPM\nGear ") + NumberText(snapshot.gear, 0));
    SetValue(QStringLiteral("fuel"),
        NumberText(snapshot.fuel_liters, 2, QStringLiteral(" L"))
        + QStringLiteral("\n") + NumberText(snapshot.fuel_range_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("cargo"), JobSummary(state));
    const QString source = RouteText(state.job.source_company, state.job.source_city);
    const QString destination = RouteText(state.job.destination_company, state.job.destination_city);
    SetValue(QStringLiteral("route"),
        source == QStringLiteral("--") && destination == QStringLiteral("--")
            ? QStringLiteral("--")
            : source + QStringLiteral("\n→ ") + destination);
    SetValue(QStringLiteral("session"), SessionSummary(state));
}

} // namespace nlsi::gui
