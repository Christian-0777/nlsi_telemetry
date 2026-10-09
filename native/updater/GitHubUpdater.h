#pragma once

#include <string>

namespace nlsi::updater {

class GitHubUpdater {
public:
    GitHubUpdater();
    ~GitHubUpdater();

    static int CompareVersions(const std::wstring& left, const std::wstring& right);
    static bool IsUpdateAvailable(const std::wstring& current_version, const std::wstring& remote_version);
    std::wstring LatestVersion() const;

private:
    std::wstring latest_version_ = L"1.3.9-beta";
};

} // namespace nlsi::updater
