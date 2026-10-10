#include "DashboardPage.h"

#include <cmath>
#include <algorithm>

#include <QFrame>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QDateTime>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QJsonParseError>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "MeasurementUnits.h"

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

QString ProgressValue(const std::optional<double>& progress) {
    if (!progress || !std::isfinite(*progress) || *progress < 0.0 || *progress > 100.0) {
        return QStringLiteral("N/A");
    }
    return FormatNumber(*progress, 1) + QLatin1Char('%');
}

class DashboardValueLabel final : public QLabel {
public:
    explicit DashboardValueLabel(QWidget* parent) : QLabel(parent) {
        setWordWrap(false);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    void SetFullText(const QString& text) {
        if (full_text_ == text) {
            return;
        }
        full_text_ = text;
        setToolTip(text);
        setAccessibleName(text);
        UpdateElision();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QLabel::resizeEvent(event);
        UpdateElision();
    }

private:
    void UpdateElision() {
        if (contentsRect().width() <= 0) {
            QLabel::setText(full_text_);
            return;
        }
        QLabel::setText(fontMetrics().elidedText(
            full_text_, Qt::ElideRight, contentsRect().width()));
    }

    QString full_text_;
};

} // namespace

DashboardPage::DashboardPage(QWidget* parent) : StatePage(parent) {
    setObjectName(QStringLiteral("dashboardPage"));
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(2, 2, 2, 2);
    page_layout->setSpacing(6);

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
    AddMetric(job, 4, 1, QStringLiteral("eta"), QStringLiteral("ARRIVAL"));

    special_job_indicator_ = new QLabel(QStringLiteral("SPECIAL JOB"), this);
    special_job_indicator_->setObjectName(QStringLiteral("specialJobIndicator"));
    special_job_indicator_->setAlignment(Qt::AlignCenter);
    special_job_indicator_->setVisible(false);
    job->addWidget(special_job_indicator_, 6, 0, 1, 2, Qt::AlignCenter);

    QGridLayout* driving = AddSection(
        QStringLiteral("drivingTelemetrySection"), QStringLiteral("DRIVING TELEMETRY"));
    AddMetric(driving, 0, 0, QStringLiteral("fuel"), QStringLiteral("FUEL"));
    AddMetric(driving, 0, 1, QStringLiteral("engine"), QStringLiteral("RPM / GEAR"));
    AddMetric(driving, 1, 0, QStringLiteral("throttle"), QStringLiteral("THROTTLE"), true);
    AddMetric(driving, 1, 1, QStringLiteral("brake"), QStringLiteral("BRAKE"), true);
    AddMetric(driving, 2, 0, QStringLiteral("cruiseControl"), QStringLiteral("CRUISE CONTROL"));
    AddMetric(driving, 2, 1, QStringLiteral("retarder"), QStringLiteral("RETARDER"), true);

    QGridLayout* game_config = AddSection(
        QStringLiteral("gameConfigSection"), QStringLiteral("GAME CONFIG"));
    AddMetric(game_config, 0, 0, QStringLiteral("game"), QStringLiteral("GAME NAME"));
    AddMetric(game_config, 0, 1, QStringLiteral("gameVersion"), QStringLiteral("GAME VERSION"));
    AddMetric(game_config, 1, 0, QStringLiteral("vehicle"), QStringLiteral("VEHICLE MAKE/MODEL"));
    AddMetric(game_config, 1, 1, QStringLiteral("vehiclePlate"), QStringLiteral("VEHICLE LICENCE PLATE"));
    AddMetric(game_config, 2, 0, QStringLiteral("trailer"), QStringLiteral("TRAILER DETAILS"));
    AddMetric(game_config, 2, 1, QStringLiteral("trailerPlate"), QStringLiteral("TRAILER LICENCE PLATE"));

    QGridLayout* travel = AddSection(
        QStringLiteral("travelExpenseSection"), QStringLiteral("TRAVEL & EXPENSE SUMMARY"));
    travel_expense_summary_ = new QPlainTextEdit(
        QStringLiteral("NO RECORDED TRAVEL OR REFUELING EVENTS."),
        travel->parentWidget());
    travel_expense_summary_->setObjectName(QStringLiteral("travelExpenseSummary"));
    travel_expense_summary_->setReadOnly(true);
    travel_expense_summary_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    travel_expense_summary_->setFixedHeight(36);
    travel_expense_summary_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    travel_expense_summary_->setFrameShape(QFrame::NoFrame);
    travel->addWidget(travel_expense_summary_, 1, 0, 1, 2);

    page_layout->addStretch(1);
}

QGridLayout* DashboardPage::AddSection(const QString& key, const QString& title) {
    auto* section = new QFrame(this);
    section->setObjectName(QStringLiteral("dashboardSection"));
    section->setProperty("sectionKey", key);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QGridLayout(section);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(5);
    layout->setColumnStretch(0, 1);
    layout->setColumnStretch(1, 1);

    auto* heading = new QLabel(title, section);
    heading->setObjectName(QStringLiteral("dashboardSectionTitle"));
    heading->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    metric_layout->addWidget(field_label);

    auto* value_row = new QWidget(metric);
    auto* value_layout = new QHBoxLayout(value_row);
    value_layout->setContentsMargins(0, 0, 0, 0);
    value_layout->setSpacing(4);
    auto* value = new DashboardValueLabel(value_row);
    value->SetFullText(QStringLiteral("N/A"));
    value->setObjectName(QStringLiteral("dashboardValue"));
    value->setProperty("fieldKey", key);
    value_layout->addWidget(value, 1, Qt::AlignVCenter);
    values_.insert(key, value);

    if (cruise_indicator) {
        if (key == QStringLiteral("brake")) {
            auto* parking_indicator = new QLabel(QStringLiteral("?"), value_row);
            parking_indicator->setObjectName(QStringLiteral("parkingBrakeIndicator"));
            parking_indicator->setProperty("fieldKey", key);
            parking_indicator->setAccessibleName(QStringLiteral("Parking brake state unknown"));
            parking_indicator->setAlignment(Qt::AlignCenter);
            parking_indicator->setFixedSize(20, 20);
            parking_indicator->setStyleSheet(
                QStringLiteral("color:#a9a3ad;background:#37323b;border-radius:10px;"));
            value_layout->addWidget(parking_indicator, 0, Qt::AlignVCenter);
            parking_brake_indicators_.insert(key, parking_indicator);
        }
        auto* indicator = new QLabel(QStringLiteral("A"), value_row);
        indicator->setObjectName(QStringLiteral("cruiseControlActiveIndicator"));
        indicator->setProperty("fieldKey", key);
        indicator->setAccessibleName(QStringLiteral("Cruise control active"));
        indicator->setAlignment(Qt::AlignCenter);
        indicator->setMinimumWidth(18);
        indicator->setVisible(false);
        value_layout->addWidget(indicator, 0, Qt::AlignLeft | Qt::AlignVCenter);
        cruise_indicators_.insert(key, indicator);
    }

    value_layout->addStretch(1);
    metric_layout->addWidget(value_row);
    grid->addWidget(metric, row + 1, column);
}

void DashboardPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    const QString normalized = value.trimmed().isEmpty()
        ? QStringLiteral("N/A") : value;
    if (label) {
        static_cast<DashboardValueLabel*>(label)->SetFullText(normalized);
    }
}

void DashboardPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const auto& snapshot = state.fast.values;
    const MeasurementSystem units = CurrentMeasurementSystem();
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
    bool planned_distance_valid = false;
    const double planned_distance = state.job.planned_distance.available
        ? QString::fromStdWString(state.job.planned_distance.value)
            .replace(QLatin1Char(','), QString())
            .toDouble(&planned_distance_valid)
        : 0.0;
    SetValue(QStringLiteral("plannedDistance"),
        planned_distance_valid && std::isfinite(planned_distance)
            ? FormatNumber(DisplayDistance(planned_distance, units), 2)
                + QLatin1Char(' ') + DistanceUnit(units).toUpper()
            : FieldValue(state.job.planned_distance));
    SetValue(QStringLiteral("remainingDistance"),
        state.progress.remaining_distance_km
                && std::isfinite(*state.progress.remaining_distance_km)
                && *state.progress.remaining_distance_km >= 0.0
            ? FormatNumber(DisplayDistance(*state.progress.remaining_distance_km, units), 2)
                + QLatin1Char(' ') + DistanceUnit(units).toUpper()
            : QStringLiteral("N/A"));
    SetValue(QStringLiteral("progress"), ProgressValue(state.progress.progress_percent));
    SetValue(QStringLiteral("eta"), ArrivalText(
        state.progress.eta_seconds, QDateTime::currentDateTimeUtc()));

    const auto& special_job = snapshot.special_job;
    const bool special_job_active = snapshot.connected && special_job.available
        && !special_job.stale
        && QString::fromStdWString(special_job.value).trimmed().compare(
            QStringLiteral("true"), Qt::CaseInsensitive) == 0;
    special_job_indicator_->setVisible(special_job_active);

    const QString fuel_range = snapshot.fuel_range_km.available
            && !snapshot.fuel_range_km.stale
            && std::isfinite(snapshot.fuel_range_km.value)
            && snapshot.fuel_range_km.value >= 0.0
        ? FormatNumber(DisplayDistance(snapshot.fuel_range_km.value, units), 0)
            + QLatin1Char(' ') + DistanceUnit(units).toUpper()
        : QStringLiteral("N/A");
    const QString fuel_quantity = snapshot.fuel_liters.available
            && !snapshot.fuel_liters.stale
            && std::isfinite(snapshot.fuel_liters.value)
            && snapshot.fuel_liters.value >= 0.0
        ? FormatNumber(DisplayFuelVolume(snapshot.fuel_liters.value, units), 2)
            + QLatin1Char(' ') + FuelVolumeUnit(units).toUpper()
        : QStringLiteral("N/A");
    SetValue(QStringLiteral("fuel"),
        fuel_range + QStringLiteral(" - ") + fuel_quantity);
    QLabel* fuel_value = values_.value(QStringLiteral("fuel"), nullptr);
    const std::optional<double> fuel_percentage =
        telemetry::FuelPercentage(snapshot.fuel_liters, snapshot.fuel_capacity_liters);
    if (fuel_value) {
        QFont font = fuel_value->font();
        const bool low_fuel = fuel_percentage && *fuel_percentage <= 20.0;
        font.setUnderline(low_fuel);
        fuel_value->setFont(font);
        fuel_value->setStyleSheet(
            low_fuel ? QStringLiteral("color:#f05b68;") : QString());
    }
    SetValue(QStringLiteral("engine"),
        NumberValue(snapshot.rpm, 0, QStringLiteral(" RPM"))
            + QStringLiteral(" / ")
            + NumberValue(snapshot.gear, 0));
    SetValue(QStringLiteral("throttle"), PercentValue(snapshot.effective_throttle));
    const auto& cruise_control = snapshot.cruise_control_active;
    const bool cruise_control_confirmed = snapshot.connected
        && cruise_control.available && !cruise_control.stale;
    const bool cruise_control_active = cruise_control_confirmed && cruise_control.value;
    SetValue(QStringLiteral("brake"), PercentValue(snapshot.effective_brake));
    const bool parking_brake_confirmed =
        snapshot.parking_brake.available && !snapshot.parking_brake.stale;
    for (auto it = parking_brake_indicators_.begin();
         it != parking_brake_indicators_.end(); ++it) {
        QLabel* indicator = it.value();
        if (!parking_brake_confirmed) {
            indicator->setText(QStringLiteral("?"));
            indicator->setAccessibleName(QStringLiteral("Parking brake state unknown"));
            indicator->setStyleSheet(
                QStringLiteral("color:#a9a3ad;background:#37323b;border-radius:10px;"));
        } else if (snapshot.parking_brake.value) {
            indicator->setText(QStringLiteral("P"));
            indicator->setAccessibleName(QStringLiteral("Parking brake engaged"));
            indicator->setStyleSheet(
                QStringLiteral("color:#ffffff;background:#b63d50;border-radius:10px;"));
        } else {
            indicator->setText(QStringLiteral("P"));
            indicator->setAccessibleName(QStringLiteral("Parking brake released"));
            indicator->setStyleSheet(
                QStringLiteral("color:#aaa4ad;background:#37323b;border-radius:10px;"));
        }
    }
    SetValue(QStringLiteral("retarder"), NumberValue(snapshot.retarder_level, 1));
    values_.value(QStringLiteral("brake"))->setVisible(!cruise_control_active);
    values_.value(QStringLiteral("retarder"))->setVisible(!cruise_control_active);
    SetValue(QStringLiteral("cruiseControl"),
        cruise_control_confirmed
                ? NumberValue(snapshot.cruise_control_speed, 0,
                    QStringLiteral(" ") + SpeedUnit(units).toUpper())
                + (cruise_control.value
                    ? QStringLiteral(" - ACTIVE") : QStringLiteral(" - INACTIVE"))
            : QStringLiteral("N/A"));
    for (auto it = cruise_indicators_.begin(); it != cruise_indicators_.end(); ++it) {
        it.value()->setVisible(cruise_control_confirmed && cruise_control.value);
    }

    SetValue(QStringLiteral("game"), FieldValue(snapshot.game_name));
    QString game_version = FieldValue(snapshot.game_version);
    if (game_version == QStringLiteral("N/A")
        && snapshot.game_id.available && !snapshot.game_id.stale) {
        const QString game_id = QString::fromStdWString(snapshot.game_id.value);
        const QString game_directory = game_id == QStringLiteral("ets2")
            ? QStringLiteral("Euro Truck Simulator 2")
            : (game_id == QStringLiteral("ats")
                ? QStringLiteral("American Truck Simulator") : QString());
        if (!game_directory.isEmpty()) {
            const QString documents = QStandardPaths::writableLocation(
                QStandardPaths::DocumentsLocation);
            const QString path = QDir(documents).filePath(
                game_directory + QStringLiteral("/game.log.txt"));
            const QFileInfo info(path);
            if (path != game_log_path_ || info.lastModified() != game_log_modified_) {
                game_log_path_ = path;
                game_log_modified_ = info.lastModified();
                game_log_version_.clear();
                QFile log(path);
                if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    static const QRegularExpression version_pattern(
                        QStringLiteral("\\[sys\\].*Game version:\\s*([^\\s,]+)"),
                        QRegularExpression::CaseInsensitiveOption);
                    const QString contents = QString::fromUtf8(log.read(256 * 1024));
                    for (const QString& line : contents.split(QLatin1Char('\n'))) {
                        const auto match = version_pattern.match(line);
                        if (match.hasMatch()) {
                            game_log_version_ = match.captured(1).trimmed();
                            break;
                        }
                    }
                }
            }
            if (!game_log_version_.isEmpty()) {
                game_version = game_log_version_;
            }
        }
    }
    SetValue(QStringLiteral("gameVersion"), game_version);
    SetValue(QStringLiteral("vehicle"), FieldValue(snapshot.vehicle));
    SetValue(QStringLiteral("vehiclePlate"), FieldValue(snapshot.vehicle_plate));
    SetValue(QStringLiteral("trailer"), FieldValue(snapshot.trailer));
    SetValue(QStringLiteral("trailerPlate"), FieldValue(snapshot.trailer_plate));
}

void DashboardPage::UpdateHistory(const session::HistorySnapshot& history) {
    struct TravelEvent {
        QString timestamp;
        QString summary;
    };
    QVector<TravelEvent> travel_events;
    for (const session::EventRecord& event : history.events) {
        const QString type = event.type.trimmed().toLower();
        QString label;
        if (type == QStringLiteral("player.fined")) {
            label = QStringLiteral("FINE");
        } else if (type == QStringLiteral("player.tollgate.paid")) {
            label = QStringLiteral("TOLL");
        } else if (type == QStringLiteral("player.use.ferry")) {
            label = QStringLiteral("FERRY");
        } else if (type == QStringLiteral("player.use.train")) {
            label = QStringLiteral("TRAIN");
        } else {
            continue;
        }

        QJsonParseError parse_error;
        const QJsonDocument document =
            QJsonDocument::fromJson(event.details.toUtf8(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }
        const QJsonObject details = document.object();
        QJsonObject event_data = details.value(QStringLiteral("data")).toObject();
        if (event_data.isEmpty()) {
            event_data = details;
        }
        const auto first_text = [&event_data](const QStringList& keys) {
            for (const QString& key : keys) {
                const QString value =
                    event_data.value(key).toVariant().toString().trimmed();
                if (!value.isEmpty()) {
                    return value;
                }
            }
            return QStringLiteral("N/A");
        };
        const QString source = first_text({QStringLiteral("source_name")});
        const QString target = first_text({QStringLiteral("target_name")});
        const QString route = source == QStringLiteral("N/A")
                && target == QStringLiteral("N/A")
            ? QStringLiteral("N/A")
            : source + QStringLiteral(" → ") + target;
        QString amount = first_text({
            QStringLiteral("fine_amount"), QStringLiteral("amount"),
            QStringLiteral("fine.amount"), QStringLiteral("pay.amount")});
        if (amount != QStringLiteral("N/A")) {
            amount += QStringLiteral(" IN-GAME CURRENCY");
        }
        travel_events.push_back({
            event.timestamp,
            QStringLiteral("%1 | %2 | FEE: %3")
                .arg(label, route, amount),
        });
    }
    std::sort(travel_events.begin(), travel_events.end(),
        [](const TravelEvent& left, const TravelEvent& right) {
            return left.timestamp < right.timestamp;
        });

    QStringList lines;
    for (const TravelEvent& event : travel_events) {
        lines.push_back(QStringLiteral("%1 | %2")
            .arg(event.timestamp, event.summary));
    }
    double refueled_liters = 0.0;
    bool has_refueled_amount = false;
    for (const session::JobRecord& job : history.jobs) {
        const QJsonValue value = job.details.value(QStringLiteral("refueled_liters"));
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0.0) {
            continue;
        }
        refueled_liters += value.toDouble();
        has_refueled_amount = true;
    }
    lines.push_back(QStringLiteral("CALCULATED REFUELED (COMPLETED JOBS): %1")
        .arg(has_refueled_amount
                ? FormatNumber(DisplayFuelVolume(
                    refueled_liters, CurrentMeasurementSystem()), 2)
                    + QStringLiteral(" ")
                    + FuelVolumeUnit(CurrentMeasurementSystem()).toUpper()
                : QStringLiteral("N/A")));
    lines.push_back(QStringLiteral("REFUELING COST: N/A (NOT PROVIDED BY SCS SDK 1.15)"));
    if (travel_events.isEmpty()) {
        lines.prepend(QStringLiteral("NO RECORDED FERRY, TRAIN, OR TOLL EVENTS."));
    }
    const QString text = lines.join(QLatin1Char('\n'));
    if (travel_expense_summary_ && travel_expense_summary_->toPlainText() != text) {
        travel_expense_summary_->setPlainText(text);
    }
}

} // namespace nlsi::gui
