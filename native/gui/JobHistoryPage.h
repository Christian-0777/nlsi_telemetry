#pragma once

#include "PageSupport.h"

class QLabel;
class QTableView;
class QStandardItemModel;
class QTabWidget;

namespace nlsi::gui {

class CurrentJobPage;

class JobHistoryPage final : public StatePage {
public:
    explicit JobHistoryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* message_ = nullptr;
};

class SessionsPage final : public StatePage {
public:
    explicit SessionsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* message_ = nullptr;
};

class EventsPage final : public StatePage {
public:
    explicit EventsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* message_ = nullptr;
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

private:
    QTabWidget* tabs_ = nullptr;
    CurrentJobPage* current_job_ = nullptr;
    JobHistoryPage* completed_jobs_ = nullptr;
};

class HistoryPage final : public StatePage {
public:
    explicit HistoryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QTabWidget* tabs_ = nullptr;
    SessionsPage* sessions_ = nullptr;
    JobHistoryPage* completed_jobs_ = nullptr;
};

} // namespace nlsi::gui
