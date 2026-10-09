#include "ModLogParser.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QRegularExpression>
#include <QUrlQuery>

namespace nlsi::gui::modlog {
namespace {

QString AttributeValue(const QString& line, const QString& name) {
    const QRegularExpression expression(
        QStringLiteral("(?:^|[,;]\\s*)%1\\s*:\\s*(?:\"([^\"]*)\"|'([^']*)'|([^,;]+))")
            .arg(QRegularExpression::escape(name)),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(line);
    if (!match.hasMatch()) {
        return {};
    }
    for (int group = 1; group <= 3; ++group) {
        if (!match.captured(group).isNull()) {
            return match.captured(group).trimmed();
        }
    }
    return {};
}

bool IsValidWorkshopId(const QString& id) {
    static const QRegularExpression digits(QStringLiteral("^[0-9]{1,20}$"));
    bool converted = false;
    const qulonglong numeric_id = id.toULongLong(&converted);
    return digits.match(id).hasMatch() && converted && numeric_id > 0;
}

void AddActiveWorkshopMod(
    const QString& line,
    QVector<WorkshopMod>& mods,
    QHash<QString, qsizetype>& index_by_id) {
    static const QRegularExpression active_entry(
        QStringLiteral("\\[mods\\]\\s+Active workshop mod ID\\s*:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = active_entry.match(line);
    if (!match.hasMatch() || !IsValidWorkshopId(match.captured(1))) {
        return;
    }

    const QString id = match.captured(1);
    auto existing = index_by_id.constFind(id);
    if (existing == index_by_id.cend()) {
        WorkshopMod mod;
        mod.id = id;
        mod.name = QStringLiteral("Workshop mod %1").arg(id);
        mods.push_back(mod);
        index_by_id.insert(id, mods.size() - 1);
        existing = index_by_id.constFind(id);
    }

    WorkshopMod& mod = mods[static_cast<qsizetype>(existing.value())];
    const QString name = AttributeValue(line, QStringLiteral("name"));
    const QString version = AttributeValue(line, QStringLiteral("version"));
    const QString author = AttributeValue(line, QStringLiteral("author"));
    if (!name.isEmpty()) {
        mod.name = name;
    }
    if (!version.isEmpty()) {
        mod.version = version;
    }
    if (!author.isEmpty()) {
        mod.author = author;
    }
}

} // namespace

QString GameLogPath(const QString& documents_directory, const QString& game_directory) {
    return QDir(documents_directory).filePath(
        game_directory + QStringLiteral("/game.log.txt"));
}

QVector<WorkshopMod> ParseActiveWorkshopMods(const QByteArray& contents) {
    QVector<WorkshopMod> mods;
    QHash<QString, qsizetype> index_by_id;
    const QString text = QString::fromUtf8(contents);
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        AddActiveWorkshopMod(line, mods, index_by_id);
    }
    return mods;
}

GameLogResult ReadGameLog(const QString& path) {
    GameLogResult result;
    result.path = path;
    const QFileInfo before(path);
    if (!before.exists() || !before.isFile()) {
        result.error = QStringLiteral("Game log not found.");
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = QStringLiteral("Game log could not be read: %1")
            .arg(file.errorString());
        return result;
    }

    QHash<QString, qsizetype> index_by_id;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine();
        if (line.isEmpty() && file.error() != QFileDevice::NoError) {
            result.error = QStringLiteral("Game log read failed: %1")
                .arg(file.errorString());
            return result;
        }
        AddActiveWorkshopMod(QString::fromUtf8(line), result.mods, index_by_id);
    }
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Game log read failed: %1")
            .arg(file.errorString());
        return result;
    }
    file.close();

    const QFileInfo after(path);
    if (!after.exists() || before.size() != after.size()
        || before.lastModified() != after.lastModified()) {
        result.error = QStringLiteral("Game log changed while being read; retrying.");
        return result;
    }

    result.last_modified = after.lastModified();
    result.stale = result.last_modified.isValid()
        && result.last_modified.secsTo(QDateTime::currentDateTime()) > 600;
    return result;
}

QUrl WorkshopSourceUrl(const QString& workshop_id) {
    if (!IsValidWorkshopId(workshop_id)) {
        return {};
    }
    QUrl url(QStringLiteral("https://steamcommunity.com/sharedfiles/filedetails/"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), workshop_id);
    url.setQuery(query);
    return url;
}

} // namespace nlsi::gui::modlog
