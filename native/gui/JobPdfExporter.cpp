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
        const char* key;
        const char* label;
        const char* suffix;
    };
    static constexpr DetailField fields[] = {
        {"income", "Earnings", ""},
        {"planned_distance_km", "Planned distance", " km"},
        {"odometer_km", "Odometer at event", " km"},
        {"remaining_navigation_km", "Remaining navigation distance", " km"},
        {"fuel_liters", "Fuel at event", " L"},
        {"market", "Job market", ""},
        {"special_job", "Special job", ""},
    };

    QString html;
    for (const DetailField& field : fields) {
        const QJsonValue value = details.value(QLatin1String(field.key));
        if (value.isUndefined() || value.isNull()) {
            continue;
        }
        const QString display = JsonDisplayValue(value);
        if (display.isEmpty()) {
            continue;
        }
        html += QStringLiteral("<tr><th>%1</th><td>%2%3</td></tr>")
            .arg(QString::fromLatin1(field.label).toHtmlEscaped(),
                display.toHtmlEscaped(),
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
        "<div class=\"generated\">Completed job records · %1 records · Generated %2</div>")
        .arg(FormatNumber(jobs.size(), 0).toHtmlEscaped(),
            QDateTime::currentDateTimeUtc().toOffsetFromUtc(8 * 60 * 60)
                .toString(QStringLiteral("MM/dd/yy HH:mm:ss.zzz")).toHtmlEscaped());

    for (qsizetype index = 0; index < jobs.size(); ++index) {
        const nlsi::session::JobRecord& job = jobs[index];
        html += QStringLiteral("<section class=\"job\"><h2>Job %1 · %2</h2><table>")
            .arg(FormatNumber(index + 1, 0).toHtmlEscaped(),
                job.status.toHtmlEscaped());
        const auto add_row = [&html](const QString& label, const QString& value) {
            if (!value.isEmpty()) {
                html += QStringLiteral("<tr><th>%1</th><td>%2</td></tr>")
                    .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
            }
        };
        add_row(QStringLiteral("Job ID"), job.identity);
        add_row(QStringLiteral("Cargo"), job.cargo);
        add_row(QStringLiteral("Origin"), job.source);
        add_row(QStringLiteral("Destination"), job.destination);
        add_row(QStringLiteral("Recorded at"), TimestampText(job.timestamp));
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

} // namespace nlsi::gui
