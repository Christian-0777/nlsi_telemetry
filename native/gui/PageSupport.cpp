#include "PageSupport.h"

#include <cmath>
#include <iomanip>
#include <sstream>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace nlsi::gui {

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
    const QString value = QString::number(field.value, 'f', precision) + suffix;
    return value + (field.stale ? QStringLiteral("  · stale") : QString());
}

QString OptionalNumberText(const std::optional<double>& value, int precision, const QString& suffix) {
    if (!value || !std::isfinite(*value)) {
        return QStringLiteral("--");
    }
    return QString::number(*value, 'f', precision) + suffix;
}

QString DurationText(const std::optional<double>& seconds) {
    if (!seconds || !std::isfinite(*seconds) || *seconds < 0.0) {
        return QStringLiteral("--");
    }
    const auto whole_seconds = static_cast<quint64>(*seconds);
    const quint64 hours = whole_seconds / 3600;
    const quint64 minutes = (whole_seconds % 3600) / 60;
    return QStringLiteral("%1:%2")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'));
}

DetailPage::DetailPage(const QString& title, const QString& description, QWidget* parent)
    : StatePage(parent) {
    auto* page_layout = new QVBoxLayout(this);
    page_layout->setContentsMargins(4, 4, 4, 4);
    page_layout->setSpacing(18);

    auto* heading = new QLabel(title, this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    page_layout->addWidget(heading);

    auto* subtitle = new QLabel(description, this);
    subtitle->setObjectName(QStringLiteral("pageDescription"));
    subtitle->setWordWrap(true);
    page_layout->addWidget(subtitle);

    auto* fields_frame = new QFrame(this);
    fields_frame->setObjectName(QStringLiteral("contentCard"));
    fields_layout_ = new QVBoxLayout(fields_frame);
    fields_layout_->setContentsMargins(20, 12, 20, 12);
    fields_layout_->setSpacing(0);
    page_layout->addWidget(fields_frame);
    page_layout->addStretch(1);
}

void DetailPage::AddField(const QString& key, const QString& label) {
    auto* row = new QFrame(this);
    row->setObjectName(QStringLiteral("detailRow"));
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 11, 0, 11);
    auto* name = new QLabel(label, row);
    name->setObjectName(QStringLiteral("detailLabel"));
    name->setWordWrap(true);
    name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto* value = new QLabel(QStringLiteral("--"), row);
    value->setObjectName(QStringLiteral("detailValue"));
    value->setWordWrap(true);
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row_layout->addWidget(name);
    row_layout->addStretch(1);
    row_layout->addWidget(value);
    if (fields_layout_->count() > 0) {
        auto* divider = new QFrame(fields_layout_->parentWidget());
        divider->setObjectName(QStringLiteral("divider"));
        divider->setFixedHeight(1);
        fields_layout_->addWidget(divider);
    }
    fields_layout_->addWidget(row);
    values_.insert(key, value);
}

void DetailPage::SetValue(const QString& key, const QString& value) {
    QLabel* label = values_.value(key, nullptr);
    if (label && label->text() != value) {
        label->setText(value);
    }
}

} // namespace nlsi::gui
