#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QColor>
#include <QCloseEvent>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFrame>
#include <QLabel>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QSize>
#include <QStandardPaths>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableView>
#include <QTabWidget>
#include <QToolButton>
#include <QPushButton>
#include <QTimer>
#include <QTemporaryDir>
#include <QDir>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <atomic>
#include <iostream>
#include <thread>

#include "gui/JobHistoryPage.h"
#include "gui/JobPdfExporter.h"
#include "gui/DashboardPage.h"
#include "gui/MainWindow.h"
#include "gui/PageSupport.h"
#include "gui/ProvidersPage.h"
#include "gui/AboutPage.h"
#include "app/SingleInstance.h"
#include "telemetry/TelemetryCore.h"
#include "updater/GitHubUpdater.h"

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

QAbstractButton* FindMessageBoxButton(QMessageBox& message_box, const QString& text) {
    for (QAbstractButton* button : message_box.buttons()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

QLabel* FindDashboardCardValue(
    nlsi::gui::DashboardPage& page,
    const QString& title) {
    for (QLabel* title_label : page.findChildren<QLabel*>(QStringLiteral("cardTitle"))) {
        if (title_label->text() == title && title_label->parentWidget()) {
            return title_label->parentWidget()->findChild<QLabel*>(
                QStringLiteral("cardValue"));
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
    if (!tabs || tabs->count() != 3) {
        std::cerr << "Session, completed-job, and trip-event history tabs are missing.\n";
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
        } else if (first_column == QStringLiteral("Job ID")) {
            jobs_table = table;
        }
    }
    if (!sessions_table || !jobs_table
        || jobs_table->model()->headerData(2, Qt::Horizontal).toString()
            != QStringLiteral("Timestamp")
        || jobs_table->model()->index(0, 0).data().toString() != QStringLiteral("job-1")) {
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
        QStringLiteral("2026-10-07T08:25:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("toll_gate"),
        QStringLiteral("{\"job_id\":\"game-job-17\",\"toll_fee\":12.5,\"currency\":\"EUR\"}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:27:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("transport"),
        QStringLiteral("{\"transport_type\":\"ferry\",\"trip_id\":\"trip-1\"}"),
    });
    history.jobs.front().identity = QStringLiteral("game-job-17");
    history.jobs.front().nlsi_job_id = QStringLiteral("JOB-NLSI-0001");
    history.jobs.front().details = {{QStringLiteral("job_id"), QStringLiteral("game-job-17")}};
    ++history.revision;
    history_page.UpdateHistory(history);
    events_page.UpdateHistory(history);
    const auto event_tables = events_page.findChildren<QTableView*>();
    if (event_tables.size() != 1
        || !event_tables.front()->model()
        || event_tables.front()->model()->rowCount() != 2) {
        std::cerr << "Persisted event details did not load into the Events table.\n";
        return false;
    }
    tabs->setCurrentIndex(2);
    QTableView* trip_events_table = nullptr;
    for (auto* table : history_page.findChildren<QTableView*>()) {
        auto* model = qobject_cast<QStandardItemModel*>(table->model());
        if (model && model->horizontalHeaderItem(0)
            && model->horizontalHeaderItem(0)->text() == QStringLiteral("Event type")) {
            trip_events_table = table;
        }
    }
    auto* trip_model = trip_events_table
        ? qobject_cast<QStandardItemModel*>(trip_events_table->model())
        : nullptr;
    if (!trip_model || trip_model->rowCount() != 2
        || trip_model->index(0, 0).data().toString() != QStringLiteral("Toll gate")
        || trip_model->index(0, 1).data().toString() != QStringLiteral("12.5")
        || trip_model->index(0, 2).data().toString() != QStringLiteral("EUR")
        || trip_model->index(0, 3).data().toString() != QStringLiteral("JOB-NLSI-0001")
        || trip_model->index(1, 0).data().toString() != QStringLiteral("Ferry")
        || trip_model->index(1, 1).data().toString() != QStringLiteral("Unavailable")
        || trip_model->index(1, 2).data().toString() != QStringLiteral("Unavailable")
        || trip_model->index(1, 3).data().toString() != QStringLiteral("trip-1")) {
        std::cerr << "Explicit toll-event details or their recorded job association were lost.\n";
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
    nlsi::gui::ActiveModsPage active_mods_page;
    active_mods_page.UpdateState(state);
    bool mods_unavailable = false;
    for (const QLabel* label : active_mods_page.findChildren<QLabel*>()) {
        mods_unavailable = mods_unavailable
            || (label->text().contains(QStringLiteral("unavailable"), Qt::CaseInsensitive)
                && label->text().contains(QStringLiteral("complete mod list"),
                    Qt::CaseInsensitive));
    }
    if (!mods_unavailable) {
        std::cerr << "Active Mods does not clearly report its unsupported telemetry source.\n";
        return false;
    }
    return true;
}

bool TestTruckControlsReportAvailability() {
    nlsi::gui::DashboardPage page;
    nlsi::telemetry::TelemetryUiState state;
    page.UpdateState(state);
    bool controls_hidden = false;
    for (const QLabel* title : page.findChildren<QLabel*>(QStringLiteral("cardTitle"))) {
        controls_hidden = controls_hidden
            || (title->text() == QStringLiteral("VEHICLE CONTROLS")
                && title->parentWidget()->isHidden());
    }
    if (!controls_hidden) {
        std::cerr << "Unverified cruise and retarder controls were shown.\n";
        return false;
    }

    state.fast.values.connected = true;
    state.fast.values.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    state.fast.values.cruise_control_speed.Set(88.5, L"TruckSim GPS", L"sample");
    state.fast.values.retarder_level.Set(2.0, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    QLabel* controls = FindDashboardCardValue(page, QStringLiteral("VEHICLE CONTROLS"));
    if (!controls || controls->text()
        != QStringLiteral("Cruise: Enabled · 88.5 km/h\nRetarder: Active · level 2")) {
        std::cerr << "Available TruckSim cruise or retarder data was not formatted correctly.\n";
        return false;
    }

    state.fast.values.retarder_level.Set(0.0, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    if (!controls->text().contains(QStringLiteral("Retarder: Inactive · level 0"))) {
        std::cerr << "Retarder status was not derived from the decoded zero level.\n";
        return false;
    }

    state.fast.values.retarder_level.Set(1.5, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    if (controls->text().contains(QStringLiteral("Retarder:"))) {
        std::cerr << "An invalid non-integer retarder level was presented as active.\n";
        return false;
    }

    state.fast.values.retarder_level.Set(2.0, L"TruckSim GPS", L"sample");
    state.fast.values.retarder_level.MarkStale();
    page.UpdateState(state);
    if (controls->text().contains(QStringLiteral("Retarder:"))) {
        std::cerr << "Stale retarder telemetry remained active on the Dashboard.\n";
        return false;
    }

    state.fast.values.connected = false;
    state.fast.values.retarder_level.stale = false;
    page.UpdateState(state);
    if (!controls->parentWidget()->isHidden()) {
        std::cerr << "Disconnected retarder telemetry remained active on the Dashboard.\n";
        return false;
    }
    return true;
}

bool TestDashboardContainsTransferredJobAndNavigationDetails() {
    nlsi::gui::DashboardPage page;
    nlsi::telemetry::TelemetryUiState state;
    auto& values = state.fast.values;
    values.connected = true;
    state.providers.trucksim = nlsi::telemetry::ProviderState::Connected;
    values.game_name.Set(L"Euro Truck Simulator 2", L"TruckSim GPS", L"sample");
    values.speed_kmh.Set(72.0, L"TruckSim GPS", L"sample");
    values.rpm.Set(1500.0, L"TruckSim GPS", L"sample");
    values.gear.Set(6.0, L"TruckSim GPS", L"sample");
    values.fuel_liters.Set(500.0, L"TruckSim GPS", L"sample");
    values.fuel_range_km.Set(800.0, L"TruckSim GPS", L"sample");
    values.odometer_km.Set(65000.0, L"TruckSim GPS", L"sample");
    values.income.Set(L"25000", L"TruckSim GPS", L"sample");
    values.planned_distance.Set(L"1200", L"TruckSim GPS", L"sample");
    values.delivery_time.Set(L"10/09/26", L"TruckSim GPS", L"sample");
    values.navigation_distance_km.Set(100.0, L"TruckSim GPS", L"sample");
    values.navigation_time_s.Set(3600.0, L"TruckSim GPS", L"sample");
    values.effective_throttle.Set(0.25, L"TruckSim GPS", L"sample");
    values.effective_brake.Set(0.05, L"TruckSim GPS", L"sample");
    values.special_job.Set(L"true", L"TruckSim GPS", L"sample");
    state.job.available = true;
    state.job.nlsi_job_id = L"JOB-NLSI-0042";
    state.job.cargo.Set(L"Furniture", L"TruckSim GPS", L"sample");
    state.job.cargo_id.Set(L"game-job-17", L"TruckSim GPS", L"sample");
    state.job.source_city.Set(L"Berlin", L"TruckSim GPS", L"sample");
    state.job.destination_city.Set(L"Paris", L"TruckSim GPS", L"sample");
    state.job.loaded.Set(true, L"TruckSim GPS", L"sample");
    state.job_status = nlsi::telemetry::JobStatus::InTransit;
    state.progress.progress_percent = 25.0;
    state.progress.remaining_distance_km = 100.0;
    state.progress.eta_seconds = 3661.0;
    state.session.status = nlsi::telemetry::SessionStatus::Driving;
    page.UpdateState(state);

    const QStringList expected_titles = {
        QStringLiteral("CONNECTION / GAME / PROVIDER"), QStringLiteral("SPEED"),
        QStringLiteral("RPM / GEAR"), QStringLiteral("FUEL / RANGE / ODOMETER"),
        QStringLiteral("THROTTLE"), QStringLiteral("BRAKE"),
        QStringLiteral("CURRENT JOB"), QStringLiteral("NAVIGATION"),
        QStringLiteral("VEHICLE CONTROLS"),
    };
    const auto titles = page.findChildren<QLabel*>(QStringLiteral("cardTitle"));
    if (titles.size() != expected_titles.size()) {
        std::cerr << "Dashboard does not contain the full consolidated set of telemetry cards.\n";
        return false;
    }
    bool session_summary_present = false;
    for (const QLabel* title : titles) {
        session_summary_present = session_summary_present
            || title->text() == QStringLiteral("SESSION SUMMARY");
    }
    for (const QString& title : expected_titles) {
        if (!FindDashboardCardValue(page, title)) {
            std::cerr << "Dashboard is missing transferred telemetry card: "
                      << title.toStdString() << '\n';
            return false;
        }
    }
    const QLabel* job = FindDashboardCardValue(page, QStringLiteral("CURRENT JOB"));
    const QLabel* navigation = FindDashboardCardValue(page, QStringLiteral("NAVIGATION"));
    if (!job || !navigation
        || !job->text().contains(QStringLiteral("JOB-NLSI-0042"))
        || !job->text().contains(QStringLiteral("Furniture"))
        || !job->text().contains(QStringLiteral("Berlin"))
        || !job->text().contains(QStringLiteral("Paris"))
        || !job->text().contains(QStringLiteral("Special job: Yes"))
        || !navigation->text().contains(QStringLiteral("100.00 km"))
        || !navigation->text().contains(QStringLiteral("Navigation time: 01:00:00.000"))
        || !navigation->text().contains(QStringLiteral("01:01:01.000"))
        || FindDashboardCardValue(page, QStringLiteral("THROTTLE"))->text()
            != QStringLiteral("25.0%")
        || FindDashboardCardValue(page, QStringLiteral("BRAKE"))->text()
            != QStringLiteral("5.0%")
        || session_summary_present) {
        std::cerr << "Dashboard is missing current-job, delivery, or navigation progress details.\n";
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

bool TestSingleInstanceGuard(QApplication& application) {
    QTemporaryDir root;
    if (!root.isValid()) {
        std::cerr << "Single-instance temporary directory could not be created.\n";
        return false;
    }

    int activations = 0;
    QString error;
    {
        nlsi::app::SingleInstance primary(root.path(), [&activations] { ++activations; });
        nlsi::app::SingleInstance duplicate(root.path(), [] {});
        if (primary.Start(&error) != nlsi::app::SingleInstance::StartResult::Started) {
            std::cerr << "Primary instance failed to start: " << error.toStdString() << '\n';
            return false;
        }
        if (duplicate.Start(&error)
            != nlsi::app::SingleInstance::StartResult::AlreadyRunning) {
            std::cerr << "A repeated launch was not rejected as already running.\n";
            return false;
        }
        std::atomic<bool> notification_finished = false;
        bool notification_succeeded = false;
        std::thread notifier([&] {
            notification_succeeded = duplicate.NotifyExistingInstance(&error);
            notification_finished.store(true);
        });
        for (int attempt = 0; attempt < 2500 && !notification_finished.load(); ++attempt) {
            application.processEvents();
            QThread::msleep(1);
        }
        notifier.join();
        if (!notification_succeeded) {
            std::cerr << "A repeated launch could not notify the active instance: "
                      << error.toStdString() << '\n';
            return false;
        }
        application.processEvents();
        if (activations != 1) {
            std::cerr << "The active instance did not receive its activation request.\n";
            return false;
        }
    }

    {
        nlsi::app::SingleInstance restarted(root.path(), [] {});
        if (restarted.Start(&error) != nlsi::app::SingleInstance::StartResult::Started) {
            std::cerr << "The instance lock was not released after normal shutdown: "
                      << error.toStdString() << '\n';
            return false;
        }
    }
    const QString server_name = QStringLiteral("nlsi-exclusive-logbook-%1")
        .arg(QString::fromLatin1(QCryptographicHash::hash(
            root.path().toUtf8(), QCryptographicHash::Sha256).toHex()));
    const QString stale_lock_path = QDir(
        QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(server_name + QStringLiteral(".lock"));
    QFile stale_lock(stale_lock_path);
    const QByteArray stale_lock_contents =
        QByteArrayLiteral("9999999999\nstale-host\nstale-instance\n");
    if (!stale_lock.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || stale_lock.write(stale_lock_contents) != stale_lock_contents.size()) {
        std::cerr << "A stale lock fixture could not be written.\n";
        return false;
    }
    if (!stale_lock.setFileTime(
            QDateTime::currentDateTime().addSecs(-60), QFileDevice::FileModificationTime)) {
        std::cerr << "The stale lock fixture could not be aged.\n";
        return false;
    }
    stale_lock.close();
    nlsi::app::SingleInstance recovered(root.path(), [] {});
    if (recovered.Start(&error) != nlsi::app::SingleInstance::StartResult::Started) {
        std::cerr << "A stale single-instance lock was not recovered: "
                  << error.toStdString() << '\n';
        return false;
    }
    return true;
}

bool TestOfflineUpdateCheck() {
    nlsi::updater::GitHubUpdater updater(
        QStringLiteral("1.4.2-beta"),
        nullptr,
        QUrl(QStringLiteral("http://127.0.0.1:1/releases")));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    nlsi::updater::UpdateCheckResult result;
    bool completed = false;
    bool was_manual = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    updater.SetResultHandler([&](const auto& check_result, bool manual) {
        result = check_result;
        completed = true;
        was_manual = manual;
        loop.quit();
    });
    timeout.start(12000);
    updater.CheckForUpdates(true);
    if (!completed) {
        loop.exec();
    }
    if (!completed || !was_manual || result.succeeded || result.error.isEmpty()) {
        std::cerr << "An offline update request did not report a failure explicitly.\n";
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
    application.setApplicationVersion(QStringLiteral("v1.4.2-beta"));
    if (!TestHistoryPagesLoadPersistedRows()) {
        return 1;
    }
    if (!TestNumberAndTimeFormatting()
        || !TestProviderSurfaceSelectsTruckSimOnly()
        || !TestTruckControlsReportAvailability()
        || !TestDashboardContainsTransferredJobAndNavigationDetails()
        || !TestCompletedJobsPdfExport()) {
        return 1;
    }
    nlsi::telemetry::TelemetryCore telemetry_core;
    nlsi::gui::MainWindow window(L"NLSI Exclusive Logbook", L"v1.4.2-beta",
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
    auto* dashboard_button = FindButton(window, QStringLiteral("Dashboard"));
    auto* jobs_button = FindButton(window, QStringLiteral("Jobs"));
    if (!about_button) {
        std::cerr << "The About navigation item is missing.\n";
        return 1;
    }
    const auto icon_has_color = [](const QIcon& icon, const QColor& expected) {
        const QImage image = icon.pixmap(QSize(24, 24)).toImage();
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y) == expected) {
                    return true;
                }
            }
        }
        return false;
    };
    if (!dashboard_button || !jobs_button
        || !dashboard_button->isChecked()
        || !icon_has_color(dashboard_button->icon(), QColor(QStringLiteral("#FFFFFF")))) {
        std::cerr << "The selected Dashboard SVG icon is not white.\n";
        return 1;
    }
    jobs_button->click();
    application.processEvents();
    if (!jobs_button->isChecked()
        || !icon_has_color(jobs_button->icon(), QColor(QStringLiteral("#FFFFFF")))
        || !icon_has_color(dashboard_button->icon(), QColor(QStringLiteral("#F896B9")))) {
        std::cerr << "Navigation icon colors did not update immediately on selection.\n";
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
    bool found_beta_channel = false;
    bool found_product = false;
    for (const QLabel* label : window.findChildren<QLabel*>()) {
        found_company = found_company
            || label->text() == QStringLiteral("Nabski Logistics and Solutions Inc.");
        found_version = found_version
            || label->text() == QStringLiteral("v1.4.2-beta");
        found_beta_channel = found_beta_channel
            || label->text() == QStringLiteral("Beta");
        if (label->text() == QStringLiteral("Product") && label->parentWidget()) {
            QLabel* value = label->parentWidget()->findChild<QLabel*>(
                QStringLiteral("detailValue"));
            found_product = value
                && value->text() == QStringLiteral("NLSI Exclusive Logbook");
        }
    }
    if (!found_company || !found_version || !found_beta_channel || !found_product) {
        std::cerr << "The About page is missing current version/channel information.\n";
        return 1;
    }
    const auto detail_rows_fit = [](QWidget& page) {
        for (QFrame* row : page.findChildren<QFrame*>(QStringLiteral("detailRow"))) {
            QLabel* name = row->findChild<QLabel*>(QStringLiteral("detailLabel"));
            QLabel* value = row->findChild<QLabel*>(QStringLiteral("detailValue"));
            if (!name || !value || name->wordWrap() || value->wordWrap()
                || name->geometry().right() >= value->geometry().left()) {
                std::cerr << "Invalid detail row: "
                          << (name ? name->text().toStdString() : "<missing label>")
                          << " label-wrap=" << (name ? name->wordWrap() : true)
                          << " value-wrap=" << (value ? value->wordWrap() : true)
                          << " label-right=" << (name ? name->geometry().right() : -1)
                          << " value-left=" << (value ? value->geometry().left() : -1)
                          << " row-width=" << row->width() << '\n';
                return false;
            }
        }
        return true;
    };
    auto* about_scroll = qobject_cast<QScrollArea*>(page_stack->widget(5));
    QWidget* about_page = about_scroll ? about_scroll->widget() : nullptr;
    QLabel* product_value = nullptr;
    if (about_page) {
        for (QLabel* label : about_page->findChildren<QLabel*>()) {
            if (label->text() == QStringLiteral("Product") && label->parentWidget()) {
                product_value = label->parentWidget()->findChild<QLabel*>(
                    QStringLiteral("detailValue"));
                break;
            }
        }
    }
    if (!about_page || !detail_rows_fit(*about_page) || !product_value
        || product_value->fontMetrics().horizontalAdvance(product_value->text())
            > product_value->contentsRect().width()) {
        std::cerr << "About two-column rows wrap, overlap, or clip the product value.\n";
        return 1;
    }
    auto* settings_button = FindButton(window, QStringLiteral("Settings"));
    settings_button->click();
    application.processEvents();
    auto* settings_scroll = qobject_cast<QScrollArea*>(page_stack->widget(4));
    QWidget* settings_page = settings_scroll ? settings_scroll->widget() : nullptr;
    QTabWidget* settings_tabs = settings_page
        ? settings_page->findChild<QTabWidget*>(QStringLiteral("contentTabs"))
        : nullptr;
    if (!settings_tabs) {
        std::cerr << "Settings tabs are missing.\n";
        return 1;
    }
    for (int index = 0; index < settings_tabs->count(); ++index) {
        settings_tabs->setCurrentIndex(index);
        application.processEvents();
        if (!detail_rows_fit(*settings_tabs->currentWidget())) {
            std::cerr << "Settings two-column rows wrap or overlap at the minimum window size.\n";
            return 1;
        }
    }

    const QList<QString> page_titles = {
        QStringLiteral("Dashboard"),
        QStringLiteral("Jobs"),
        QStringLiteral("History"),
        QStringLiteral("Events"),
        QStringLiteral("Settings"),
        QStringLiteral("About"),
    };
    const QList<QString> page_subtitles = {
        QStringLiteral("Driving telemetry, current job, and navigation status."),
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
    bool cancel_prompt_verified = false;
    QTimer::singleShot(0, [&cancel_prompt_verified] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QAbstractButton* cancel = dialog
            ? FindMessageBoxButton(*dialog, QStringLiteral("Cancel"))
            : nullptr;
        if (dialog
            && dialog->text()
                == QStringLiteral("Are you sure you want to exit NLSI Exclusive Logbook?")
            && cancel) {
            cancel_prompt_verified = true;
            cancel->click();
        }
    });
    close_button->click();
    if (!window.isVisible() || !cancel_prompt_verified) {
        std::cerr << "Cancel did not leave the window open.\n";
        return 1;
    }
    bool exit_prompt_verified = false;
    QTimer::singleShot(0, [&exit_prompt_verified] {
        auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QAbstractButton* exit = dialog
            ? FindMessageBoxButton(*dialog, QStringLiteral("Exit"))
            : nullptr;
        if (dialog
            && dialog->text()
                == QStringLiteral("Are you sure you want to exit NLSI Exclusive Logbook?")
            && exit) {
            exit_prompt_verified = true;
            exit->click();
        }
    });
    close_button->click();
    if (window.isVisible() || !exit_prompt_verified) {
        std::cerr << "The explicit Exit action did not close the window.\n";
        return 1;
    }
    auto* update_button = window.findChild<QPushButton*>(
        QStringLiteral("checkForUpdatesButton"));
    auto* update_status = window.findChild<QLabel*>(
        QStringLiteral("updateCheckStatus"));
    if (!update_button || update_button->accessibleName() != QStringLiteral("Check for Updates")
        || !update_status
        || !update_status->text().startsWith(QStringLiteral("Update check failed:"))) {
        std::cerr << "About does not expose updates or reported a disabled check as successful.\n";
        return 1;
    }
    if (!TestSingleInstanceGuard(application) || !TestOfflineUpdateCheck()) {
        return 1;
    }

    std::cout << "Window size, full-width navigation, and responsive scrolling checks passed.\n";
    return 0;
}
