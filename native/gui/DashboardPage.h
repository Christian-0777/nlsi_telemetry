#pragma once

#include <QHash>
#include <QVector>

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
    QLabel* special_job_indicator_ = nullptr;
    QPlainTextEdit* travel_expense_summary_ = nullptr;
};

} // namespace nlsi::gui
