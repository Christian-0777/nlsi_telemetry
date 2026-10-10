#include "MainWindow.h"

#include <QFrame>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QDebug>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPushButton>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include "AboutPage.h"
#include "DashboardPage.h"
#include "IconTheme.h"
#include "JobHistoryPage.h"
#include "SettingsPage.h"
#include "time/ApplicationTime.h"

namespace nlsi::gui {
namespace {

class UppercaseButtonTextFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Paint) {
            if (auto* button = qobject_cast<QAbstractButton*>(watched)) {
                const QString upper = button->text().toUpper();
                if (button->text() != upper) {
                    button->setText(upper);
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

MainWindow::MainWindow(
    const std::wstring& title,
    const std::wstring& version,
    telemetry::TelemetryCore& telemetry_core,
    QWidget* parent)
    : QMainWindow(parent),
      telemetry_core_(telemetry_core) {
    setWindowTitle(QString::fromStdWString(title));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));
    QCoreApplication::instance()->installEventFilter(new UppercaseButtonTextFilter(this));
    setMinimumSize(900, 600);
    resize(900, 600);

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    auto* root_layout = new QHBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* sidebar = new QFrame(root);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(210);
    sidebar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_layout->setSpacing(4);

    auto* brand = new QWidget(sidebar);
    auto* brand_layout = new QHBoxLayout(brand);
    brand_layout->setContentsMargins(12, 18, 12, 0);
    brand_layout->setSpacing(10);
    auto* logo = new QLabel(brand);
    logo->setObjectName(QStringLiteral("brandLogo"));
    logo->setPixmap(QPixmap(QStringLiteral(":/images/logo.png"))
        .scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(44, 44);
    logo->setAlignment(Qt::AlignCenter);
    brand_layout->addWidget(logo);

    auto* brand_text = new QVBoxLayout();
    brand_text->setContentsMargins(0, 0, 0, 0);
    brand_text->setSpacing(0);
    auto* brand_title = new QLabel(QStringLiteral("NABSKI"), brand);
    brand_title->setObjectName(QStringLiteral("brandTitle"));
    brand_text->addWidget(brand_title);
    auto* product = new QLabel(QStringLiteral("Logistics Solutions Inc."), brand);
    product->setObjectName(QStringLiteral("brandSubtitle"));
    product->setWordWrap(true);
    brand_text->addWidget(product);
    brand_layout->addLayout(brand_text, 1);
    sidebar_layout->addWidget(brand);
    sidebar_layout->addSpacing(18);

    auto* content = new QWidget(root);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(20, 16, 20, 10);
    content_layout->setSpacing(12);

    auto* header = new QFrame(content);
    header->setObjectName(QStringLiteral("appHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(18, 12, 18, 12);
    active_page_title_ = new QLabel(QStringLiteral("Dashboard"), header);
    active_page_title_->setObjectName(QStringLiteral("headerTitle"));
    active_page_title_->setMinimumWidth(0);
    active_page_title_->setWordWrap(false);
    active_page_title_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    const QString version_label = QString::fromStdWString(version);
    header_clock_ = new QLabel(header);
    header_clock_->setObjectName(QStringLiteral("headerClock"));
    header_clock_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header_clock_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    header_clock_->setWordWrap(false);
    header_clock_->setMinimumWidth(0);
    header_clock_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header_layout->addWidget(active_page_title_, 0, Qt::AlignLeft | Qt::AlignVCenter);
    header_layout->addWidget(header_clock_, 1);
    content_layout->addWidget(header);

    page_stack_ = new QStackedWidget(content);
    page_stack_->setObjectName(QStringLiteral("pageStack"));
    page_stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    page_stack_->setMinimumSize(0, 0);
    content_layout->addWidget(page_stack_, 1);

    struct NavigationEntry {
        QString key;
        QString title;
        QString icon;
        StatePage* page;
    };
    const QList<NavigationEntry> workspace_entries = {
        {QStringLiteral("dashboard"), QStringLiteral("DASHBOARD"),
            QStringLiteral("dashboard"),
            new DashboardPage(this)},
        {QStringLiteral("jobs"), QStringLiteral("COMPLETED JOBS"),
            QStringLiteral("jobs"),
            new JobsPage(this)},
        {QStringLiteral("history"), QStringLiteral("HISTORY"),
            QStringLiteral("history"),
            new HistoryPage(this)},
        {QStringLiteral("events"), QStringLiteral("EVENTS"),
            QStringLiteral("events"),
            new EventsPage(this)},
    };
    const QList<NavigationEntry> system_entries = {
        {QStringLiteral("settings"), QStringLiteral("SETTINGS"),
            QStringLiteral("settings"),
            new SettingsPage(this)},
        {QStringLiteral("about"), QStringLiteral("ABOUT"),
            QStringLiteral("about"),
            new AboutPage(version_label, this)},
    };

    auto* workspace_label = new QLabel(QStringLiteral("WORKSPACE"), sidebar);
    workspace_label->setObjectName(QStringLiteral("sectionLabel"));
    sidebar_layout->addWidget(workspace_label);

    const auto add_navigation_entry = [this, sidebar, sidebar_layout](
                                          const NavigationEntry& entry) {
        auto* button = new QToolButton(sidebar);
        button->setObjectName(QStringLiteral("navigationButton"));
        button->setText(entry.title.toUpper());
        button->setIcon(NavigationIcon(entry.icon, false));
        button->setIconSize(QSize(18, 18));
        button->setToolTip(entry.title);
        button->setAccessibleName(entry.title);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sidebar_layout->addWidget(button);
        navigation_buttons_.insert(entry.key, button);
        pages_.insert(entry.key, entry.page);
        auto* page_scroll = new QScrollArea(page_stack_);
        page_scroll->setObjectName(QStringLiteral("pageScroll"));
        page_scroll->setWidgetResizable(true);
        page_scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        page_scroll->setMinimumSize(0, 0);
        page_scroll->setFrameShape(QFrame::NoFrame);
        page_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        page_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        page_scroll->setWidget(entry.page);
        page_scroll_areas_.insert(entry.key, page_scroll);
        page_stack_->addWidget(page_scroll);
        connect(button, &QToolButton::clicked, this, [this, key = entry.key] {
            ActivatePage(key);
        });
    };

    for (const auto& entry : workspace_entries) {
        add_navigation_entry(entry);
    }

    auto* system_label = new QLabel(QStringLiteral("SYSTEM"), sidebar);
    system_label->setObjectName(QStringLiteral("sectionLabel"));
    system_label->setContentsMargins(0, 10, 0, 0);
    sidebar_layout->addWidget(system_label);
    for (const auto& entry : system_entries) {
        add_navigation_entry(entry);
    }

    sidebar_layout->addStretch(1);
    auto* close_button = new QToolButton(sidebar);
    close_button->setObjectName(QStringLiteral("navigationButton"));
    close_button->setText(QStringLiteral("CLOSE"));
    close_button->setIcon(NavigationIcon(QStringLiteral("close"), false));
    close_button->setIconSize(QSize(18, 18));
    close_button->setToolTip(QStringLiteral("Close NLSI Exclusive Logbook"));
    close_button->setAccessibleName(QStringLiteral("Close NLSI Exclusive Logbook"));
    close_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    close_button->setAutoRaise(true);
    close_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    sidebar_layout->addWidget(close_button);
    connect(close_button, &QToolButton::clicked, this, &QWidget::close);

    root_layout->addWidget(sidebar);
    root_layout->addWidget(content, 1);
    setCentralWidget(root);

    connection_indicator_ = new QLabel(QStringLiteral("NOT CONNECTED"), this);
    connection_indicator_->setObjectName(QStringLiteral("statusIndicator"));
    statusBar()->addPermanentWidget(connection_indicator_);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        notification_tray_ = new QSystemTrayIcon(windowIcon(), this);
        notification_tray_->setToolTip(QStringLiteral("NLSI Exclusive Logbook"));
        notification_tray_->show();
    }

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setObjectName(QStringLiteral("historyRefreshTimer"));
    refresh_timer_->setInterval(250);
    connect(refresh_timer_, &QTimer::timeout, this, [this] {
        RefreshState();
    });
    clock_timer_ = new QTimer(this);
    clock_timer_->setTimerType(Qt::PreciseTimer);
    clock_timer_->setInterval(1000);
    connect(clock_timer_, &QTimer::timeout, this, &MainWindow::RefreshClock);
    shutdown_timer_ = new QTimer(this);
    shutdown_timer_->setInterval(200);
    connect(shutdown_timer_, &QTimer::timeout, this, &MainWindow::PollShutdown);
    ActivatePage(QStringLiteral("dashboard"));
    RefreshClock();
    RefreshState();
    refresh_timer_->start();
    clock_timer_->start();
}

void MainWindow::ActivatePage(const QString& key) {
    StatePage* page = pages_.value(key, nullptr);
    if (!page) {
        return;
    }
    page_stack_->setCurrentWidget(page_scroll_areas_.value(key));
    active_page_title_->setText(navigation_buttons_.value(key)->text().toUpper());
    active_page_title_->setProperty(
        "dashboardActive", key == QStringLiteral("dashboard"));
    active_page_title_->style()->unpolish(active_page_title_);
    active_page_title_->style()->polish(active_page_title_);
    RefreshClock();
    for (auto it = navigation_buttons_.cbegin(); it != navigation_buttons_.cend(); ++it) {
        const bool selected = it.key() == key;
        it.value()->setChecked(selected);
        it.value()->setIcon(NavigationIcon(it.key(), selected));
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    RefreshClock();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (shutdown_complete_) {
        event->accept();
        return;
    }
    if (!shutdown_started_) {
        QMessageBox confirmation(QMessageBox::Question,
            QStringLiteral("Exit NLSI Exclusive Logbook"),
            QStringLiteral("Are you sure you want to exit NLSI Exclusive Logbook?"),
            QMessageBox::NoButton,
            this);
        QPushButton* exit_button = confirmation.addButton(
            QStringLiteral("Exit"), QMessageBox::AcceptRole);
        QPushButton* cancel_button = confirmation.addButton(
            QStringLiteral("Cancel"), QMessageBox::RejectRole);
        confirmation.setDefaultButton(cancel_button);
        confirmation.setEscapeButton(cancel_button);
        confirmation.exec();
        if (confirmation.clickedButton() != exit_button) {
            event->ignore();
            return;
        }
        shutdown_started_ = true;
        telemetry_core_.BeginShutdown();
    }
    event->ignore();
    if (!shutdown_dialog_) {
        shutdown_dialog_ = new QProgressDialog(
            QStringLiteral("Stopping telemetry producers and saving accepted local records."),
            QStringLiteral("Keep app open"), 0, 0, this);
        shutdown_dialog_->setWindowTitle(QStringLiteral("Saving local data"));
        shutdown_dialog_->setWindowModality(Qt::WindowModal);
        shutdown_dialog_->setMinimumDuration(0);
        shutdown_dialog_->setAutoClose(false);
        shutdown_dialog_->setAutoReset(false);
        connect(shutdown_dialog_, &QProgressDialog::canceled, this, [this] {
            shutdown_timer_->stop();
            shutdown_dialog_->hide();
        });
    }
    shutdown_dialog_->show();
    shutdown_wait_.start();
    shutdown_timer_->start();
    PollShutdown();
}

void MainWindow::RefreshClock() {
    const QDateTime manila_time = nlsi::time::NowLocal();
    const bool compact = header_clock_->width() < 400;
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const QString text = manila_time.isValid()
        ? QStringLiteral("%1 - %2 - ASIA/MANILA | PING: --")
            .arg(english.toString(manila_time, compact
                    ? QStringLiteral("MMM d, yyyy")
                    : QStringLiteral("MMMM d, yyyy"))
                    .toUpper(),
                manila_time.toString(QStringLiteral("HH:mm:ss")))
        : QStringLiteral("TIME UNAVAILABLE - ASIA/MANILA | PING: --");
    if (header_clock_->text() != text) {
        header_clock_->setText(text);
    }
}

void MainWindow::PollShutdown() {
    const auto progress = telemetry_core_.PollShutdown();
    if (progress.state == telemetry::TelemetryCore::ShutdownState::Completed) {
        shutdown_timer_->stop();
        shutdown_complete_ = true;
        if (shutdown_dialog_) {
            shutdown_dialog_->hide();
        }
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }
    if (progress.state == telemetry::TelemetryCore::ShutdownState::Failed) {
        shutdown_timer_->stop();
        if (shutdown_dialog_) {
            shutdown_dialog_->hide();
        }
        QMessageBox prompt(QMessageBox::Critical,
            QStringLiteral("Local data could not be fully saved"),
            QStringLiteral("%1\n\n%2 accepted local write(s) remain queued. "
                "Writes already copied to recovery files will be retried at next start; "
                "an in-memory write that could not be copied may be lost if you exit. "
                "Exiting now will not be reported as a clean shutdown.")
                .arg(QString::fromStdWString(progress.error))
                .arg(progress.queued_writes),
            QMessageBox::NoButton,
            this);
        QPushButton* exit_button = prompt.addButton(
            QStringLiteral("Exit with write error"), QMessageBox::DestructiveRole);
        QPushButton* keep_button = prompt.addButton(
            QStringLiteral("Keep app open"), QMessageBox::RejectRole);
        prompt.setDefaultButton(keep_button);
        prompt.exec();
        if (prompt.clickedButton() == exit_button) {
            shutdown_complete_ = true;
            QTimer::singleShot(0, this, &QWidget::close);
        }
        return;
    }

    if (shutdown_dialog_) {
        shutdown_dialog_->setLabelText(
            QStringLiteral("Draining accepted local telemetry writes: %1 queued; "
                "%2 local-only records pending synchronization.")
                .arg(progress.queued_writes)
                .arg(progress.pending_records));
    }
    if (shutdown_wait_.elapsed() >= 5000) {
        shutdown_wait_.restart();
        QMessageBox prompt(QMessageBox::Warning,
            QStringLiteral("Telemetry shutdown is taking longer than expected"),
            QStringLiteral("%1 accepted write(s) remain in progress. You may keep waiting "
                "or leave the application open. The writer will not be forcibly terminated.")
                .arg(progress.queued_writes),
            QMessageBox::NoButton,
            this);
        QPushButton* wait_button = prompt.addButton(
            QStringLiteral("Keep waiting"), QMessageBox::AcceptRole);
        QPushButton* open_button = prompt.addButton(
            QStringLiteral("Keep app open"), QMessageBox::RejectRole);
        prompt.setDefaultButton(wait_button);
        prompt.exec();
        if (prompt.clickedButton() == open_button) {
            shutdown_timer_->stop();
            if (shutdown_dialog_) {
                shutdown_dialog_->hide();
            }
        }
    }
}

void MainWindow::RefreshState() {
    const auto state = telemetry_core_.UiState();
    const auto history = telemetry_core_.History();
    for (const std::wstring& event_id : telemetry_core_.TakeNewlyCompletedJobNotifications()) {
        if (!notification_tray_) {
            qWarning("Desktop notification unavailable: Windows system tray is not available.");
            continue;
        }
        Q_UNUSED(event_id);
        notification_tray_->showMessage(
            QStringLiteral("NLSI Exclusive Logbook"),
            QStringLiteral("Job has finished"),
            QSystemTrayIcon::Information,
            5000);
    }
    for (const std::wstring& message : telemetry_core_.TakeNewExpenseNotifications()) {
        if (!notification_tray_) {
            qWarning("Expense notification unavailable: Windows system tray is not available.");
            continue;
        }
        notification_tray_->showMessage(
            QStringLiteral("NLSI Exclusive Logbook · Travel expense"),
            QString::fromStdWString(message),
            QSystemTrayIcon::Information,
            5000);
    }

    for (StatePage* page : pages_) {
        page->UpdateState(state);
        page->UpdateHistory(history);
    }

    const QString connection_text = state.fast.values.connected
        ? QStringLiteral("CONNECTED")
        : QStringLiteral("NOT CONNECTED");
    if (connection_indicator_->text() != connection_text) {
        connection_indicator_->setText(connection_text);
    }
}

} // namespace nlsi::gui
