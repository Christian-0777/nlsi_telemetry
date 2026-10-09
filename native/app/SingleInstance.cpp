#include "SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>

namespace nlsi::app {

SingleInstance::SingleInstance(
    const QString& user_scope,
    std::function<void()> activate_existing,
    QObject* parent)
    : QObject(parent),
      server_name_(QStringLiteral("nlsi-exclusive-logbook-%1")
          .arg(QString::fromLatin1(QCryptographicHash::hash(
              user_scope.toUtf8(), QCryptographicHash::Sha256).toHex()))),
      lock_file_(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
          .filePath(server_name_ + QStringLiteral(".lock"))),
      activate_existing_(std::move(activate_existing)) {
    lock_file_.setStaleLockTime(30000);
}

SingleInstance::StartResult SingleInstance::Start(QString* error) {
    if (!lock_file_.tryLock(0)) {
        if (lock_file_.error() == QLockFile::LockFailedError) {
            return StartResult::AlreadyRunning;
        }
        if (error) {
            *error = QStringLiteral("Could not acquire the application instance lock (%1).")
                .arg(static_cast<int>(lock_file_.error()));
        }
        return StartResult::Failed;
    }

    if (!server_.listen(server_name_)) {
        QLocalServer::removeServer(server_name_);
        if (!server_.listen(server_name_)) {
            if (error) {
                *error = QStringLiteral("Could not start the single-instance service: %1")
                    .arg(server_.errorString());
            }
            lock_file_.unlock();
            return StartResult::Failed;
        }
    }

    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (server_.hasPendingConnections()) {
            QLocalSocket* socket = server_.nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QByteArray request = socket->readAll();
                if (request == QByteArrayLiteral("activate") && activate_existing_) {
                    activate_existing_();
                }
                socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    return StartResult::Started;
}

bool SingleInstance::NotifyExistingInstance(QString* error) const {
    QLocalSocket socket;
    socket.connectToServer(server_name_, QIODevice::WriteOnly);
    if (!socket.waitForConnected(1000)) {
        if (error) {
            *error = QStringLiteral("Connection failed: %1").arg(socket.errorString());
        }
        return false;
    }
    if (socket.write(QByteArrayLiteral("activate")) != 8) {
        if (error) {
            *error = QStringLiteral("Activation write failed: %1").arg(socket.errorString());
        }
        return false;
    }
    socket.flush();
    if (socket.bytesToWrite() == 0 || socket.waitForBytesWritten(1000)
        || socket.bytesToWrite() == 0) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Activation delivery failed: %1 (pending %2 bytes).")
            .arg(socket.errorString())
            .arg(socket.bytesToWrite());
    }
    return false;
}

} // namespace nlsi::app
