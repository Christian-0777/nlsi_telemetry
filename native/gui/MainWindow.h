#pragma once

#include <QHash>
#include <QElapsedTimer>
#include <QMainWindow>

#include <string>

#include "PageSupport.h"
#include "telemetry/TelemetryCore.h"

class QStackedWidget;
class QScrollArea;
class QToolButton;
class QTimer;
class QLabel;
class QCloseEvent;
class QResizeEvent;
class QProgressDialog;
class QSystemTrayIcon;

namespace nlsi::gui {

class MainWindow final : public QMainWindow {
public:
    MainWindow(
        const std::wstring& title,
        const std::wstring& version,
        telemetry::TelemetryCore& telemetry_core,
        QWidget* parent = nullptr);

private:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void ActivatePage(const QString& key);
    void RefreshState();
    void RefreshClock();
    void PollShutdown();

    telemetry::TelemetryCore& telemetry_core_;
    QStackedWidget* page_stack_ = nullptr;
    QHash<QString, QScrollArea*> page_scroll_areas_;
    QLabel* active_page_title_ = nullptr;
    QLabel* header_clock_ = nullptr;
    QLabel* connection_indicator_ = nullptr;
    QHash<QString, StatePage*> pages_;
    QHash<QString, QToolButton*> navigation_buttons_;
    QTimer* refresh_timer_ = nullptr;
    QTimer* clock_timer_ = nullptr;
    QTimer* shutdown_timer_ = nullptr;
    QProgressDialog* shutdown_dialog_ = nullptr;
    QSystemTrayIcon* notification_tray_ = nullptr;
    QElapsedTimer shutdown_wait_;
    bool shutdown_started_ = false;
    bool shutdown_complete_ = false;
};

} // namespace nlsi::gui
