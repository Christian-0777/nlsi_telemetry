#pragma once

#include "PageSupport.h"
#include "ModLogParser.h"

#include <cstdint>

#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QVector>

class QLabel;
class QTableView;
class QStandardItemModel;
class QPushButton;
class QVBoxLayout;
class QWidget;
class QTabWidget;
class QNetworkAccessManager;
class QFileSystemWatcher;
class QTimer;
class QShowEvent;
class QHideEvent;
template <typename T>
class QFutureWatcher;

namespace nlsi::gui {

class JobHistoryPage final : public StatePage {
public:
    explicit JobHistoryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QLabel* message_ = nullptr;
    QVBoxLayout* cards_layout_ = nullptr;
    QWidget* cards_container_ = nullptr;
    QVector<session::JobRecord> jobs_;
    std::uint64_t history_revision_ = static_cast<std::uint64_t>(-1);
};

class SessionsPage final : public StatePage {
public:
    explicit SessionsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QLabel* message_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    std::uint64_t history_revision_ = static_cast<std::uint64_t>(-1);
    QString live_status_;
};

class EventsPage final : public StatePage {
public:
    explicit EventsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QLabel* message_ = nullptr;
    QWidget* entries_container_ = nullptr;
    QVBoxLayout* entries_layout_ = nullptr;
    std::uint64_t history_revision_ = static_cast<std::uint64_t>(-1);
    qsizetype event_count_ = -1;
};

class TripEventsPage final : public StatePage {
public:
    explicit TripEventsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QLabel* message_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    std::uint64_t history_revision_ = static_cast<std::uint64_t>(-1);
};

class ActiveModsPage final : public StatePage {
public:
    explicit ActiveModsPage(
        QWidget* parent = nullptr,
        const QString& documents_directory = {},
        bool load_workshop_previews = true);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct GamePanel {
        QString title;
        QWidget* page = nullptr;
        QLabel* message = nullptr;
        QVBoxLayout* mods_layout = nullptr;
        QPushButton* reinitialize_button = nullptr;
        QFutureWatcher<modlog::GameLogResult>* init_watcher = nullptr;
        modlog::GameLogResult result;
        QString status;
        QString signature;
        QDateTime retry_after;
        bool manual_reinitialize = false;
        bool initializing = false;
    };

    void RefreshLogs();
    void InitializeLog(qsizetype panel_index, bool manual);
    void RenderGamePanel(GamePanel& panel);
    void LoadThumbnail(const QString& workshop_id);

    QVector<GamePanel> game_panels_;
    QNetworkAccessManager* network_ = nullptr;
    QFileSystemWatcher* file_watcher_ = nullptr;
    QTimer* refresh_timer_ = nullptr;
    QHash<QString, QPixmap> thumbnails_;
    QSet<QString> checked_thumbnails_;
    QString documents_directory_;
    bool load_workshop_previews_ = true;
};

class JobsPage final : public StatePage {
public:
    explicit JobsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    JobHistoryPage* completed_jobs_ = nullptr;
};

class HistoryPage final : public StatePage {
public:
    explicit HistoryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QTabWidget* tabs_ = nullptr;
    SessionsPage* sessions_ = nullptr;
    TripEventsPage* trip_events_ = nullptr;
};

} // namespace nlsi::gui
