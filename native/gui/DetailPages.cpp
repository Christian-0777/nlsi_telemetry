#include "LiveDrivePage.h"
#include "CurrentJobPage.h"
#include "TelemetryPage.h"
#include "ProvidersPage.h"
#include "SettingsPage.h"
#include "AboutPage.h"
#include "ActiveModsPage.h"

#include <cmath>

#include <QCoreApplication>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

QString StatusText(telemetry::ProviderState state) {
    return QString::fromStdWString(telemetry::FormatStatus(state));
}

QString GameConnection(const telemetry::TelemetryUiState& state) {
    return state.fast.values.connected
        ? QStringLiteral("CONNECTED")
        : StatusText(state.providers.nlsi);
}

QString JobRoute(
    const telemetry::TelemetryField<std::wstring>& company,
    const telemetry::TelemetryField<std::wstring>& city) {
    const QString company_name = FieldText(company);
    const QString city_name = FieldText(city);
    if (company_name == QStringLiteral("--")) {
        return city_name;
    }
    if (city_name == QStringLiteral("--") || city_name == company_name) {
        return company_name;
    }
    return company_name + QStringLiteral(" · ") + city_name;
}

class ApplicationSettingsPage final : public DetailPage {
public:
    explicit ApplicationSettingsPage(QWidget* parent = nullptr)
        : DetailPage(QStringLiteral("Application Settings"),
            QStringLiteral("Advanced configuration and display preferences."), parent) {
        AddField(QStringLiteral("version"), QStringLiteral("Application version"));
        AddField(QStringLiteral("channel"), QStringLiteral("Release channel"));
        AddField(QStringLiteral("port"), QStringLiteral("NLSI telemetry UDP port"));
        AddField(QStringLiteral("refresh"), QStringLiteral("UI refresh rate"));
        AddField(QStringLiteral("transport"), QStringLiteral("Telemetry transport"));
        AddField(QStringLiteral("display"), QStringLiteral("Unavailable values"));
    }

    void UpdateState(const telemetry::TelemetryUiState&) override {
        SetValue(QStringLiteral("version"), QCoreApplication::applicationVersion());
        SetValue(QStringLiteral("channel"), QStringLiteral("Alpha"));
        SetValue(QStringLiteral("port"), QStringLiteral("28745"));
        SetValue(QStringLiteral("refresh"), QStringLiteral("4 Hz (250 ms)"));
        SetValue(QStringLiteral("transport"), QStringLiteral("Local SCS plugin UDP"));
        SetValue(QStringLiteral("display"), QStringLiteral("Hidden when not supplied"));
    }
};

} // namespace

LiveDrivePage::LiveDrivePage(QWidget* parent)
    : DetailPage(QStringLiteral("Live Drive"),
        QStringLiteral("Current vehicle data from the active telemetry provider."), parent) {
    AddField(QStringLiteral("game"), QStringLiteral("Game"));
    AddField(QStringLiteral("connection"), QStringLiteral("Connection"));
    AddField(QStringLiteral("speed"), QStringLiteral("Speed"));
    AddField(QStringLiteral("rpm"), QStringLiteral("Engine RPM"));
    AddField(QStringLiteral("gear"), QStringLiteral("Gear"));
    AddField(QStringLiteral("fuel"), QStringLiteral("Fuel"));
    AddField(QStringLiteral("range"), QStringLiteral("Fuel range"));
    AddField(QStringLiteral("odometer"), QStringLiteral("Odometer"));
    AddField(QStringLiteral("navigation"), QStringLiteral("Navigation distance"));
    AddField(QStringLiteral("navigationTime"), QStringLiteral("Navigation time"));
    AddField(QStringLiteral("session"), QStringLiteral("Session"));
}

void LiveDrivePage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("game"), FieldText(values.game_name));
    SetValue(QStringLiteral("connection"), GameConnection(state));
    SetValue(QStringLiteral("speed"), NumberText(values.speed_kmh, 1, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("rpm"), NumberText(values.rpm, 0));
    SetValue(QStringLiteral("gear"), NumberText(values.gear, 0));
    SetValue(QStringLiteral("fuel"), NumberText(values.fuel_liters, 1, QStringLiteral(" L")));
    SetValue(QStringLiteral("range"), NumberText(values.fuel_range_km, 1, QStringLiteral(" km")));
    SetValue(QStringLiteral("odometer"), NumberText(values.odometer_km, 1, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigation"),
        NumberText(values.navigation_distance_km, 1, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigationTime"),
        values.navigation_time_s.available && !values.navigation_time_s.stale
            ? DurationText(values.navigation_time_s.value)
            : QStringLiteral("--"));
    SetValue(QStringLiteral("session"),
        QString::fromStdWString(telemetry::FormatSessionStatus(state.session.status)));
}

CurrentJobPage::CurrentJobPage(QWidget* parent)
    : DetailPage(QStringLiteral("Current Job"),
        QStringLiteral("Job identity is held separately from live progress values."), parent) {
    AddField(QStringLiteral("status"), QStringLiteral("Status"));
    AddField(QStringLiteral("cargo"), QStringLiteral("Cargo"));
    AddField(QStringLiteral("cargoId"), QStringLiteral("Cargo ID"));
    AddField(QStringLiteral("source"), QStringLiteral("Source"));
    AddField(QStringLiteral("destination"), QStringLiteral("Destination"));
    AddField(QStringLiteral("income"), QStringLiteral("Income"));
    AddField(QStringLiteral("planned"), QStringLiteral("Planned distance"));
    AddField(QStringLiteral("loaded"), QStringLiteral("Loaded"));
    AddField(QStringLiteral("remaining"), QStringLiteral("Remaining distance"));
    AddField(QStringLiteral("progress"), QStringLiteral("Progress"));
    AddField(QStringLiteral("eta"), QStringLiteral("ETA"));
}

void CurrentJobPage::UpdateState(const telemetry::TelemetryUiState& state) {
    SetValue(QStringLiteral("status"),
        QString::fromStdWString(telemetry::FormatJobStatus(state.job_status)));
    SetValue(QStringLiteral("cargo"), FieldText(state.job.cargo));
    SetValue(QStringLiteral("cargoId"), FieldText(state.job.cargo_id));
    SetValue(QStringLiteral("source"), JobRoute(state.job.source_company, state.job.source_city));
    SetValue(QStringLiteral("destination"),
        JobRoute(state.job.destination_company, state.job.destination_city));
    SetValue(QStringLiteral("income"), FieldText(state.job.income));
    SetValue(QStringLiteral("planned"),
        state.job.planned_distance.available
            ? FieldText(state.job.planned_distance) + QStringLiteral(" km")
            : QStringLiteral("--"));
    const QString loaded = !state.job.loaded.available
        ? QStringLiteral("--")
        : (state.job.loaded.value ? QStringLiteral("Yes") : QStringLiteral("No"));
    SetValue(QStringLiteral("loaded"), loaded);
    SetValue(QStringLiteral("remaining"),
        OptionalNumberText(state.progress.remaining_distance_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("progress"),
        OptionalNumberText(state.progress.progress_percent, 1, QStringLiteral("%")));
    SetValue(QStringLiteral("eta"), DurationText(state.progress.eta_seconds));
}

TelemetryPage::TelemetryPage(QWidget* parent)
    : DetailPage(QStringLiteral("Telemetry"),
        QStringLiteral("Normalized values only. Unavailable fields remain blank."), parent) {
    AddField(QStringLiteral("game"), QStringLiteral("Game"));
    AddField(QStringLiteral("sample"), QStringLiteral("Last sample"));
    AddField(QStringLiteral("speed"), QStringLiteral("Speed"));
    AddField(QStringLiteral("rpm"), QStringLiteral("RPM"));
    AddField(QStringLiteral("gear"), QStringLiteral("Gear"));
    AddField(QStringLiteral("throttle"), QStringLiteral("Effective throttle"));
    AddField(QStringLiteral("brake"), QStringLiteral("Effective brake"));
    AddField(QStringLiteral("retarder"), QStringLiteral("Retarder level"));
    AddField(QStringLiteral("cruise"), QStringLiteral("Cruise control speed"));
    AddField(QStringLiteral("fuel"), QStringLiteral("Fuel"));
    AddField(QStringLiteral("fuelRange"), QStringLiteral("Fuel range"));
    AddField(QStringLiteral("odometer"), QStringLiteral("Odometer"));
    AddField(QStringLiteral("navigation"), QStringLiteral("Navigation distance"));
    AddField(QStringLiteral("navigationTime"), QStringLiteral("Navigation time"));
    AddField(QStringLiteral("eta"), QStringLiteral("Calculated ETA"));
}

void TelemetryPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("game"), FieldText(values.game_name));
    SetValue(QStringLiteral("sample"), QString::fromStdWString(values.timestamp));
    SetValue(QStringLiteral("speed"), NumberText(values.speed_kmh, 2, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("rpm"), NumberText(values.rpm, 0));
    SetValue(QStringLiteral("gear"), NumberText(values.gear, 0));
    const auto percentage_text = [](const telemetry::TelemetryField<double>& field) {
        if (!field.available || !std::isfinite(field.value)) {
            return QStringLiteral("--");
        }
        QString text = QString::number(field.value * 100.0, 'f', 0) + QLatin1Char('%');
        if (field.stale) {
            text += QStringLiteral("  · stale");
        }
        return text;
    };
    SetValue(QStringLiteral("throttle"), percentage_text(values.effective_throttle));
    SetValue(QStringLiteral("brake"), percentage_text(values.effective_brake));
    SetValue(QStringLiteral("retarder"), NumberText(values.retarder_level, 0));
    SetValue(QStringLiteral("cruise"),
        NumberText(values.cruise_control_speed, 1, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("fuel"), NumberText(values.fuel_liters, 2, QStringLiteral(" L")));
    SetValue(QStringLiteral("fuelRange"), NumberText(values.fuel_range_km, 1, QStringLiteral(" km")));
    SetValue(QStringLiteral("odometer"), NumberText(values.odometer_km, 1, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigation"),
        NumberText(values.navigation_distance_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigationTime"),
        values.navigation_time_s.available && !values.navigation_time_s.stale
            ? DurationText(values.navigation_time_s.value)
            : QStringLiteral("--"));
    SetValue(QStringLiteral("eta"),
        values.eta_seconds.available && !values.eta_seconds.stale
            ? DurationText(values.eta_seconds.value)
            : QStringLiteral("--"));
}

ProvidersPage::ProvidersPage(QWidget* parent)
    : DetailPage(QStringLiteral("Providers"),
        QStringLiteral("NLSI is preferred. RenCloud status is shown independently."), parent) {
    AddField(QStringLiteral("nlsi"), QStringLiteral("NLSI"));
    AddField(QStringLiteral("rencloud"), QStringLiteral("RenCloud"));
    AddField(QStringLiteral("combined"), QStringLiteral("Combined"));
    AddField(QStringLiteral("freshness"), QStringLiteral("Telemetry sample"));
    AddField(QStringLiteral("source"), QStringLiteral("Active telemetry source"));
    AddField(QStringLiteral("fallback"), QStringLiteral("Fallback"));
    AddField(QStringLiteral("error"), QStringLiteral("Last provider message"));
}

void ProvidersPage::UpdateState(const telemetry::TelemetryUiState& state) {
    SetValue(QStringLiteral("nlsi"), StatusText(state.providers.nlsi));
    SetValue(QStringLiteral("rencloud"), StatusText(state.providers.rencloud));
    SetValue(QStringLiteral("combined"),
        QString::fromStdWString(telemetry::FormatCombinedStatus(state.providers.combined)));
    SetValue(QStringLiteral("freshness"),
        QString::fromStdWString(state.providers.telemetry_freshness));
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("source"),
        values.speed_kmh.available ? QString::fromStdWString(values.speed_kmh.source) : QStringLiteral("--"));
    SetValue(QStringLiteral("fallback"),
        state.providers.rencloud == telemetry::ProviderState::Connected
            ? QStringLiteral("Available for missing NLSI fields")
            : QStringLiteral("Unavailable: no RenCloud telemetry connection"));
    SetValue(QStringLiteral("error"),
        state.providers.last_error.empty()
            ? QStringLiteral("--")
            : QString::fromStdWString(state.providers.last_error));
}

SettingsPage::SettingsPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(18);
    auto* heading = new QLabel(QStringLiteral("Settings"), this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Provider health, telemetry diagnostics, mod availability, and application configuration."),
        this);
    description->setObjectName(QStringLiteral("pageDescription"));
    description->setWordWrap(true);
    layout->addWidget(description);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("contentTabs"));
    application_settings_ = new ApplicationSettingsPage(tabs_);
    providers_ = new ProvidersPage(tabs_);
    telemetry_ = new TelemetryPage(tabs_);
    active_mods_ = new ActiveModsPage(tabs_);
    tabs_->addTab(application_settings_, QStringLiteral("Application"));
    tabs_->addTab(providers_, QStringLiteral("Providers"));
    tabs_->addTab(telemetry_, QStringLiteral("Telemetry"));
    tabs_->addTab(active_mods_, QStringLiteral("Active Mods"));
    layout->addWidget(tabs_, 1);
}

void SettingsPage::UpdateState(const telemetry::TelemetryUiState& state) {
    application_settings_->UpdateState(state);
    providers_->UpdateState(state);
    telemetry_->UpdateState(state);
    active_mods_->UpdateState(state);
}

AboutPage::AboutPage(const QString& version, QWidget* parent)
    : DetailPage(QStringLiteral("About"),
        QStringLiteral("Application version and implementation details."), parent),
      version_(version) {
    AddField(QStringLiteral("product"), QStringLiteral("Product"));
    AddField(QStringLiteral("version"), QStringLiteral("Version"));
    AddField(QStringLiteral("channel"), QStringLiteral("Channel"));
    AddField(QStringLiteral("framework"), QStringLiteral("UI framework"));
    AddField(QStringLiteral("backend"), QStringLiteral("Telemetry backend"));
}

void AboutPage::UpdateState(const telemetry::TelemetryUiState&) {
    SetValue(QStringLiteral("product"), QStringLiteral("NLSI Exclusive Logbook"));
    SetValue(QStringLiteral("version"), version_);
    SetValue(QStringLiteral("channel"), QStringLiteral("Alpha"));
    SetValue(QStringLiteral("framework"),
        QStringLiteral("Qt %1 Widgets").arg(QString::fromLatin1(qVersion())));
    SetValue(QStringLiteral("backend"), QStringLiteral("C++20 · SCS Telemetry SDK"));
}

} // namespace nlsi::gui
