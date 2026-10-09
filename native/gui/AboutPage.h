#pragma once

#include "PageSupport.h"

namespace nlsi::updater {
class GitHubUpdater;
}

namespace nlsi::gui {

class UpdateControls;

class AboutPage final : public DetailPage {
public:
    explicit AboutPage(const QString& version, QWidget* parent = nullptr);
    ~AboutPage() override;
    void UpdateState(const telemetry::TelemetryUiState& state) override;

private:
    QString version_;
    UpdateControls* update_controls_ = nullptr;
    nlsi::updater::GitHubUpdater* updater_ = nullptr;
};

} // namespace nlsi::gui
