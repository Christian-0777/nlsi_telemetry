#pragma once

#include <QHash>
#include <QVector>

#include "PageSupport.h"

class QLabel;
class QGridLayout;
class QFrame;
class QResizeEvent;

namespace nlsi::gui {

class DashboardPage final : public StatePage {
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QGridLayout* AddSection(const QString& key, const QString& title);
    void AddMetric(
        QGridLayout* grid,
        int row,
        int column,
        const QString& key,
        const QString& title,
        bool cruise_indicator = false);
    void RegisterResponsiveLabel(QLabel* label, int emphasis = 0);
    void ApplyResponsiveFontSize();
    void SetValue(const QString& key, const QString& value);

    QHash<QString, QLabel*> values_;
    QHash<QString, QLabel*> cruise_indicators_;
    QVector<QLabel*> responsive_labels_;
    QLabel* special_job_indicator_ = nullptr;
    int responsive_font_size_ = 0;
};

} // namespace nlsi::gui
