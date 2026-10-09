#include "App.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

#include "gui/MainWindow.h"
#include "SingleInstance.h"
#include "time/ApplicationTime.h"

App::App()
    : version_label_([this] {
          const QString channel = QString::fromLatin1(NLSI_CHANNEL);
          return QStringLiteral("v%1-%2")
              .arg(QString::fromLatin1(NLSI_VERSION),
                  channel)
              .toStdWString();
      }()) {
}
App::~App() = default;

int App::Run() {
    int argc = 1;
    char app_name[] = "NLSI-Exclusive-Logbook";
    char* argv[] = {app_name, nullptr};
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Exclusive Logbook"));
    application.setApplicationVersion(QString::fromStdWString(version_label_));
    application.setOrganizationName(QStringLiteral("NLSI"));
    application.setApplicationDisplayName(QString::fromStdWString(product_name_));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

    if (!nlsi::time::IsAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"),
            QStringLiteral("The required IANA time-zone data for Asia/Manila is unavailable. "
                "Install or repair the Qt time-zone data and restart the application."));
        return 1;
    }

    const QString user_data_path = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (user_data_path.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"),
            QStringLiteral("Windows did not provide a writable application data directory."));
        return 1;
    }
    nlsi::gui::MainWindow* existing_window = nullptr;
    nlsi::app::SingleInstance instance(user_data_path, [&existing_window] {
        if (!existing_window) {
            return;
        }
        if (existing_window->isMinimized()) {
            existing_window->showNormal();
        }
        existing_window->show();
        existing_window->raise();
        existing_window->activateWindow();
    }, &application);
    QString instance_error;
    const auto instance_result = instance.Start(&instance_error);
    if (instance_result == nlsi::app::SingleInstance::StartResult::AlreadyRunning) {
        QString activation_error;
        const bool activated = instance.NotifyExistingInstance(&activation_error);
        QMessageBox::information(nullptr,
            QStringLiteral("NLSI Exclusive Logbook is already running"),
            activated
                ? QStringLiteral("The existing NLSI Exclusive Logbook window has been notified.")
                : QStringLiteral("The existing application is running, but its window could not "
                    "be activated automatically. Please switch to it manually. %1")
                    .arg(activation_error));
        return 0;
    }
    if (instance_result == nlsi::app::SingleInstance::StartResult::Failed) {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"), instance_error);
        return 1;
    }

    QFile stylesheet(QStringLiteral(":/styles/app.qss"));
    if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"),
            QStringLiteral("Could not load the application stylesheet."));
        return 1;
    }

    QTextStream stylesheet_stream(&stylesheet);
    application.setStyleSheet(stylesheet_stream.readAll());

    Initialize();
    nlsi::gui::MainWindow main_window(product_name_, version_label_, telemetry_core_);
    existing_window = &main_window;
    main_window.show();

    const int result = application.exec();
    Shutdown();
    return result;
}

std::wstring App::ProductName() const {
    return product_name_;
}

std::wstring App::VersionLabel() const {
    return version_label_;
}

void App::Initialize() {
    const QString user_data_path = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (user_data_path.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Startup error"),
            QStringLiteral("Windows did not provide a writable application data directory."));
        return;
    }
    telemetry_core_.Initialize(user_data_path.toStdWString());
}

void App::Shutdown() {
    telemetry_core_.Shutdown();
}
