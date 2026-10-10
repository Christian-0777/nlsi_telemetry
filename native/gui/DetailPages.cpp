#include "CurrentJobPage.h"
#include "TelemetryPage.h"
#include "ProvidersPage.h"
#include "SettingsPage.h"
#include "AboutPage.h"
#include "ActiveModsPage.h"

#include <cmath>

#include <QCoreApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSizePolicy>
#include <QUrl>
#include <QTabWidget>
#include <QVBoxLayout>

#include "updater/GitHubUpdater.h"
#include "MeasurementUnits.h"

namespace nlsi::gui {

class UpdateControls final : public QWidget {
public:
    explicit UpdateControls(QWidget* parent = nullptr)
        : QWidget(parent) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(14);
        button = new QPushButton(QStringLiteral("Check for Updates"), this);
        button->setObjectName(QStringLiteral("checkForUpdatesButton"));
        button->setAccessibleName(QStringLiteral("Check for Updates"));
        status = new QLabel(QStringLiteral("Checking for published releases…"), this);
        status->setObjectName(QStringLiteral("updateCheckStatus"));
        status->setWordWrap(true);
        status->setMinimumWidth(0);
        status->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(button);
        layout->addWidget(status, 1);
    }

    QPushButton* button = nullptr;
    QLabel* status = nullptr;
};

namespace {

QString ReleaseChannelText() {
    const QString version = QCoreApplication::applicationVersion();
    const qsizetype separator = version.lastIndexOf(QLatin1Char('-'));
    if (separator < 0 || separator == version.size() - 1) {
        return QStringLiteral("Unavailable");
    }
    QString channel = version.mid(separator + 1);
    channel[0] = channel[0].toUpper();
    return channel;
}

QString StatusText(telemetry::ProviderState state) {
    return QString::fromStdWString(telemetry::FormatStatus(state));
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

        auto* units_row = new QWidget(this);
        auto* units_layout = new QHBoxLayout(units_row);
        units_layout->setContentsMargins(0, 8, 0, 8);
        units_layout->setSpacing(16);
        auto* units_label = new QLabel(QStringLiteral("MEASUREMENT SYSTEM"), units_row);
        units_label->setObjectName(QStringLiteral("detailLabel"));
        measurement_system_ = new QComboBox(units_row);
        measurement_system_->setObjectName(QStringLiteral("measurementSystemSelector"));
        measurement_system_->addItem(QStringLiteral("Metric"), QStringLiteral("metric"));
        measurement_system_->addItem(QStringLiteral("US customary"), QStringLiteral("us"));
        const MeasurementSystem selected_system = CurrentMeasurementSystem();
        measurement_system_->setCurrentIndex(
            selected_system == MeasurementSystem::USCustomary ? 1 : 0);
        units_layout->addWidget(units_label);
        units_layout->addWidget(measurement_system_, 1);
        AddContentWidget(units_row);
        connect(measurement_system_, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                SetMeasurementSystem(index == 1
                    ? MeasurementSystem::USCustomary
                    : MeasurementSystem::Metric);
            });
    }

    void UpdateState(const telemetry::TelemetryUiState&) override {
        SetValue(QStringLiteral("version"), QCoreApplication::applicationVersion());
        SetValue(QStringLiteral("channel"), ReleaseChannelText());
        SetValue(QStringLiteral("refresh"), QStringLiteral("4 Hz (250 ms)"));
        SetValue(QStringLiteral("transport"), QStringLiteral("TruckSim GPS shared memory"));
        SetValue(QStringLiteral("display"), QStringLiteral("Hidden when not supplied"));
    }

private:
    QComboBox* measurement_system_ = nullptr;
};

} // namespace

CurrentJobPage::CurrentJobPage(QWidget* parent)
    : DetailPage(parent) {
    AddField(QStringLiteral("status"), QStringLiteral("Status"));
    AddField(QStringLiteral("nlsiJobId"), QStringLiteral("NLSI job ID"));
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
    const MeasurementSystem units = CurrentMeasurementSystem();
    SetValue(QStringLiteral("status"),
        QString::fromStdWString(telemetry::FormatJobStatus(state.job_status)));
    SetValue(QStringLiteral("nlsiJobId"), state.job.nlsi_job_id.empty()
        ? QStringLiteral("--")
        : QString::fromStdWString(state.job.nlsi_job_id));
    SetValue(QStringLiteral("cargo"), FieldText(state.job.cargo));
    SetValue(QStringLiteral("cargoId"), FieldText(state.job.cargo_id));
    SetValue(QStringLiteral("source"), JobRoute(state.job.source_company, state.job.source_city));
    SetValue(QStringLiteral("destination"),
        JobRoute(state.job.destination_company, state.job.destination_city));
    SetValue(QStringLiteral("income"), NumericText(FieldText(state.job.income)));
    bool planned_distance_valid = false;
    const double planned_distance = state.job.planned_distance.available
        ? QString::fromStdWString(state.job.planned_distance.value)
            .replace(QLatin1Char(','), QString())
            .toDouble(&planned_distance_valid)
        : 0.0;
    SetValue(QStringLiteral("planned"),
        planned_distance_valid && std::isfinite(planned_distance)
            ? FormatNumber(DisplayDistance(planned_distance, units), 2)
                + QLatin1Char(' ') + DistanceUnit(units)
            : QStringLiteral("--"));
    const QString loaded = !state.job.loaded.available
        ? QStringLiteral("--")
        : (state.job.loaded.value ? QStringLiteral("Yes") : QStringLiteral("No"));
    SetValue(QStringLiteral("loaded"), loaded);
    SetValue(QStringLiteral("remaining"),
        state.progress.remaining_distance_km
                && std::isfinite(*state.progress.remaining_distance_km)
            ? FormatNumber(DisplayDistance(*state.progress.remaining_distance_km, units), 2)
                + QLatin1Char(' ') + DistanceUnit(units)
            : QStringLiteral("--"));
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
    AddField(QStringLiteral("parkingBrake"), QStringLiteral("Parking brake"));
    AddField(QStringLiteral("fuelRange"), QStringLiteral("Fuel range"));
    AddField(QStringLiteral("odometer"), QStringLiteral("Odometer"));
    AddField(QStringLiteral("navigation"), QStringLiteral("Navigation distance"));
    AddField(QStringLiteral("navigationTime"), QStringLiteral("Navigation time"));
    AddField(QStringLiteral("eta"), QStringLiteral("Calculated ETA"));
}

void TelemetryPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const MeasurementSystem units = CurrentMeasurementSystem();
    const auto& values = state.fast.values;
    SetValue(QStringLiteral("game"), FieldText(values.game_name));
    SetValue(QStringLiteral("sample"), TimestampText(QString::fromStdWString(values.timestamp)));
    SetValue(QStringLiteral("speed"),
        values.speed_kmh.available && std::isfinite(values.speed_kmh.value)
            ? FormatNumber(DisplaySpeed(values.speed_kmh.value, units), 2)
                + QLatin1Char(' ') + SpeedUnit(units)
            : QStringLiteral("--"));
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
        values.cruise_control_speed.available && std::isfinite(values.cruise_control_speed.value)
            ? FormatNumber(DisplaySpeed(values.cruise_control_speed.value, units), 2)
                + QLatin1Char(' ') + SpeedUnit(units)
            : QStringLiteral("--"));
    const auto fuel_value = [&units](const telemetry::TelemetryField<double>& field) {
        return field.available && !field.stale && std::isfinite(field.value)
                && field.value >= 0.0
            ? FormatNumber(DisplayFuelVolume(field.value, units), 2)
                + QLatin1Char(' ') + FuelVolumeUnit(units)
            : QStringLiteral("--");
    };
    const auto distance_value = [&units](const telemetry::TelemetryField<double>& field) {
        return field.available && !field.stale && std::isfinite(field.value)
                && field.value >= 0.0
            ? FormatNumber(DisplayDistance(field.value, units), 2)
                + QLatin1Char(' ') + DistanceUnit(units)
            : QStringLiteral("--");
    };
    SetValue(QStringLiteral("fuel"), fuel_value(values.fuel_liters));
    SetValue(QStringLiteral("parkingBrake"),
        values.parking_brake.available && !values.parking_brake.stale
            ? (values.parking_brake.value
                ? QStringLiteral("Engaged") : QStringLiteral("Released"))
            : QStringLiteral("Unknown"));
    SetValue(QStringLiteral("fuelRange"), distance_value(values.fuel_range_km));
    SetValue(QStringLiteral("odometer"), distance_value(values.odometer_km));
    SetValue(QStringLiteral("navigation"),
        distance_value(values.navigation_distance_km));
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
    AddField(QStringLiteral("nlsi"), QStringLiteral("NLSI SCS telemetry"));
    AddField(QStringLiteral("nlsiError"), QStringLiteral("NLSI provider message"));
    AddField(QStringLiteral("scsPosition"), QStringLiteral("SCS position provider"));
    AddField(QStringLiteral("scsPositionMapping"), QStringLiteral("Position interface"));
    AddField(QStringLiteral("scsPositionAge"), QStringLiteral("Position sample age"));
    AddField(QStringLiteral("scsPositionError"), QStringLiteral("Position provider message"));
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
    AddField(QStringLiteral("sync"), QStringLiteral("Online synchronization"));
    AddField(QStringLiteral("error"), QStringLiteral("Last provider message"));
}

void ProvidersPage::UpdateState(const telemetry::TelemetryUiState& state) {
    SetValue(QStringLiteral("trucksim"), StatusText(state.providers.trucksim));
    SetValue(QStringLiteral("nlsi"), StatusText(state.providers.nlsi));
    SetValue(QStringLiteral("nlsiError"),
        state.providers.nlsi_error.empty()
            ? QStringLiteral("None")
            : QString::fromStdWString(state.providers.nlsi_error));
    const auto& position = state.scs_position;
    const QString position_status = position.state == providers::ScsPositionState::Connected
        ? QStringLiteral("Connected")
        : position.state == providers::ScsPositionState::Stale
            ? QStringLiteral("Stale")
            : QStringLiteral("Disconnected");
    SetValue(QStringLiteral("scsPosition"), position_status);
    SetValue(QStringLiteral("scsPositionMapping"),
        QStringLiteral("Local\\NLSI.SCS.Position.v1"));
    SetValue(QStringLiteral("scsPositionAge"),
        position.available
            ? QStringLiteral("%1 ms").arg(position.age_ms)
            : QStringLiteral("Unavailable"));
    SetValue(QStringLiteral("scsPositionError"),
        position.error.empty() ? QStringLiteral("None")
            : QString::fromStdWString(position.error));
    SetValue(QStringLiteral("combined"),
        QString::fromStdWString(telemetry::FormatCombinedStatus(state.providers.combined)));
    SetValue(QStringLiteral("freshness"),
        TimestampText(QString::fromStdWString(state.providers.telemetry_freshness)));
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
            : TimestampText(QString::fromStdWString(state.providers.trucksim_last_read)));
    SetValue(QStringLiteral("dataAge"),
        QString::fromStdWString(state.providers.trucksim_data_age));
    SetValue(QStringLiteral("sourceTimestamp"),
        state.providers.trucksim_source_timestamp.empty()
            ? QStringLiteral("--")
            : TimestampText(QString::fromStdWString(state.providers.trucksim_source_timestamp)));
    SetValue(QStringLiteral("win32"),
        state.providers.trucksim_win32_error == 0
            ? QStringLiteral("None")
            : QString::number(state.providers.trucksim_win32_error));
    SetValue(QStringLiteral("storage"),
        state.providers.storage_error.empty()
            ? QStringLiteral("Ready in the current user's application data directory")
            : QString::fromStdWString(state.providers.storage_error));
    SetValue(QStringLiteral("sync"), QString::fromStdWString(state.providers.sync_state));
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
    tabs_->addTab(application_settings_, QStringLiteral("APPLICATION"));
    tabs_->addTab(providers_, QStringLiteral("PROVIDERS"));
    tabs_->addTab(telemetry_, QStringLiteral("TELEMETRY"));
    tabs_->addTab(active_mods_, QStringLiteral("ACTIVE MODS"));
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
    update_controls_ = new UpdateControls(this);
    AddContentWidget(update_controls_);
    updater_ = new nlsi::updater::GitHubUpdater(
        QCoreApplication::applicationVersion(), this);
    connect(update_controls_->button, &QPushButton::clicked, this, [this] {
        update_controls_->button->setEnabled(false);
        update_controls_->status->setText(QStringLiteral("Checking GitHub releases…"));
        updater_->CheckForUpdates(true);
    });
    updater_->SetResultHandler([this](const nlsi::updater::UpdateCheckResult& result, bool) {
        update_controls_->button->setEnabled(true);
        const QString current = QCoreApplication::applicationVersion();
        if (!result.succeeded) {
            update_controls_->status->setText(
                QStringLiteral("Update check failed: %1").arg(result.error));
            update_controls_->status->setToolTip(update_controls_->status->text());
            return;
        }
        if (result.release.version.isEmpty()) {
            update_controls_->status->setText(
                QStringLiteral("No published releases are available."));
            update_controls_->status->setToolTip(update_controls_->status->text());
            return;
        }
        if (!result.update_available) {
            update_controls_->status->setText(
                QStringLiteral("Current: %1 · no newer release found.")
                    .arg(current));
            update_controls_->status->setToolTip(result.from_cache
                ? QStringLiteral("Showing the last successful cached check. Use Check for Updates "
                    "to request a fresh result.")
                : update_controls_->status->text());
            return;
        }

        const QString available = QStringLiteral("v%1 · %2")
            .arg(result.release.version, result.release.channel);
        update_controls_->status->setText(
            QStringLiteral("Update available: %1").arg(available));
        update_controls_->status->setToolTip(result.from_cache
            ? QStringLiteral("Showing cached release data; use Check for Updates to refresh.")
            : available);
        QString notes = result.release.notes.trimmed();
        constexpr qsizetype kMaximumNotesCharacters = 6000;
        if (notes.size() > kMaximumNotesCharacters) {
            notes.truncate(kMaximumNotesCharacters);
            notes += QStringLiteral("\n\n(Release notes truncated.)");
        }
        const QString release_name = result.release.name.isEmpty()
            ? available : result.release.name;
        auto* dialog = new QMessageBox(
            QMessageBox::Information,
            QStringLiteral("NLSI Exclusive Logbook update available"),
            QStringLiteral("Current version: %1\nAvailable version: %2\nChannel: %3\n\n%4")
                .arg(current, available, result.release.channel,
                    notes.isEmpty() ? release_name : release_name + QStringLiteral("\n\n") + notes),
            QMessageBox::NoButton,
            this);
        dialog->setTextFormat(Qt::PlainText);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        auto* open_release = dialog->addButton(
            QStringLiteral("Open release page"), QMessageBox::ActionRole);
        dialog->addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
        connect(open_release, &QPushButton::clicked, dialog, [dialog, url = result.release.url] {
            if (!QDesktopServices::openUrl(url)) {
                QMessageBox::warning(dialog, QStringLiteral("Could not open release page"),
                    QStringLiteral("The default browser could not open %1.")
                        .arg(url.toString()));
            }
            dialog->close();
        });
        dialog->open();
    });
    updater_->CheckForUpdates(false);
}

AboutPage::~AboutPage() = default;

void AboutPage::UpdateState(const telemetry::TelemetryUiState&) {
    SetValue(QStringLiteral("product"), QStringLiteral("NLSI Exclusive Logbook"));
    SetValue(QStringLiteral("company"), QStringLiteral("Nabski Logistics and Solutions Inc."));
    const QString app_version = QCoreApplication::applicationVersion();
    SetValue(QStringLiteral("version"), app_version.isEmpty() ? version_ : app_version);
    SetValue(QStringLiteral("channel"), ReleaseChannelText());
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
        QStringLiteral("Third-party notices, including Lucide ISC, are in the licenses folder."));
}

} // namespace nlsi::gui
