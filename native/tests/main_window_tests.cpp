#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QMainWindow>
#include <QScrollArea>
#include <QSize>
#include <QScrollBar>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

#include <iostream>

#include "gui/MainWindow.h"
#include "telemetry/TelemetryCore.h"

namespace {

bool CheckLayout(QMainWindow& window) {
    auto* sidebar = window.findChild<QFrame*>(QStringLiteral("sidebar"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("pageStack"));
    if (!sidebar || !stack || stack->width() <= 0 || stack->height() <= 0 ||
        sidebar->height() != window.centralWidget()->height()) {
        std::cerr << "Invalid main content geometry: sidebar="
                  << (sidebar ? sidebar->width() : -1) << 'x'
                  << (sidebar ? sidebar->height() : -1) << ", stack="
                  << (stack ? stack->width() : -1) << 'x'
                  << (stack ? stack->height() : -1) << ".\n";
        return false;
    }

    const auto buttons = sidebar->findChildren<QToolButton*>(
        QStringLiteral("navigationButton"));
    int navigation_width = -1;
    for (const auto* button : buttons) {
        const QRect bounds(button->mapTo(sidebar, QPoint(0, 0)), button->size());
        if (button->width() <= 0 || button->height() <= 0 ||
            !sidebar->rect().contains(bounds) ||
            bounds.left() != sidebar->contentsRect().left() ||
            (navigation_width >= 0 && button->width() != navigation_width)) {
            std::cerr << "Invalid navigation geometry: " << button->text().toStdString()
                      << " at " << bounds.x() << ',' << bounds.y() << ' '
                      << bounds.width() << 'x' << bounds.height()
                      << ", sidebar contents x=" << sidebar->contentsRect().x()
                      << " width=" << sidebar->contentsRect().width()
                      << ", prior navigation width=" << navigation_width << ".\n";
            return false;
        }
        navigation_width = button->width();
    }
    return true;
}

QToolButton* FindButton(QWidget& window, const QString& title) {
    const auto buttons = window.findChildren<QToolButton*>(
        QStringLiteral("navigationButton"));
    for (auto* button : buttons) {
        if (button->text() == title) {
            return button;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    nlsi::telemetry::TelemetryCore telemetry_core;
    nlsi::gui::MainWindow window(L"NLSI Exclusive Logbook", L"v1.3.4 Alpha",
        telemetry_core);

    if (window.size() != QSize(900, 600) ||
        window.minimumSize() != QSize(900, 600)) {
        std::cerr << "The default or minimum window size is not 900x600.\n";
        return 1;
    }

    window.show();
    application.processEvents();

    auto* logo = window.findChild<QLabel*>(QStringLiteral("brandLogo"));
    auto* brand_title = window.findChild<QLabel*>(QStringLiteral("brandTitle"));
    auto* brand_subtitle = window.findChild<QLabel*>(QStringLiteral("brandSubtitle"));
    auto* version_badge = window.findChild<QLabel*>(QStringLiteral("versionBadge"));
    auto* page_title = window.findChild<QLabel*>(QStringLiteral("headerTitle"));
    auto* page_stack = window.findChild<QStackedWidget*>(QStringLiteral("pageStack"));
    if (!logo || !brand_title || !brand_subtitle || !version_badge ||
        !page_title || !page_stack) {
        std::cerr << "The brand, Alpha version badge, or shared page stack is missing.\n";
        return 1;
    }
    if (brand_title->text() != QStringLiteral("NLSI") ||
        brand_subtitle->text() != QStringLiteral("Exclusive Logbook") ||
        version_badge->text() != QStringLiteral("v1.3.4 Alpha") ||
        logo->geometry().right() >= brand_title->geometry().left() ||
        brand_title->geometry().top() >= brand_subtitle->geometry().top() ||
        qAbs(logo->geometry().center().y() -
             (brand_title->geometry().top() + brand_subtitle->geometry().bottom()) / 2) > 3) {
        std::cerr << "The horizontal brand header or Alpha identity is incorrect.\n";
        return 1;
    }

    const QList<QString> page_titles = {
        QStringLiteral("Dashboard"),
        QStringLiteral("Live Drive"),
        QStringLiteral("Jobs"),
        QStringLiteral("History"),
        QStringLiteral("Events"),
        QStringLiteral("Settings"),
        QStringLiteral("About"),
    };
    for (qsizetype index = 0; index < page_titles.size(); ++index) {
        auto* button = FindButton(window, page_titles[index]);
        if (!button) {
            std::cerr << "Missing navigation item: "
                      << page_titles[index].toStdString() << '\n';
            return 1;
        }
        if (button->icon().isNull()) {
            std::cerr << "Navigation item has no icon: "
                      << page_titles[index].toStdString() << '\n';
            return 1;
        }
        button->click();
        application.processEvents();
        if (page_stack->currentIndex() != index ||
            page_title->text() != page_titles[index] ||
            !button->isChecked()) {
            std::cerr << "Navigation did not activate the expected page: "
                      << page_titles[index].toStdString() << '\n';
            return 1;
        }
    }

    for (const QSize size : {QSize(900, 600), QSize(1280, 800), QSize(1920, 1080)}) {
        window.showNormal();
        window.resize(size);
        FindButton(window, QStringLiteral("Dashboard"))->click();
        application.processEvents();
        if (!CheckLayout(window)) {
            std::cerr << "Sidebar or page layout failed at "
                      << size.width() << 'x' << size.height() << ".\n";
            return 1;
        }
        auto* dashboard_scroll = qobject_cast<QScrollArea*>(page_stack->currentWidget());
        if (!dashboard_scroll ||
            dashboard_scroll->verticalScrollBar()->isVisible() ||
            dashboard_scroll->horizontalScrollBar()->isVisible()) {
            std::cerr << "The Dashboard shows unnecessary scrollbars at "
                      << size.width() << 'x' << size.height() << ".\n";
            return 1;
        }

        FindButton(window, QStringLiteral("Live Drive"))->click();
        application.processEvents();
        auto* live_drive_scroll = qobject_cast<QScrollArea*>(page_stack->currentWidget());
        const bool should_scroll_vertically = size == QSize(900, 600);
        if (!live_drive_scroll ||
            live_drive_scroll->verticalScrollBar()->isVisible() != should_scroll_vertically ||
            live_drive_scroll->horizontalScrollBar()->isVisible()) {
            std::cerr << "Live Drive scrolling does not match its content at "
                      << size.width() << 'x' << size.height() << ".\n";
            return 1;
        }
    }

    window.showMaximized();
    FindButton(window, QStringLiteral("Dashboard"))->click();
    application.processEvents();
    if (!CheckLayout(window)) {
        std::cerr << "Sidebar or page layout failed when maximized.\n";
        return 1;
    }

    auto* close_button = FindButton(window, QStringLiteral("Close"));
    if (!close_button) {
        std::cerr << "The bottom Close action is missing.\n";
        return 1;
    }
    if (close_button->icon().isNull()) {
        std::cerr << "The Close action has no icon.\n";
        return 1;
    }
    close_button->click();
    if (window.isVisible()) {
        std::cerr << "The Close action did not close the window.\n";
        return 1;
    }

    std::cout << "Window size, full-width navigation, and responsive scrolling checks passed.\n";
    return 0;
}
