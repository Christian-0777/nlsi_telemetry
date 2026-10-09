#include "JobHistoryPage.h"
#include "CurrentJobPage.h"

#include <QFrame>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include "JobPdfExporter.h"

namespace nlsi::gui {
namespace {

void ConfigureTable(QTableView* table, QStandardItemModel* model) {
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

QWidget* MakeHistoryPage(
    const QStringList& columns,
    QLabel*& message,
    QStandardItemModel*& model,
    QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(14);
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
    model = new QStandardItemModel(table);
    model->setHorizontalHeaderLabels(columns);
    ConfigureTable(table, model);
    layout->addWidget(table, 1);
    return page;
}

} // namespace

JobHistoryPage::JobHistoryPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        {QStringLiteral("Timestamp"), QStringLiteral("Cargo"), QStringLiteral("Source"),
            QStringLiteral("Destination"), QStringLiteral("Earnings"),
            QStringLiteral("Planned distance"), QStringLiteral("Status")},
        message_,
        model_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    export_button_ = new QPushButton(QStringLiteral("Export to PDF"), this);
    export_button_->setObjectName(QStringLiteral("exportJobsPdfButton"));
    export_button_->setEnabled(false);
    layout->addWidget(export_button_, 0, Qt::AlignRight);
    layout->addWidget(content);
    connect(export_button_, &QPushButton::clicked, this, [this] {
        if (jobs_.isEmpty()) {
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Export completed jobs"),
            QStringLiteral("completed-jobs.pdf"),
            QStringLiteral("PDF files (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        QString error;
        if (!ExportJobsToPdf(path, jobs_, &error)) {
            QMessageBox::critical(this, QStringLiteral("PDF export failed"), error);
            return;
        }
        QMessageBox::information(
            this,
            QStringLiteral("PDF exported"),
            QStringLiteral("Completed job records were exported to %1.").arg(path));
    });
}

void JobHistoryPage::UpdateState(const telemetry::TelemetryUiState&) {
}

void JobHistoryPage::UpdateHistory(const session::HistorySnapshot& history) {
    jobs_ = history.jobs;
    export_button_->setEnabled(!jobs_.isEmpty());
    if (history_revision_ != history.revision) {
        model_->removeRows(0, model_->rowCount());
        for (const auto& job : history.jobs) {
            const int row = model_->rowCount();
            model_->insertRow(row);
            model_->setItem(row, 0, new QStandardItem(TimestampText(job.timestamp)));
            model_->setItem(row, 1, new QStandardItem(job.cargo));
            model_->setItem(row, 2, new QStandardItem(job.source));
            model_->setItem(row, 3, new QStandardItem(job.destination));
            const QJsonValue income = job.details.value(QStringLiteral("income"));
            const QJsonValue planned_distance =
                job.details.value(QStringLiteral("planned_distance_km"));
            model_->setItem(row, 4, new QStandardItem(
                income.isUndefined() || income.isNull()
                    ? QStringLiteral("--")
                    : NumericText(income.toVariant().toString())));
            const QString planned_text = planned_distance.isUndefined() || planned_distance.isNull()
                ? QStringLiteral("--")
                : NumericText(planned_distance.toVariant().toString()) + QStringLiteral(" km");
            model_->setItem(row, 5, new QStandardItem(planned_text));
            model_->setItem(row, 6, new QStandardItem(job.status));
        }
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("History error: %1").arg(history.error)
        : history.jobs.isEmpty()
            ? QStringLiteral("No completed or cancelled jobs have been recorded.")
        : QStringLiteral("%1 completed or cancelled job records.")
            .arg(QLocale(QLocale::English, QLocale::UnitedStates).toString(history.jobs.size()));
    if (message_->text() != text) message_->setText(text);
}

SessionsPage::SessionsPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        {QStringLiteral("Game"), QStringLiteral("Started"), QStringLiteral("Ended"),
            QStringLiteral("Status")},
        message_,
        model_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void SessionsPage::UpdateState(const telemetry::TelemetryUiState& state) {
    live_status_ = QStringLiteral("Current session status: %1.")
        .arg(QString::fromStdWString(telemetry::FormatSessionStatus(state.session.status)));
}

void SessionsPage::UpdateHistory(const session::HistorySnapshot& history) {
    if (history_revision_ != history.revision) {
        model_->removeRows(0, model_->rowCount());
        for (const auto& session : history.sessions) {
            const int row = model_->rowCount();
            model_->insertRow(row);
            model_->setItem(row, 0, new QStandardItem(session.game));
            model_->setItem(row, 1, new QStandardItem(TimestampText(session.started_at)));
            model_->setItem(row, 2, new QStandardItem(TimestampText(session.ended_at)));
            model_->setItem(row, 3, new QStandardItem(session.result));
        }
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("%1 History error: %2").arg(live_status_, history.error)
        : QStringLiteral("%1 %2 session records.")
            .arg(live_status_)
            .arg(history.sessions.size());
    if (message_->text() != text) message_->setText(text);
}

EventsPage::EventsPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        {QStringLiteral("Timestamp"), QStringLiteral("Source"), QStringLiteral("Event"),
            QStringLiteral("Details")},
        message_,
        model_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void EventsPage::UpdateState(const telemetry::TelemetryUiState&) {
}

void EventsPage::UpdateHistory(const session::HistorySnapshot& history) {
    if (history_revision_ != history.revision) {
        model_->removeRows(0, model_->rowCount());
        for (const auto& event : history.events) {
            const int row = model_->rowCount();
            model_->insertRow(row);
            model_->setItem(row, 0, new QStandardItem(TimestampText(event.timestamp)));
            model_->setItem(row, 1, new QStandardItem(event.source));
            model_->setItem(row, 2, new QStandardItem(event.type));
            model_->setItem(row, 3, new QStandardItem(event.details));
        }
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("History error: %1").arg(history.error)
        : history.events.isEmpty()
            ? QStringLiteral("No provider events have been recorded.")
            : QStringLiteral("%1 provider events.")
                .arg(QLocale(QLocale::English, QLocale::UnitedStates).toString(history.events.size()));
    if (message_->text() != text) message_->setText(text);
}

ActiveModsPage::ActiveModsPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(14);
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
    layout->setSpacing(14);

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

void JobsPage::UpdateHistory(const session::HistorySnapshot& history) {
    completed_jobs_->UpdateHistory(history);
}

HistoryPage::HistoryPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(14);

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

void HistoryPage::UpdateHistory(const session::HistorySnapshot& history) {
    sessions_->UpdateHistory(history);
    completed_jobs_->UpdateHistory(history);
}

} // namespace nlsi::gui
