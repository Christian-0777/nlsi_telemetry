#pragma once

#include "PageSupport.h"

namespace nlsi::gui {

class ProvidersPage final : public DetailPage {
public:
    explicit ProvidersPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;
};

} // namespace nlsi::gui
