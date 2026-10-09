#include "GitHubUpdater.h"

#include <algorithm>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>

namespace nlsi::updater {
namespace {

struct SemanticVersion {
    qint64 major = 0;
    qint64 minor = 0;
    qint64 patch = 0;
    QStringList prerelease;
    QString normalized;
};

bool ParseSemanticVersion(const QString& input, SemanticVersion* version) {
    QString text = input.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        text.remove(0, 1);
    }
    const qsizetype build_metadata = text.indexOf(QLatin1Char('+'));
    if (build_metadata >= 0) {
        text.truncate(build_metadata);
    }
    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\."
            "(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return false;
    }
    bool major_valid = false;
    bool minor_valid = false;
    bool patch_valid = false;
    version->major = match.captured(1).toLongLong(&major_valid);
    version->minor = match.captured(2).toLongLong(&minor_valid);
    version->patch = match.captured(3).toLongLong(&patch_valid);
    if (!major_valid || !minor_valid || !patch_valid) {
        return false;
    }
    if (!match.captured(4).isEmpty()) {
        version->prerelease = match.captured(4).split(QLatin1Char('.'));
    }
    version->normalized = text;
    return true;
}

int CompareIdentifiers(const QString& left, const QString& right) {
    bool left_numeric = false;
    bool right_numeric = false;
    const qulonglong left_number = left.toULongLong(&left_numeric);
    const qulonglong right_number = right.toULongLong(&right_numeric);
    if (left_numeric && right_numeric) {
        return left_number < right_number ? -1 : (left_number > right_number ? 1 : 0);
    }
    if (left_numeric != right_numeric) {
        return left_numeric ? -1 : 1;
    }
    const int comparison = QString::compare(left, right, Qt::CaseSensitive);
    return comparison < 0 ? -1 : (comparison > 0 ? 1 : 0);
}

QString ReleaseChannel(const SemanticVersion& version, bool prerelease) {
    if (version.prerelease.isEmpty() && !prerelease) {
        return QStringLiteral("Stable");
    }
    const QString identifier = version.prerelease.value(0).toLower();
    if (identifier == QStringLiteral("alpha")) {
        return QStringLiteral("Alpha");
    }
    if (identifier == QStringLiteral("beta")) {
        return QStringLiteral("Beta");
    }
    if (identifier == QStringLiteral("rc")) {
        return QStringLiteral("Release candidate");
    }
    return QStringLiteral("Prerelease");
}

QString EffectiveVersion(const SemanticVersion& version, bool prerelease) {
    if (prerelease && version.prerelease.isEmpty()) {
        return version.normalized + QStringLiteral("-beta");
    }
    return version.normalized;
}

bool IsPublishedReleaseUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0
        && url.userInfo().isEmpty();
}

constexpr qint64 kCheckCooldownSeconds = 30 * 60;
constexpr int kRequestTimeoutMilliseconds = 10000;

} // namespace

GitHubUpdater::GitHubUpdater(
    QString current_version,
    QObject* parent,
    QUrl api_endpoint)
    : QObject(parent),
      current_version_(std::move(current_version)),
      api_endpoint_(std::move(api_endpoint)) {
}

void GitHubUpdater::SetResultHandler(ResultHandler handler) {
    result_handler_ = std::move(handler);
}

int GitHubUpdater::CompareVersions(const QString& left, const QString& right) {
    SemanticVersion lhs;
    SemanticVersion rhs;
    if (!ParseSemanticVersion(left, &lhs) || !ParseSemanticVersion(right, &rhs)) {
        return 0;
    }
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major ? -1 : 1;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor ? -1 : 1;
    }
    if (lhs.patch != rhs.patch) {
        return lhs.patch < rhs.patch ? -1 : 1;
    }
    if (lhs.prerelease.isEmpty() != rhs.prerelease.isEmpty()) {
        return lhs.prerelease.isEmpty() ? 1 : -1;
    }
    for (qsizetype index = 0;
         index < std::min(lhs.prerelease.size(), rhs.prerelease.size());
         ++index) {
        const int comparison = CompareIdentifiers(
            lhs.prerelease[index], rhs.prerelease[index]);
        if (comparison != 0) {
            return comparison;
        }
    }
    if (lhs.prerelease.size() == rhs.prerelease.size()) {
        return 0;
    }
    return lhs.prerelease.size() < rhs.prerelease.size() ? -1 : 1;
}

bool GitHubUpdater::ParsePublishedReleases(
    const QByteArray& payload,
    const QString& current_version,
    UpdateCheckResult* result,
    QString* error) {
    if (!result) {
        if (error) {
            *error = QStringLiteral("No result destination was provided.");
        }
        return false;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = QStringLiteral("GitHub returned an invalid releases response: %1")
                .arg(parse_error.errorString());
        }
        return false;
    }
    SemanticVersion current;
    if (!ParseSemanticVersion(current_version, &current)) {
        if (error) {
            *error = QStringLiteral("The current application version is not valid semantic versioning.");
        }
        return false;
    }

    SemanticVersion newest_version;
    bool has_newest = false;
    ReleaseInfo newest;
    const QJsonArray releases = document.array();
    for (qsizetype index = 0; index < releases.size(); ++index) {
        if (!releases[index].isObject()) {
            if (error) {
                *error = QStringLiteral("GitHub release entry %1 is not an object.").arg(index);
            }
            return false;
        }
        const QJsonObject release = releases[index].toObject();
        if (release.value(QStringLiteral("draft")).toBool()) {
            continue;
        }
        const QString published_at = release.value(QStringLiteral("published_at")).toString();
        if (published_at.isEmpty()) {
            continue;
        }
        const QDateTime published = QDateTime::fromString(published_at, Qt::ISODateWithMs);
        if (!published.isValid()) {
            if (error) {
                *error = QStringLiteral("GitHub returned an invalid published_at timestamp.");
            }
            return false;
        }
        const QString tag = release.value(QStringLiteral("tag_name")).toString();
        SemanticVersion candidate;
        if (!ParseSemanticVersion(tag, &candidate)) {
            if (error) {
                *error = QStringLiteral("GitHub published a release with an invalid version tag.");
            }
            return false;
        }
        const bool prerelease = release.value(QStringLiteral("prerelease")).toBool();
        const QString effective_version = EffectiveVersion(candidate, prerelease);
        if (!ParseSemanticVersion(effective_version, &candidate)) {
            if (error) {
                *error = QStringLiteral("GitHub returned an invalid prerelease version.");
            }
            return false;
        }
        if (!IsPublishedReleaseUrl(QUrl(release.value(QStringLiteral("html_url")).toString()))) {
            if (error) {
                *error = QStringLiteral("GitHub returned an invalid release page URL.");
            }
            return false;
        }
        if (!has_newest || CompareVersions(candidate.normalized, newest_version.normalized) > 0) {
            newest_version = candidate;
            newest.version = candidate.normalized;
            newest.channel = ReleaseChannel(candidate, prerelease);
            newest.name = release.value(QStringLiteral("name")).toString();
            newest.notes = release.value(QStringLiteral("body")).toString();
            newest.url = QUrl(release.value(QStringLiteral("html_url")).toString());
            has_newest = true;
        }
    }

    *result = {};
    result->succeeded = true;
    if (has_newest) {
        result->release = newest;
        result->update_available =
            CompareVersions(current.normalized, newest_version.normalized) < 0;
    }
    return true;
}

UpdateCheckResult GitHubUpdater::CachedResult() const {
    QSettings settings(QStringLiteral("NLSI"), QStringLiteral("Exclusive Logbook"));
    const qint64 last_success = settings.value(
        QStringLiteral("updates/lastSuccessfulCheck"), 0).toLongLong();
    if (last_success <= 0
        || QDateTime::currentDateTimeUtc().toSecsSinceEpoch() - last_success
            >= kCheckCooldownSeconds) {
        return {};
    }

    UpdateCheckResult result;
    result.succeeded = true;
    result.from_cache = true;
    const QString version = settings.value(QStringLiteral("updates/version")).toString();
    if (!version.isEmpty()) {
        result.release.version = version;
        result.release.channel = settings.value(QStringLiteral("updates/channel")).toString();
        result.release.name = settings.value(QStringLiteral("updates/name")).toString();
        result.release.notes = settings.value(QStringLiteral("updates/notes")).toString();
        result.release.url = QUrl(settings.value(QStringLiteral("updates/url")).toString());
        result.update_available = CompareVersions(current_version_, version) < 0;
    }
    return result;
}

void GitHubUpdater::CheckForUpdates(bool manual) {
    if (!manual && qEnvironmentVariableIsSet("NLSI_DISABLE_UPDATE_CHECK")) {
        UpdateCheckResult result;
        result.error = QStringLiteral("Automatic update checks are disabled in this test run.");
        Complete(result, false);
        return;
    }
    if (active_reply_) {
        UpdateCheckResult result;
        result.error = QStringLiteral("An update check is already in progress.");
        Complete(result, manual);
        return;
    }
    if (!manual) {
        const UpdateCheckResult cached = CachedResult();
        if (cached.succeeded) {
            Complete(cached, false);
            return;
        }
    }

    QNetworkRequest request(api_endpoint_);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "NLSI-Exclusive-Logbook");
    request.setTransferTimeout(kRequestTimeoutMilliseconds);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy);
    active_reply_ = network_.get(request);
    active_manual_ = manual;
    connect(active_reply_, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = active_reply_;
        active_reply_.clear();
        if (!reply) {
            return;
        }
        UpdateCheckResult result;
        const int status_code = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status_code < 200
            || status_code >= 300) {
            if (status_code == 403 || status_code == 429) {
                result.error = QStringLiteral("GitHub rate-limited the request (HTTP %1).")
                    .arg(status_code);
            } else if (status_code != 0) {
                result.error = QStringLiteral("GitHub returned HTTP %1.").arg(status_code);
            } else {
                result.error = reply->errorString();
            }
        } else {
            QString error;
            ParsePublishedReleases(
                reply->readAll(), current_version_, &result, &error);
            if (!result.succeeded) {
                result.error = error;
            } else {
                QSettings settings(
                    QStringLiteral("NLSI"), QStringLiteral("Exclusive Logbook"));
                settings.setValue(QStringLiteral("updates/lastSuccessfulCheck"),
                    QDateTime::currentDateTimeUtc().toSecsSinceEpoch());
                settings.setValue(QStringLiteral("updates/version"), result.release.version);
                settings.setValue(QStringLiteral("updates/channel"), result.release.channel);
                settings.setValue(QStringLiteral("updates/name"), result.release.name);
                settings.setValue(QStringLiteral("updates/notes"), result.release.notes);
                settings.setValue(QStringLiteral("updates/url"), result.release.url.toString());
            }
        }
        reply->deleteLater();
        Complete(result, active_manual_);
    });
}

void GitHubUpdater::Complete(const UpdateCheckResult& result, bool manual) {
    if (result_handler_) {
        result_handler_(result, manual);
    }
}

} // namespace nlsi::updater
