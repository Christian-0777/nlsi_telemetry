#pragma once

#include <QHash>

#include "PageSupport.h"

class QLabel;
class QGridLayout;

namespace nlsi::gui {

class DashboardPage final : public StatePage {
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* AddCard(const QString& key, const QString& title, int row, int column,
        QGridLayout* grid);
    void SetValue(const QString& key, const QString& value);

    QHash<QString, QLabel*> values_;
};

} // namespace nlsi::gui
