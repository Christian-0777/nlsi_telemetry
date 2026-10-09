#pragma once

#include <QHash>

#include "PageSupport.h"

class QLabel;
class QGridLayout;
class QFrame;

namespace nlsi::gui {

class DashboardPage final : public StatePage {
public:
    explicit DashboardPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QLabel* AddCard(const QString& key, const QString& title, int row, int column,
        QGridLayout* grid, int column_span = 1);
    void SetValue(const QString& key, const QString& value);
    void SetCardVisible(const QString& key, bool visible);

    QHash<QString, QLabel*> values_;
    QHash<QString, QFrame*> cards_;
};

} // namespace nlsi::gui
