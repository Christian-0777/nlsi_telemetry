#pragma once

#include "PageSupport.h"

namespace nlsi::gui {

class LiveDrivePage final : public DetailPage {
public:
    explicit LiveDrivePage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
};

} // namespace nlsi::gui
