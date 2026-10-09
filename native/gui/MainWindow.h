#pragma once

#include <QHash>
#include <QMainWindow>

#include <string>

#include "PageSupport.h"
#include "telemetry/TelemetryCore.h"

class QStackedWidget;
class QScrollArea;
class QToolButton;
class QTimer;
class QLabel;

namespace nlsi::gui {

class MainWindow final : public QMainWindow {
public:
    MainWindow(
        const std::wstring& title,
        const std::wstring& version,
        telemetry::TelemetryCore& telemetry_core,
        QWidget* parent = nullptr);

private:
    void ActivatePage(const QString& key);
    void RefreshState();
    void RefreshClock();

    telemetry::TelemetryCore& telemetry_core_;
    QStackedWidget* page_stack_ = nullptr;
    QHash<QString, QScrollArea*> page_scroll_areas_;
    QLabel* active_page_title_ = nullptr;
    QLabel* active_page_subtitle_ = nullptr;
    QLabel* header_clock_ = nullptr;
    QLabel* connection_indicator_ = nullptr;
    QHash<QString, StatePage*> pages_;
    QHash<QString, QToolButton*> navigation_buttons_;
    QHash<QString, QString> page_subtitles_;
    QTimer* refresh_timer_ = nullptr;
    QTimer* clock_timer_ = nullptr;
};

} // namespace nlsi::gui
