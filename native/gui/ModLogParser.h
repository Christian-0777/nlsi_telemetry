#pragma once

#include <QDateTime>
#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QString>
#include <QUrl>

namespace nlsi::gui::modlog {

struct WorkshopMod {
    QString id;
    QString name;
    QString version;
    QString author;
    bool subscribed = false;
    bool mounted = false;
    bool active_workshop = false;
    bool active_local = false;

    bool operator==(const WorkshopMod&) const = default;
};

struct GameLogResult {
    QString path;
    QVector<WorkshopMod> mods;
    QHash<QString, qsizetype> mod_indices;
    QString error;
    QByteArray pending_line;
    QDateTime last_modified;
    QDateTime file_created;
    quint64 file_id = 0;
    qint64 offset = 0;
    qint64 last_game_time_ms = -1;
    bool initialized = false;
    bool session_boundary_known = false;
    bool stale = false;
};

QString GameLogPath(const QString& documents_directory, const QString& game_directory);
QVector<WorkshopMod> ParseActiveWorkshopMods(const QByteArray& contents);
GameLogResult ReadGameLog(const QString& path);
bool GameLogNeedsReinitialize(const QString& path, const GameLogResult& current);
bool ReadAppendedGameLog(const QString& path, GameLogResult& current);
QUrl WorkshopSourceUrl(const QString& workshop_id);

} // namespace nlsi::gui::modlog
