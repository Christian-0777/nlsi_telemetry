#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QSize>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableView>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QWidget>

#include <iostream>

#include "gui/JobHistoryPage.h"
#include "gui/JobPdfExporter.h"
#include "gui/LiveDrivePage.h"
#include "gui/MainWindow.h"
#include "gui/PageSupport.h"
#include "gui/ProvidersPage.h"
#include "gui/AboutPage.h"
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

bool TestHistoryPagesLoadPersistedRows() {
    nlsi::gui::HistoryPage history_page;
    nlsi::session::HistorySnapshot history;
    history.revision = 1;
    history.sessions.push_back({
        QStringLiteral("session-1"),
        QStringLiteral("ets2"),
        QStringLiteral("2026-10-07T08:00:00Z"),
        QStringLiteral("2026-10-07T08:30:00Z"),
        QStringLiteral("gameplay_ended"),
    });
    history.jobs.push_back({
        QStringLiteral("job-1"),
        QStringLiteral("Furniture"),
        QStringLiteral("Berlin"),
        QStringLiteral("Paris"),
        QStringLiteral("Delivered"),
        QStringLiteral("2026-10-07T08:29:00Z"),
    });
    history_page.UpdateState({});
    history_page.UpdateHistory(history);

    auto* tabs = history_page.findChild<QTabWidget*>(QStringLiteral("contentTabs"));
    if (!tabs || tabs->count() != 2) {
        std::cerr << "Session and completed-job history tabs are missing.\n";
        return false;
    }
    QTableView* sessions_table = nullptr;
    QTableView* jobs_table = nullptr;
    for (auto* table : history_page.findChildren<QTableView*>()) {
        auto* model = qobject_cast<QStandardItemModel*>(table->model());
        if (!model || model->rowCount() != 1) {
            continue;
        }
        const QString first_column = model->headerData(0, Qt::Horizontal).toString();
        if (first_column == QStringLiteral("Game")) {
            sessions_table = table;
        } else if (first_column == QStringLiteral("Timestamp")) {
            jobs_table = table;
        }
    }
    if (!sessions_table || !jobs_table) {
        std::cerr << "Persisted session or completed-job records did not load into their tables.\n";
        return false;
    }
    if (!history_page.findChildren<QLabel*>(QStringLiteral("pageTitle")).isEmpty()
        || !history_page.findChildren<QLabel*>(QStringLiteral("pageDescription")).isEmpty()) {
        std::cerr << "History tabs contain redundant page headings below the shared header.\n";
        return false;
    }
    tabs->setCurrentIndex(1);
    if (tabs->currentIndex() != 1) {
        std::cerr << "The completed-job history tab could not be selected.\n";
        return false;
    }
    tabs->setCurrentIndex(0);
    if (tabs->currentIndex() != 0) {
        std::cerr << "The session history tab could not be selected.\n";
        return false;
    }

    nlsi::gui::EventsPage events_page;
    history.events.push_back({
        QStringLiteral("2026-10-07T08:29:00Z"),
        QStringLiteral("NLSI"),
        QStringLiteral("job.delivered"),
        QStringLiteral("{\"cargo\":\"Furniture\"}"),
    });
    ++history.revision;
    events_page.UpdateHistory(history);
    const auto event_tables = events_page.findChildren<QTableView*>();
    if (event_tables.size() != 1
        || !event_tables.front()->model()
        || event_tables.front()->model()->rowCount() != 1) {
        std::cerr << "Persisted event details did not load into the Events table.\n";
        return false;
    }
    return true;
}

bool TestNumberAndTimeFormatting() {
    nlsi::telemetry::TelemetryField<double> amount;
    amount.Set(123456.789, L"TruckSim GPS", L"sample");
    if (nlsi::gui::NumberText(amount) != QStringLiteral("123,456.79")
        || nlsi::gui::NumberText(amount, 0) != QStringLiteral("123,457")
        || nlsi::gui::NumericText(QStringLiteral("25000")) != QStringLiteral("25,000")
        || nlsi::gui::NumericText(QStringLiteral("25000.5")) != QStringLiteral("25,000.50")
        || nlsi::gui::TimestampText(QStringLiteral("2026-10-07T08:36:46.123Z"))
            != QStringLiteral("10/07/26 16:36:46.123")
        || nlsi::gui::DurationText(3661.25) != QStringLiteral("01:01:01.250")) {
        std::cerr << "Locale-independent number, date, or time formatting is incorrect.\n";
        return false;
    }
    return true;
}

bool TestProviderSurfaceSelectsTruckSimOnly() {
    nlsi::gui::ProvidersPage providers_page;
    nlsi::telemetry::TelemetryUiState state;
    state.providers.trucksim = nlsi::telemetry::ProviderState::Connected;
    providers_page.UpdateState(state);
    const auto labels = providers_page.findChildren<QLabel*>();
    bool found_trucksim = false;
    for (const QLabel* label : labels) {
        const QString text = label->text();
        found_trucksim = found_trucksim || text == QStringLiteral("TruckSim GPS");
        if (text.contains(QStringLiteral("RenCloud"), Qt::CaseInsensitive)
            || text == QStringLiteral("NLSI")) {
            std::cerr << "An inactive telemetry provider remains exposed in Settings.\n";
            return false;
        }
    }
    if (!found_trucksim) {
        std::cerr << "TruckSim GPS is not the active provider in Settings.\n";
        return false;
    }
    return true;
}

bool TestTruckControlsReportAvailability() {
    nlsi::gui::LiveDrivePage page;
    nlsi::telemetry::TelemetryUiState state;
    page.UpdateState(state);
    bool cruise_unavailable = false;
    bool retarder_unavailable = false;
    for (const QLabel* label : page.findChildren<QLabel*>(QStringLiteral("detailValue"))) {
        if (label->text() == QStringLiteral("Unavailable")) {
            const auto* row = label->parentWidget();
            const auto* name = row ? row->findChild<QLabel*>(QStringLiteral("detailLabel")) : nullptr;
            if (name && name->text() == QStringLiteral("Cruise control")) {
                cruise_unavailable = true;
            } else if (name && name->text() == QStringLiteral("Retarder")) {
                retarder_unavailable = true;
            }
        }
    }
    if (!cruise_unavailable || !retarder_unavailable) {
        std::cerr << "Missing cruise or retarder data was not made explicit.\n";
        return false;
    }

    state.fast.values.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    state.fast.values.cruise_control_speed.Set(88.5, L"TruckSim GPS", L"sample");
    state.fast.values.retarder_active.Set(true, L"TruckSim GPS", L"sample");
    state.fast.values.retarder_level.Set(2.0, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    bool cruise_enabled = false;
    bool retarder_level = false;
    for (const QLabel* label : page.findChildren<QLabel*>(QStringLiteral("detailValue"))) {
        cruise_enabled = cruise_enabled
            || label->text() == QStringLiteral("Enabled · set to 88.50 km/h");
        retarder_level = retarder_level
            || label->text() == QStringLiteral("Enabled · level 2");
    }
    if (!cruise_enabled || !retarder_level) {
        std::cerr << "Available TruckSim cruise or retarder data was not formatted correctly.\n";
        return false;
    }
    return true;
}

bool TestCompletedJobsPdfExport() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "Temporary PDF export directory could not be created.\n";
        return false;
    }
    nlsi::session::JobRecord job{
        QStringLiteral("job-123"),
        QStringLiteral("Furniture"),
        QStringLiteral("Berlin Logistics · Berlin"),
        QStringLiteral("Paris Freight · Paris"),
        QStringLiteral("Delivered"),
        QStringLiteral("2026-10-07T08:36:46.123Z"),
        {{QStringLiteral("income"), QStringLiteral("25000")},
            {QStringLiteral("planned_distance_km"), QStringLiteral("1200")},
            {QStringLiteral("odometer_km"), 65000.5}},
    };
    const QString original_details = QString::fromUtf8(
        QJsonDocument(job.details).toJson(QJsonDocument::Compact));
    const QString path = directory.filePath(QStringLiteral("completed-jobs.pdf"));
    QString error;
    if (!nlsi::gui::ExportJobsToPdf(path, {job}, &error)) {
        std::cerr << "Completed jobs PDF export failed: " << error.toStdString() << '\n';
        return false;
    }
    QFile pdf(path);
    if (!pdf.open(QIODevice::ReadOnly)
        || !pdf.read(5).startsWith("%PDF-")
        || QFileInfo(path).size() < 1000
        || QString::fromUtf8(QJsonDocument(job.details).toJson(QJsonDocument::Compact))
            != original_details) {
        std::cerr << "PDF output is invalid or export modified the source job record.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    QFile stylesheet(QStringLiteral(":/styles/app.qss"));
    if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "The application stylesheet could not be loaded for UI tests.\n";
        return 1;
    }
    application.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    application.setApplicationVersion(QStringLiteral("v1.3.9-beta"));
    if (!TestHistoryPagesLoadPersistedRows()) {
        return 1;
    }
    if (!TestNumberAndTimeFormatting()
        || !TestProviderSurfaceSelectsTruckSimOnly()
        || !TestTruckControlsReportAvailability()
        || !TestCompletedJobsPdfExport()) {
        return 1;
    }
    nlsi::telemetry::TelemetryCore telemetry_core;
    nlsi::gui::MainWindow window(L"NLSI Exclusive Logbook", L"v1.3.9-beta",
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
    auto* sidebar = window.findChild<QFrame*>(QStringLiteral("sidebar"));
    auto* page_title = window.findChild<QLabel*>(QStringLiteral("headerTitle"));
    auto* page_subtitle = window.findChild<QLabel*>(QStringLiteral("headerSubtitle"));
    auto* page_stack = window.findChild<QStackedWidget*>(QStringLiteral("pageStack"));
    auto* header_clock = window.findChild<QLabel*>(QStringLiteral("headerClock"));
    if (!logo || !brand_title || !brand_subtitle || !sidebar ||
        !page_title || !page_subtitle || !page_stack || !header_clock) {
        std::cerr << "The brand or shared page stack is missing.\n";
        return 1;
    }
    if (!window.findChildren<QLabel*>(QStringLiteral("versionBadge")).isEmpty()) {
        std::cerr << "A version badge is still present in the page header.\n";
        return 1;
    }
    const QRegularExpression clock_pattern(
        QStringLiteral("^\\d{2}/\\d{2}/\\d{2} - \\d{2}:\\d{2}:\\d{2}\\.\\d{3}"
            " \\| Asia/Manila \\| Ping: N/A ms$"));
    if (!clock_pattern.match(header_clock->text()).hasMatch()) {
        std::cerr << "The Manila millisecond clock or unavailable ping display is incorrect.\n";
        return 1;
    }
    if (brand_title->text() != QStringLiteral("NABSKI") ||
        brand_subtitle->text() != QStringLiteral("Logistics Solutions Inc.") ||
        logo->geometry().right() >= brand_title->geometry().left() ||
        brand_subtitle->geometry().right() >= sidebar->width() ||
        !brand_subtitle->wordWrap() ||
        brand_subtitle->fontMetrics().horizontalAdvance(QStringLiteral("Logistics"))
            > brand_subtitle->contentsRect().width() ||
        brand_subtitle->fontMetrics().horizontalAdvance(QStringLiteral("Solutions"))
            > brand_subtitle->contentsRect().width() ||
        brand_title->geometry().top() >= brand_subtitle->geometry().top() ||
        qAbs(logo->geometry().center().y() -
             (brand_title->geometry().top() + brand_subtitle->geometry().bottom()) / 2) > 3) {
        std::cerr << "The sidebar branding or fit is incorrect: title="
                  << brand_title->text().toStdString() << " subtitle="
                  << brand_subtitle->text().toStdString() << " logo-right="
                  << logo->geometry().right() << " title-left="
                  << brand_title->geometry().left() << " subtitle-right="
                  << brand_subtitle->geometry().right() << " sidebar-width="
                  << sidebar->width() << " subtitle-label-width="
                  << brand_subtitle->contentsRect().width() << " word-widths="
                  << brand_subtitle->fontMetrics().horizontalAdvance(QStringLiteral("Logistics"))
                  << ','
                  << brand_subtitle->fontMetrics().horizontalAdvance(QStringLiteral("Solutions"))
                  << '\n';
        return 1;
    }
    auto* about_button = FindButton(window, QStringLiteral("About"));
    if (!about_button) {
        std::cerr << "The About navigation item is missing.\n";
        return 1;
    }
    about_button->click();
    application.processEvents();
    bool found_discord = false;
    bool found_ceo = false;
    bool found_developer = false;
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        const QString text = label->text();
        found_discord = found_discord || text.contains(QStringLiteral("https://discord.gg/gerAGTS6YB"));
        found_ceo = found_ceo || text.contains(QStringLiteral("https://www.tiktok.com/@nabskiplays"));
        found_developer = found_developer
            || text.contains(QStringLiteral("https://www.tiktok.com/@kape_073"));
    }
    if (!found_discord || !found_ceo || !found_developer) {
        std::cerr << "The About page is missing a required social link.\n";
        return 1;
    }
    bool found_company = false;
    bool found_version = false;
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        found_company = found_company
            || label->text() == QStringLiteral("Nabski Logistics and Solutions Inc.");
        found_version = found_version
            || label->text() == QStringLiteral("v1.3.9-beta");
    }
    if (!found_company || !found_version) {
        std::cerr << "The About page is missing its company name or current version.\n";
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
    const QList<QString> page_subtitles = {
        QStringLiteral("Live telemetry and current driving status."),
        QStringLiteral("Detailed vehicle telemetry while you drive."),
        QStringLiteral("Current delivery details and completed job records."),
        QStringLiteral("Recorded sessions, trips, and completed deliveries."),
        QStringLiteral("Provider events and recorded event details."),
        QStringLiteral("Application preferences and provider diagnostics."),
        QStringLiteral("Product information, release details, and acknowledgements."),
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
            page_subtitle->text() != page_subtitles[index] ||
            !button->isChecked()) {
            std::cerr << "Navigation did not activate the expected page: "
                      << page_titles[index].toStdString() << '\n';
            return 1;
        }
        if (!window.findChildren<QLabel*>(QStringLiteral("pageTitle")).isEmpty()
            || !window.findChildren<QLabel*>(QStringLiteral("pageDescription")).isEmpty()) {
            std::cerr << "A page contains a duplicate title or subtitle: "
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
        const bool should_scroll_vertically = size.height() == 600;
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
