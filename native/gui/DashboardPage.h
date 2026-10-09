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
    QLabel* AddCard(const QString& key, const QString& title, QGridLayout* grid);
    void ReflowCards();
    void SetValue(const QString& key, const QString& value);
    void SetCardVisible(const QString& key, bool visible);

    QHash<QString, QLabel*> values_;
    QHash<QString, QFrame*> cards_;
    QVector<QFrame*> ordered_cards_;
    QGridLayout* grid_ = nullptr;
    int grid_columns_ = 0;
};

} // namespace nlsi::gui
