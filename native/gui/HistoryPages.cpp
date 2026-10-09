#include "JobHistoryPage.h"
#include "CurrentJobPage.h"

#include <QFrame>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QJsonParseError>
#include <QJsonValue>
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

QString GameJobId(const QJsonObject& details) {
    const QJsonValue direct = details.value(QStringLiteral("job_id"));
    if (direct.isString()) {
        return direct.toString();
    }
    const QJsonValue job = details.value(QStringLiteral("job"));
    return job.isObject()
        ? job.toObject().value(QStringLiteral("id")).toString()
        : QString();
}

QString EventFieldText(const QJsonObject& details, const QStringList& keys) {
    for (const QString& key : keys) {
        QJsonValue value = details.value(key);
        if (key.contains(QLatin1Char('.'))) {
            const QStringList path = key.split(QLatin1Char('.'));
            value = details.value(path.front());
            for (qsizetype index = 1; index < path.size() && value.isObject(); ++index) {
                value = value.toObject().value(path[index]);
            }
        }
        if (value.isString() && !value.toString().trimmed().isEmpty()) {
            return value.toString().trimmed();
        }
        if (value.isDouble()) {
            return value.toVariant().toString();
        }
    }
    return {};
}

QString TripEventCategory(const session::EventRecord& event, const QJsonObject& details) {
    const QString type = event.type.trimmed().toLower();
    const QString transport = EventFieldText(
        details, {QStringLiteral("transport_type")}).toLower();
    const auto has_prefix = [](const QString& value, const QString& name) {
        return value == name || value.startsWith(name + QLatin1Char('.'))
            || value.startsWith(name + QLatin1Char('_'))
            || value.startsWith(name + QLatin1Char('-'));
    };
    if (has_prefix(type, QStringLiteral("toll"))
        || type == QStringLiteral("tollgate")
        || type.startsWith(QStringLiteral("tollgate."))) {
        return QStringLiteral("Toll gate");
    }
    if (has_prefix(type, QStringLiteral("ferry"))
        || (type == QStringLiteral("transport") && transport == QStringLiteral("ferry"))) {
        return QStringLiteral("Ferry");
    }
    if (has_prefix(type, QStringLiteral("train"))
        || (type == QStringLiteral("transport") && transport == QStringLiteral("train"))) {
        return QStringLiteral("Train");
    }
    return {};
}

QString TripJobAssociation(
    const QJsonObject& details,
    const QVector<session::JobRecord>& jobs) {
    const QString job_id = EventFieldText(details, {
        QStringLiteral("nlsi_job_id"), QStringLiteral("job_id"),
        QStringLiteral("job.id"), QStringLiteral("game_job_id")});
    if (!job_id.isEmpty()) {
        for (const auto& job : jobs) {
            const QString stored_game_id = GameJobId(job.details);
            if (job.nlsi_job_id == job_id || job.identity == job_id
                || stored_game_id == job_id) {
                return job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id;
            }
        }
        return job_id;
    }
    return EventFieldText(details, {
        QStringLiteral("trip_id"), QStringLiteral("session_id")});
}

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
        {QStringLiteral("Job ID"), QStringLiteral("Game job ID"),
            QStringLiteral("Timestamp"), QStringLiteral("Cargo"), QStringLiteral("Source"),
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
            model_->setItem(row, 0, new QStandardItem(
                job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id));
            model_->setItem(row, 1, new QStandardItem(GameJobId(job.details)));
            model_->setItem(row, 2, new QStandardItem(TimestampText(job.timestamp)));
            model_->setItem(row, 3, new QStandardItem(job.cargo));
            model_->setItem(row, 4, new QStandardItem(job.source));
            model_->setItem(row, 5, new QStandardItem(job.destination));
            const QJsonValue income = job.details.value(QStringLiteral("income"));
            const QJsonValue planned_distance =
                job.details.value(QStringLiteral("planned_distance_km"));
            model_->setItem(row, 6, new QStandardItem(
                income.isUndefined() || income.isNull()
                    ? QStringLiteral("--")
                    : NumericText(income.toVariant().toString())));
            const QString planned_text = planned_distance.isUndefined() || planned_distance.isNull()
                ? QStringLiteral("--")
                : NumericText(planned_distance.toVariant().toString()) + QStringLiteral(" km");
            model_->setItem(row, 7, new QStandardItem(planned_text));
            model_->setItem(row, 8, new QStandardItem(job.status));
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
    message_ = new QLabel(
        QStringLiteral("Active mods are unavailable: supported telemetry does not expose a "
            "verifiable complete mod list."),
        card);
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
    trip_events_ = new TripEventsPage(tabs_);
    tabs_->addTab(sessions_, QStringLiteral("Sessions & trips"));
    tabs_->addTab(completed_jobs_, QStringLiteral("Completed jobs"));
    tabs_->addTab(trip_events_, QStringLiteral("Tolls & transport"));
    layout->addWidget(tabs_, 1);
}

void HistoryPage::UpdateState(const telemetry::TelemetryUiState& state) {
    sessions_->UpdateState(state);
    completed_jobs_->UpdateState(state);
    trip_events_->UpdateState(state);
}

void HistoryPage::UpdateHistory(const session::HistorySnapshot& history) {
    sessions_->UpdateHistory(history);
    completed_jobs_->UpdateHistory(history);
    trip_events_->UpdateHistory(history);
}

TripEventsPage::TripEventsPage(QWidget* parent) : StatePage(parent) {
    auto* content = MakeHistoryPage(
        {QStringLiteral("Event type"), QStringLiteral("Recorded fee"),
            QStringLiteral("Currency"), QStringLiteral("Trip/job association")},
        message_,
        model_,
        this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(content);
}

void TripEventsPage::UpdateState(const telemetry::TelemetryUiState&) {
}

void TripEventsPage::UpdateHistory(const session::HistorySnapshot& history) {
    if (history_revision_ != history.revision) {
        model_->removeRows(0, model_->rowCount());
        for (const auto& event : history.events) {
            QJsonParseError parse_error;
            const QJsonDocument document =
                QJsonDocument::fromJson(event.details.toUtf8(), &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
                continue;
            }
            const QJsonObject details = document.object();
            const QString category = TripEventCategory(event, details);
            if (category.isEmpty()) {
                continue;
            }
            const QString fee = EventFieldText(details, {
                QStringLiteral("toll_fee"), QStringLiteral("fee"),
                QStringLiteral("amount"), QStringLiteral("price")});
            const QString currency = EventFieldText(details, {
                QStringLiteral("currency"), QStringLiteral("currency_code")});
            const QString association = TripJobAssociation(details, history.jobs);
            const int row = model_->rowCount();
            model_->insertRow(row);
            model_->setItem(row, 0, new QStandardItem(category));
            model_->setItem(row, 1, new QStandardItem(
                fee.isEmpty() ? QStringLiteral("Unavailable") : fee));
            model_->setItem(row, 2, new QStandardItem(
                currency.isEmpty() ? QStringLiteral("Unavailable") : currency));
            model_->setItem(row, 3, new QStandardItem(
                association.isEmpty() ? QStringLiteral("Unavailable") : association));
        }
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("History error: %1").arg(history.error)
        : model_->rowCount() == 0
            ? QStringLiteral("No toll-gate, ferry or train events have been recorded. "
                "TruckSim GPS revision 13 records job delivery/cancellation events but does "
                "not expose verified toll fees or ferry/train crossings.")
            : QStringLiteral("Only explicit provider events are shown; missing fees, currency "
                "or trip/job identifiers are marked unavailable.");
    if (message_->text() != text) {
        message_->setText(text);
    }
}

} // namespace nlsi::gui
