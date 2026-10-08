#include "MainWindow.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include "AboutPage.h"
#include "DashboardPage.h"
#include "JobHistoryPage.h"
#include "LiveDrivePage.h"
#include "SettingsPage.h"

namespace nlsi::gui {

MainWindow::MainWindow(
    const std::wstring& title,
    const std::wstring& version,
    telemetry::TelemetryCore& telemetry_core,
    QWidget* parent)
    : QMainWindow(parent),
      telemetry_core_(telemetry_core) {
    setWindowTitle(QString::fromStdWString(title));
    setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));
    setMinimumSize(900, 600);
    resize(1240, 800);

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("appRoot"));
    auto* root_layout = new QHBoxLayout(root);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    auto* sidebar = new QFrame(root);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setMinimumWidth(190);
    sidebar->setMaximumWidth(220);
    auto* sidebar_layout = new QVBoxLayout(sidebar);
    sidebar_layout->setContentsMargins(14, 20, 14, 16);
    sidebar_layout->setSpacing(6);

    auto* logo = new QLabel(sidebar);
    logo->setObjectName(QStringLiteral("brandLogo"));
    logo->setPixmap(QPixmap(QStringLiteral(":/images/logo.png"))
        .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sidebar_layout->addWidget(logo);

    auto* brand = new QLabel(QStringLiteral("NLSI"), sidebar);
    brand->setObjectName(QStringLiteral("brandTitle"));
    sidebar_layout->addWidget(brand);
    auto* product = new QLabel(QStringLiteral("EXCLUSIVE LOGBOOK"), sidebar);
    product->setObjectName(QStringLiteral("brandSubtitle"));
    sidebar_layout->addWidget(product);
    auto* navigation_label = new QLabel(QStringLiteral("WORKSPACE"), sidebar);
    navigation_label->setObjectName(QStringLiteral("sectionLabel"));
    sidebar_layout->addWidget(navigation_label);
    sidebar_layout->addSpacing(8);

    auto* content = new QWidget(root);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(20, 16, 20, 10);
    content_layout->setSpacing(12);

    auto* header = new QFrame(content);
    header->setObjectName(QStringLiteral("appHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(18, 12, 18, 12);
    active_page_title_ = new QLabel(QStringLiteral("Dashboard"), header);
    active_page_title_->setObjectName(QStringLiteral("headerTitle"));
    const QString version_label = QString::fromStdWString(version);
    auto* header_version = new QLabel(version_label, header);
    header_version->setObjectName(QStringLiteral("versionBadge"));
    header_layout->addWidget(active_page_title_);
    header_layout->addStretch(1);
    header_layout->addWidget(header_version);
    content_layout->addWidget(header);

    page_stack_ = new QStackedWidget(content);
    page_stack_->setObjectName(QStringLiteral("pageStack"));
    auto* page_scroll = new QScrollArea(content);
    page_scroll->setObjectName(QStringLiteral("pageScroll"));
    page_scroll->setWidgetResizable(true);
    page_scroll->setFrameShape(QFrame::NoFrame);
    page_scroll->setWidget(page_stack_);
    content_layout->addWidget(page_scroll, 1);

    struct NavigationEntry {
        QString key;
        QString title;
        StatePage* page;
    };
    const QList<NavigationEntry> entries = {
        {QStringLiteral("dashboard"), QStringLiteral("Dashboard"), new DashboardPage(this)},
        {QStringLiteral("liveDrive"), QStringLiteral("Live Drive"), new LiveDrivePage(this)},
        {QStringLiteral("jobs"), QStringLiteral("Jobs"), new JobsPage(this)},
        {QStringLiteral("history"), QStringLiteral("History"), new HistoryPage(this)},
        {QStringLiteral("events"), QStringLiteral("Events"), new EventsPage(this)},
        {QStringLiteral("settings"), QStringLiteral("Settings"), new SettingsPage(this)},
        {QStringLiteral("about"), QStringLiteral("About"), new AboutPage(version_label, this)},
    };

    for (const auto& entry : entries) {
        auto* button = new QToolButton(sidebar);
        button->setObjectName(QStringLiteral("navigationButton"));
        button->setText(entry.title);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCheckable(true);
        button->setAutoRaise(true);
        sidebar_layout->addWidget(button);
        navigation_buttons_.insert(entry.key, button);
        pages_.insert(entry.key, entry.page);
        page_stack_->addWidget(entry.page);
        connect(button, &QToolButton::clicked, this, [this, key = entry.key] {
            ActivatePage(key);
        });
    }
    sidebar_layout->addStretch(1);
    root_layout->addWidget(sidebar);
    root_layout->addWidget(content, 1);
    setCentralWidget(root);

    connection_indicator_ = new QLabel(QStringLiteral("NLSI · CONNECTING"), this);
    connection_indicator_->setObjectName(QStringLiteral("statusIndicator"));
    statusBar()->addPermanentWidget(connection_indicator_);

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(250);
    connect(refresh_timer_, &QTimer::timeout, this, [this] {
        RefreshState();
    });
    ActivatePage(QStringLiteral("dashboard"));
    RefreshState();
    refresh_timer_->start();
}

void MainWindow::ActivatePage(const QString& key) {
    StatePage* page = pages_.value(key, nullptr);
    if (!page) {
        return;
    }
    page_stack_->setCurrentWidget(page);
    active_page_title_->setText(navigation_buttons_.value(key)->text());
    for (auto it = navigation_buttons_.cbegin(); it != navigation_buttons_.cend(); ++it) {
        it.value()->setChecked(it.key() == key);
    }
}

void MainWindow::RefreshState() {
    const auto state = telemetry_core_.UiState();

    for (StatePage* page : pages_) {
        page->UpdateState(state);
    }

    const QString provider = QString::fromStdWString(telemetry::FormatStatus(state.providers.nlsi));
    const QString connection_text = QStringLiteral("NLSI · %1").arg(provider);
    if (connection_indicator_->text() != connection_text) {
        connection_indicator_->setText(connection_text);
    }
}

} // namespace nlsi::gui
