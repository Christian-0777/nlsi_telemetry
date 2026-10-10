#include "DashboardPage.h"

#include <cmath>

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

QString FieldValue(const telemetry::TelemetryField<std::wstring>& field) {
    if (!field.available || field.value.empty()) {
        return QStringLiteral("N/A");
    }
    QString value = QString::fromStdWString(field.value).trimmed();
    if (value.isEmpty()) {
        return QStringLiteral("N/A");
    }
    if (field.stale) {
        value += QStringLiteral(" · stale");
    }
    return value;
}

QString NumberValue(
    const telemetry::TelemetryField<double>& field,
    int precision,
    const QString& suffix = {}) {
    if (!field.available || !std::isfinite(field.value)) {
        return QStringLiteral("N/A");
    }
    QString value = FormatNumber(field.value, precision) + suffix;
    if (field.stale) {
        value += QStringLiteral(" · stale");
    }
    return value;
}

QString RouteValue(
    const telemetry::TelemetryField<std::wstring>& company,
    const telemetry::TelemetryField<std::wstring>& city) {
    const QString company_text = FieldValue(company);
    const QString city_text = FieldValue(city);
    if (company_text == QStringLiteral("N/A")) {
        return city_text;
    }
    if (city_text == QStringLiteral("N/A") || company_text == city_text) {
        return company_text;
    }
    return company_text + QStringLiteral(" · ") + city_text;
}

QString PercentValue(const telemetry::TelemetryField<double>& field) {
    if (!field.available || !std::isfinite(field.value)) {
        return QStringLiteral("N/A");
    }
    QString value = FormatNumber(field.value * 100.0, 1) + QLatin1Char('%');
    if (field.stale) {
        value += QStringLiteral(" · stale");
    }
    return value;
}

QString GameIdentity(const telemetry::TelemetrySnapshot& snapshot) {
    const QString name = FieldValue(snapshot.game_name);
    const QString id = FieldValue(snapshot.game_id);
    if (name == QStringLiteral("N/A")) {
        return id;
    }
    if (id == QStringLiteral("N/A")) {
        return name;
    }
    return QStringLiteral("%1 (%2)").arg(name, id);
}

QString ProgressValue(const std::optional<double>& progress) {
    if (!progress || !std::isfinite(*progress) || *progress < 0.0 || *progress > 100.0) {
        return QStringLiteral("N/A");
    }
    return FormatNumber(*progress, 1) + QLatin1Char('%');
}

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : StatePage(parent) {
    setObjectName(QStringLiteral("dashboardPage"));
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(2, 2, 2, 2);
    page_layout->setSpacing(8);

    QGridLayout* job = AddSection(
        QStringLiteral("currentJobSection"), QStringLiteral("CURRENT JOB"));
    AddMetric(job, 0, 0, QStringLiteral("jobId"), QStringLiteral("JOB"));
    AddMetric(job, 0, 1, QStringLiteral("jobStatus"), QStringLiteral("STATUS"));
    AddMetric(job, 1, 0, QStringLiteral("cargo"), QStringLiteral("CARGO"));
    AddMetric(job, 1, 1, QStringLiteral("income"), QStringLiteral("INCOME"));
    AddMetric(job, 2, 0, QStringLiteral("source"), QStringLiteral("FROM"));
    AddMetric(job, 2, 1, QStringLiteral("destination"), QStringLiteral("TO"));
    AddMetric(job, 3, 0, QStringLiteral("plannedDistance"), QStringLiteral("PLANNED DISTANCE"));
    AddMetric(job, 3, 1, QStringLiteral("remainingDistance"), QStringLiteral("REMAINING DISTANCE"));
    AddMetric(job, 4, 0, QStringLiteral("progress"), QStringLiteral("PROGRESS"));
    AddMetric(job, 4, 1, QStringLiteral("eta"), QStringLiteral("ETA"));

    special_job_indicator_ = new QLabel(QStringLiteral("SPECIAL JOB"), this);
    special_job_indicator_->setObjectName(QStringLiteral("specialJobIndicator"));
    special_job_indicator_->setAlignment(Qt::AlignCenter);
    special_job_indicator_->setVisible(false);
    RegisterResponsiveLabel(special_job_indicator_, 2);
    job->addWidget(special_job_indicator_, 6, 0, 1, 2, Qt::AlignCenter);

    QGridLayout* driving = AddSection(
        QStringLiteral("drivingTelemetrySection"), QStringLiteral("DRIVING TELEMETRY"));
    AddMetric(driving, 0, 0, QStringLiteral("fuel"), QStringLiteral("FUEL"));
    AddMetric(driving, 0, 1, QStringLiteral("engine"), QStringLiteral("RPM / GEAR"));
    AddMetric(driving, 1, 0, QStringLiteral("throttle"), QStringLiteral("THROTTLE"), true);
    AddMetric(driving, 1, 1, QStringLiteral("brake"), QStringLiteral("BRAKE"), true);
    AddMetric(driving, 2, 0, QStringLiteral("cruiseControl"), QStringLiteral("CC"));
    AddMetric(driving, 2, 1, QStringLiteral("retarder"), QStringLiteral("RETARDER"), true);

    QGridLayout* connection = AddSection(
        QStringLiteral("connectionSection"), QStringLiteral("CONNECTION"));
    AddMetric(connection, 0, 0, QStringLiteral("connection"), QStringLiteral("PROVIDER"));
    AddMetric(connection, 0, 1, QStringLiteral("game"), QStringLiteral("GAME"));
    AddMetric(connection, 1, 0, QStringLiteral("gameVersion"), QStringLiteral("VERSION"));
    AddMetric(connection, 1, 1, QStringLiteral("vehicle"), QStringLiteral("VEHICLE"));

    page_layout->addStretch(1);
    ApplyResponsiveFontSize();
}

QGridLayout* DashboardPage::AddSection(const QString& key, const QString& title) {
    auto* section = new QFrame(this);
    section->setObjectName(QStringLiteral("dashboardSection"));
    section->setProperty("sectionKey", key);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QGridLayout(section);
    layout->setContentsMargins(10, 7, 10, 8);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(5);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    auto* heading = new QLabel(title, section);
    heading->setObjectName(QStringLiteral("dashboardSectionTitle"));
    heading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    RegisterResponsiveLabel(heading, 1);
    layout->addWidget(heading, 0, 0, 1, 2);
    layout->setRowMinimumHeight(1, 2);
    static_cast<QVBoxLayout*>(this->layout())->addWidget(section);
    return layout;
}

void DashboardPage::AddMetric(
    QGridLayout* grid,
    int row,
    int column,
    const QString& key,
    const QString& title,
    bool cruise_indicator) {
    auto* metric = new QWidget(grid->parentWidget());
    metric->setObjectName(QStringLiteral("dashboardMetric"));
    metric->setProperty("fieldKey", key);
    metric->setMinimumWidth(0);
    metric->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* metric_layout = new QVBoxLayout(metric);
    metric_layout->setContentsMargins(0, 0, 0, 0);
    metric_layout->setSpacing(1);

    auto* field_label = new QLabel(title, metric);
    field_label->setObjectName(QStringLiteral("dashboardFieldLabel"));
    field_label->setWordWrap(true);
    field_label->setMinimumWidth(0);
    RegisterResponsiveLabel(field_label);
    metric_layout->addWidget(field_label);

    auto* value_row = new QWidget(metric);
    auto* value_layout = new QHBoxLayout(value_row);
    value_layout->setContentsMargins(0, 0, 0, 0);
    value_layout->setSpacing(4);
    auto* value = new QLabel(QStringLiteral("N/A"), value_row);
    value->setObjectName(QStringLiteral("dashboardValue"));
    value->setProperty("fieldKey", key);
    value->setWordWrap(true);
    value->setMinimumWidth(0);
    value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    RegisterResponsiveLabel(value, 2);
    value_layout->addWidget(value, 0, Qt::AlignLeft | Qt::AlignVCenter);
    values_.insert(key, value);

    if (cruise_indicator) {
        auto* indicator = new QLabel(QStringLiteral("A"), value_row);
        indicator->setObjectName(QStringLiteral("cruiseControlActiveIndicator"));
        indicator->setProperty("fieldKey", key);
        indicator->setAccessibleName(QStringLiteral("Cruise control active"));
        indicator->setAlignment(Qt::AlignCenter);
        indicator->setMinimumWidth(18);
        indicator->setVisible(false);
        RegisterResponsiveLabel(indicator, 2);
        value_layout->addWidget(indicator, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cruise_indicators_.insert(key, indicator);
    }

    value_layout->addStretch(1);
    metric_layout->addWidget(value_row);
    grid->addWidget(metric, row + 1, column);
}

void DashboardPage::RegisterResponsiveLabel(QLabel* label, int emphasis) {
    label->setProperty("fontEmphasis", emphasis);
    responsive_labels_.push_back(label);
}

void DashboardPage::resizeEvent(QResizeEvent* event) {
    StatePage::resizeEvent(event);
    ApplyResponsiveFontSize();
}

void DashboardPage::ApplyResponsiveFontSize() {
    const QWidget* app_window = window();
    const int window_width = app_window ? app_window->width() : width();
    const int growth = qBound(0, window_width - 900, 900);
    const int base_size = qBound(8, 8 + growth * 6 / 900, 14);
    if (base_size == responsive_font_size_) {
        return;
    }
    responsive_font_size_ = base_size;
    for (QLabel* label : responsive_labels_) {
        const int emphasis = label->property("fontEmphasis").toInt();
        const int pixel_size = qMin(14, base_size + emphasis);
        const QString style = QStringLiteral("font-size: %1px;").arg(pixel_size);
        if (label->styleSheet() != style) {
            label->setStyleSheet(style);
        }
    }
}

void DashboardPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    const QString normalized = value.trimmed().isEmpty()
        ? QStringLiteral("N/A") : value;
    if (label && label->text() != normalized) {
        label->setText(normalized);
    }
}

void DashboardPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& snapshot = state.fast.values;
    SetValue(QStringLiteral("jobId"),
        state.fast.values.connected && state.job.available && !state.job.nlsi_job_id.empty()
            ? QString::fromStdWString(state.job.nlsi_job_id)
            : QStringLiteral("N/A"));

    QString job_status = QStringLiteral("N/A");
    if (state.job_status == telemetry::JobStatus::NoJob
        && snapshot.has_job.available && !snapshot.has_job.stale && !snapshot.has_job.value) {
        job_status = QString::fromStdWString(telemetry::FormatJobStatus(state.job_status));
    } else if (snapshot.connected && state.job.available) {
        job_status = QString::fromStdWString(telemetry::FormatJobStatus(state.job_status));
    }
    SetValue(QStringLiteral("jobStatus"), job_status);
    SetValue(QStringLiteral("cargo"), FieldValue(state.job.cargo));
    SetValue(QStringLiteral("income"), FieldValue(state.job.income));
    SetValue(QStringLiteral("source"),
        RouteValue(state.job.source_company, state.job.source_city));
    SetValue(QStringLiteral("destination"),
        RouteValue(state.job.destination_company, state.job.destination_city));
    SetValue(QStringLiteral("plannedDistance"), FieldValue(state.job.planned_distance));
    SetValue(QStringLiteral("remainingDistance"),
        state.progress.remaining_distance_km
            && std::isfinite(*state.progress.remaining_distance_km)
            && *state.progress.remaining_distance_km >= 0.0
            ? FormatNumber(*state.progress.remaining_distance_km, 2) + QStringLiteral(" km")
            : QStringLiteral("N/A"));
    SetValue(QStringLiteral("progress"), ProgressValue(state.progress.progress_percent));
    SetValue(QStringLiteral("eta"), DurationText(state.progress.eta_seconds));

    const auto& special_job = snapshot.special_job;
    const bool special_job_active = snapshot.connected && special_job.available
        && !special_job.stale
        && QString::fromStdWString(special_job.value).trimmed().compare(
            QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    special_job_indicator_->setVisible(special_job_active);

    SetValue(QStringLiteral("fuel"), NumberValue(snapshot.fuel_liters, 2, QStringLiteral(" L")));
    SetValue(QStringLiteral("engine"),
        NumberValue(snapshot.rpm, 0, QStringLiteral(" RPM"))
            + QStringLiteral(" / ")
            + NumberValue(snapshot.gear, 0));
    SetValue(QStringLiteral("throttle"), PercentValue(snapshot.effective_throttle));
    SetValue(QStringLiteral("brake"), PercentValue(snapshot.effective_brake));
    SetValue(QStringLiteral("retarder"), NumberValue(snapshot.retarder_level, 1));

    const auto& cruise_control = snapshot.cruise_control_active;
    const bool cruise_control_confirmed = snapshot.connected
        && cruise_control.available && !cruise_control.stale;
    SetValue(QStringLiteral("cruiseControl"),
        cruise_control_confirmed
            ? (cruise_control.value ? QStringLiteral("ACTIVE") : QStringLiteral("INACTIVE"))
            : QStringLiteral("N/A"));
    for (auto it = cruise_indicators_.begin(); it != cruise_indicators_.end(); ++it) {
        it.value()->setVisible(cruise_control_confirmed && cruise_control.value);
    }

    const QString provider_status = QString::fromStdWString(
        telemetry::FormatStatus(state.providers.trucksim));
    SetValue(QStringLiteral("connection"),
        QStringLiteral("%1 · TruckSim GPS (%2)")
            .arg(snapshot.connected ? QStringLiteral("CONNECTED")
                                    : QStringLiteral("NOT CONNECTED"),
                provider_status));
    SetValue(QStringLiteral("game"), GameIdentity(snapshot));
    SetValue(QStringLiteral("gameVersion"), QStringLiteral("N/A"));
    SetValue(QStringLiteral("vehicle"), QStringLiteral("N/A"));
}

} // namespace nlsi::gui
