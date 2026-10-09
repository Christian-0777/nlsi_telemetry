#pragma once

#include <functional>

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkReply;

namespace nlsi::updater {

struct ReleaseInfo {
    QString version;
    QString channel;
    QString name;
    QString notes;
    QUrl url;
};

struct UpdateCheckResult {
    bool succeeded = false;
    bool update_available = false;
    bool from_cache = false;
    QString error;
    ReleaseInfo release;
};

class GitHubUpdater final : public QObject {
public:
    using ResultHandler = std::function<void(const UpdateCheckResult&, bool)>;

    explicit GitHubUpdater(
        QString current_version,
        QObject* parent = nullptr,
        QUrl api_endpoint = QUrl(
            QStringLiteral("https://api.github.com/repos/"
                "Christian-0777/nlsi_telemetry/releases")));

    void SetResultHandler(ResultHandler handler);
    void CheckForUpdates(bool manual = false);

    static int CompareVersions(const QString& left, const QString& right);
    static bool ParsePublishedReleases(
        const QByteArray& payload,
        const QString& current_version,
        UpdateCheckResult* result,
        QString* error);

private:
    UpdateCheckResult CachedResult() const;
    void Complete(const UpdateCheckResult& result, bool manual);

    QString current_version_;
    QUrl api_endpoint_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> active_reply_;
    ResultHandler result_handler_;
    bool active_manual_ = false;
};

} // namespace nlsi::updater
