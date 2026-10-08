#include "JobHistoryPage.h"
#include "CurrentJobPage.h"

#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace nlsi::gui {
namespace {

void ConfigureTable(QTableView* table, QStandardItemModel* model) {
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

QWidget* MakeHistoryPage(
    const QString& title,
    const QString& description,
    const QStringList& columns,
    QLabel*& message,
    QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(18);
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto* subtitle = new QLabel(description, page);
    subtitle->setObjectName(QStringLiteral("pageDescription"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);
    auto* message_card = new QFrame(page);
    message_card->setObjectName(QStringLiteral("contentCard"));
    auto* message_layout = new QVBoxLayout(message_card);
    message = new QLabel(page);
    message->setObjectName(QStringLiteral("detailLabel"));
    message->setWordWrap(true);
    message_layout->addWidget(message);
    layout->addWidget(message_card);
    auto* table = new QTableView(page);
    table->setObjectName(QStringLiteral("dataTable"));
    auto* model = new QStandardItemModel(table);
    model->setHorizontalHeaderLabels(columns);
    ConfigureTable(table, model);
    layout->addWidget(table, 1);
    return page;
}

} // namespace

JobHistoryPage::JobHistoryPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        QStringLiteral("Job History"),
        QStringLiteral("Completed and cancelled deliveries require a persisted native job event source."),
        {QStringLiteral("Cargo"), QStringLiteral("Source"), QStringLiteral("Destination"),
            QStringLiteral("Status"), QStringLiteral("Progress")},
        message_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void JobHistoryPage::UpdateState(const telemetry::TelemetryUiState&) {
    const QString text = QStringLiteral("No historical job records are available from the native C++ backend.");
    if (message_->text() != text) {
        message_->setText(text);
    }
}

SessionsPage::SessionsPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        QStringLiteral("Sessions"),
        QStringLiteral("Session status follows the live SCS telemetry lifecycle."),
        {QStringLiteral("Session"), QStringLiteral("Started"), QStringLiteral("Ended"),
            QStringLiteral("Status")},
        message_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void SessionsPage::UpdateState(const telemetry::TelemetryUiState& state) {
    const QString text = QStringLiteral("Current session status: %1. Native session history is not persisted.")
        .arg(QString::fromStdWString(telemetry::FormatSessionStatus(state.session.status)));
    if (message_->text() != text) {
        message_->setText(text);
    }
}

EventsPage::EventsPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        QStringLiteral("Events"),
        QStringLiteral("Native event persistence is not currently available."),
        {QStringLiteral("Timestamp"), QStringLiteral("Source"), QStringLiteral("Event"),
            QStringLiteral("Details")},
        message_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void EventsPage::UpdateState(const telemetry::TelemetryUiState&) {
    const QString text = QStringLiteral("No native event stream is currently available.");
    if (message_->text() != text) {
        message_->setText(text);
    }
}

ActiveModsPage::ActiveModsPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(18);
    auto* heading = new QLabel(QStringLiteral("Active Mods"), this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Mod enumeration is not provided reliably by the SCS telemetry packet."),
        this);
    description->setObjectName(QStringLiteral("pageDescription"));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("contentCard"));
    auto* card_layout = new QVBoxLayout(card);
    message_ = new QLabel(QStringLiteral("Unavailable from the active telemetry API."), card);
    message_->setObjectName(QStringLiteral("detailLabel"));
    message_->setWordWrap(true);
    card_layout->addWidget(message_);
    layout->addWidget(card);
    layout->addStretch(1);
}

void ActiveModsPage::UpdateState(const telemetry::TelemetryUiState&) {
}

JobsPage::JobsPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(18);
    auto* heading = new QLabel(QStringLiteral("Jobs"), this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Current delivery details and a completed-jobs view. Native job history is not persisted."),
        this);
    description->setObjectName(QStringLiteral("pageDescription"));
    layout->addWidget(description);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("contentTabs"));
    current_job_ = new CurrentJobPage(tabs_);
    completed_jobs_ = new JobHistoryPage(tabs_);
    tabs_->addTab(current_job_, QStringLiteral("Current job"));
    tabs_->addTab(completed_jobs_, QStringLiteral("Completed jobs"));
    layout->addWidget(tabs_, 1);
}

void JobsPage::UpdateState(const telemetry::TelemetryUiState& state) {
    current_job_->UpdateState(state);
    completed_jobs_->UpdateState(state);
}

HistoryPage::HistoryPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(18);
    auto* heading = new QLabel(QStringLiteral("History"), this);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    auto* description = new QLabel(
        QStringLiteral("Session, trip, and completed-job history. Native history records are not currently persisted."),
        this);
    description->setObjectName(QStringLiteral("pageDescription"));
    description->setWordWrap(true);
    layout->addWidget(description);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("contentTabs"));
    sessions_ = new SessionsPage(tabs_);
    completed_jobs_ = new JobHistoryPage(tabs_);
    tabs_->addTab(sessions_, QStringLiteral("Sessions & trips"));
    tabs_->addTab(completed_jobs_, QStringLiteral("Completed jobs"));
    layout->addWidget(tabs_, 1);
}

void HistoryPage::UpdateState(const telemetry::TelemetryUiState& state) {
    sessions_->UpdateState(state);
    completed_jobs_->UpdateState(state);
}

} // namespace nlsi::gui
