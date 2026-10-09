#pragma once

#include <QDateTime>
#include <QVector>
#include <QString>
#include <QUrl>

namespace nlsi::gui::modlog {

struct WorkshopMod {
    QString id;
    QString name;
    QString version;
    QString author;

    bool operator==(const WorkshopMod&) const = default;
};

struct GameLogResult {
    QString path;
    QVector<WorkshopMod> mods;
    QString error;
    QDateTime last_modified;
    bool stale = false;
};

QString GameLogPath(const QString& documents_directory, const QString& game_directory);
QVector<WorkshopMod> ParseActiveWorkshopMods(const QByteArray& contents);
GameLogResult ReadGameLog(const QString& path);
QUrl WorkshopSourceUrl(const QString& workshop_id);

} // namespace nlsi::gui::modlog
