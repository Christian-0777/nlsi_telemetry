#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "telemetry/TelemetryUiState.h"
#include "session/HistoryStore.h"

class QLabel;
class QUrl;
class QVBoxLayout;
class QWidget;

namespace nlsi::gui {

QString FieldText(const telemetry::TelemetryField<std::wstring>& field);
QString NumberText(const telemetry::TelemetryField<double>& field, int precision = 2,
    const QString& suffix = {});
QString OptionalNumberText(const std::optional<double>& value, int precision,
    const QString& suffix = {});
QString DurationText(const std::optional<double>& seconds);
QString FormatNumber(double value, int precision);
QString NumericText(const QString& value);
QString TimestampText(const QString& value);

class StatePage : public QWidget {
public:
    using QWidget::QWidget;
    virtual void UpdateState(const telemetry::TelemetryUiState& state) = 0;
    virtual void UpdateHistory(const session::HistorySnapshot&) {}
};

class DetailPage : public StatePage {
public:
    explicit DetailPage(QWidget* parent = nullptr);
    void AddField(const QString& key, const QString& label);
    void SetExternalLink(const QString& key, const QString& label, const QUrl& url);
    void AddContentWidget(QWidget* widget);

protected:
    void SetValue(const QString& key, const QString& value);

private:
    QVBoxLayout* page_layout_ = nullptr;
    QVBoxLayout* fields_layout_ = nullptr;
    QHash<QString, QLabel*> values_;
};

} // namespace nlsi::gui
