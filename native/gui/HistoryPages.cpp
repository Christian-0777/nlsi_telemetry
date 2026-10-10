#include "JobHistoryPage.h"
#include "CurrentJobPage.h"

#include <QDateTime>
#include <QDir>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
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
#include <QResizeEvent>
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
#include <QGridLayout>
#include <QFile>

#include <QtConcurrent/QtConcurrentRun>

#include "JobPdfExporter.h"

namespace nlsi::gui {
namespace {

QString GameJobId(const QJsonObject& details) {
    const QJsonValue direct = details.value(QStringLiteral("job_id"));
    if (direct.isString()) {
        return direct.toString();
    }
    const QJsonValue job = details.value(QStringLiteral("job"));
    if (job.isObject()) {
        return job.toObject().value(QStringLiteral("id")).toString();
    }
    if (details.value(QStringLiteral("cargo_id")).isString()) {
        return details.value(QStringLiteral("cargo_id")).toString();
    }
    const QJsonObject data = details.value(QStringLiteral("data")).toObject();
    return data.value(QStringLiteral("job_id")).toString(
        data.value(QStringLiteral("cargo_id")).toString());
}

QString EventFieldText(const QJsonObject& details, const QStringList& keys) {
    const auto find_value = [&keys](const QJsonObject& object) {
        for (const QString& key : keys) {
            QJsonValue value = object.value(key);
            if (key.contains(QLatin1Char('.'))) {
                const QStringList path = key.split(QLatin1Char('.'));
                value = object.value(path.front());
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
        return QString();
    };
    const QString direct_value = find_value(details);
    if (!direct_value.isEmpty()) {
        return direct_value;
    }
    const QJsonValue data = details.value(QStringLiteral("data"));
    return data.isObject() ? find_value(data.toObject()) : QString();
}

QString CompletedJobField(const session::JobRecord& job, const QStringList& keys) {
    const QString value = EventFieldText(job.details, keys);
    return value.isEmpty() ? QStringLiteral("N/A") : value;
}

QString WithUnit(const QString& value, const QString& unit) {
    if (value == QStringLiteral("N/A") || value.endsWith(unit, Qt::CaseInsensitive)) {
        return value;
    }
    return value + QLatin1Char(' ') + unit;
}

QString WrapLongTokens(const QString& text);

class ResponsiveJobFields final : public QWidget {
public:
    explicit ResponsiveJobFields(QWidget* parent)
        : QWidget(parent), grid_(new QGridLayout(this)) {
        setObjectName(QStringLiteral("completedJobFields"));
        grid_->setContentsMargins(0, 0, 0, 0);
        grid_->setHorizontalSpacing(20);
        grid_->setVerticalSpacing(8);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void AddField(const QString& label, const QString& value) {
        auto* field = new QWidget(this);
        field->setMinimumWidth(0);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* field_layout = new QVBoxLayout(field);
        field_layout->setContentsMargins(0, 0, 0, 0);
        field_layout->setSpacing(2);
        auto* field_label = new QLabel(label.toUpper(), field);
        field_label->setObjectName(QStringLiteral("completedJobFieldLabel"));
        auto* field_value = new QLabel(WrapLongTokens(value), field);
        field_value->setObjectName(QStringLiteral("completedJobFieldValue"));
        field_value->setWordWrap(true);
        field_value->setMinimumWidth(0);
        field_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        field_layout->addWidget(field_label);
        field_layout->addWidget(field_value);
        fields_.push_back(field);
        arranged_columns_ = 0;
        ArrangeFields();
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        ArrangeFields();
    }

private:
    void ArrangeFields() {
        const int columns = width() < 700 ? 1 : 2;
        if (columns == arranged_columns_) {
            return;
        }
        arranged_columns_ = columns;
        setProperty("columnCount", columns);
        for (qsizetype index = 0; index < fields_.size(); ++index) {
            grid_->removeWidget(fields_[index]);
            grid_->addWidget(
                fields_[index],
                static_cast<int>(index) / columns,
                static_cast<int>(index) % columns);
        }
        grid_->setColumnStretch(0, 1);
        grid_->setColumnStretch(1, columns == 2 ? 1 : 0);
    }

    QGridLayout* grid_ = nullptr;
    QVector<QWidget*> fields_;
    int arranged_columns_ = 0;
};

QWidget* MakeCompletedJobCard(const session::JobRecord& job, QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("completedJobCard"));
    card->setMinimumWidth(0);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);

    auto* header = new QHBoxLayout();
    auto* job_id = new QLabel(
        QStringLiteral("JOB ID: %1").arg(
            job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id),
        card);
    job_id->setObjectName(QStringLiteral("completedJobId"));
    const QString game_job_id = GameJobId(job.details);
    auto* game_id = new QLabel(
        QStringLiteral("GAME JOB ID: %1").arg(
            game_job_id.isEmpty() ? QStringLiteral("N/A") : game_job_id),
        card);
    game_id->setObjectName(QStringLiteral("completedGameJobId"));
    game_id->setWordWrap(true);
    game_id->setMinimumWidth(0);
    auto* status = new QLabel(QStringLiteral("STATUS: %1").arg(job.status), card);
    status->setObjectName(QStringLiteral("completedJobStatus"));
    status->setProperty("terminalStatus", job.status.toLower());
    status->setWordWrap(true);
    status->setMinimumWidth(0);
    job_id->setWordWrap(true);
    job_id->setMinimumWidth(0);
    job_id->setText(WrapLongTokens(job_id->text()));
    game_id->setText(WrapLongTokens(game_id->text()));
    header->addWidget(job_id, 1);
    header->addWidget(game_id, 1);
    header->addWidget(status, 0, Qt::AlignRight);
    layout->addLayout(header);
    auto* recorded_at = new QLabel(
        QStringLiteral("RECORDED AT: %1").arg(TimestampText(job.timestamp)), card);
    recorded_at->setObjectName(QStringLiteral("completedJobFieldLabel"));
    layout->addWidget(recorded_at);

    auto* fields = new ResponsiveJobFields(card);

    const auto field = [&job](const QStringList& keys) {
        return CompletedJobField(job, keys);
    };
    fields->AddField(QStringLiteral("Cargo"),
        job.cargo.isEmpty()
            ? field({QStringLiteral("cargo"), QStringLiteral("cargo_name"),
                QStringLiteral("cargo.id")})
            : job.cargo);
    fields->AddField(QStringLiteral("Weight"),
        field({QStringLiteral("weight"), QStringLiteral("cargo_weight")}));
    fields->AddField(QStringLiteral("From"),
        field({QStringLiteral("source_city"), QStringLiteral("source.city")}));
    fields->AddField(QStringLiteral("To"),
        field({QStringLiteral("destination_city"), QStringLiteral("destination.city")}));
    fields->AddField(QStringLiteral("From company"),
        field({QStringLiteral("source_company"), QStringLiteral("source.company")}));
    fields->AddField(QStringLiteral("To company"),
        field({QStringLiteral("destination_company"), QStringLiteral("destination.company")}));
    fields->AddField(QStringLiteral("Planned distance"),
        WithUnit(field({QStringLiteral("planned_distance_km"),
            QStringLiteral("planned_distance")}), QStringLiteral("km")));
    fields->AddField(QStringLiteral("Driven distance"),
        WithUnit(field({QStringLiteral("driven_distance_km"),
            QStringLiteral("distance_driven_km")}), QStringLiteral("km")));
    fields->AddField(QStringLiteral("Delivery time (game minutes)"),
        field({QStringLiteral("delivery_time_game_minutes"),
            QStringLiteral("delivery.time")}));
    fields->AddField(QStringLiteral("Income"), field({QStringLiteral("income")}));
    fields->AddField(QStringLiteral("Offences"),
        field({QStringLiteral("offences"), QStringLiteral("offenses")}));
    fields->AddField(QStringLiteral("XP"),
        field({QStringLiteral("xp"), QStringLiteral("experience")}));
    fields->AddField(QStringLiteral("Damage"),
        field({QStringLiteral("damage"), QStringLiteral("damage_percent")}));
    fields->AddField(QStringLiteral("Time taken (real)"),
        field({QStringLiteral("real_elapsed_time"), QStringLiteral("elapsed_time")}));
    fields->AddField(QStringLiteral("Max speed"),
        WithUnit(field({QStringLiteral("max_speed_kmh"),
            QStringLiteral("maximum_speed_kmh")}), QStringLiteral("km/h")));
    layout->addWidget(fields);

    auto* vehicle_title = new QLabel(QStringLiteral("VEHICLE AND FUEL"), card);
    vehicle_title->setObjectName(QStringLiteral("completedJobSectionTitle"));
    layout->addWidget(vehicle_title);
    auto* vehicle_fields = new ResponsiveJobFields(card);
    vehicle_fields->AddField(QStringLiteral("Truck used"),
        field({QStringLiteral("truck"), QStringLiteral("truck_name"),
            QStringLiteral("vehicle")}));
    vehicle_fields->AddField(QStringLiteral("Trailer used"),
        field({QStringLiteral("trailer"), QStringLiteral("trailer_name")}));
    vehicle_fields->AddField(QStringLiteral("Truck licence plate"),
        field({QStringLiteral("truck_license_plate"),
            QStringLiteral("truck_licence_plate")}));
    vehicle_fields->AddField(QStringLiteral("Truck plate country"),
        field({QStringLiteral("truck_license_plate_country"),
            QStringLiteral("truck_license_plate_country_id")}));
    vehicle_fields->AddField(QStringLiteral("Trailer licence plate"),
        field({QStringLiteral("trailer_license_plate"),
            QStringLiteral("trailer_licence_plate")}));
    vehicle_fields->AddField(QStringLiteral("Trailer plate country"),
        field({QStringLiteral("trailer_license_plate_country"),
            QStringLiteral("trailer_license_plate_country_id")}));
    vehicle_fields->AddField(QStringLiteral("Fuel usage"),
        WithUnit(field({QStringLiteral("fuel_used_liters"),
            QStringLiteral("fuel_usage_liters")}), QStringLiteral("L")));
    vehicle_fields->AddField(QStringLiteral("Fuel usage basis"),
        field({QStringLiteral("fuel_used_source")}));
    vehicle_fields->AddField(QStringLiteral("Refueled"),
        WithUnit(field({QStringLiteral("refueled_liters"),
            QStringLiteral("fuel_added_liters")}), QStringLiteral("L")));
    vehicle_fields->AddField(QStringLiteral("Refueled amount basis"),
        field({QStringLiteral("refueled_source")}));
    vehicle_fields->AddField(QStringLiteral("Refuel cost"),
        field({QStringLiteral("refuel_cost")}));
    vehicle_fields->AddField(QStringLiteral("Average consumption"),
        WithUnit(field({QStringLiteral("average_consumption"),
            QStringLiteral("average_consumption_l_per_100km")}), QStringLiteral("L/100 km")));
    vehicle_fields->AddField(QStringLiteral("Average consumption basis"),
        field({QStringLiteral("average_consumption_source")}));
    layout->addWidget(vehicle_fields);

    auto* actions = new QHBoxLayout();
    actions->addStretch(1);
    auto* export_button = new QPushButton(QStringLiteral("EXPORT PDF"), card);
    export_button->setObjectName(QStringLiteral("exportJobPdfButton"));
    export_button->setProperty("persistedJobId", job.identity);
    actions->addWidget(export_button);
    layout->addLayout(actions);
    QObject::connect(export_button, &QPushButton::clicked, card, [card, job] {
        const QString raw_id = job.nlsi_job_id.isEmpty() ? job.identity : job.nlsi_job_id;
        QString safe_id = raw_id;
        safe_id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("-"));
        if (safe_id.isEmpty()) {
            safe_id = QStringLiteral("completed-job");
        }
        QString path = QFileDialog::getSaveFileName(
            card, QStringLiteral("Export completed job"),
            QStringLiteral("NLSI-%1.pdf").arg(safe_id), QStringLiteral("PDF files (*.pdf)"));
        if (path.isEmpty()) {
            return;
        }
        if (QFileInfo::exists(path)) {
            const auto answer = QMessageBox::question(
                card, QStringLiteral("Confirm overwrite"),
                QStringLiteral("The selected file already exists. Overwrite it?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        QString error;
        if (!ExportJobToPdf(path, job, &error)) {
            QMessageBox::critical(card, QStringLiteral("PDF export failed"), error);
            return;
        }
        QMessageBox::information(
            card, QStringLiteral("PDF exported"),
            QStringLiteral("The completed job report was exported to %1.").arg(path));
    });
    return card;
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

QString WrapLongTokens(const QString& text) {
    QString wrapped;
    wrapped.reserve(text.size() + text.size() / 28);
    qsizetype token_length = 0;
    for (const QChar character : text) {
        if (character.isSpace()) {
            token_length = 0;
        } else if (!character.isLowSurrogate() && token_length >= 28) {
            wrapped += QChar(0x200b);
            token_length = 0;
        }
        wrapped += character;
        if (!character.isLowSurrogate()) {
            ++token_length;
        }
    }
    return wrapped;
}

QString EventDataText(const session::EventRecord& event) {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(event.details.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        const QString details = event.details.trimmed();
        return details.isEmpty() ? QStringLiteral("N/A (no event data recorded)") : details;
    }

    QJsonObject data = document.object();
    if (data.value(QStringLiteral("data")).isObject()) {
        data = data.value(QStringLiteral("data")).toObject();
    } else {
        data.remove(QStringLiteral("provider"));
    }
    if (data.isEmpty()) {
        return QStringLiteral("N/A (no event data recorded)");
    }
    return QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Indented)).trimmed();
}

QWidget* MakeEventEntry(const session::EventRecord& event, QWidget* parent) {
    auto* entry = new QFrame(parent);
    entry->setObjectName(QStringLiteral("eventEntry"));
    entry->setMinimumWidth(0);
    entry->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* layout = new QVBoxLayout(entry);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(6);

    const auto add_field = [entry, layout](
                               const QString& label,
                               const QString& value,
                               const QString& object_name) {
        auto* field_label = new QLabel(label.toUpper(), entry);
        field_label->setObjectName(QStringLiteral("eventFieldLabel"));
        auto* field_value = new QLabel(WrapLongTokens(
            value.trimmed().isEmpty() ? QStringLiteral("N/A") : value), entry);
        field_value->setObjectName(object_name);
        field_value->setWordWrap(true);
        field_value->setMinimumWidth(0);
        field_value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        field_value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(field_label);
        layout->addWidget(field_value);
    };

    add_field(QStringLiteral("Timestamp"), TimestampText(event.timestamp),
        QStringLiteral("eventTimestamp"));
    add_field(QStringLiteral("Source"), event.source, QStringLiteral("eventSource"));
    add_field(QStringLiteral("Event"), event.type, QStringLiteral("eventType"));
    add_field(QStringLiteral("Data"), EventDataText(event), QStringLiteral("eventData"));
    return entry;
}

void ConfigureTable(QTableView* table, QStandardItemModel* model) {
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setWordWrap(true);
    table->setTextElideMode(Qt::ElideRight);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->verticalHeader()->hide();
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
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
    QStringList uppercase_columns;
    uppercase_columns.reserve(columns.size());
    for (const QString& column : columns) {
        uppercase_columns.push_back(column.toUpper());
    }
    model->setHorizontalHeaderLabels(uppercase_columns);
    ConfigureTable(table, model);
    layout->addWidget(table, 1);
    return page;
}

} // namespace

JobHistoryPage::JobHistoryPage(QWidget* parent) : StatePage(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("detailLabel"));
    message_->setWordWrap(true);
    layout->addWidget(message_);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("completedJobsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    cards_container_ = new QWidget(scroll);
    cards_container_->setObjectName(QStringLiteral("completedJobsCards"));
    cards_container_->setMinimumWidth(0);
    cards_layout_ = new QVBoxLayout(cards_container_);
    cards_layout_->setContentsMargins(2, 2, 2, 2);
    cards_layout_->setSpacing(14);
    cards_layout_->addStretch(1);
    scroll->setWidget(cards_container_);
    layout->addWidget(scroll, 1);
}

void JobHistoryPage::UpdateState(const telemetry::TelemetryUiState&) {
}

void JobHistoryPage::UpdateHistory(const session::HistorySnapshot& history) {
    if (history_revision_ != history.revision) {
        jobs_.clear();
        while (cards_layout_->count() > 1) {
            QLayoutItem* item = cards_layout_->takeAt(0);
            delete item->widget();
            delete item;
        }
        for (const auto& job : history.jobs) {
            const QString status = job.status.trimmed();
            if (status.compare(QStringLiteral("Delivered"), Qt::CaseInsensitive) != 0
                && status.compare(QStringLiteral("Cancelled"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            jobs_.push_back(job);
            cards_layout_->insertWidget(cards_layout_->count() - 1,
                MakeCompletedJobCard(job, cards_container_));
        }
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("History error: %1").arg(history.error)
        : jobs_.isEmpty()
            ? QStringLiteral("No completed or cancelled jobs have been recorded.")
        : QStringLiteral("%1 completed or cancelled job records.")
            .arg(QLocale(QLocale::English, QLocale::UnitedStates).toString(jobs_.size()));
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
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    message_ = new QLabel(this);
    message_->setObjectName(QStringLiteral("detailLabel"));
    message_->setWordWrap(true);
    layout->addWidget(message_);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("eventsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    entries_container_ = new QWidget(scroll);
    entries_container_->setObjectName(QStringLiteral("eventEntries"));
    entries_container_->setMinimumWidth(0);
    entries_layout_ = new QVBoxLayout(entries_container_);
    entries_layout_->setContentsMargins(2, 2, 2, 2);
    entries_layout_->setSpacing(8);
    entries_layout_->setAlignment(Qt::AlignTop);
    scroll->setWidget(entries_container_);
    layout->addWidget(scroll, 1);
}

void EventsPage::UpdateState(const telemetry::TelemetryUiState&) {
}

void EventsPage::UpdateHistory(const session::HistorySnapshot& history) {
    if (history_revision_ != history.revision) {
        const qsizetype current_count = history.events.size();
        constexpr qsizetype max_visible_events = 500;
        if (event_count_ < 0 || current_count < event_count_) {
            while (QLayoutItem* item = entries_layout_->takeAt(0)) {
                delete item->widget();
                delete item;
            }
            const qsizetype visible_count = qMin(current_count, max_visible_events);
            for (qsizetype index = 0; index < visible_count; ++index) {
                entries_layout_->addWidget(
                    MakeEventEntry(history.events[index], entries_container_));
            }
        } else if (current_count > event_count_) {
            const qsizetype added_count = current_count - event_count_;
            for (qsizetype index = added_count; index > 0; --index) {
                entries_layout_->insertWidget(0,
                    MakeEventEntry(history.events[index - 1], entries_container_));
            }
            while (entries_layout_->count() > max_visible_events) {
                QLayoutItem* item = entries_layout_->takeAt(entries_layout_->count() - 1);
                delete item->widget();
                delete item;
            }
        }
        event_count_ = current_count;
        history_revision_ = history.revision;
    }
    const QString text = !history.error.isEmpty()
        ? QStringLiteral("History error: %1").arg(history.error)
        : history.events.isEmpty()
            ? QStringLiteral("No provider events have been recorded.")
            : QStringLiteral("%1 provider events; showing the newest %2.")
                .arg(QLocale(QLocale::English, QLocale::UnitedStates)
                    .toString(history.events.size()))
                .arg(qMin(history.events.size(), qsizetype(500)));
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
        auto* status_row = new QHBoxLayout();
        panel.message = new QLabel(panel.page);
        panel.message->setObjectName(QStringLiteral("activeModsStatus"));
        panel.message->setWordWrap(true);
        panel.message->setMinimumWidth(0);
        panel.message->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        status_row->addWidget(panel.message, 1);
        panel.reinitialize_button = new QPushButton(
            QStringLiteral("Reinitialize"), panel.page);
        panel.reinitialize_button->setObjectName(
            title.startsWith(QStringLiteral("Euro"))
                ? QStringLiteral("reinitializeEts2LogButton")
                : QStringLiteral("reinitializeAtsLogButton"));
        panel.reinitialize_button->setAccessibleName(
            QStringLiteral("Reinitialize %1 game log monitoring").arg(title));
        status_row->addWidget(panel.reinitialize_button, 0, Qt::AlignTop);
        page_layout->addLayout(status_row);

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
        panel.init_watcher = new QFutureWatcher<modlog::GameLogResult>(this);
        game_panels_.push_back(panel);
    };
    add_game_panel(QStringLiteral("Euro Truck Simulator 2"));
    add_game_panel(QStringLiteral("American Truck Simulator"));

    network_ = new QNetworkAccessManager(this);
    file_watcher_ = new QFileSystemWatcher(this);
    connect(file_watcher_, &QFileSystemWatcher::directoryChanged,
        this, [this] { RefreshLogs(); });
    connect(file_watcher_, &QFileSystemWatcher::fileChanged,
        this, [this] { RefreshLogs(); });

    for (qsizetype index = 0; index < game_panels_.size(); ++index) {
        GamePanel& panel = game_panels_[index];
        connect(panel.reinitialize_button, &QPushButton::clicked, this,
            [this, index] { InitializeLog(index, true); });
        connect(panel.init_watcher, &QFutureWatcher<modlog::GameLogResult>::finished,
            this, [this, index] {
                GamePanel& completed_panel = game_panels_[index];
                completed_panel.result = completed_panel.init_watcher->result();
                completed_panel.initializing = false;
                completed_panel.reinitialize_button->setEnabled(true);
                if (completed_panel.result.error.isEmpty()) {
                    const QString boundary = completed_panel.result.session_boundary_known
                        ? QStringLiteral("session boundary confirmed")
                        : QStringLiteral("session boundary uncertain");
                    completed_panel.status = completed_panel.manual_reinitialize
                        ? QStringLiteral("Reinitialization complete. Monitoring; %1.")
                            .arg(boundary)
                        : QStringLiteral("Monitoring game log; %1.").arg(boundary);
                } else if (completed_panel.result.error.contains(
                        QStringLiteral("not found"), Qt::CaseInsensitive)) {
                    completed_panel.status = QStringLiteral(
                        "Game log unavailable; waiting for the game to start.");
                    completed_panel.retry_after = QDateTime::currentDateTime().addSecs(2);
                } else {
                    completed_panel.status = QStringLiteral("Game log read error.");
                    completed_panel.retry_after = QDateTime::currentDateTime().addSecs(2);
                }
                completed_panel.manual_reinitialize = false;
                completed_panel.signature.clear();
                RenderGamePanel(completed_panel);

                const QFileInfo info(completed_panel.result.path);
                const QString file_path = info.absoluteFilePath();
                if (info.exists() && info.isFile()
                    && !file_watcher_->files().contains(file_path)) {
                    file_watcher_->addPath(file_path);
                }
            });
    }

    refresh_timer_ = new QTimer(this);
    refresh_timer_->setObjectName(QStringLiteral("activeModsRefreshTimer"));
    refresh_timer_->setInterval(1000);
    connect(refresh_timer_, &QTimer::timeout, this, [this] { RefreshLogs(); });
    RefreshLogs();
    refresh_timer_->start();
}

void ActiveModsPage::showEvent(QShowEvent* event) {
    StatePage::showEvent(event);
    RefreshLogs();
}

void ActiveModsPage::hideEvent(QHideEvent* event) {
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
    if (!documents.isEmpty() && !file_watcher_->directories().contains(
            QFileInfo(documents).absoluteFilePath())) {
        file_watcher_->addPath(documents);
    }
    for (qsizetype index = 0; index < game_panels_.size(); ++index) {
        GamePanel& panel = game_panels_[index];
        if (documents.isEmpty()) {
            panel.result = {};
            panel.result.error = QStringLiteral(
                "Windows did not provide a Documents directory.");
            panel.status = QStringLiteral("Game log unavailable.");
            RenderGamePanel(panel);
            continue;
        }

        const QString path = modlog::GameLogPath(documents, directories[index]);
        const QFileInfo info(path);
        const QString game_directory = info.absolutePath();
        if (QDir(game_directory).exists()
            && !file_watcher_->directories().contains(game_directory)) {
            file_watcher_->addPath(game_directory);
        }
        if (panel.initializing) {
            continue;
        }

        if (!info.exists() || !info.isFile()) {
            if (panel.result.initialized || panel.result.path != path
                || panel.result.error != QStringLiteral("Game log not found.")) {
                panel.result = {};
                panel.result.path = path;
                panel.result.error = QStringLiteral("Game log not found.");
                panel.status = QStringLiteral(
                    "Game log unavailable; waiting for the game to start.");
                panel.signature.clear();
                RenderGamePanel(panel);
            }
            if (!panel.retry_after.isValid()
                || panel.retry_after <= QDateTime::currentDateTime()) {
                InitializeLog(index, false);
            }
            continue;
        }

        if (!panel.result.initialized
            || modlog::GameLogNeedsReinitialize(path, panel.result)) {
            if (!panel.retry_after.isValid()
                || panel.retry_after <= QDateTime::currentDateTime()) {
                InitializeLog(index, false);
            }
            continue;
        }

        const QString previous_signature = panel.signature;
        const qint64 previous_offset = panel.result.offset;
        if (!modlog::ReadAppendedGameLog(path, panel.result)) {
            panel.status = QStringLiteral("Game log read error.");
            if (!panel.result.initialized) {
                panel.retry_after = QDateTime::currentDateTime();
                InitializeLog(index, false);
            } else {
                panel.signature.clear();
                RenderGamePanel(panel);
            }
            continue;
        }

        if (panel.result.offset > previous_offset) {
            panel.status = QStringLiteral("Monitoring appended game log content; %1.")
                .arg(panel.result.session_boundary_known
                    ? QStringLiteral("session boundary confirmed")
                    : QStringLiteral("session boundary uncertain"));
        }
        panel.signature = QString::number(panel.result.offset)
            + QLatin1Char('|')
            + panel.result.last_modified.toString(Qt::ISODateWithMs)
            + QLatin1Char('|') + QString::number(panel.result.mods.size())
            + QLatin1Char('|') + QString::number(panel.result.stale)
            + QLatin1Char('|') + panel.status;
        if (panel.signature != previous_signature) {
            RenderGamePanel(panel);
        }
    }
}

void ActiveModsPage::InitializeLog(qsizetype panel_index, bool manual) {
    if (panel_index < 0 || panel_index >= game_panels_.size()) {
        return;
    }
    GamePanel& panel = game_panels_[panel_index];
    if (panel.initializing) {
        return;
    }
    const QString documents = documents_directory_.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : documents_directory_;
    const QStringList directories = {
        QStringLiteral("Euro Truck Simulator 2"),
        QStringLiteral("American Truck Simulator"),
    };
    const QString path = documents.isEmpty()
        ? QString()
        : modlog::GameLogPath(documents, directories[panel_index]);
    panel.result = {};
    panel.result.path = path;
    panel.status = manual
        ? QStringLiteral("Reinitializing game log...")
        : QStringLiteral("Initializing game log...");
    panel.manual_reinitialize = manual;
    panel.initializing = true;
    panel.retry_after = QDateTime::currentDateTime().addSecs(2);
    panel.reinitialize_button->setEnabled(false);
    panel.signature.clear();
    RenderGamePanel(panel);

    panel.init_watcher->setFuture(QtConcurrent::run([path] {
        return modlog::ReadGameLog(path);
    }));
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
        const QString error = panel.result.error.contains(
                QStringLiteral("not found"), Qt::CaseInsensitive)
            ? QStringLiteral("Game log unavailable.")
            : QStringLiteral("Game log read error: %1").arg(panel.result.error);
        panel.message->setText(WrapLongTokens(QStringLiteral("%1 %2\n%3")
            .arg(panel.status, error, panel.result.path)));
        panel.message->setToolTip(panel.result.path);
        return;
    }

    QString status = panel.status;
    if (panel.result.stale) {
        status += QStringLiteral(" This game log has not changed for over 10 minutes; "
            "the active-mod list may be stale. ");
    }
    status += panel.result.mods.isEmpty()
        ? QStringLiteral(" No mod evidence was recorded in this game log.")
        : QStringLiteral(" %1 mod entr%2 recorded in the current log.")
            .arg(panel.result.mods.size())
            .arg(panel.result.mods.size() == 1 ? QStringLiteral("y") : QStringLiteral("ies"));
    if (!panel.result.session_boundary_known && !panel.result.mods.isEmpty()) {
        status += QStringLiteral(
            " Session boundary uncertain: listed entries are log evidence, "
            "not confirmation that they are active in this game session.");
    }
    panel.message->setText(WrapLongTokens(status));
    panel.message->setToolTip(panel.result.path);

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
        auto* name = new QLabel(WrapLongTokens(mod.name), card);
        name->setObjectName(QStringLiteral("modName"));
        name->setWordWrap(true);
        name->setMinimumWidth(0);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        name->setToolTip(mod.name);
        name->setAccessibleName(mod.name);
        details->addWidget(name);
        const QString version = mod.version.isEmpty()
            ? QStringLiteral("Unavailable") : mod.version;
        const QString author = mod.author.isEmpty()
            ? QStringLiteral("Unavailable") : mod.author;
        auto* metadata = new QLabel(
            WrapLongTokens(
                QStringLiteral("Version: %1 · Author: %2").arg(version, author)), card);
        metadata->setObjectName(QStringLiteral("modMetadata"));
        metadata->setWordWrap(true);
        details->addWidget(metadata);

        QStringList evidence;
        if (mod.subscribed) {
            evidence.push_back(QStringLiteral("Subscribed (not proof of load)"));
        }
        if (mod.mounted) {
            evidence.push_back(QStringLiteral("Mounted package"));
        }
        if (mod.active_workshop) {
            evidence.push_back(QStringLiteral("Active Workshop entry"));
        }
        if (mod.active_local) {
            evidence.push_back(QStringLiteral("Active local entry"));
        }
        auto* evidence_label = new QLabel(
            WrapLongTokens(QStringLiteral("Evidence: %1")
                .arg(evidence.join(QStringLiteral(", ")))), card);
        evidence_label->setObjectName(QStringLiteral("modEvidence"));
        evidence_label->setWordWrap(true);
        evidence_label->setMinimumWidth(0);
        details->addWidget(evidence_label);

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
            if (load_workshop_previews_ && mod.active_workshop
                && !checked_thumbnails_.contains(mod.id)) {
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
    completed_jobs_ = new JobHistoryPage(this);
    layout->addWidget(completed_jobs_, 1);
}

void JobsPage::UpdateState(const telemetry::TelemetryUiState& state) {
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
    trip_events_ = new TripEventsPage(tabs_);
    tabs_->addTab(sessions_, QStringLiteral("SESSIONS & TRIPS"));
    tabs_->addTab(trip_events_, QStringLiteral("TOLLS & TRANSPORT"));
    layout->addWidget(tabs_, 1);
}

void HistoryPage::UpdateState(const telemetry::TelemetryUiState& state) {
    sessions_->UpdateState(state);
    trip_events_->UpdateState(state);
}

void HistoryPage::UpdateHistory(const session::HistorySnapshot& history) {
    sessions_->UpdateHistory(history);
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
