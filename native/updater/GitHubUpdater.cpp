#include "GitHubUpdater.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {
std::vector<int> ParseVersionParts(const std::wstring& version) {
    std::vector<int> parts;
    std::wstring token;
    for (wchar_t ch : version) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            token.push_back(ch);
        } else if (!token.empty()) {
            parts.push_back(std::stoi(token));
            token.clear();
        }
    }
    if (!token.empty()) {
        parts.push_back(std::stoi(token));
    }
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    return parts;
}
} // namespace

namespace nlsi::updater {

GitHubUpdater::GitHubUpdater() = default;
GitHubUpdater::~GitHubUpdater() = default;

int GitHubUpdater::CompareVersions(const std::wstring& left, const std::wstring& right) {
    const auto lhs = ParseVersionParts(left);
    const auto rhs = ParseVersionParts(right);
    for (size_t i = 0; i < std::max(lhs.size(), rhs.size()); ++i) {
        const int lv = i < lhs.size() ? lhs[i] : 0;
        const int rv = i < rhs.size() ? rhs[i] : 0;
        if (lv < rv) {
            return -1;
        }
        if (lv > rv) {
            return 1;
        }
    }
    return 0;
}

bool GitHubUpdater::IsUpdateAvailable(const std::wstring& current_version, const std::wstring& remote_version) {
    return CompareVersions(current_version, remote_version) < 0;
}

std::wstring GitHubUpdater::LatestVersion() const {
    return latest_version_;
}

} // namespace nlsi::updater
