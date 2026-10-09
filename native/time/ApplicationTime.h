#pragma once

#include <QDateTime>
#include <QByteArray>
#include <QString>
#include <QRegularExpression>
#include <QTimeZone>

namespace nlsi::time {

inline constexpr char kZoneId[] = "Asia/Manila";

inline QTimeZone Zone() {
    return QTimeZone(QByteArray(kZoneId));
}

inline bool IsAvailable() {
    return Zone().isValid();
}

inline QDateTime NowUtc() {
    return QDateTime::currentDateTimeUtc();
}

inline QDateTime NowLocal() {
    const QTimeZone zone = Zone();
    return zone.isValid() ? NowUtc().toTimeZone(zone) : QDateTime{};
}

inline QDateTime ParseInstant(const QString& value) {
    const bool has_explicit_zone = value.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)
        || QRegularExpression(QStringLiteral("[+-]\\d{2}:?\\d{2}$")).match(value).hasMatch();
    if (!has_explicit_zone) {
        return {};
    }
    QDateTime timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(value, Qt::ISODate);
    }
    const QTimeZone zone = Zone();
    return timestamp.isValid() && zone.isValid()
        ? timestamp.toTimeZone(zone)
        : QDateTime{};
}

inline QString UtcTimestampNow() {
    return NowUtc().toString(Qt::ISODateWithMs);
}

inline QString LocalTimestampNow() {
    const QDateTime local = NowLocal();
    return local.isValid() ? local.toString(Qt::ISODateWithMs) : QString{};
}

} // namespace nlsi::time
