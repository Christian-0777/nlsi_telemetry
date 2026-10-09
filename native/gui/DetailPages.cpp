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
#include <QUrl>
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
        : StatusText(state.providers.trucksim);
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
        : DetailPage(parent) {
        AddField(QStringLiteral("version"), QStringLiteral("Application version"));
        AddField(QStringLiteral("channel"), QStringLiteral("Release channel"));
        AddField(QStringLiteral("refresh"), QStringLiteral("UI refresh rate"));
        AddField(QStringLiteral("transport"), QStringLiteral("Telemetry transport"));
        AddField(QStringLiteral("display"), QStringLiteral("Unavailable values"));
    }

    void UpdateState(const telemetry::TelemetryUiState&) override {
        SetValue(QStringLiteral("version"), QCoreApplication::applicationVersion());
        SetValue(QStringLiteral("channel"), QStringLiteral("Beta"));
        SetValue(QStringLiteral("refresh"), QStringLiteral("4 Hz (250 ms)"));
        SetValue(QStringLiteral("transport"), QStringLiteral("TruckSim GPS shared memory"));
        SetValue(QStringLiteral("display"), QStringLiteral("Hidden when not supplied"));
    }
};

} // namespace

LiveDrivePage::LiveDrivePage(QWidget* parent)
    : DetailPage(parent) {
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
    AddField(QStringLiteral("cruise"), QStringLiteral("Cruise control"));
    AddField(QStringLiteral("retarder"), QStringLiteral("Retarder"));
    AddField(QStringLiteral("session"), QStringLiteral("Session"));
}

void LiveDrivePage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("game"), FieldText(values.game_name));
    SetValue(QStringLiteral("connection"), GameConnection(state));
    SetValue(QStringLiteral("speed"), NumberText(values.speed_kmh, 2, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("rpm"), NumberText(values.rpm, 0));
    SetValue(QStringLiteral("gear"), NumberText(values.gear, 0));
    SetValue(QStringLiteral("fuel"), NumberText(values.fuel_liters, 2, QStringLiteral(" L")));
    SetValue(QStringLiteral("range"), NumberText(values.fuel_range_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("odometer"), NumberText(values.odometer_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigation"),
        NumberText(values.navigation_distance_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("navigationTime"),
        values.navigation_time_s.available && !values.navigation_time_s.stale
            ? DurationText(values.navigation_time_s.value)
            : QStringLiteral("--"));
    const QString cruise_status = !values.cruise_control_active.available
        ? QStringLiteral("Unavailable")
        : values.cruise_control_active.value
            ? QStringLiteral("Enabled")
            : QStringLiteral("Disabled");
    const QString cruise_speed = NumberText(
        values.cruise_control_speed, 2, QStringLiteral(" km/h"));
    SetValue(QStringLiteral("cruise"),
        cruise_speed == QStringLiteral("--")
            ? cruise_status
            : QStringLiteral("%1 · set to %2").arg(cruise_status, cruise_speed));
    const QString retarder_status = !values.retarder_active.available
        ? QStringLiteral("Unavailable")
        : values.retarder_active.value
            ? QStringLiteral("Enabled")
            : QStringLiteral("Disabled");
    const QString retarder_level = NumberText(values.retarder_level, 0);
    SetValue(QStringLiteral("retarder"),
        retarder_level == QStringLiteral("--")
            ? retarder_status
            : QStringLiteral("%1 · level %2").arg(retarder_status, retarder_level));
    SetValue(QStringLiteral("session"),
        QString::fromStdWString(telemetry::FormatSessionStatus(state.session.status)));
}

CurrentJobPage::CurrentJobPage(QWidget* parent)
    : DetailPage(parent) {
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
    SetValue(QStringLiteral("income"), NumericText(FieldText(state.job.income)));
    SetValue(QStringLiteral("planned"),
        state.job.planned_distance.available
            ? NumericText(FieldText(state.job.planned_distance)) + QStringLiteral(" km")
            : QStringLiteral("--"));
    const QString loaded = !state.job.loaded.available
        ? QStringLiteral("--")
        : (state.job.loaded.value ? QStringLiteral("Yes") : QStringLiteral("No"));
    SetValue(QStringLiteral("loaded"), loaded);
    SetValue(QStringLiteral("remaining"),
        OptionalNumberText(state.progress.remaining_distance_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("progress"),
        OptionalNumberText(state.progress.progress_percent, 2, QStringLiteral("%")));
    SetValue(QStringLiteral("eta"), DurationText(state.progress.eta_seconds));
}

TelemetryPage::TelemetryPage(QWidget* parent)
    : DetailPage(parent) {
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
        QString text = FormatNumber(field.value * 100.0, 2) + QLatin1Char('%');
        if (field.stale) {
            text += QStringLiteral("  · stale");
        }
        return text;
    };
    SetValue(QStringLiteral("throttle"), percentage_text(values.effective_throttle));
    SetValue(QStringLiteral("brake"), percentage_text(values.effective_brake));
    SetValue(QStringLiteral("retarder"), NumberText(values.retarder_level, 0));
    SetValue(QStringLiteral("cruise"),
        NumberText(values.cruise_control_speed, 2, QStringLiteral(" km/h")));
    SetValue(QStringLiteral("fuel"), NumberText(values.fuel_liters, 2, QStringLiteral(" L")));
    SetValue(QStringLiteral("fuelRange"), NumberText(values.fuel_range_km, 2, QStringLiteral(" km")));
    SetValue(QStringLiteral("odometer"), NumberText(values.odometer_km, 2, QStringLiteral(" km")));
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
    : DetailPage(parent) {
    AddField(QStringLiteral("trucksim"), QStringLiteral("TruckSim GPS"));
    AddField(QStringLiteral("combined"), QStringLiteral("Combined"));
    AddField(QStringLiteral("freshness"), QStringLiteral("Telemetry sample"));
    AddField(QStringLiteral("source"), QStringLiteral("Active telemetry source"));
    AddField(QStringLiteral("mapping"), QStringLiteral("TruckSim GPS mapping"));
    AddField(QStringLiteral("mappingOpen"), QStringLiteral("Mapping handle"));
    AddField(QStringLiteral("viewMapped"), QStringLiteral("Shared-memory view"));
    AddField(QStringLiteral("layout"), QStringLiteral("Detected layout"));
    AddField(QStringLiteral("failureStage"), QStringLiteral("TruckSim GPS stage"));
    AddField(QStringLiteral("lastRead"), QStringLiteral("Last successful read"));
    AddField(QStringLiteral("dataAge"), QStringLiteral("Source data age"));
    AddField(QStringLiteral("sourceTimestamp"), QStringLiteral("Plugin timestamps"));
    AddField(QStringLiteral("win32"), QStringLiteral("Windows error"));
    AddField(QStringLiteral("storage"), QStringLiteral("Local logs and history"));
    AddField(QStringLiteral("error"), QStringLiteral("Last provider message"));
}

void ProvidersPage::UpdateState(const telemetry::TelemetryUiState& state) {
    SetValue(QStringLiteral("trucksim"), StatusText(state.providers.trucksim));
    SetValue(QStringLiteral("combined"),
        QString::fromStdWString(telemetry::FormatCombinedStatus(state.providers.combined)));
    SetValue(QStringLiteral("freshness"),
        QString::fromStdWString(state.providers.telemetry_freshness));
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("source"),
        values.speed_kmh.available ? QString::fromStdWString(values.speed_kmh.source) : QStringLiteral("--"));
    SetValue(QStringLiteral("mapping"),
        QString::fromStdWString(state.providers.trucksim_mapping_name));
    SetValue(QStringLiteral("mappingOpen"),
        state.providers.trucksim_mapping_open ? QStringLiteral("Open") : QStringLiteral("Closed"));
    SetValue(QStringLiteral("viewMapped"),
        state.providers.trucksim_view_mapped ? QStringLiteral("Mapped") : QStringLiteral("Not mapped"));
    SetValue(QStringLiteral("layout"),
        QString::fromStdWString(state.providers.trucksim_layout));
    SetValue(QStringLiteral("failureStage"),
        QString::fromStdWString(state.providers.trucksim_stage));
    SetValue(QStringLiteral("lastRead"),
        state.providers.trucksim_last_read.empty()
            ? QStringLiteral("Never")
            : QString::fromStdWString(state.providers.trucksim_last_read));
    SetValue(QStringLiteral("dataAge"),
        QString::fromStdWString(state.providers.trucksim_data_age));
    SetValue(QStringLiteral("sourceTimestamp"),
        state.providers.trucksim_source_timestamp.empty()
            ? QStringLiteral("--")
            : QString::fromStdWString(state.providers.trucksim_source_timestamp));
    SetValue(QStringLiteral("win32"),
        state.providers.trucksim_win32_error == 0
            ? QStringLiteral("None")
            : QString::number(state.providers.trucksim_win32_error));
    SetValue(QStringLiteral("storage"),
        state.providers.storage_error.empty()
            ? QStringLiteral("Ready in the current user's application data directory")
            : QString::fromStdWString(state.providers.storage_error));
    SetValue(QStringLiteral("error"),
        state.providers.trucksim_error.empty()
            ? (state.providers.last_error.empty()
                ? QStringLiteral("None")
                : QString::fromStdWString(state.providers.last_error))
            : QString::fromStdWString(state.providers.trucksim_error));
}

SettingsPage::SettingsPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(14);

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
    : DetailPage(parent),
      version_(version) {
    AddField(QStringLiteral("product"), QStringLiteral("Product"));
    AddField(QStringLiteral("company"), QStringLiteral("Company"));
    AddField(QStringLiteral("version"), QStringLiteral("Version"));
    AddField(QStringLiteral("channel"), QStringLiteral("Channel"));
    AddField(QStringLiteral("framework"), QStringLiteral("UI framework"));
    AddField(QStringLiteral("backend"), QStringLiteral("Telemetry backend"));
    AddField(QStringLiteral("discord"), QStringLiteral("Community"));
    AddField(QStringLiteral("ceo"), QStringLiteral("CEO"));
    AddField(QStringLiteral("developer"), QStringLiteral("Developer"));
    AddField(QStringLiteral("plugin"), QStringLiteral("Telemetry plugin attribution"));
    AddField(QStringLiteral("licenses"), QStringLiteral("Third-party notices"));
}

void AboutPage::UpdateState(const telemetry::TelemetryUiState&) {
    SetValue(QStringLiteral("product"), QStringLiteral("NLSI Exclusive Logbook"));
    SetValue(QStringLiteral("company"), QStringLiteral("Nabski Logistics and Solutions Inc."));
    const QString app_version = QCoreApplication::applicationVersion();
    SetValue(QStringLiteral("version"), app_version.isEmpty() ? version_ : app_version);
    SetValue(QStringLiteral("channel"), QStringLiteral("Beta"));
    SetValue(QStringLiteral("framework"),
        QStringLiteral("Qt %1 Widgets").arg(QString::fromLatin1(qVersion())));
    SetValue(QStringLiteral("backend"), QStringLiteral("TruckSim GPS shared-memory telemetry"));
    SetExternalLink(QStringLiteral("discord"),
        QStringLiteral("Nabski Logistics and Solutions Inc. Community"),
        QUrl(QStringLiteral("https://discord.gg/gerAGTS6YB")));
    SetExternalLink(QStringLiteral("ceo"), QStringLiteral("Follow CEO on TikTok"),
        QUrl(QStringLiteral("https://www.tiktok.com/@nabskiplays")));
    SetExternalLink(QStringLiteral("developer"), QStringLiteral("Follow Developer on TikTok"),
        QUrl(QStringLiteral("https://www.tiktok.com/@kape_073")));
    SetExternalLink(QStringLiteral("plugin"), QStringLiteral("TruckSim GPS project (MIT license)"),
        QUrl(QStringLiteral("https://github.com/TruckSim-GPS/trucksim-gps-plugin")));
    SetValue(QStringLiteral("licenses"),
        QStringLiteral("TruckSim GPS and SCS SDK notices are installed in the licenses folder."));
}

} // namespace nlsi::gui
