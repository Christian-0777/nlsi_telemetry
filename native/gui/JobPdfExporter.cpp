#include "JobPdfExporter.h"

#include <QDateTime>
#include <QFileInfo>
#include <QJsonValue>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QTextDocument>

#include <cmath>

#include "PageSupport.h"
#include "time/ApplicationTime.h"

namespace nlsi::gui {
namespace {

QString JsonDisplayValue(const QJsonValue& value) {
    if (value.isString()) {
        const QString text = value.toString();
        const QString formatted_time = TimestampText(text);
        if (formatted_time != text) {
            return formatted_time;
        }
        return NumericText(text);
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        const int precision = number == std::floor(number) ? 0 : 2;
        return FormatNumber(number, precision);
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    }
    return {};
}

QString DetailsHtml(const QJsonObject& details) {
    struct DetailField {
        const char* keys;
        const char* label;
        const char* suffix;
    };
    static constexpr DetailField fields[] = {
        {"weight|cargo_weight", "WEIGHT", ""},
        {"source_city|source.city", "FROM", ""},
        {"destination_city|destination.city", "TO", ""},
        {"source_company|source.company", "FROM COMPANY", ""},
        {"destination_company|destination.company", "TO COMPANY", ""},
        {"planned_distance_km", "PLANNED DISTANCE", " km"},
        {"driven_distance_km|distance_driven_km", "DRIVEN DISTANCE", " km"},
        {"income", "INCOME", ""},
        {"offences|offenses", "OFFENCES", ""},
        {"xp|experience", "XP", ""},
        {"damage|damage_percent", "DAMAGE", ""},
        {"real_elapsed_time|elapsed_time", "TIME TAKEN (REAL)", ""},
        {"max_speed_kmh|maximum_speed_kmh", "MAX SPEED", " km/h"},
        {"truck|truck_name|vehicle", "TRUCK USED", ""},
        {"trailer|trailer_name", "TRAILER USED", ""},
        {"truck_license_plate|truck_licence_plate", "TRUCK LICENCE PLATE", ""},
        {"trailer_license_plate|trailer_licence_plate", "TRAILER LICENCE PLATE", ""},
        {"fuel_used_liters|fuel_usage_liters", "FUEL USAGE", " L"},
        {"refueled_liters|fuel_added_liters", "REFUELED", " L"},
        {"refuel_cost", "REFUEL COST", ""},
        {"average_consumption|average_consumption_l_per_100km", "AVERAGE CONSUMPTION", " L/100 km"},
        {"odometer_km", "ODOMETER AT EVENT", " km"},
        {"remaining_navigation_km", "REMAINING NAVIGATION DISTANCE", " km"},
        {"fuel_liters", "FUEL AT EVENT", " L"},
        {"market", "JOB MARKET", ""},
        {"special_job", "SPECIAL JOB", ""},
    };

    QString html;
    for (const DetailField& field : fields) {
        QJsonValue value;
        for (const QString& key : QString::fromLatin1(field.keys).split(QLatin1Char('|'))) {
            value = details.value(key);
            if (key.contains(QLatin1Char('.'))) {
                const QStringList path = key.split(QLatin1Char('.'));
                value = details.value(path.front());
                for (qsizetype i = 1; i < path.size() && value.isObject(); ++i) {
                    value = value.toObject().value(path[i]);
                }
            }
            if (!value.isUndefined() && !value.isNull()) {
                break;
            }
        }
        const QString display = JsonDisplayValue(value);
        html += QStringLiteral("<tr><th>%1</th><td>%2%3</td></tr>")
            .arg(QString::fromLatin1(field.label).toHtmlEscaped(),
                (display.isEmpty() ? QStringLiteral("N/A") : display).toHtmlEscaped(),
                QString::fromLatin1(field.suffix).toHtmlEscaped());
    }
    return html;
}

} // namespace

bool ExportJobsToPdf(
    const QString& path,
    const QVector<nlsi::session::JobRecord>& jobs,
    QString* error) {
    if (jobs.isEmpty()) {
        if (error) {
            *error = QStringLiteral("There are no completed job records to export.");
        }
        return false;
    }
    if (path.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("A PDF output path is required.");
        }
        return false;
    }
    const QDateTime generated_at = nlsi::time::NowLocal();
    if (!generated_at.isValid()) {
        if (error) {
            *error = QStringLiteral(
                "The required IANA time-zone data for Asia/Manila is unavailable.");
        }
        return false;
    }

    QString html = QStringLiteral(
        "<html><head><meta charset=\"utf-8\"><style>"
        "body{font-family:'Segoe UI',sans-serif;font-size:10pt;color:#25212a}"
        "h1{font-size:19pt;color:#a92d70;margin-bottom:5px}"
        ".generated{color:#665c68;font-size:9pt;margin-bottom:18px}"
        ".job{border:1px solid #ded6df;padding:12px;margin:0 0 14px 0}"
        "h2{font-size:13pt;color:#a92d70;margin:0 0 7px 0}"
        "table{width:100%;border-collapse:collapse}"
        "th,td{text-align:left;vertical-align:top;padding:4px 7px;border-bottom:1px solid #eee}"
        "th{width:28%;color:#514854;font-weight:bold}"
        "</style></head><body><h1>NLSI Exclusive Logbook</h1>"
        "<div class=\"generated\">%1 · Generated %2</div>")
        .arg(jobs.size() == 1
                    ? QStringLiteral("Completed job report")
                    : QStringLiteral("%1 completed job records")
                        .arg(FormatNumber(jobs.size(), 0))
                        .toHtmlEscaped(),
            (generated_at.toString(QStringLiteral("MMMM d, yyyy hh:mm:ss AP"))
                    + QStringLiteral(" Asia/Manila")).toHtmlEscaped());

    for (const nlsi::session::JobRecord& job : jobs) {
        html += QStringLiteral("<section class=\"job\"><h2>JOB %1 · %2</h2><table>")
            .arg((job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id).toHtmlEscaped(),
                job.status.toHtmlEscaped());
        const auto add_row = [&html](const QString& label, const QString& value) {
            if (!value.isEmpty()) {
                html += QStringLiteral("<tr><th>%1</th><td>%2</td></tr>")
                    .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
            }
        };
        add_row(QStringLiteral("JOB ID"),
            job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id);
        const QJsonValue game_job_id = job.details.value(QStringLiteral("job_id"));
        if (game_job_id.isString()) {
            add_row(QStringLiteral("GAME JOB ID"), game_job_id.toString());
        }
        add_row(QStringLiteral("CARGO"), job.cargo.isEmpty() ? QStringLiteral("N/A") : job.cargo);
        add_row(QStringLiteral("ORIGIN"), job.source.isEmpty() ? QStringLiteral("N/A") : job.source);
        add_row(QStringLiteral("DESTINATION"), job.destination.isEmpty() ? QStringLiteral("N/A") : job.destination);
        add_row(QStringLiteral("RECORDED AT"), TimestampText(job.timestamp));
        html += DetailsHtml(job.details);
        html += QStringLiteral("</table></section>");
    }
    html += QStringLiteral("</body></html>");

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Could not create PDF %1: %2")
                .arg(path, output.errorString());
        }
        return false;
    }
    {
        QPdfWriter writer(&output);
        writer.setTitle(QStringLiteral("NLSI Exclusive Logbook — Completed Jobs"));
        writer.setCreator(QStringLiteral("NLSI Exclusive Logbook"));
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(16, 16, 16, 16), QPageLayout::Millimeter);
        QTextDocument document;
        document.setHtml(html);
        document.print(&writer);
    }
    if (!output.commit()) {
        if (error) {
            *error = QStringLiteral("Could not finish PDF %1: %2")
                .arg(path, output.errorString());
        }
        return false;
    }
    if (QFileInfo(path).size() == 0) {
        if (error) {
            *error = QStringLiteral("PDF export produced an empty file: %1").arg(path);
        }
        return false;
    }
    return true;
}

bool ExportJobToPdf(
    const QString& path,
    const nlsi::session::JobRecord& job,
    QString* error) {
    return ExportJobsToPdf(path, QVector<nlsi::session::JobRecord>{job}, error);
}

} // namespace nlsi::gui
