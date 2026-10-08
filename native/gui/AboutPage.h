#pragma once

#include "PageSupport.h"

namespace nlsi::gui {

class AboutPage final : public DetailPage {
public:
    explicit AboutPage(const QString& version, QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QString version_;
};

} // namespace nlsi::gui
