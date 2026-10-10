#include "PageSupport.h"

#include "time/ApplicationTime.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QDateTime>
#include <QUrl>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

QString WrapLongTokens(const QString& text) {
    QString wrapped;
    wrapped.reserve(text.size() + text.size() / 28);
    qsizetype token_length = 0;
    for (const QChar character : text) {
        if (character.isSpace()) {
            token_length = 0;
        } else if (!character.isLowSurrogate() && token_length >= 28) {
            wrapped += QChar(0x200b);
            token_length = 0;
        }
        wrapped += character;
        if (!character.isLowSurrogate()) {
            ++token_length;
        }
    }
    return wrapped;
}

} // namespace

QString FieldText(const telemetry::TelemetryField<std::wstring>& field) {
    if (!field.available) {
        return QStringLiteral("--");
    }
    return QString::fromStdWString(field.value) + (field.stale ? QStringLiteral("  · stale") : QString());
}

QString NumberText(const telemetry::TelemetryField<double>& field, int precision, const QString& suffix) {
    if (!field.available || !std::isfinite(field.value)) {
        return QStringLiteral("--");
    }
    const QString value = FormatNumber(field.value, precision) + suffix;
    return value + (field.stale ? QStringLiteral("  · stale") : QString());
}

QString OptionalNumberText(const std::optional<double>& value, int precision, const QString& suffix) {
    if (!value || !std::isfinite(*value)) {
        return QStringLiteral("--");
    }
    return FormatNumber(*value, precision) + suffix;
}

QString DurationText(const std::optional<double>& seconds) {
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0.0) {
        return QStringLiteral("N/A");
    }
    const double rounded_seconds = std::round(*seconds);
    if (rounded_seconds >= static_cast<double>(std::numeric_limits<qint64>::max())) {
        return QStringLiteral("N/A");
    }
    const auto whole_seconds = static_cast<quint64>(rounded_seconds);
    const quint64 hours = whole_seconds / 3600;
    const quint64 minutes = (whole_seconds % 3600) / 60;
    const quint64 remainder_seconds = whole_seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qulonglong>(hours), 2, 10, QLatin1Char('0'))
        .arg(static_cast<qulonglong>(minutes), 2, 10, QLatin1Char('0'))
        .arg(static_cast<qulonglong>(remainder_seconds), 2, 10, QLatin1Char('0'));
}

QString ArrivalText(const std::optional<double>& seconds, const QDateTime& now_utc) {
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0.0 || !now_utc.isValid()) {
        return QStringLiteral("N/A");
    }
    const QTimeZone zone = nlsi::time::Zone();
    const double milliseconds = std::round(*seconds * 1000.0);
    if (!zone.isValid() || !std::isfinite(milliseconds)
        || milliseconds > static_cast<double>(std::numeric_limits<qint64>::max())) {
        return QStringLiteral("N/A");
    }
    const QDateTime arrival = now_utc.toUTC()
        .addMSecs(static_cast<qint64>(milliseconds)).toTimeZone(zone);
    if (!arrival.isValid()) {
        return QStringLiteral("N/A");
    }
    const quint64 whole_seconds = static_cast<quint64>(
        std::ceil(*seconds));
    const quint64 minutes_left = (whole_seconds + 59) / 60;
    const quint64 hours = minutes_left / 60;
    const quint64 minutes = minutes_left % 60;
    const QString remaining = hours > 0
        ? QStringLiteral("%1 HR %2 MIN LEFT")
            .arg(static_cast<qulonglong>(hours))
            .arg(static_cast<qulonglong>(minutes))
        : QStringLiteral("%1 MIN LEFT").arg(static_cast<qulonglong>(minutes));
    return remaining + QStringLiteral(" - ")
        + arrival.toString(QStringLiteral("HH:mm:ss"))
        + QStringLiteral(" ASIA/MANILA");
}

QString FormatNumber(double value, int precision) {
    return QLocale(QLocale::English, QLocale::UnitedStates).toString(value, 'f', precision);
}

QString NumericText(const QString& value) {
    bool valid = false;
    const double number = value.trimmed().toDouble(&valid);
    if (!valid || !std::isfinite(number)) {
        return value;
    }
    const bool integer = !value.contains(QLatin1Char('.'))
        && !value.contains(QLatin1Char('e'), Qt::CaseInsensitive);
    return FormatNumber(number, integer ? 0 : 2);
}

QString TimestampText(const QString& value) {
    const QDateTime timestamp = nlsi::time::ParseInstant(value);
    if (!timestamp.isValid()) {
        return value;
    }
    return timestamp.toString(QStringLiteral("MM/dd/yy HH:mm:ss.zzz"))
        + QStringLiteral(" Asia/Manila");
}

DetailPage::DetailPage(QWidget* parent)
    : StatePage(parent) {
    page_layout_ = new QVBoxLayout(this);
    page_layout_->setContentsMargins(4, 4, 4, 4);
    page_layout_->setSpacing(14);

    auto* fields_frame = new QFrame(this);
    fields_frame->setObjectName(QStringLiteral("contentCard"));
    fields_layout_ = new QVBoxLayout(fields_frame);
    fields_layout_->setContentsMargins(20, 12, 20, 12);
    fields_layout_->setSpacing(0);
    page_layout_->addWidget(fields_frame);
    page_layout_->addStretch(1);
}

void DetailPage::AddField(const QString& key, const QString& label) {
    auto* row = new QFrame(this);
    row->setObjectName(QStringLiteral("detailRow"));
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 8, 0, 8);
    row_layout->setSpacing(16);
    auto* name = new QLabel(label.toUpper(), row);
    name->setObjectName(QStringLiteral("detailLabel"));
    name->setWordWrap(true);
    name->setMinimumWidth(0);
    name->setMaximumWidth(190);
    name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* value = new QLabel(QStringLiteral("--"), row);
    value->setObjectName(QStringLiteral("detailValue"));
    value->setWordWrap(true);
    value->setMinimumWidth(0);
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row_layout->addWidget(name);
    row_layout->addWidget(value, 1);
    if (fields_layout_->count() > 0) {
        auto* divider = new QFrame(fields_layout_->parentWidget());
        divider->setObjectName(QStringLiteral("divider"));
        divider->setFixedHeight(1);
        fields_layout_->addWidget(divider);
    }
    fields_layout_->addWidget(row);
    values_.insert(key, value);
}

void DetailPage::AddContentWidget(QWidget* widget) {
    if (page_layout_ && widget) {
        page_layout_->insertWidget(page_layout_->count() - 1, widget);
    }
}

void DetailPage::SetExternalLink(const QString& key, const QString& label, const QUrl& url) {
    QLabel* value = values_.value(key, nullptr);
    if (!value) {
        return;
    }
    value->setTextFormat(Qt::RichText);
    value->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    value->setOpenExternalLinks(true);
    const QString safe_label = label.toHtmlEscaped();
    const QString safe_url = url.toString(QUrl::FullyEncoded).toHtmlEscaped();
    SetValue(key, QStringLiteral("<a href=\"%1\">%2</a>").arg(safe_url, safe_label));
}

void DetailPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    const QString display_value = label && label->textFormat() == Qt::PlainText
        ? WrapLongTokens(value) : value;
    if (label && label->text() != display_value) {
        label->setText(display_value);
        label->setToolTip(value);
        label->setAccessibleName(value);
    }
}

} // namespace nlsi::gui
