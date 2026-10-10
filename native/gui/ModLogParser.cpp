#include "ModLogParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrlQuery>

#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nlsi::gui::modlog {
namespace {

constexpr qint64 kMaximumPendingLineBytes = 1024 * 1024;
constexpr qsizetype kMaximumMods = 10000;

quint64 FileIdentity(const QString& path) {
#ifdef Q_OS_WIN
    const HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    BY_HANDLE_FILE_INFORMATION information{};
    const bool obtained = GetFileInformationByHandle(handle, &information) != 0;
    CloseHandle(handle);
    if (!obtained) {
        return 0;
    }
    return (static_cast<quint64>(information.nFileIndexHigh) << 32)
        | information.nFileIndexLow;
#else
    Q_UNUSED(path);
    return 0;
#endif
}

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

void ResetSession(GameLogResult& result) {
    result.mods.clear();
    result.mod_indices.clear();
    result.last_game_time_ms = -1;
    result.session_boundary_known = true;
}

qsizetype EnsureMod(
    GameLogResult& result,
    const QString& key,
    const QString& id,
    const QString& default_name) {
    const auto existing = result.mod_indices.constFind(key);
    if (existing != result.mod_indices.cend()) {
        return existing.value();
    }
    if (result.mods.size() >= kMaximumMods) {
        result.error = QStringLiteral(
            "The game log contains more than %1 distinct mod entries.")
            .arg(kMaximumMods);
        return -1;
    }
    WorkshopMod mod;
    mod.id = id;
    mod.name = default_name;
    result.mods.push_back(std::move(mod));
    const qsizetype index = result.mods.size() - 1;
    result.mod_indices.insert(key, index);
    return index;
}

qint64 GameTimeMilliseconds(const QString& line) {
    static const QRegularExpression timestamp(
        QStringLiteral("^(\\d+):(\\d{2}):(\\d{2})\\.(\\d{1,3})\\s*:"));
    const auto match = timestamp.match(line);
    if (!match.hasMatch()) {
        return -1;
    }
    bool hours_ok = false;
    bool minutes_ok = false;
    bool seconds_ok = false;
    bool milliseconds_ok = false;
    const qint64 hours = match.captured(1).toLongLong(&hours_ok);
    const qint64 minutes = match.captured(2).toLongLong(&minutes_ok);
    const qint64 seconds = match.captured(3).toLongLong(&seconds_ok);
    const QString milliseconds_text = match.captured(4).leftJustified(3, QLatin1Char('0'));
    const qint64 milliseconds = milliseconds_text.toLongLong(&milliseconds_ok);
    if (!hours_ok || !minutes_ok || !seconds_ok || !milliseconds_ok) {
        return -1;
    }
    return (((hours * 60 + minutes) * 60 + seconds) * 1000) + milliseconds;
}

bool IsSessionStartMarker(const QString& line) {
    static const QRegularExpression marker(
        QStringLiteral("\\[sys\\].*(?:executable\\s*:|application version|game version|"
            "starting (?:the )?game|launching (?:the )?game)"),
        QRegularExpression::CaseInsensitiveOption);
    return marker.match(line).hasMatch();
}

QString MountedPackageName(const QString& line) {
    QString name = AttributeValue(line, QStringLiteral("name"));
    if (name.isEmpty()) {
        name = AttributeValue(line, QStringLiteral("package"));
    }
    if (name.isEmpty()) {
        static const QRegularExpression package(
            QStringLiteral("\\b(?:mounted\\s+(?:mod\\s+)?package|"
                "(?:mod\\s+)?package\\s+mounted|mod\\s+package)"
                "\\s*[:=]?\\s*[\"']?([^,\"';]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = package.match(line);
        if (match.hasMatch()) {
            name = match.captured(1).trimmed();
        }
    }
    if (name.isEmpty()) {
        return {};
    }
    name = QDir::fromNativeSeparators(name);
    name = name.section(QLatin1Char('/'), -1);
    name.remove(QRegularExpression(QStringLiteral("\\.(?:scs|zip|rar)$"),
        QRegularExpression::CaseInsensitiveOption));
    return name.trimmed();
}

QString MountedPackageKey(const QString& name) {
    static const QRegularExpression collection(
        QStringLiteral("^(promods)(?:[-_ ].*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = collection.match(name);
    if (match.hasMatch()) {
        return QStringLiteral("mounted:promods");
    }
    return QStringLiteral("mounted:%1").arg(name.toCaseFolded());
}

void ApplyAttributes(WorkshopMod& mod, const QString& line) {
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

void AddEvidence(const QString& line, GameLogResult& result) {
    static const QRegularExpression subscribed(
        QStringLiteral("\\[mods\\]\\s+Subscribed workshop mod ID\\s*:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression active_workshop(
        QStringLiteral("\\[mods\\]\\s+Active workshop mod ID\\s*:\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression active_local(
        QStringLiteral("\\[mods\\]\\s+Active local mod ID\\s*:\\s*([^\\s,;]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression mounted_event(
        QStringLiteral("\\b(?:successfully\\s+)?mounted\\b.*\\bmod(?:\\s+package)?\\b|"
            "\\bmod(?:\\s+package)?\\b.*\\bmounted\\b"),
        QRegularExpression::CaseInsensitiveOption);

    auto match = subscribed.match(line);
    if (match.hasMatch() && IsValidWorkshopId(match.captured(1))) {
        const QString id = match.captured(1);
        const qsizetype index = EnsureMod(
            result, QStringLiteral("workshop:%1").arg(id), id,
            QStringLiteral("Workshop mod %1").arg(id));
        if (index >= 0) {
            result.mods[index].subscribed = true;
            ApplyAttributes(result.mods[index], line);
        }
        return;
    }

    match = active_workshop.match(line);
    if (match.hasMatch() && IsValidWorkshopId(match.captured(1))) {
        const QString id = match.captured(1);
        const qsizetype index = EnsureMod(
            result, QStringLiteral("workshop:%1").arg(id), id,
            QStringLiteral("Workshop mod %1").arg(id));
        if (index >= 0) {
            result.mods[index].active_workshop = true;
            ApplyAttributes(result.mods[index], line);
        }
        return;
    }

    match = active_local.match(line);
    if (match.hasMatch()) {
        const QString id = match.captured(1);
        const qsizetype index = EnsureMod(
            result, QStringLiteral("local:%1").arg(id), id,
            AttributeValue(line, QStringLiteral("name")).isEmpty()
                ? QStringLiteral("Local mod %1").arg(id)
                : AttributeValue(line, QStringLiteral("name")));
        if (index >= 0) {
            result.mods[index].active_local = true;
            ApplyAttributes(result.mods[index], line);
        }
        return;
    }

    if (mounted_event.match(line).hasMatch()) {
        const QString name = MountedPackageName(line);
        if (name.isEmpty()) {
            return;
        }
        const QString key = MountedPackageKey(name);
        const bool is_promods = key == QStringLiteral("mounted:promods");
        const qsizetype index = EnsureMod(
            result, key, key.mid(QStringLiteral("mounted:").size()),
            is_promods ? QStringLiteral("ProMods") : name);
        if (index >= 0) {
            result.mods[index].mounted = true;
            ApplyAttributes(result.mods[index], line);
            if (is_promods) {
                result.mods[index].name = QStringLiteral("ProMods");
            }
        }
    }
}

void ProcessLine(const QByteArray& bytes, GameLogResult& result) {
    QString line = QString::fromUtf8(bytes);
    if (line.endsWith(QLatin1Char('\r'))) {
        line.chop(1);
    }
    if (IsSessionStartMarker(line)) {
        ResetSession(result);
    }

    const qint64 timestamp = GameTimeMilliseconds(line);
    if (timestamp >= 0) {
        if (result.last_game_time_ms >= 0
            && timestamp + 5000 < result.last_game_time_ms) {
            ResetSession(result);
        }
        result.last_game_time_ms = timestamp;
    }
    AddEvidence(line, result);
}

bool ConsumeBytes(const QByteArray& bytes, GameLogResult& result) {
    QByteArray input = std::move(result.pending_line);
    result.pending_line.clear();
    input.append(bytes);

    qsizetype line_start = 0;
    while (true) {
        const qsizetype newline = input.indexOf('\n', line_start);
        if (newline < 0) {
            break;
        }
        ProcessLine(input.mid(line_start, newline - line_start), result);
        line_start = newline + 1;
    }
    result.pending_line = input.mid(line_start);
    if (result.pending_line.size() > kMaximumPendingLineBytes) {
        result.pending_line.clear();
        result.error = QStringLiteral(
            "A game-log line exceeded the 1 MiB parsing limit and was discarded.");
        return false;
    }
    return result.error.isEmpty();
}

void UpdateFileMetadata(GameLogResult& result, const QFileInfo& info) {
    result.last_modified = info.lastModified();
    result.file_created = info.birthTime();
    result.file_id = FileIdentity(result.path);
    result.stale = result.last_modified.isValid()
        && result.last_modified.secsTo(QDateTime::currentDateTime()) > 600;
}

bool SameFileGeneration(const QFileInfo& info, const GameLogResult& current) {
    const quint64 current_file_id = FileIdentity(info.absoluteFilePath());
    if (current.file_id != 0 && current_file_id != 0) {
        return current.file_id == current_file_id;
    }
    return current.file_created.isValid() && info.birthTime().isValid()
        ? current.file_created == info.birthTime()
        : info.lastModified() == current.last_modified;
}

} // namespace

QString GameLogPath(const QString& documents_directory, const QString& game_directory) {
    return QDir(documents_directory).filePath(
        game_directory + QStringLiteral("/game.log.txt"));
}

QVector<WorkshopMod> ParseActiveWorkshopMods(const QByteArray& contents) {
    GameLogResult result;
    const QList<QByteArray> lines = contents.split('\n');
    for (const QByteArray& line : lines) {
        ProcessLine(line, result);
    }
    QVector<WorkshopMod> active;
    for (const WorkshopMod& mod : result.mods) {
        if (mod.active_workshop) {
            active.push_back(mod);
        }
    }
    return active;
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
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Game log could not be read: %1")
            .arg(file.errorString());
        return result;
    }

    const quint64 before_file_id = FileIdentity(path);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            result.error = QStringLiteral("Game log read failed: %1")
                .arg(file.errorString());
            return result;
        }
        result.offset += chunk.size();
        if (!ConsumeBytes(chunk, result) && !result.error.isEmpty()) {
            return result;
        }
    }
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Game log read failed: %1")
            .arg(file.errorString());
        return result;
    }
    file.close();

    const QFileInfo after(path);
    const quint64 after_file_id = FileIdentity(path);
    if (!after.exists() || before.size() != after.size()
        || before.lastModified() != after.lastModified()
        || (before_file_id != 0 && after_file_id != 0
            && before_file_id != after_file_id)
        || (before.birthTime().isValid() && after.birthTime().isValid()
            && before.birthTime() != after.birthTime())) {
        result.mods.clear();
        result.mod_indices.clear();
        result.error = QStringLiteral("Game log changed while being read; retrying.");
        return result;
    }

    result.initialized = true;
    UpdateFileMetadata(result, after);
    result.error.clear();
    return result;
}

bool GameLogNeedsReinitialize(const QString& path, const GameLogResult& current) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !current.initialized) {
        return true;
    }
    return info.size() < current.offset || !SameFileGeneration(info, current);
}

bool ReadAppendedGameLog(const QString& path, GameLogResult& current) {
    if (GameLogNeedsReinitialize(path, current)) {
        current.mods.clear();
        current.mod_indices.clear();
        current.initialized = false;
        current.error = QStringLiteral("Game log was replaced or truncated; reinitializing.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        current.error = QStringLiteral("Game log could not be read: %1")
            .arg(file.errorString());
        return false;
    }
    if (!file.seek(current.offset)) {
        current.error = QStringLiteral("Game log could not seek to its previous read position: %1")
            .arg(file.errorString());
        return false;
    }

    current.error.clear();
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(64 * 1024);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            current.error = QStringLiteral("Game log read failed: %1")
                .arg(file.errorString());
            return false;
        }
        current.offset += chunk.size();
        if (!ConsumeBytes(chunk, current)) {
            return false;
        }
    }
    if (file.error() != QFileDevice::NoError) {
        current.error = QStringLiteral("Game log read failed: %1")
            .arg(file.errorString());
        return false;
    }
    current.error.clear();
    UpdateFileMetadata(current, QFileInfo(path));
    return true;
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
