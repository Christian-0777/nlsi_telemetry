#pragma once

#include "PageSupport.h"

namespace nlsi::gui {

class TelemetryPage final : public DetailPage {
public:
    explicit TelemetryPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
};

} // namespace nlsi::gui
