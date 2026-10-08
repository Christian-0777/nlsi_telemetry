#pragma once

#include "PageSupport.h"

class QTabWidget;

namespace nlsi::gui {

class SettingsPage final : public StatePage {
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QTabWidget* tabs_ = nullptr;
    StatePage* application_settings_ = nullptr;
    StatePage* providers_ = nullptr;
    StatePage* telemetry_ = nullptr;
    StatePage* active_mods_ = nullptr;
};

} // namespace nlsi::gui
