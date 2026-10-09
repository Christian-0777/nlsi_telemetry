#include "JobHistoryPage.h"
#include "CurrentJobPage.h"

#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHideEvent>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QSet>

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

QString TripEventCategory(const session::EventRecord& event) {
    const QString type = event.type.trimmed().toLower();
    if (type == QStringLiteral("player.tollgate.paid")) {
        return QStringLiteral("Toll gate");
    }
    if (type == QStringLiteral("player.use.ferry")) {
        return QStringLiteral("Ferry");
    }
    if (type == QStringLiteral("player.use.train")) {
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

ActiveModsPage::ActiveModsPage(
    QWidget* parent,
    const QString& documents_directory,
    bool load_workshop_previews)
    : StatePage(parent),
      documents_directory_(documents_directory),
      load_workshop_previews_(load_workshop_previews) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(8);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("activeModsGames"));
    layout->addWidget(tabs, 1);

    const auto add_game_panel = [this, tabs](const QString& title) {
        GamePanel panel;
        panel.title = title;
        panel.page = new QWidget(tabs);
        auto* page_layout = new QVBoxLayout(panel.page);
        page_layout->setContentsMargins(8, 8, 8, 8);
        page_layout->setSpacing(8);
        panel.message = new QLabel(panel.page);
        panel.message->setObjectName(QStringLiteral("activeModsStatus"));
        panel.message->setWordWrap(true);
        page_layout->addWidget(panel.message);

        auto* scroll = new QScrollArea(panel.page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* list = new QWidget(scroll);
        panel.mods_layout = new QVBoxLayout(list);
        panel.mods_layout->setContentsMargins(0, 0, 0, 0);
        panel.mods_layout->setSpacing(6);
        scroll->setWidget(list);
        page_layout->addWidget(scroll, 1);
        tabs->addTab(panel.page, title);
        game_panels_.push_back(panel);
    };
    add_game_panel(QStringLiteral("Euro Truck Simulator 2"));
    add_game_panel(QStringLiteral("American Truck Simulator"));

    network_ = new QNetworkAccessManager(this);
    refresh_timer_ = new QTimer(this);
    refresh_timer_->setInterval(5000);
    connect(refresh_timer_, &QTimer::timeout, this, [this] { RefreshLogs(); });
}

void ActiveModsPage::showEvent(QShowEvent* event) {
    StatePage::showEvent(event);
    RefreshLogs();
    refresh_timer_->start();
}

void ActiveModsPage::hideEvent(QHideEvent* event) {
    refresh_timer_->stop();
    StatePage::hideEvent(event);
}

void ActiveModsPage::RefreshLogs() {
    const QString documents = documents_directory_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : documents_directory_;
    const QStringList directories = {
        QStringLiteral("Euro Truck Simulator 2"),
        QStringLiteral("American Truck Simulator"),
    };
    for (qsizetype index = 0; index < game_panels_.size(); ++index) {
        GamePanel& panel = game_panels_[index];
        modlog::GameLogResult result;
        if (documents.isEmpty()) {
            result.error = QStringLiteral("Windows did not provide a Documents directory.");
        } else {
            const QString path = modlog::GameLogPath(documents, directories[index]);
            const QFileInfo file_info(path);
            if (file_info.exists() && file_info.isFile() && file_info.isReadable()
                && file_info.size() == panel.file_size
                && file_info.lastModified() == panel.result.last_modified
                && panel.result.error.isEmpty()) {
                result = panel.result;
                result.stale = result.last_modified.isValid()
                    && result.last_modified.secsTo(QDateTime::currentDateTime()) > 600;
            } else {
                result = modlog::ReadGameLog(path);
            }
        }

        QString signature = result.path + QLatin1Char('|') + result.error
            + QLatin1Char('|') + result.last_modified.toString(Qt::ISODateWithMs)
            + QLatin1Char('|') + QString::number(result.stale);
        for (const auto& mod : result.mods) {
            signature += QLatin1Char('|') + mod.id + QLatin1Char(':')
                + mod.name + QLatin1Char(':') + mod.version + QLatin1Char(':') + mod.author;
        }
        if (signature == panel.signature) {
            continue;
        }
        panel.signature = signature;
        panel.file_size = result.error.isEmpty()
            ? QFileInfo(result.path).size() : -1;
        panel.result = std::move(result);
        RenderGamePanel(panel);
    }
}

void ActiveModsPage::RenderGamePanel(GamePanel& panel) {
    panel.message->clear();
    while (QLayoutItem* item = panel.mods_layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (!panel.result.error.isEmpty()) {
        panel.message->setText(QStringLiteral("%1 %2")
            .arg(panel.result.error, panel.result.path));
        return;
    }

    QString status;
    if (panel.result.stale) {
        status = QStringLiteral("This game log has not changed for over 10 minutes; "
            "the active-mod list may be stale. ");
    }
    status += panel.result.mods.isEmpty()
        ? QStringLiteral("No active Workshop mods were recorded in this game log.")
        : QStringLiteral("%1 active Workshop mod%2 in the latest game log.")
            .arg(panel.result.mods.size())
            .arg(panel.result.mods.size() == 1 ? QString() : QStringLiteral("s"));
    panel.message->setText(status);

    for (const auto& mod : panel.result.mods) {
        auto* card = new QFrame(panel.mods_layout->parentWidget());
        card->setObjectName(QStringLiteral("contentCard"));
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(10, 8, 10, 8);
        row->setSpacing(12);

        auto* thumbnail = new QLabel(card);
        thumbnail->setObjectName(QStringLiteral("modThumbnail"));
        thumbnail->setFixedSize(96, 68);
        thumbnail->setAlignment(Qt::AlignCenter);
        thumbnail->setWordWrap(true);
        if (thumbnails_.contains(mod.id)) {
            thumbnail->setPixmap(thumbnails_.value(mod.id).scaled(
                thumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            thumbnail->setText(QStringLiteral("Preview\nunavailable"));
        }
        row->addWidget(thumbnail);

        auto* details = new QVBoxLayout();
        details->setContentsMargins(0, 0, 0, 0);
        details->setSpacing(3);
        auto* name = new QLabel(mod.name, card);
        name->setObjectName(QStringLiteral("modName"));
        name->setWordWrap(true);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        details->addWidget(name);
        const QString version = mod.version.isEmpty()
            ? QStringLiteral("Unavailable") : mod.version;
        const QString author = mod.author.isEmpty()
            ? QStringLiteral("Unavailable") : mod.author;
        auto* metadata = new QLabel(
            QStringLiteral("Version: %1 · Author: %2").arg(version, author), card);
        metadata->setObjectName(QStringLiteral("modMetadata"));
        metadata->setWordWrap(true);
        details->addWidget(metadata);

        const QUrl source = modlog::WorkshopSourceUrl(mod.id);
        if (source.isValid()) {
            auto* link = new QLabel(card);
            link->setObjectName(QStringLiteral("modSourceLink"));
            link->setTextFormat(Qt::RichText);
            link->setTextInteractionFlags(
                Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
            link->setOpenExternalLinks(true);
            link->setText(QStringLiteral("<a href=\"%1\">Open Workshop source page</a>")
                .arg(source.toString(QUrl::FullyEncoded).toHtmlEscaped()));
            link->setAccessibleName(QStringLiteral("Workshop source for %1").arg(mod.name));
            details->addWidget(link);
            if (load_workshop_previews_ && !checked_thumbnails_.contains(mod.id)) {
                LoadThumbnail(mod.id);
            }
        } else {
            auto* unavailable = new QLabel(QStringLiteral("Source page unavailable"), card);
            unavailable->setObjectName(QStringLiteral("modSourceLink"));
            details->addWidget(unavailable);
        }
        details->addStretch(1);
        row->addLayout(details, 1);
        panel.mods_layout->addWidget(card);
    }
    panel.mods_layout->addStretch(1);
}

void ActiveModsPage::LoadThumbnail(const QString& workshop_id) {
    if (checked_thumbnails_.contains(workshop_id)) {
        return;
    }
    checked_thumbnails_.insert(workshop_id);
    const QUrl source = modlog::WorkshopSourceUrl(workshop_id);
    if (!source.isValid()) {
        return;
    }

    QNetworkRequest request(source);
    request.setHeader(QNetworkRequest::UserAgentHeader,
        QStringLiteral("NLSI-Exclusive-Logbook"));
    request.setTransferTimeout(8000);
    QNetworkReply* page_reply = network_->get(request);
    connect(page_reply, &QIODevice::readyRead, this, [page_reply] {
        if (page_reply->bytesAvailable() > 4 * 1024 * 1024) {
            page_reply->abort();
        }
    });
    connect(page_reply, &QNetworkReply::finished, this, [this, page_reply, workshop_id] {
        const int status = page_reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (page_reply->error() != QNetworkReply::NoError || status < 200 || status >= 300
            || page_reply->size() > 4 * 1024 * 1024) {
            page_reply->deleteLater();
            return;
        }
        const QByteArray html = page_reply->readAll();
        page_reply->deleteLater();
        static const QRegularExpression preview_meta(
            QStringLiteral("<meta\\b(?=[^>]*\\bproperty\\s*=\\s*[\"']og:image[\"'])"
                "(?=[^>]*\\bcontent\\s*=\\s*[\"']([^\"']+)[\"'])[^>]*>"),
            QRegularExpression::CaseInsensitiveOption
                | QRegularExpression::DotMatchesEverythingOption);
        const auto match = preview_meta.match(QString::fromUtf8(html));
        if (!match.hasMatch()) {
            return;
        }
        const QUrl preview(match.captured(1));
        const QString host = preview.host().toLower();
        if (preview.scheme() != QStringLiteral("https")
            || !(host.endsWith(QStringLiteral(".steamusercontent.com"))
                || host == QStringLiteral("steamusercontent.com")
                || host.endsWith(QStringLiteral(".steamstatic.com"))
                || host == QStringLiteral("steamstatic.com"))) {
            return;
        }

        QNetworkRequest image_request(preview);
        image_request.setHeader(QNetworkRequest::UserAgentHeader,
            QStringLiteral("NLSI-Exclusive-Logbook"));
        image_request.setTransferTimeout(8000);
        QNetworkReply* image_reply = network_->get(image_request);
        connect(image_reply, &QIODevice::readyRead, this, [image_reply] {
            if (image_reply->bytesAvailable() > 5 * 1024 * 1024) {
                image_reply->abort();
            }
        });
        connect(image_reply, &QNetworkReply::finished, this,
            [this, image_reply, workshop_id] {
                const int image_status = image_reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                if (image_reply->error() == QNetworkReply::NoError
                    && image_status >= 200 && image_status < 300
                    && image_reply->size() <= 5 * 1024 * 1024) {
                    QPixmap image;
                    if (image.loadFromData(image_reply->readAll())) {
                        thumbnails_.insert(workshop_id, image);
                        for (GamePanel& panel : game_panels_) {
                            RenderGamePanel(panel);
                        }
                    }
                }
                image_reply->deleteLater();
            });
    });
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
        QSet<QString> seen_events;
        for (const auto& event : history.events) {
            const QString category = TripEventCategory(event);
            if (category.isEmpty()) {
                continue;
            }
            const QString event_identity = event.timestamp + QLatin1Char('|')
                + event.type.trimmed().toLower() + QLatin1Char('|') + event.details;
            if (seen_events.contains(event_identity)) {
                continue;
            }
            seen_events.insert(event_identity);

            QJsonParseError parse_error;
            const QJsonDocument document =
                QJsonDocument::fromJson(event.details.toUtf8(), &parse_error);
            const QJsonObject details = parse_error.error == QJsonParseError::NoError
                    && document.isObject()
                ? document.object() : QJsonObject();
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
            ? QStringLiteral("No supported toll-gate, ferry, or train telemetry events have "
                "been recorded. Only player.use.ferry, player.use.train, and "
                "player.tollgate.paid are recognized.")
            : QStringLiteral("Only explicit supported telemetry events are shown. Missing "
                "fees, currency, and trip/job identifiers are marked unavailable.");
    if (message_->text() != text) {
        message_->setText(text);
    }
}

} // namespace nlsi::gui
