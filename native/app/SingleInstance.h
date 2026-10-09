#pragma once

#include <functional>

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

namespace nlsi::app {

class SingleInstance final : public QObject {
public:
    enum class StartResult {
        Started,
        AlreadyRunning,
        Failed,
    };

    SingleInstance(
        const QString& user_scope,
        std::function<void()> activate_existing,
        QObject* parent = nullptr);

    StartResult Start(QString* error = nullptr);
    bool NotifyExistingInstance(QString* error = nullptr) const;

private:
    QString server_name_;
    QLockFile lock_file_;
    QLocalServer server_;
    std::function<void()> activate_existing_;
};

} // namespace nlsi::app
