#include "App.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTextStream>

#include "gui/MainWindow.h"

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
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

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
