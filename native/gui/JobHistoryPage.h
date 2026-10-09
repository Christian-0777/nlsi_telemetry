#pragma once

#include "PageSupport.h"

#include <cstdint>

class QLabel;
class QTableView;
class QStandardItemModel;
class QTabWidget;
class QPushButton;

namespace nlsi::gui {

class CurrentJobPage;

class JobHistoryPage final : public StatePage {
public:
    explicit JobHistoryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QLabel* message_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    QPushButton* export_button_ = nullptr;
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
    QStandardItemModel* model_ = nullptr;
    std::uint64_t history_revision_ = static_cast<std::uint64_t>(-1);
};

class ActiveModsPage final : public StatePage {
public:
    explicit ActiveModsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* message_ = nullptr;
};

class JobsPage final : public StatePage {
public:
    explicit JobsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QTabWidget* tabs_ = nullptr;
    CurrentJobPage* current_job_ = nullptr;
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
    JobHistoryPage* completed_jobs_ = nullptr;
};

} // namespace nlsi::gui
