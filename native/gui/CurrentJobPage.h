#pragma once

#include "PageSupport.h"

namespace nlsi::gui {

class CurrentJobPage final : public DetailPage {
public:
    explicit CurrentJobPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
};

} // namespace nlsi::gui
