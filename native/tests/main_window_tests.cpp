#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QColor>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QGridLayout>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QFrame>
#include <QLabel>
#include <QPlainTextEdit>
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
#include <QSet>
#include <QStandardPaths>
#include <QSettings>
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
#include <QUrlQuery>
#include <QWidget>

#include <atomic>
#include <algorithm>
#include <iostream>
#include <limits>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "gui/JobHistoryPage.h"
#include "gui/JobPdfExporter.h"
#include "gui/DashboardPage.h"
#include "gui/MainWindow.h"
#include "gui/ModLogParser.h"
#include "gui/PageSupport.h"
#include "gui/MeasurementUnits.h"
#include "gui/ProvidersPage.h"
#include "gui/AboutPage.h"
#include "app/SingleInstance.h"
#include "providers/ScsPositionIpc.h"
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
        if (button->text().compare(text, Qt::CaseInsensitive) == 0) {
            return button;
        }
    }
    return nullptr;
}

QLabel* FindDashboardCardValue(
    nlsi::gui::DashboardPage& page,
    const QString& title) {
    for (QLabel* title_label : page.findChildren<QLabel*>(QStringLiteral("cardTitle"))) {
        if (title_label->text() == title) {
            QWidget* card = title_label->parentWidget();
            while (card && card->objectName() != QStringLiteral("telemetryCard")) {
                card = card->parentWidget();
            }
            if (card) {
                return card->findChild<QLabel*>(QStringLiteral("cardValue"));
            }
        }
    }
    return nullptr;
}

QLabel* FindDashboardField(
    nlsi::gui::DashboardPage& page,
    const QString& key) {
    for (QLabel* label : page.findChildren<QLabel*>(QStringLiteral("dashboardValue"))) {
        if (label->property("fieldKey").toString() == key) {
            return label;
        }
    }
    return nullptr;
}

QLabel* FindDashboardCruiseIndicator(
    nlsi::gui::DashboardPage& page,
    const QString& key) {
    for (QLabel* label : page.findChildren<QLabel*>(
             QStringLiteral("cruiseControlActiveIndicator"))) {
        if (label->property("fieldKey").toString() == key) {
            return label;
        }
    }
    return nullptr;
}

bool WriteTextFile(const QString& path, const QByteArray& contents) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        return false;
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

bool WaitForModLogInitialization(nlsi::gui::ActiveModsPage& page, int timeout_ms = 5000) {
    QElapsedTimer elapsed;
    elapsed.start();
    const auto buttons = page.findChildren<QPushButton*>();
    while (elapsed.elapsed() < timeout_ms) {
        bool running = false;
        for (auto* button : buttons) {
            if (button->objectName().startsWith(QStringLiteral("reinitialize"))) {
                running = running || !button->isEnabled();
            }
        }
        if (!running) {
            QApplication::processEvents();
            return true;
        }

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(qMin(25, qMax(1, timeout_ms - static_cast<int>(elapsed.elapsed()))));
        loop.exec();
    }
    return false;
}

const nlsi::gui::modlog::WorkshopMod* FindMod(
    const nlsi::gui::modlog::GameLogResult& result,
    const QString& id) {
    for (const auto& mod : result.mods) {
        if (mod.id == id) {
            return &mod;
        }
    }
    return nullptr;
}

bool TestModLogParsingAndSourceLinks() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        std::cerr << "A temporary Documents directory could not be created.\n";
        return false;
    }
    const QString ets2_path = nlsi::gui::modlog::GameLogPath(
        directory.path(), QStringLiteral("Euro Truck Simulator 2"));
    const QString ats_path = nlsi::gui::modlog::GameLogPath(
        directory.path(), QStringLiteral("American Truck Simulator"));
    const QByteArray ets2_log = QByteArrayLiteral(
        "00:00:00.000 : [sys] Game version: 1.58\n"
        "00:00:01.000 : [mods] Subscribed workshop mod ID: 111111111\n"
        "00:00:02.000 : [mods] Active workshop mod ID: 123456789, version: 1.2, "
            "source: Steam Workshop, name: \"Pink Truck\", author: 'NLSI'\n"
        "00:00:03.000 : [mods] Active workshop mod ID: 123456789, version: 1.3, "
            "source: Steam Workshop\n"
        "00:00:04.000 : [mods] Active local mod ID: 222222222, name: Local-only\n"
        "00:00:05.000 : [mods] Active workshop mod ID: 18446744073709551616\n");
    const QString long_ats_mod_name(240, QLatin1Char('X'));
    const QByteArray ats_log = QStringLiteral(
        "00:00:00.000 : [sys] Game version: 1.58\n"
        "00:00:01.000 : [mods] Active workshop mod ID: 987654321, name: \"%1\", "
        "version: 2.0\n").arg(long_ats_mod_name).toUtf8();
    if (!WriteTextFile(ets2_path, ets2_log) || !WriteTextFile(ats_path, ats_log)) {
        std::cerr << "ETS2/ATS game-log fixtures could not be written.\n";
        return false;
    }

    const auto ets2 = nlsi::gui::modlog::ReadGameLog(ets2_path);
    const auto ats = nlsi::gui::modlog::ReadGameLog(ats_path);
    const auto missing = nlsi::gui::modlog::ReadGameLog(
        directory.filePath(QStringLiteral("missing/game.log.txt")));
    const auto* active_workshop = FindMod(ets2, QStringLiteral("123456789"));
    const auto* subscribed_workshop = FindMod(ets2, QStringLiteral("111111111"));
    const auto* active_local = FindMod(ets2, QStringLiteral("222222222"));
    if (!ets2.error.isEmpty() || ets2.mods.size() != 3
        || !ets2.session_boundary_known || !active_workshop
        || active_workshop->name != QStringLiteral("Pink Truck")
        || active_workshop->version != QStringLiteral("1.3")
        || active_workshop->author != QStringLiteral("NLSI")
        || !active_workshop->active_workshop
        || !subscribed_workshop || !subscribed_workshop->subscribed
        || subscribed_workshop->active_workshop
        || !active_local || !active_local->active_local
        || !ats.error.isEmpty() || ats.mods.size() != 1
        || ats.mods.front().id != QStringLiteral("987654321")
        || ats.mods.front().name != long_ats_mod_name
        || ats.mods.front().version != QStringLiteral("2.0")
        || !missing.mods.isEmpty()
        || !missing.error.contains(QStringLiteral("not found"))) {
        std::cerr << "The game-log parser did not isolate active ETS2/ATS Workshop mods "
            "or handle a missing log correctly.\n";
        return false;
    }
    QFile stale_fixture(ets2_path);
    if (!stale_fixture.open(QIODevice::ReadWrite)
        || !stale_fixture.setFileTime(
            QDateTime::currentDateTime().addSecs(-660), QFileDevice::FileModificationTime)) {
        std::cerr << "A stale game-log fixture could not be prepared.\n";
        return false;
    }
    stale_fixture.close();
    const auto stale = nlsi::gui::modlog::ReadGameLog(ets2_path);
    if (!stale.error.isEmpty() || !stale.stale || stale.mods.size() != 3) {
        std::cerr << "A stale log was not flagged while retaining its parsed active entries.\n";
        return false;
    }
    const HANDLE read_lock = CreateFileW(
        reinterpret_cast<LPCWSTR>(ets2_path.utf16()),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (read_lock == INVALID_HANDLE_VALUE) {
        std::cerr << "An unreadable-log fixture could not be locked.\n";
        return false;
    }
    const auto unreadable = nlsi::gui::modlog::ReadGameLog(ets2_path);
    CloseHandle(read_lock);
    if (!unreadable.error.contains(QStringLiteral("could not be read"))) {
        std::cerr << "An unreadable game log did not report its read failure.\n";
        return false;
    }

    const QUrl source = nlsi::gui::modlog::WorkshopSourceUrl(
        active_workshop->id);
    const QUrl invalid_source = nlsi::gui::modlog::WorkshopSourceUrl(
        QStringLiteral("../123"));
    if (!source.isValid()
        || source.host() != QStringLiteral("steamcommunity.com")
        || QUrlQuery(source).queryItemValue(QStringLiteral("id"))
            != QStringLiteral("123456789")
        || invalid_source.isValid()) {
        std::cerr << "A Workshop source link was invalid or accepted an unsafe ID.\n";
        return false;
    }

    nlsi::gui::ActiveModsPage active_mods_page(nullptr, directory.path(), false);
    active_mods_page.show();
    if (!WaitForModLogInitialization(active_mods_page)) {
        std::cerr << "The Active Mods page did not finish asynchronous log initialization.\n";
        return false;
    }
    const auto links = active_mods_page.findChildren<QLabel*>(
        QStringLiteral("modSourceLink"));
    if (links.size() != 4
        || !links.front()->text().contains(QStringLiteral("steamcommunity.com"))
        || active_mods_page.findChildren<QLabel*>(
            QStringLiteral("modThumbnail")).size() != 4) {
        std::cerr << "The per-game mod cards did not display active mods with source links "
            "and thumbnail slots.\n";
        return false;
    }
    auto* tabs = active_mods_page.findChild<QTabWidget*>(
        QStringLiteral("activeModsGames"));
    if (!tabs || tabs->count() != 2
        || !tabs->tabText(0).contains(QStringLiteral("Euro Truck"))
        || !tabs->tabText(1).contains(QStringLiteral("American Truck"))) {
        std::cerr << "The Active Mods page does not separate ETS2 and ATS game logs.\n";
        return false;
    }
    tabs->setCurrentIndex(1);
    active_mods_page.resize(900, 600);
    QApplication::processEvents();
    const auto names = active_mods_page.findChildren<QLabel*>(
        QStringLiteral("modName"));
    QLabel* long_mod_name = nullptr;
    for (QLabel* name : names) {
        if (name->accessibleName() == long_ats_mod_name) {
            long_mod_name = name;
            break;
        }
    }
    if (!long_mod_name || !long_mod_name->wordWrap()
        || !long_mod_name->text().contains(QChar(0x200b))) {
        std::cerr << "A very long mod identifier did not receive a wrap opportunity.\n";
        return false;
    }
    active_mods_page.resize(1100, 750);
    QApplication::processEvents();
    auto* ets2_reinitialize = active_mods_page.findChild<QPushButton*>(
        QStringLiteral("reinitializeEts2LogButton"));
    auto* ats_reinitialize = active_mods_page.findChild<QPushButton*>(
        QStringLiteral("reinitializeAtsLogButton"));
    tabs->setCurrentIndex(0);
    QApplication::processEvents();
    const bool ets2_control_visible = ets2_reinitialize
        && ets2_reinitialize->isVisibleTo(&active_mods_page);
    tabs->setCurrentIndex(1);
    QApplication::processEvents();
    const bool ats_control_visible = ats_reinitialize
        && ats_reinitialize->isVisibleTo(&active_mods_page);
    if (!ets2_control_visible || !ats_control_visible) {
        std::cerr << "A Reinitialize control became inaccessible while resizing the page.\n";
        return false;
    }
    tabs->setCurrentIndex(0);

    auto* reinitialize = active_mods_page.findChild<QPushButton*>(
        QStringLiteral("reinitializeEts2LogButton"));
    if (!reinitialize || reinitialize->text() != QStringLiteral("Reinitialize")
        || reinitialize->accessibleName().isEmpty()) {
        std::cerr << "The ETS2 log reinitialization control is missing or inaccessible.\n";
        return false;
    }
    reinitialize->click();
    reinitialize->click();
    if (reinitialize->isEnabled()
        || !WaitForModLogInitialization(active_mods_page)) {
        std::cerr << "Repeated reinitialization was not serialized or did not complete.\n";
        return false;
    }
    const auto timers = active_mods_page.findChildren<QTimer*>(
        QStringLiteral("activeModsRefreshTimer"));
    if (timers.size() != 1 || !timers.front()->isActive()) {
        std::cerr << "Repeated reinitialization created duplicate log monitors.\n";
        return false;
    }
    return true;
}

bool TestIncrementalGameLogMonitoring() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return false;
    }
    const QString path = nlsi::gui::modlog::GameLogPath(
        directory.path(), QStringLiteral("Euro Truck Simulator 2"));
    const QByteArray initial = QByteArrayLiteral(
        "00:00:00.000 : [sys] Executable: eurotrucks2.exe\n"
        "00:00:01.000 : [mods] Subscribed workshop mod ID: 111111111\n"
        "00:00:02.000 : [mods] Active workshop mod ID: 123456789, name: \"Café 🚚\"\n"
        "00:00:03.000 : [mod_package_manager] Mounted mod package: promods-map-v1.scs\n"
        "00:00:04.000 : [mod_package_manager] Mounted mod package: promods-assets-v2.scs\n"
        "00:00:05.000 : [mods] Active local mod ID: local_pack, name: Local-only\n");
    if (!WriteTextFile(path, initial)) {
        return false;
    }
    auto state = nlsi::gui::modlog::ReadGameLog(path);
    const auto* subscribed = FindMod(state, QStringLiteral("111111111"));
    const auto* active = FindMod(state, QStringLiteral("123456789"));
    const auto* local = FindMod(state, QStringLiteral("local_pack"));
    const auto* package = FindMod(state, QStringLiteral("promods"));
    if (!state.error.isEmpty() || !state.initialized || state.offset != initial.size()
        || !state.session_boundary_known || !subscribed || subscribed->active_workshop
        || !active || active->name != QString::fromUtf8("Café 🚚")
        || !active->active_workshop || !local || !local->active_local
        || !package || !package->mounted
        || std::count_if(state.mods.cbegin(), state.mods.cend(),
            [](const auto& mod) { return mod.name == QStringLiteral("ProMods"); }) != 1) {
        std::cerr << "Initial session evidence or archive grouping was incorrect: "
                  << "error=" << state.error.toStdString()
                  << ", initialized=" << state.initialized
                  << ", boundary=" << state.session_boundary_known
                  << ", mods=" << state.mods.size()
                  << ", subscribed=" << (subscribed != nullptr)
                  << ", active=" << (active != nullptr)
                  << ", local=" << (local != nullptr)
                  << ", mounted=" << (package != nullptr && package->mounted)
                  << ", entries=";
        for (const auto& mod : state.mods) {
            std::cerr << '[' << mod.id.toStdString() << ':'
                      << mod.name.toStdString() << ':'
                      << mod.mounted << ']';
        }
        std::cerr << ".\n";
        return false;
    }
    const QString uncertain_path = directory.filePath(
        QStringLiteral("uncertain/game.log.txt"));
    if (!WriteTextFile(uncertain_path, QByteArrayLiteral(
            "00:00:08.000 : [mods] Active workshop mod ID: 909090909\n"))) {
        return false;
    }
    const auto uncertain = nlsi::gui::modlog::ReadGameLog(uncertain_path);
    if (!uncertain.error.isEmpty() || uncertain.session_boundary_known
        || !FindMod(uncertain, QStringLiteral("909090909"))) {
        std::cerr << "An uncertain session boundary was treated as confirmed.\n";
        return false;
    }

    QFile append(path);
    if (!append.open(QIODevice::WriteOnly | QIODevice::Append)
        || append.write("00:00:06.000 : [mods] Active workshop mod ID: 777777777, name: \"") < 0
        || append.write(QString::fromUtf8("東京 🚛").toUtf8()) < 0
        || append.write("\"") != 1) {
        return false;
    }
    append.close();
    if (!nlsi::gui::modlog::ReadAppendedGameLog(path, state)
        || FindMod(state, QStringLiteral("777777777"))) {
        std::cerr << "An incomplete appended UTF-8 line was parsed prematurely.\n";
        return false;
    }
    if (!append.open(QIODevice::WriteOnly | QIODevice::Append)
        || append.write("\n") != 1) {
        return false;
    }
    append.close();
    if (!nlsi::gui::modlog::ReadAppendedGameLog(path, state)) {
        std::cerr << "A completed appended line could not be read incrementally.\n";
        return false;
    }
    const auto* appended = FindMod(state, QStringLiteral("777777777"));
    if (!appended || appended->name != QString::fromUtf8("東京 🚛")) {
        std::cerr << "The completed UTF-8 line was corrupted or not deduplicated.\n";
        return false;
    }

    const QByteArray restarted_in_place = QByteArrayLiteral(
        "00:00:00.000 : [sys] Game version: 1.58\n"
        "00:00:01.000 : [mods] Active workshop mod ID: 555555555, name: New session\n");
    QFile restart_append(path);
    if (!restart_append.open(QIODevice::WriteOnly | QIODevice::Append)
        || restart_append.write(restarted_in_place) != restarted_in_place.size()) {
        return false;
    }
    restart_append.close();
    if (!nlsi::gui::modlog::ReadAppendedGameLog(path, state)
        || !state.session_boundary_known
        || FindMod(state, QStringLiteral("123456789"))
        || !FindMod(state, QStringLiteral("555555555"))) {
        std::cerr << "An appended game restart did not establish a fresh session.\n";
        return false;
    }

    const QByteArray restarted = QByteArrayLiteral(
        "00:00:00.000 : [sys] Executable: eurotrucks2.exe\n"
        "00:00:01.000 : [mods] Active workshop mod ID: 888888888, name: Fresh\n");
    if (!WriteTextFile(path, restarted)
        || !nlsi::gui::modlog::GameLogNeedsReinitialize(path, state)) {
        std::cerr << "Log truncation was not detected before incremental reading.\n";
        return false;
    }
    state = nlsi::gui::modlog::ReadGameLog(path);
    if (!state.error.isEmpty() || !state.session_boundary_known
        || FindMod(state, QStringLiteral("123456789"))
        || !FindMod(state, QStringLiteral("888888888"))) {
        std::cerr << "A restarted game log retained stale mod evidence.\n";
        return false;
    }

    const QString rotated_path = directory.filePath(
        QStringLiteral("Euro Truck Simulator 2/game.log.txt.rotated"));
    if (!QFile::rename(path, rotated_path)
        || !WriteTextFile(path, restarted)
        || !nlsi::gui::modlog::GameLogNeedsReinitialize(path, state)) {
        std::cerr << "Replacement/rotation of the game log was not detected.\n";
        return false;
    }
    const auto missing = nlsi::gui::modlog::ReadGameLog(
        directory.filePath(QStringLiteral("missing/game.log.txt")));
    const auto not_a_file = nlsi::gui::modlog::ReadGameLog(directory.path());
    if (!missing.error.contains(QStringLiteral("not found"))
        || !not_a_file.error.contains(QStringLiteral("not found"))) {
        std::cerr << "Missing and non-file log paths were not reported clearly.\n";
        return false;
    }
    return true;
}

bool TestMonitorStartsBeforeGame() {
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return false;
    }
    nlsi::gui::ActiveModsPage page(nullptr, directory.path(), false);
    page.show();
    QApplication::processEvents();
    auto* tabs = page.findChild<QTabWidget*>(QStringLiteral("activeModsGames"));
    auto* ets2_page = tabs ? tabs->widget(0) : nullptr;
    auto* status = ets2_page
        ? ets2_page->findChild<QLabel*>(QStringLiteral("activeModsStatus"))
        : nullptr;
    if (!status) {
        return false;
    }
    const QString path = nlsi::gui::modlog::GameLogPath(
        directory.path(), QStringLiteral("Euro Truck Simulator 2"));
    if (!WriteTextFile(path, QByteArrayLiteral(
            "00:00:00.000 : [sys] Executable: eurotrucks2.exe\n"
            "00:00:01.000 : [mods] Active workshop mod ID: 101010101\n"))) {
        return false;
    }

    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    poll.setInterval(20);
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (status->text().contains(QStringLiteral("Monitoring game log"))) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    poll.start();
    loop.exec();
    if (!status->text().contains(QStringLiteral("Monitoring game log"))
        || !FindMod(nlsi::gui::modlog::ReadGameLog(path), QStringLiteral("101010101"))) {
        std::cerr << "A game log created after the app opened was not detected.\n";
        return false;
    }
    return true;
}

bool TestHistoryPagesLoadPersistedRows() {
    nlsi::gui::HistoryPage history_page;
    nlsi::gui::JobsPage jobs_page;
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
        {{QStringLiteral("source_city"), QStringLiteral("Berlin")},
            {QStringLiteral("destination_city"), QStringLiteral("Paris")},
            {QStringLiteral("income"), QStringLiteral("25000")},
            {QStringLiteral("planned_distance"), QStringLiteral("1200")},
            {QStringLiteral("truck_license_plate_country"), QStringLiteral("Germany")},
            {QStringLiteral("trailer_license_plate_country"), QStringLiteral("France")},
            {QStringLiteral("fuel_used_source"),
                QStringLiteral("CALCULATED FROM VALID FUEL-LEVEL TELEMETRY")},
            {QStringLiteral("refueled_source"),
                QStringLiteral("CALCULATED FROM FUEL-LEVEL INCREASES")},
            {QStringLiteral("average_consumption_source"),
                QStringLiteral("CALCULATED USING REPORTED SCS JOB DISTANCE")}},
    });
    history.jobs.push_back({
        QStringLiteral("job-pending"),
        QStringLiteral("Pending cargo"),
        QStringLiteral("Berlin"),
        QStringLiteral("Paris"),
        QStringLiteral("Pending"),
        QStringLiteral("2026-10-07T08:28:00Z"),
    });
    history.jobs.push_back({
        QStringLiteral("job-active"),
        QStringLiteral("Active cargo"),
        QStringLiteral("Berlin"),
        QStringLiteral("Paris"),
        QStringLiteral("In progress"),
        QStringLiteral("2026-10-07T08:27:00Z"),
    });
    history.jobs.push_back({
        QStringLiteral("job-2"),
        QStringLiteral("Wood"),
        QStringLiteral("Oslo"),
        QStringLiteral("Stockholm"),
        QStringLiteral("Cancelled"),
        QStringLiteral("2026-10-07T08:26:00Z"),
    });
    history_page.UpdateState({});
    history_page.UpdateHistory(history);
    jobs_page.UpdateState({});
    jobs_page.UpdateHistory(history);

    auto* tabs = history_page.findChild<QTabWidget*>(QStringLiteral("contentTabs"));
    if (!tabs || tabs->count() != 2
        || tabs->tabText(0) == QStringLiteral("COMPLETED JOBS")
        || tabs->tabText(1) == QStringLiteral("COMPLETED JOBS")) {
        std::cerr << "The History page still contains a Completed Jobs tab.\n";
        return false;
    }
    QTableView* sessions_table = nullptr;
    for (auto* table : history_page.findChildren<QTableView*>()) {
        auto* model = qobject_cast<QStandardItemModel*>(table->model());
        if (!model || model->rowCount() != 1) {
            continue;
        }
        const QString first_column = model->headerData(0, Qt::Horizontal).toString();
        if (first_column == QStringLiteral("GAME")) {
            sessions_table = table;
        }
    }
    const auto history_job_cards = history_page.findChildren<QFrame*>(
        QStringLiteral("completedJobCard"));
    const auto job_cards = jobs_page.findChildren<QFrame*>(
        QStringLiteral("completedJobCard"));
    const auto job_exports = jobs_page.findChildren<QPushButton*>(
        QStringLiteral("exportJobPdfButton"));
    if (!sessions_table || !history_job_cards.isEmpty()
        || job_cards.size() != 2 || job_exports.size() != 2) {
        std::cerr << "Only delivered/cancelled persisted jobs should render on Jobs with per-job exports.\n";
        return false;
    }
    QSet<QString> exported_job_ids;
    for (const QPushButton* export_button : job_exports) {
        exported_job_ids.insert(export_button->property("persistedJobId").toString());
    }
    if (exported_job_ids != QSet<QString>{
            QStringLiteral("job-1"), QStringLiteral("job-2")}) {
        std::cerr << "Each completed-job export is not tied to its own persisted job ID.\n";
        return false;
    }
    QFrame* delivered_card = nullptr;
    for (QFrame* card : job_cards) {
        const QLabel* job_id = card->findChild<QLabel*>(
            QStringLiteral("completedJobId"));
        if (job_id && job_id->text().contains(QStringLiteral("job-1"))) {
            delivered_card = card;
            break;
        }
    }
    QStringList delivered_values;
    if (delivered_card) {
        for (const QLabel* value : delivered_card->findChildren<QLabel*>(
                 QStringLiteral("completedJobFieldValue"))) {
            delivered_values.push_back(value->text());
        }
    }
    if (!delivered_card
        || !delivered_values.contains(QStringLiteral("Berlin"))
        || !delivered_values.contains(QStringLiteral("Paris"))
        || !delivered_values.contains(QStringLiteral("25000"))
        || !delivered_values.contains(QStringLiteral("1,200.00 km"))
        || !delivered_values.contains(QStringLiteral("Germany"))
        || !delivered_values.contains(QStringLiteral("France"))
        || !delivered_values.contains(QStringLiteral(
            "CALCULATED FROM VALID FUEL-LEVEL TELEMETRY"))
        || !delivered_values.contains(QStringLiteral(
            "CALCULATED USING REPORTED SCS JOB DISTANCE"))
        || !delivered_values.contains(QStringLiteral("N/A"))) {
        std::cerr << "Completed-job cards did not map available fields or mark missing data N/A.\n";
        return false;
    }
    if (!history_page.findChildren<QLabel*>(QStringLiteral("pageTitle")).isEmpty()
        || !history_page.findChildren<QLabel*>(QStringLiteral("pageDescription")).isEmpty()) {
        std::cerr << "History tabs contain redundant page headings below the shared header.\n";
        return false;
    }
    jobs_page.resize(500, 650);
    jobs_page.show();
    QApplication::processEvents();
    auto* jobs_scroll = jobs_page.findChild<QScrollArea*>(
        QStringLiteral("completedJobsScroll"));
    const auto job_fields = jobs_page.findChildren<QWidget*>(
        QStringLiteral("completedJobFields"));
    if (!jobs_scroll || jobs_scroll->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff
        || job_fields.isEmpty()
        || job_fields.front()->property("columnCount").toInt() != 1) {
        std::cerr << "Completed-job fields did not use a wrapped, single-column narrow layout.\n";
        return false;
    }
    jobs_page.resize(1200, 900);
    QApplication::processEvents();
    if (job_fields.front()->property("columnCount").toInt() != 2) {
        std::cerr << "Completed-job fields did not expand into balanced columns.\n";
        return false;
    }
    jobs_page.hide();

    nlsi::gui::EventsPage events_page;
    history.events.push_back({
        QStringLiteral("2026-10-07T08:25:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.tollgate.paid"),
        QStringLiteral("{\"job_id\":\"game-job-17\",\"toll_fee\":12.5,\"currency\":\"EUR\"}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:27:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.use.ferry"),
        QStringLiteral("{\"trip_id\":\"trip-1\"}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:28:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.use.train"),
        QStringLiteral("{}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:29:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("startup"),
        QStringLiteral("player.use.ferry"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:29:30Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("traffic.train.count"),
        QStringLiteral("{\"count\":3}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:29:45Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.use.train.trigger"),
        QStringLiteral("{}"),
    });
    history.events.push_back({
        QStringLiteral("2026-10-07T08:25:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.tollgate.paid"),
        QStringLiteral("{\"job_id\":\"game-job-17\",\"toll_fee\":12.5,\"currency\":\"EUR\"}"),
    });
    history.jobs.front().identity = QStringLiteral("game-job-17");
    history.jobs.front().nlsi_job_id = QStringLiteral("JOB-NLSI-0001");
    history.jobs.front().details = {{QStringLiteral("job_id"), QStringLiteral("game-job-17")}};
    ++history.revision;
    history_page.UpdateHistory(history);
    events_page.UpdateHistory(history);
    const auto event_entries = events_page.findChildren<QFrame*>(
        QStringLiteral("eventEntry"));
    if (events_page.findChildren<QTableView*>().size() != 0
        || event_entries.size() != 7) {
        std::cerr << "Persisted events were not rendered as wrapped event entries.\n";
        return false;
    }
    const auto first_event_entry = event_entries.front();
    if (!first_event_entry->findChild<QLabel*>(QStringLiteral("eventTimestamp"))
        || first_event_entry->findChild<QLabel*>(QStringLiteral("eventTimestamp"))->text()
            != QStringLiteral("10/07/26 16:25:00.000 Asia/Manila")
        || first_event_entry->findChild<QLabel*>(QStringLiteral("eventSource"))->text()
            != QStringLiteral("TruckSim GPS")
        || first_event_entry->findChild<QLabel*>(QStringLiteral("eventType"))->text()
            != QStringLiteral("player.tollgate.paid")
        || !first_event_entry->findChild<QLabel*>(QStringLiteral("eventData"))->text()
            .contains(QStringLiteral("job_id"))) {
        std::cerr << "An event entry omitted its timestamp, source, event type, or event data.\n";
        return false;
    }
    bool empty_data_explicit = false;
    for (QFrame* entry : event_entries) {
        const QLabel* type = entry->findChild<QLabel*>(QStringLiteral("eventType"));
        const QLabel* data = entry->findChild<QLabel*>(QStringLiteral("eventData"));
        empty_data_explicit = empty_data_explicit
            || (type && type->text() == QStringLiteral("player.use.train")
                && data && data->text() == QStringLiteral("N/A (no event data recorded)"));
    }
    if (!empty_data_explicit) {
        std::cerr << "An event with empty data was not explicitly marked unavailable.\n";
        return false;
    }

    const QString long_event_data(360, QLatin1Char('X'));
    history.events.prepend({
        QStringLiteral("2026-10-07T08:30:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("player.long_message"),
        QStringLiteral("{\"provider\":\"TruckSim GPS\",\"data\":{\"message\":\"")
            + long_event_data + QStringLiteral("\"}}"),
    });
    ++history.revision;
    events_page.UpdateHistory(history);
    QApplication::processEvents();
    const auto updated_entries = events_page.findChildren<QFrame*>(
        QStringLiteral("eventEntry"));
    auto* event_scroll = events_page.findChild<QScrollArea*>(
        QStringLiteral("eventsScroll"));
    QFrame* newest_event_entry = nullptr;
    for (QFrame* entry : updated_entries) {
        const QLabel* event_type = entry->findChild<QLabel*>(
            QStringLiteral("eventType"));
        if (event_type && event_type->text() == QStringLiteral("player.long_message")) {
            newest_event_entry = entry;
            break;
        }
    }
    QLabel* newest_event_data = newest_event_entry
        ? newest_event_entry->findChild<QLabel*>(QStringLiteral("eventData"))
        : nullptr;
    QString wrapped_long_data = newest_event_data ? newest_event_data->text() : QString();
    wrapped_long_data.remove(QChar(0x200b));
    if (updated_entries.size() != 8 || !event_scroll
        || event_scroll->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff
        || !newest_event_data || !newest_event_data->wordWrap()
        || !wrapped_long_data.contains(long_event_data)
        || newest_event_data->text().contains(QStringLiteral("provider"))) {
        std::cerr << "New events, long wrapped data, or event-only data handling failed.\n";
        return false;
    }
    tabs->setCurrentIndex(1);
    QTableView* trip_events_table = nullptr;
    for (auto* table : history_page.findChildren<QTableView*>()) {
        auto* model = qobject_cast<QStandardItemModel*>(table->model());
        if (model && model->horizontalHeaderItem(0)
            && model->horizontalHeaderItem(0)->text() == QStringLiteral("EVENT TYPE")) {
            trip_events_table = table;
        }
    }
    auto* trip_model = trip_events_table
        ? qobject_cast<QStandardItemModel*>(trip_events_table->model())
        : nullptr;
    if (!trip_model || trip_model->rowCount() != 3
        || trip_model->index(0, 0).data().toString() != QStringLiteral("Toll gate")
        || trip_model->index(0, 1).data().toString() != QStringLiteral("12.5")
        || trip_model->index(0, 2).data().toString() != QStringLiteral("EUR")
        || trip_model->index(0, 3).data().toString() != QStringLiteral("JOB-NLSI-0001")
        || trip_model->index(1, 0).data().toString() != QStringLiteral("Ferry")
        || trip_model->index(1, 1).data().toString() != QStringLiteral("Unavailable")
        || trip_model->index(1, 2).data().toString() != QStringLiteral("Unavailable")
        || trip_model->index(1, 3).data().toString() != QStringLiteral("trip-1")
        || trip_model->index(2, 0).data().toString() != QStringLiteral("Train")
        || trip_model->index(2, 1).data().toString() != QStringLiteral("Unavailable")) {
        std::cerr << "Exact supported trip events, fees, or associations were misreported.\n";
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
            != QStringLiteral("10/07/26 16:36:46.123 Asia/Manila")
        || nlsi::gui::TimestampText(QStringLiteral("2026-10-07T08:36:46.123"))
            != QStringLiteral("2026-10-07T08:36:46.123")
        || nlsi::gui::DurationText(3661.25) != QStringLiteral("01:01:01")
        || nlsi::gui::DurationText(-1.0) != QStringLiteral("N/A")
        || nlsi::gui::DurationText(std::numeric_limits<double>::infinity())
            != QStringLiteral("N/A")
        || nlsi::gui::ArrivalText(
            240.0, QDateTime::fromString(
                QStringLiteral("2026-10-10T13:01:39Z"), Qt::ISODate))
            != QStringLiteral("4 MIN LEFT - 21:05:39 ASIA/MANILA")
        || nlsi::gui::ArrivalText(
            4800.0, QDateTime::fromString(
                QStringLiteral("2026-10-10T13:01:39Z"), Qt::ISODate))
            != QStringLiteral("1 HR 20 MIN LEFT - 22:21:39 ASIA/MANILA")
        || nlsi::gui::ArrivalText(-1.0, QDateTime::currentDateTimeUtc())
            != QStringLiteral("N/A")
        || nlsi::gui::ArrivalText(1.0, {})
            != QStringLiteral("N/A")) {
        std::cerr << "Locale-independent number, date, or time formatting is incorrect.\n";
        return false;
    }
    return true;
}

bool TestGlobalTypographySizes() {
    nlsi::gui::DashboardPage dashboard;
    dashboard.show();
    QApplication::processEvents();
    const auto section_titles = dashboard.findChildren<QLabel*>(
        QStringLiteral("dashboardSectionTitle"));
    const auto dashboard_labels = dashboard.findChildren<QLabel*>(
        QStringLiteral("dashboardFieldLabel"));
    const auto dashboard_values = dashboard.findChildren<QLabel*>(
        QStringLiteral("dashboardValue"));
    if (section_titles.isEmpty() || dashboard_labels.isEmpty()
        || dashboard_values.isEmpty()) {
        std::cerr << "Dashboard typography test could not locate its labels.\n";
        return false;
    }
    for (const QLabel* label : section_titles) {
        if (label->font().pixelSize() != 12) {
            std::cerr << "Dashboard section titles are not 12 px.\n";
            return false;
        }
    }
    for (const QLabel* label : dashboard_labels) {
        if (label->font().pixelSize() != 11) {
            std::cerr << "Dashboard field labels are not 11 px.\n";
            return false;
        }
    }
    for (const QLabel* label : dashboard_values) {
        if (label->font().pixelSize() != 11) {
            std::cerr << "Dashboard telemetry values are not 11 px.\n";
            return false;
        }
    }

    nlsi::gui::JobsPage jobs;
    jobs.show();
    nlsi::session::HistorySnapshot history;
    history.jobs.push_back({
        QStringLiteral("typography-job"),
        QStringLiteral("Cargo"),
        QStringLiteral("Origin"),
        QStringLiteral("Destination"),
        QStringLiteral("Delivered"),
        QStringLiteral("2026-10-07T08:29:00Z"),
    });
    jobs.UpdateHistory(history);
    QApplication::processEvents();
    for (const QLabel* label : jobs.findChildren<QLabel*>(
             QStringLiteral("completedJobFieldLabel"))) {
        if (label->font().pixelSize() != 11) {
            std::cerr << "Completed-job field labels are not 11 px.\n";
            return false;
        }
    }
    for (const QLabel* label : jobs.findChildren<QLabel*>(
             QStringLiteral("completedJobFieldValue"))) {
        if (label->font().pixelSize() != 11) {
            std::cerr << "Completed-job field values are not 11 px.\n";
            return false;
        }
    }
    const auto completed_titles = jobs.findChildren<QLabel*>(
        QStringLiteral("completedJobId"));
    if (completed_titles.isEmpty() || completed_titles.front()->font().pixelSize() != 12) {
        std::cerr << "Completed-job card titles are not 12 px.\n";
        return false;
    }

    nlsi::gui::EventsPage events;
    history.events.push_back({
        QStringLiteral("2026-10-07T08:30:00Z"),
        QStringLiteral("TruckSim GPS"),
        QStringLiteral("event.test"),
        QStringLiteral("{\"message\":\"value\"}"),
    });
    events.UpdateHistory(history);
    events.show();
    QApplication::processEvents();
    const auto event_labels = events.findChildren<QLabel*>(
        QStringLiteral("eventFieldLabel"));
    const auto event_values = events.findChildren<QLabel*>(
        QStringLiteral("eventData"));
    if (event_labels.isEmpty() || event_values.isEmpty()
        || event_labels.front()->font().pixelSize() != 12
        || event_values.front()->font().pixelSize() != 11) {
        std::cerr << "Event-entry headings or data do not use the shared typography sizes.\n";
        return false;
    }
    QPushButton button(QStringLiteral("Button"));
    button.ensurePolished();
    if (button.font().pixelSize() != 11) {
        std::cerr << "Button text is not 11 px.\n";
        return false;
    }
    return true;
}

bool TestShutdownIsIdempotent() {
    nlsi::telemetry::TelemetryCore telemetry_core;
    const auto first = telemetry_core.PollShutdown();
    telemetry_core.BeginShutdown();
    const auto second = telemetry_core.PollShutdown();
    return first.state == nlsi::telemetry::TelemetryCore::ShutdownState::Completed
        && second.state == nlsi::telemetry::TelemetryCore::ShutdownState::Completed;
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
        found_trucksim = found_trucksim || text == QStringLiteral("TRUCKSIM GPS");
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
    values.has_job.Set(true, L"TruckSim GPS", L"sample");
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
        QStringLiteral("CURRENT JOB"), QStringLiteral("CURRENT POSITION"),
        QStringLiteral("NAVIGATION"),
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
    const QLabel* position = FindDashboardCardValue(page, QStringLiteral("CURRENT POSITION"));
    const QLabel* navigation = FindDashboardCardValue(page, QStringLiteral("NAVIGATION"));
    if (!job || !position || !navigation
        || !job->text().contains(QStringLiteral("JOB-NLSI-0042"))
        || !job->text().contains(QStringLiteral("Furniture"))
        || !job->text().contains(QStringLiteral("Berlin"))
        || !job->text().contains(QStringLiteral("Paris"))
        || position->text() != QStringLiteral("Unavailable\n→ Destination: Paris")
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

    state.scs_position.state = nlsi::providers::ScsPositionState::Connected;
    state.scs_position.available = true;
    state.scs_position.game_id = nlsi::providers::kScsPositionGameEts2;
    state.scs_position.x = -123.5;
    state.scs_position.y = 87.25;
    state.scs_position.z = 900.125;
    page.UpdateState(state);
    if (!position->text().startsWith(QStringLiteral("ETS2 · X -123.50 m · Y 87.25 m · Z 900.13 m"))
        || !position->text().contains(QStringLiteral("Destination: Paris"))) {
        std::cerr << "Dashboard did not show SCS world coordinates separately from the job destination.\n";
        return false;
    }
    state.scs_position.state = nlsi::providers::ScsPositionState::Stale;
    page.UpdateState(state);
    if (!position->text().contains(QStringLiteral("· stale"))) {
        std::cerr << "Dashboard did not mark old SCS position coordinates as stale.\n";
        return false;
    }
    state.scs_position = {};
    page.UpdateState(state);

    state.job.destination_city.Set(
        L"Very Long Destination City Name Used To Validate Responsive Dashboard Wrapping",
        L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    if (!position->wordWrap()
        || !position->text().contains(QStringLiteral("Very Long Destination City Name"))) {
        std::cerr << "A long active-job destination was truncated on Current Position.\n";
        return false;
    }
    state.job.destination_city.MarkStale();
    page.UpdateState(state);
    if (position->text() != QStringLiteral("Unavailable\n→ Destination: Unavailable")) {
        std::cerr << "A stale destination remained visible in Current Position.\n";
        return false;
    }
    state.job.destination_city.Set(L"Paris", L"TruckSim GPS", L"sample");
    values.has_job.MarkStale();
    page.UpdateState(state);
    if (position->text() != QStringLiteral("Unavailable")) {
        std::cerr << "A destination remained visible after the active-job state went stale.\n";
        return false;
    }
    values.has_job.Set(true, L"TruckSim GPS", L"sample");
    state.job.available = false;
    values.has_job.Set(false, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    if (position->text() != QStringLiteral("Unavailable")) {
        std::cerr << "Current Position showed a destination during free driving.\n";
        return false;
    }

    page.resize(480, 500);
    page.show();
    QApplication::processEvents();
    auto* grid = page.findChild<QGridLayout*>(QStringLiteral("dashboardCardGrid"));
    if (!grid || grid->itemAtPosition(0, 1)) {
        std::cerr << "Dashboard cards did not auto-fit to one column at narrow width"
                  << " (page width " << page.width()
                  << ", grid columns " << (grid ? grid->columnCount() : -1)
                  << ").\n";
        return false;
    }
    page.resize(700, 500);
    if (!grid->itemAtPosition(0, 1)) {
        std::cerr << "Dashboard cards did not auto-fit to two columns at medium width.\n";
        return false;
    }
    page.resize(900, 500);
    if (!grid->itemAtPosition(0, 2)) {
        std::cerr << "Dashboard cards did not auto-fit to three columns at wide width.\n";
        return false;
    }
    for (const QLabel* value : page.findChildren<QLabel*>(QStringLiteral("cardValue"))) {
        if (!value->wordWrap() || value->minimumWidth() != 0) {
            std::cerr << "A dashboard card value does not wrap and shrink consistently.\n";
            return false;
        }
    }
    return true;
}

bool TestDashboardCruiseControlIndicators() {
    nlsi::gui::DashboardPage page;
    page.resize(1600, 1000);
    page.show();
    QApplication::processEvents();
    nlsi::telemetry::TelemetryUiState state;
    auto& snapshot = state.fast.values;
    snapshot.connected = true;
    snapshot.effective_throttle.Set(0.25, L"TruckSim GPS", L"sample");
    snapshot.effective_brake.Set(0.05, L"TruckSim GPS", L"sample");
    snapshot.retarder_level.Set(2.0, L"TruckSim GPS", L"sample");

    const QStringList keys = {
        QStringLiteral("throttle"),
        QStringLiteral("brake"),
        QStringLiteral("retarder"),
    };
    for (const QString& key : keys) {
        if (!FindDashboardCruiseIndicator(page, key)
            || !FindDashboardCruiseIndicator(page, key)->isHidden()) {
            std::cerr << "Cruise active marker was visible without confirmed cruise control.\n";
            return false;
        }
    }

    snapshot.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    snapshot.cruise_control_speed.Set(94.0, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    QApplication::processEvents();
    for (const QString& key : keys) {
        const QLabel* indicator = FindDashboardCruiseIndicator(page, key);
        if (!indicator || indicator->isHidden() || indicator->text() != QStringLiteral("A")) {
            std::cerr << "Confirmed cruise control did not show the matching A indicators.\n";
            return false;
        }
    }
    if (FindDashboardField(page, QStringLiteral("throttle"))->text()
            != QStringLiteral("25.0%")
        || !FindDashboardField(page, QStringLiteral("brake"))->isHidden()
        || !FindDashboardField(page, QStringLiteral("retarder"))->isHidden()
        || FindDashboardField(page, QStringLiteral("cruiseControl"))->text()
            != QStringLiteral("94 KM/H - ACTIVE")) {
        std::cerr << "Active cruise control did not hide brake/retarder values or show its set speed.\n";
        return false;
    }

    snapshot.cruise_control_active.Set(false, L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    for (const QString& key : keys) {
        if (!FindDashboardCruiseIndicator(page, key)->isHidden()) {
            std::cerr << "A cruise marker remained after cruise control became inactive.\n";
            return false;
        }
    }
    if (FindDashboardField(page, QStringLiteral("brake"))->isHidden()
        || FindDashboardField(page, QStringLiteral("retarder"))->isHidden()
        || FindDashboardField(page, QStringLiteral("brake"))->text()
            != QStringLiteral("5.0%")
        || FindDashboardField(page, QStringLiteral("retarder"))->text()
            != QStringLiteral("2.0")) {
        std::cerr << "Inactive cruise control did not restore the numeric brake and retarder values.\n";
        return false;
    }

    snapshot.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    snapshot.cruise_control_active.MarkStale();
    page.UpdateState(state);
    for (const QString& key : keys) {
        if (!FindDashboardCruiseIndicator(page, key)->isHidden()) {
            std::cerr << "A cruise marker remained while cruise telemetry was stale.\n";
            return false;
        }
    }

    snapshot.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    snapshot.connected = false;
    page.UpdateState(state);
    for (const QString& key : keys) {
        if (!FindDashboardCruiseIndicator(page, key)->isHidden()) {
            std::cerr << "A cruise marker remained after telemetry disconnected.\n";
            return false;
        }
    }
    return true;
}

bool TestLowFuelWarningAndParkingBrakeStates() {
    nlsi::gui::DashboardPage page;
    nlsi::telemetry::TelemetryUiState state;
    auto& snapshot = state.fast.values;
    snapshot.connected = true;
    snapshot.effective_brake.Set(0.1, L"TruckSim GPS", L"sample");
    snapshot.fuel_liters.Set(200.0, L"SCS SDK channel", L"sample");
    snapshot.fuel_capacity_liters.Set(1000.0, L"SCS SDK configuration", L"sample");
    snapshot.parking_brake.Set(true, L"SCS SDK channel", L"sample");
    page.UpdateState(state);

    QLabel* fuel = FindDashboardField(page, QStringLiteral("fuel"));
    QLabel* parking = page.findChild<QLabel*>(QStringLiteral("parkingBrakeIndicator"));
    if (!fuel || !parking || !fuel->font().underline()
        || !fuel->styleSheet().contains(QStringLiteral("#f05b68"))
        || parking->text() != QStringLiteral("P")
        || parking->accessibleName() != QStringLiteral("Parking brake engaged")
        || !parking->styleSheet().contains(QStringLiteral("#b63d50"))
        || fuel->text().contains(QLatin1Char('%'))) {
        std::cerr << "Fuel warning or confirmed parking-brake ON state was not displayed safely.\n";
        return false;
    }

    snapshot.fuel_liters.Set(200.1, L"SCS SDK channel", L"sample");
    snapshot.parking_brake.Set(false, L"SCS SDK channel", L"sample");
    page.UpdateState(state);
    if (fuel->font().underline() || !fuel->styleSheet().isEmpty()
        || parking->accessibleName() != QStringLiteral("Parking brake released")
        || parking->styleSheet().contains(QStringLiteral("#b63d50"))) {
        std::cerr << "Fuel warning did not clear above 20 percent or parking-brake OFF was wrong.\n";
        return false;
    }

    snapshot.fuel_liters.Set(199.9, L"SCS SDK channel", L"sample");
    snapshot.parking_brake.MarkStale();
    page.UpdateState(state);
    if (!fuel->font().underline()
        || parking->text() != QStringLiteral("?")
        || parking->accessibleName() != QStringLiteral("Parking brake state unknown")) {
        std::cerr << "Below-threshold fuel or stale parking-brake state was not handled safely.\n";
        return false;
    }

    snapshot.fuel_capacity_liters.Set(0.0, L"SCS SDK configuration", L"sample");
    snapshot.parking_brake = {};
    page.UpdateState(state);
    if (fuel->font().underline()
        || parking->text() != QStringLiteral("?")
        || !parking->styleSheet().contains(QStringLiteral("#37323b"))) {
        std::cerr << "Invalid capacity or missing parking telemetry was treated as confirmed.\n";
        return false;
    }
    return true;
}

bool TestMeasurementUnitConversionsAndSettings() {
    QSettings settings(QStringLiteral("NLSI"), QStringLiteral("Exclusive Logbook"));
    const QString key = QStringLiteral("measurement/system");
    const bool had_previous_value = settings.contains(key);
    const QVariant previous_value = settings.value(key);
    struct RestoreSetting {
        QSettings& settings;
        QString key;
        bool had_value;
        QVariant value;
        ~RestoreSetting() {
            if (had_value) {
                settings.setValue(key, value);
            } else {
                settings.remove(key);
            }
        }
    } restore_setting{settings, key, had_previous_value, previous_value};
    settings.remove(key);
    if (nlsi::gui::CurrentMeasurementSystem()
        != nlsi::gui::MeasurementSystem::Metric) {
        std::cerr << "Measurement settings did not default to Metric.\n";
        return false;
    }
    const double distance_miles = nlsi::gui::DisplayDistance(
        10.0, nlsi::gui::MeasurementSystem::USCustomary);
    const double speed_mph = nlsi::gui::DisplaySpeed(
        100.0, nlsi::gui::MeasurementSystem::USCustomary);
    const double fuel_gallons = nlsi::gui::DisplayFuelVolume(
        3.785411784, nlsi::gui::MeasurementSystem::USCustomary);
    const double mass_pounds = nlsi::gui::DisplayMassKilograms(
        1.0, nlsi::gui::MeasurementSystem::USCustomary);
    const auto mpg = nlsi::gui::DisplayFuelEconomy(
        10.0, nlsi::gui::MeasurementSystem::USCustomary);
    if (std::abs(distance_miles - 6.21371192237334) > 0.000001
        || std::abs(speed_mph - 62.1371192237334) > 0.000001
        || std::abs(fuel_gallons - 1.0) > 0.000001
        || std::abs(mass_pounds - 2.20462262185) > 0.000001
        || !mpg || std::abs(*mpg - 23.5214583) > 0.000001
        || nlsi::gui::DisplayFuelEconomy(
            0.0, nlsi::gui::MeasurementSystem::USCustomary)) {
        std::cerr << "A presentation-boundary unit conversion was incorrect.\n";
        return false;
    }

    nlsi::gui::SetMeasurementSystem(nlsi::gui::MeasurementSystem::Metric);
    nlsi::telemetry::TelemetryCore telemetry_core;
    nlsi::gui::MainWindow window(
        L"NLSI Exclusive Logbook", L"v1.5.3-beta", telemetry_core);
    QComboBox* selector = window.findChild<QComboBox*>(
        QStringLiteral("measurementSystemSelector"));
    if (!selector || selector->count() != 2
        || selector->itemText(0) != QStringLiteral("Metric")
        || selector->itemText(1) != QStringLiteral("US customary")) {
        std::cerr << "Settings does not expose the Metric and US customary options.\n";
        return false;
    }
    selector->setCurrentIndex(1);
    if (nlsi::gui::CurrentMeasurementSystem()
        != nlsi::gui::MeasurementSystem::USCustomary) {
        std::cerr << "Selecting US customary did not persist the presentation setting.\n";
        return false;
    }
    selector->setCurrentIndex(0);
    if (nlsi::gui::CurrentMeasurementSystem()
        != nlsi::gui::MeasurementSystem::Metric) {
        std::cerr << "Selecting Metric did not persist the presentation setting.\n";
        return false;
    }
    return true;
}

bool TestDashboardLayoutAndResponsiveText() {
    const nlsi::gui::MeasurementSystem previous_units =
        nlsi::gui::CurrentMeasurementSystem();
    struct RestoreMeasurementSystem {
        nlsi::gui::MeasurementSystem value;
        ~RestoreMeasurementSystem() {
            nlsi::gui::SetMeasurementSystem(value);
        }
    } restore_units{previous_units};
    nlsi::gui::SetMeasurementSystem(nlsi::gui::MeasurementSystem::USCustomary);

    nlsi::gui::DashboardPage page;
    nlsi::session::HistorySnapshot history;
    history.events = {
        {QStringLiteral("2026-10-07T08:30:00Z"), QStringLiteral("SCS SDK"),
            QStringLiteral("player.use.ferry"),
            QStringLiteral("{\"data\":{\"source_name\":\"Calais\",\"target_name\":\"Dover\",\"amount\":45}}")},
        {QStringLiteral("2026-10-07T08:10:00Z"), QStringLiteral("SCS SDK"),
            QStringLiteral("player.tollgate.paid"),
            QStringLiteral("{\"data\":{\"amount\":12.5}}")},
        {QStringLiteral("2026-10-07T08:20:00Z"), QStringLiteral("SCS SDK"),
            QStringLiteral("player.use.train"),
            QStringLiteral("{\"data\":{\"source_name\":\"Rotterdam\",\"target_name\":\"Hull\",\"amount\":80}}")},
    };
    history.jobs.push_back({
        QStringLiteral("job-1"), QStringLiteral("Cargo"), QStringLiteral("A"),
        QStringLiteral("B"), QStringLiteral("Delivered"),
        QStringLiteral("2026-10-07T08:40:00Z"),
        {{QStringLiteral("refueled_liters"), 38.5}},
    });
    page.UpdateHistory(history);
    const QPlainTextEdit* travel_summary =
        page.findChild<QPlainTextEdit*>(QStringLiteral("travelExpenseSummary"));
    if (!travel_summary
        || travel_summary->toPlainText().indexOf(QStringLiteral("TOLL"))
            >= travel_summary->toPlainText().indexOf(QStringLiteral("TRAIN"))
        || travel_summary->toPlainText().indexOf(QStringLiteral("TRAIN"))
            >= travel_summary->toPlainText().indexOf(QStringLiteral("FERRY"))
        || !travel_summary->toPlainText().contains(QStringLiteral("10.17 US GAL"))
        || !travel_summary->toPlainText().contains(QStringLiteral(
            "REFUELING COST: N/A (NOT PROVIDED BY SCS SDK 1.15)"))) {
        std::cerr << "Travel summary did not preserve chronological events or explicit gaps.\n";
        return false;
    }
    nlsi::telemetry::TelemetryUiState state;
    auto& snapshot = state.fast.values;
    snapshot.connected = true;
    state.providers.trucksim = nlsi::telemetry::ProviderState::Connected;
    snapshot.game_name.Set(L"Euro Truck Simulator 2", L"TruckSim GPS", L"sample");
    snapshot.game_id.Set(L"ets2", L"TruckSim GPS", L"sample");
    snapshot.game_version.Set(L"1.58.1.2s", L"SCS SDK configuration", L"sample");
    snapshot.vehicle.Set(L"Volvo FH16", L"SCS SDK configuration", L"sample");
    snapshot.vehicle_plate.Set(L"NLSI 1", L"SCS SDK configuration", L"sample");
    snapshot.trailer.Set(L"Schmitz Refrigerated", L"SCS SDK configuration", L"sample");
    snapshot.trailer_plate.Set(L"TR 2", L"SCS SDK configuration", L"sample");
    snapshot.fuel_liters.Set(500.0, L"TruckSim GPS", L"sample");
    snapshot.fuel_range_km.Set(180.0, L"TruckSim GPS", L"sample");
    snapshot.rpm.Set(1500.0, L"TruckSim GPS", L"sample");
    snapshot.gear.Set(6.0, L"TruckSim GPS", L"sample");
    snapshot.effective_throttle.Set(0.25, L"TruckSim GPS", L"sample");
    snapshot.effective_brake.Set(0.05, L"TruckSim GPS", L"sample");
    snapshot.cruise_control_active.Set(true, L"TruckSim GPS", L"sample");
    snapshot.cruise_control_speed.Set(94.0, L"TruckSim GPS", L"sample");
    snapshot.retarder_level.Set(2.0, L"TruckSim GPS", L"sample");
    snapshot.special_job.Set(L"true", L"TruckSim GPS", L"sample");
    state.job.available = true;
    state.job.nlsi_job_id = L"JOB-NLSI-0042";
    state.job.cargo.Set(L"Furniture", L"TruckSim GPS", L"sample");
    state.job.income.Set(L"$25,000", L"TruckSim GPS", L"sample");
    state.job.source_city.Set(L"Berlin", L"TruckSim GPS", L"sample");
    state.job.destination_city.Set(L"Paris", L"TruckSim GPS", L"sample");
    state.job.planned_distance.Set(L"1200", L"TruckSim GPS", L"sample");
    state.job_status = nlsi::telemetry::JobStatus::InTransit;
    state.progress.progress_percent = 25.0;
    state.progress.remaining_distance_km = 100.0;
    state.progress.eta_seconds = 3661.0;
    page.UpdateState(state);

    const QHash<QString, QString> expected = {
        {QStringLiteral("jobId"), QStringLiteral("JOB-NLSI-0042")},
        {QStringLiteral("jobStatus"), QStringLiteral("IN TRANSIT")},
        {QStringLiteral("cargo"), QStringLiteral("Furniture")},
        {QStringLiteral("income"), QStringLiteral("$25,000")},
        {QStringLiteral("source"), QStringLiteral("Berlin")},
        {QStringLiteral("destination"), QStringLiteral("Paris")},
        {QStringLiteral("plannedDistance"), QStringLiteral("745.65 MI")},
        {QStringLiteral("remainingDistance"), QStringLiteral("62.14 MI")},
        {QStringLiteral("progress"), QStringLiteral("25.0%")},
        {QStringLiteral("eta"), QStringLiteral("1 HR 2 MIN LEFT")},
        {QStringLiteral("fuel"), QStringLiteral("112 MI - 132.09 US GAL")},
        {QStringLiteral("game"), QStringLiteral("Euro Truck Simulator 2")},
        {QStringLiteral("gameVersion"), QStringLiteral("1.58.1.2s")},
        {QStringLiteral("vehicle"), QStringLiteral("Volvo FH16")},
        {QStringLiteral("vehiclePlate"), QStringLiteral("NLSI 1")},
        {QStringLiteral("trailer"), QStringLiteral("Schmitz Refrigerated")},
        {QStringLiteral("trailerPlate"), QStringLiteral("TR 2")},
    };
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const QLabel* field = FindDashboardField(page, it.key());
        if (!field
            || (it.key() == QStringLiteral("eta")
                ? !field->accessibleName().startsWith(it.value())
                : field->accessibleName() != it.value())) {
            std::cerr << "Dashboard field did not match its telemetry source: "
                      << it.key().toStdString() << " (expected '"
                      << it.value().toStdString() << "', received '"
                      << (field ? field->accessibleName().toStdString() : "missing")
                      << "')\n";
            return false;
        }
        const auto sections = page.findChildren<QFrame*>();
        QFrame* game_config = nullptr;
        for (QFrame* section : sections) {
            if (section->property("sectionKey").toString() == QStringLiteral("gameConfigSection")) {
                game_config = section;
                break;
            }
        }
        if (!game_config
            || game_config->findChildren<QLabel*>(QStringLiteral("dashboardFieldLabel")).size() != 6
            || !page.findChildren<QFrame*>(QStringLiteral("connectionSection")).isEmpty()) {
            std::cerr << "Dashboard Game Config does not contain only the six required fields.\n";
            return false;
        }
    }
    QLabel* special = page.findChild<QLabel*>(QStringLiteral("specialJobIndicator"));
    if (!special || special->isHidden()) {
        std::cerr << "The verified special-job indicator was not shown.\n";
        return false;
    }
    snapshot.special_job.Set(L"false", L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    if (!special->isHidden()) {
        std::cerr << "The special-job indicator remained visible when its flag was false.\n";
        return false;
    }
    snapshot.special_job.Set(L"true", L"TruckSim GPS", L"sample");
    snapshot.special_job.MarkStale();
    page.UpdateState(state);
    if (!special->isHidden()) {
        std::cerr << "A stale special-job flag remained visible.\n";
        return false;
    }

    const QList<QSize> window_sizes = {
        QSize(900, 600),
        QSize(1366, 768),
        QSize(1920, 1080),
    };
    int dashboard_value_size = 0;
    for (const auto& size : window_sizes) {
        page.resize(size.width(), size.height());
        page.show();
        QApplication::processEvents();
        int value_size = 0;
        for (const QLabel* label : page.findChildren<QLabel*>()) {
            if (label->objectName() != QStringLiteral("dashboardSectionTitle")
                && label->objectName() != QStringLiteral("dashboardFieldLabel")
                && label->objectName() != QStringLiteral("dashboardValue")
                && label->objectName() != QStringLiteral("cruiseControlActiveIndicator")
                && label->objectName() != QStringLiteral("specialJobIndicator")) {
                continue;
            }
            const int pixel_size = label->font().pixelSize();
            const int expected_size = label->objectName()
                    == QStringLiteral("dashboardSectionTitle")
                ? 12 : 11;
            if (pixel_size != expected_size) {
                std::cerr << "Dashboard typography differs from the shared 11/12 px sizes: "
                          << pixel_size << ".\n";
                return false;
            }
            if (label->objectName() == QStringLiteral("dashboardValue")) {
                value_size = pixel_size;
                if (label->wordWrap() || label->minimumWidth() != 0) {
                    std::cerr << "A dashboard value cannot wrap and shrink to its cell width.\n";
                    return false;
                }
            }
        }
        if (value_size != 11
            || (dashboard_value_size != 0 && value_size != dashboard_value_size)) {
            std::cerr << "Dashboard values did not retain the shared 11 px size while resizing.\n";
            return false;
        }
        dashboard_value_size = value_size;
    }

    state.progress.eta_seconds = -1.0;
    page.UpdateState(state);
    if (FindDashboardField(page, QStringLiteral("eta"))->text()
        != QStringLiteral("N/A")) {
        std::cerr << "An invalid ETA was not presented as unavailable.\n";
        return false;
    }
    state.job.cargo.Set(std::wstring(300, L'W'), L"TruckSim GPS", L"sample");
    page.UpdateState(state);
    page.resize(900, 600);
    QApplication::processEvents();
    const QLabel* long_value = FindDashboardField(page, QStringLiteral("cargo"));
    if (!long_value || long_value->wordWrap()
        || long_value->toolTip().size() < 300
        || long_value->text().size() > 300) {
        std::cerr << "A long dashboard value wrapped or lost its complete accessible text.\n";
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
    if (!nlsi::gui::ExportJobToPdf(path, job, &error)) {
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
    nlsi::session::JobRecord cancelled_job{
        QStringLiteral("job-456"),
        QStringLiteral("Steel"),
        QStringLiteral("Hamburg"),
        QStringLiteral("Lyon"),
        QStringLiteral("Cancelled"),
        QStringLiteral("2026-10-07T09:36:46.123Z"),
        {{QStringLiteral("income"), QStringLiteral("12500")}},
        QStringLiteral("JOB-NLSI-0042"),
    };
    const QString cancelled_path = directory.filePath(QStringLiteral("cancelled-job.pdf"));
    if (!nlsi::gui::ExportJobToPdf(cancelled_path, cancelled_job, &error)
        || !QFileInfo::exists(cancelled_path)
        || QFileInfo(cancelled_path).size() < 1000
        || !cancelled_job.details.contains(QStringLiteral("income"))
        || job.identity != QStringLiteral("job-123")
        || cancelled_job.identity != QStringLiteral("job-456")) {
        std::cerr << "Per-job PDF exports did not remain isolated to their selected records.\n";
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
        for (int attempt = 0; attempt < 500 && activations == 0; ++attempt) {
            application.processEvents();
            QThread::msleep(1);
        }
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
        QStringLiteral("1.5.3-beta"),
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
    const nlsi::gui::MeasurementSystem previous_units =
        nlsi::gui::CurrentMeasurementSystem();
    struct RestoreMeasurementSystem {
        nlsi::gui::MeasurementSystem value;
        ~RestoreMeasurementSystem() {
            nlsi::gui::SetMeasurementSystem(value);
        }
    } restore_units{previous_units};
    nlsi::gui::SetMeasurementSystem(nlsi::gui::MeasurementSystem::Metric);

    QFile stylesheet(QStringLiteral(":/styles/app.qss"));
    if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "The application stylesheet could not be loaded for UI tests.\n";
        return 1;
    }
    application.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    application.setApplicationVersion(QStringLiteral("v1.5.3-beta"));
    if (!TestModLogParsingAndSourceLinks()
        || !TestIncrementalGameLogMonitoring()
        || !TestMonitorStartsBeforeGame()
        || !TestHistoryPagesLoadPersistedRows()) {
        return 1;
    }
    if (!TestNumberAndTimeFormatting()
        || !TestGlobalTypographySizes()
        || !TestShutdownIsIdempotent()
        || !TestProviderSurfaceSelectsTruckSimOnly()
        || !TestDashboardCruiseControlIndicators()
        || !TestLowFuelWarningAndParkingBrakeStates()
        || !TestMeasurementUnitConversionsAndSettings()
        || !TestDashboardLayoutAndResponsiveText()
        || !TestCompletedJobsPdfExport()) {
        return 1;
    }
    nlsi::telemetry::TelemetryCore telemetry_core;
    nlsi::gui::MainWindow window(L"NLSI Exclusive Logbook", L"v1.5.3-beta",
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
    auto* page_stack = window.findChild<QStackedWidget*>(QStringLiteral("pageStack"));
    auto* header_clock = window.findChild<QLabel*>(QStringLiteral("headerClock"));
    auto* history_refresh_timer = window.findChild<QTimer*>(
        QStringLiteral("historyRefreshTimer"));
    if (!logo || !brand_title || !brand_subtitle || !sidebar ||
        !page_title || !page_stack || !header_clock || !history_refresh_timer) {
        std::cerr << "The brand or shared page stack is missing.\n";
        return 1;
    }
    if (history_refresh_timer->interval() != 250
        || history_refresh_timer->thread() != window.thread()) {
        std::cerr << "Persisted history is not polled for GUI refresh on the GUI thread.\n";
        return 1;
    }
    if (!window.findChildren<QLabel*>(QStringLiteral("versionBadge")).isEmpty()) {
        std::cerr << "A version badge is still present in the page header.\n";
        return 1;
    }
    const QRegularExpression clock_pattern(
        QStringLiteral("^[A-Z]{3,9} \\d{1,2}, \\d{4} - \\d{2}:\\d{2}:\\d{2} "
            "- ASIA/MANILA \\| PING: --$"));
    if (!clock_pattern.match(header_clock->text()).hasMatch()
        || header_clock->wordWrap()
        || page_title->text() != QStringLiteral("DASHBOARD")
        || page_title->font().pixelSize() != 14
        || header_clock->geometry().intersects(page_title->geometry())) {
        std::cerr << "The Manila wall clock or unavailable ping display is incorrect.\n";
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
    auto* about_button = FindButton(window, QStringLiteral("ABOUT"));
    auto* dashboard_button = FindButton(window, QStringLiteral("DASHBOARD"));
    auto* jobs_button = FindButton(window, QStringLiteral("COMPLETED JOBS"));
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
        || page_title->text() != QStringLiteral("COMPLETED JOBS")
        || !icon_has_color(jobs_button->icon(), QColor(QStringLiteral("#FFFFFF")))
        || !icon_has_color(dashboard_button->icon(), QColor(QStringLiteral("#F896B9")))) {
        std::cerr << "Navigation icon colors did not update immediately on selection.\n";
        return 1;
    }
    about_button->click();
    application.processEvents();
    if (page_title->text() != QStringLiteral("ABOUT")) {
        std::cerr << "The shared header did not update its title when the page changed.\n";
        return 1;
    }
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
            || label->text() == QStringLiteral("v1.5.3-beta");
        found_beta_channel = found_beta_channel
            || label->text() == QStringLiteral("Beta");
        if (label->text() == QStringLiteral("PRODUCT") && label->parentWidget()) {
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
            if (!name || !value || !name->wordWrap() || !value->wordWrap()
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
            if (label->text() == QStringLiteral("PRODUCT") && label->parentWidget()) {
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
    auto* settings_button = FindButton(window, QStringLiteral("SETTINGS"));
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
        QStringLiteral("DASHBOARD"),
        QStringLiteral("COMPLETED JOBS"),
        QStringLiteral("HISTORY"),
        QStringLiteral("EVENTS"),
        QStringLiteral("SETTINGS"),
        QStringLiteral("ABOUT"),
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
        FindButton(window, QStringLiteral("DASHBOARD"))->click();
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
    FindButton(window, QStringLiteral("DASHBOARD"))->click();
    application.processEvents();
    if (!CheckLayout(window)) {
        std::cerr << "Sidebar or page layout failed when maximized.\n";
        return 1;
    }

    auto* close_button = FindButton(window, QStringLiteral("CLOSE"));
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
    application.processEvents();
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
