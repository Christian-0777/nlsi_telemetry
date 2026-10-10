#pragma once

#include <QHash>
#include <QVector>
#include <QDateTime>

#include "PageSupport.h"

class QLabel;
class QGridLayout;
class QFrame;
class QPlainTextEdit;

namespace nlsi::gui {

class DashboardPage final : public StatePage {
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
    void UpdateHistory(const session::HistorySnapshot& history) override;

private:
    QGridLayout* AddSection(const QString& key, const QString& title);
    void AddMetric(
        QGridLayout* grid,
        int row,
        int column,
        const QString& key,
        const QString& title,
        bool cruise_indicator = false);
    void SetValue(const QString& key, const QString& value);

    QHash<QString, QLabel*> values_;
    QHash<QString, QLabel*> cruise_indicators_;
    QHash<QString, QLabel*> parking_brake_indicators_;
    QString game_log_path_;
    QString game_log_version_;
    QDateTime game_log_modified_;
    QLabel* special_job_indicator_ = nullptr;
    QPlainTextEdit* travel_expense_summary_ = nullptr;
};

} // namespace nlsi::gui
