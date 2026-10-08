#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "telemetry/TelemetryUiState.h"

class QLabel;
class QVBoxLayout;

namespace nlsi::gui {

QString FieldText(const telemetry::TelemetryField<std::wstring>& field);
QString NumberText(const telemetry::TelemetryField<double>& field, int precision = 1,
    const QString& suffix = {});
QString OptionalNumberText(const std::optional<double>& value, int precision,
    const QString& suffix = {});
QString DurationText(const std::optional<double>& seconds);

class StatePage : public QWidget {
public:
    using QWidget::QWidget;
    virtual void UpdateState(const telemetry::TelemetryUiState& state) = 0;
};

class DetailPage : public StatePage {
public:
    DetailPage(const QString& title, const QString& description, QWidget* parent = nullptr);
    void AddField(const QString& key, const QString& label);

protected:
    void SetValue(const QString& key, const QString& value);

private:
    QVBoxLayout* fields_layout_ = nullptr;
    QHash<QString, QLabel*> values_;
};

} // namespace nlsi::gui
