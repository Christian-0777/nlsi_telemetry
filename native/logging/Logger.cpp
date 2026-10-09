#include "Logger.h"

#include <windows.h>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>

#include "time/ApplicationTime.h"

namespace nlsi::logging {

Logger::Logger(const std::wstring& log_path) : log_path_(log_path) {
    const QFileInfo text_log(QString::fromStdWString(log_path_));
    nlsi_log_path_ = QDir(text_log.path())
        .filePath(text_log.completeBaseName() + QStringLiteral(".nlsi")).toStdWString();
}

Logger::~Logger() = default;

bool Logger::Log(const std::wstring& message, std::wstring* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    QFile file(QString::fromStdWString(log_path_));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        const QString reason = QStringLiteral("Could not open %1: %2")
            .arg(file.fileName(), file.errorString());
        if (error) {
            *error = reason.toStdWString();
        }
        OutputDebugStringW((reason + QLatin1Char('\n')).toStdWString().c_str());
        return false;
    }

    const QByteArray line = QString::fromStdWString(message).toUtf8() + '\n';
    if (file.write(line) != line.size() || !file.flush()) {
        const QString reason = QStringLiteral("Could not write %1: %2")
            .arg(file.fileName(), file.errorString());
        if (error) {
            *error = reason.toStdWString();
        }
        OutputDebugStringW((reason + QLatin1Char('\n')).toStdWString().c_str());
        return false;
    }
    return WriteNlsiRecord(message, error);
}

std::wstring Logger::LogPath() const {
    return log_path_;
}

std::wstring Logger::NlsiLogPath() const {
    return nlsi_log_path_;
}

bool Logger::EnsureNlsiLogValid(std::wstring* error) {
    if (nlsi_log_validated_) {
        return true;
    }
    QStringList entries;
    if (!QFileInfo::exists(QString::fromStdWString(nlsi_log_path_))) {
        nlsi_log_validated_ = true;
        return true;
    }
    if (!ReadNlsiLog(nlsi_log_path_, &entries, error)) {
        return false;
    }
    nlsi_log_validated_ = true;
    return true;
}

bool Logger::WriteNlsiRecord(const std::wstring& message, std::wstring* error) {
    if (!EnsureNlsiLogValid(error)) {
        return false;
    }
    QFile file(QString::fromStdWString(nlsi_log_path_));
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append | QIODevice::Text)) {
        const QString reason = QStringLiteral("Could not open %1: %2")
            .arg(file.fileName(), file.errorString());
        if (error) {
            *error = reason.toStdWString();
        }
        nlsi_log_validated_ = false;
        return false;
    }

    if (file.size() == 0) {
        const QJsonObject header{
            {QStringLiteral("format"), QStringLiteral("nlsi-log")},
            {QStringLiteral("schema_version"), 1},
            {QStringLiteral("record_type"), QStringLiteral("header")},
        };
        const QByteArray header_line = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
        if (file.write(header_line) != header_line.size()) {
            const QString reason = QStringLiteral("Could not write NLSI log header to %1: %2")
                .arg(file.fileName(), file.errorString());
            if (error) {
                *error = reason.toStdWString();
            }
            nlsi_log_validated_ = false;
            return false;
        }
    } else {
        if (!file.seek(file.size() - 1) || file.read(1) != QByteArray("\n")) {
            const QString reason = QStringLiteral(
                "NLSI log %1 has an incomplete trailing record; it was not modified.")
                .arg(file.fileName());
            if (error) {
                *error = reason.toStdWString();
            }
            nlsi_log_validated_ = false;
            return false;
        }
        if (!file.seek(file.size())) {
            const QString reason = QStringLiteral("Could not seek to the end of %1: %2")
                .arg(file.fileName(), file.errorString());
            if (error) {
                *error = reason.toStdWString();
            }
            nlsi_log_validated_ = false;
            return false;
        }
    }

    const QJsonObject record{
        {QStringLiteral("record_type"), QStringLiteral("entry")},
        {QStringLiteral("timestamp"), nlsi::time::UtcTimestampNow()},
        {QStringLiteral("message"), QString::fromStdWString(message)},
    };
    const QByteArray line = QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size() || !file.flush()) {
        const QString reason = QStringLiteral("Could not append to %1: %2")
            .arg(file.fileName(), file.errorString());
        if (error) {
            *error = reason.toStdWString();
        }
        nlsi_log_validated_ = false;
        return false;
    }
    return true;
}

bool Logger::ReadNlsiLog(
    const std::wstring& path,
    QStringList* messages,
    std::wstring* error) {
    const QString file_path = QString::fromStdWString(path);
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString reason = QStringLiteral("Could not read %1: %2")
            .arg(file_path, file.errorString());
        if (error) {
            *error = reason.toStdWString();
        }
        return false;
    }

    QStringList parsed_messages;
    int line_number = 0;
    bool header_read = false;
    while (!file.atEnd()) {
        ++line_number;
        const QByteArray line = file.readLine();
        if (!line.endsWith('\n')) {
            if (error) {
                *error = QStringLiteral("Incomplete NLSI log record at line %1 in %2.")
                    .arg(line_number).arg(file_path).toStdWString();
            }
            return false;
        }
        QJsonParseError parse_error;
        const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = QStringLiteral("Invalid NLSI log JSON at line %1 in %2: %3")
                    .arg(line_number).arg(file_path, parse_error.errorString()).toStdWString();
            }
            return false;
        }
        const QJsonObject object = document.object();
        if (!header_read) {
            if (object.value(QStringLiteral("record_type")).toString() != QStringLiteral("header")
                || object.value(QStringLiteral("format")).toString() != QStringLiteral("nlsi-log")
                || object.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
                if (error) {
                    *error = QStringLiteral("Unsupported NLSI log header or schema version in %1.")
                        .arg(file_path).toStdWString();
                }
                return false;
            }
            header_read = true;
            continue;
        }
        if (object.value(QStringLiteral("record_type")).toString() != QStringLiteral("entry")
            || !object.value(QStringLiteral("timestamp")).isString()
            || !QDateTime::fromString(
                    object.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs).isValid()
            || !object.value(QStringLiteral("message")).isString()) {
            if (error) {
                *error = QStringLiteral("Invalid NLSI log entry at line %1 in %2.")
                    .arg(line_number).arg(file_path).toStdWString();
            }
            return false;
        }
        parsed_messages.push_back(object.value(QStringLiteral("message")).toString());
    }
    if (!header_read) {
        if (error) {
            *error = QStringLiteral("NLSI log %1 is empty and has no schema header.")
                .arg(file_path).toStdWString();
        }
        return false;
    }
    if (messages) {
        *messages = std::move(parsed_messages);
    }
    return true;
}

} // namespace nlsi::logging
