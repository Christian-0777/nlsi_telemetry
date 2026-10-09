#include "SessionManager.h"

#include <chrono>
#include <sstream>

namespace nlsi::session {

SessionManager::SessionManager() = default;
SessionManager::~SessionManager() = default;

void SessionManager::Start() {
    Start(L"", L"");
}

void SessionManager::Start(const std::wstring& timestamp, const std::wstring& game) {
    if (active_) {
        return;
    }
    active_ = true;
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::wstringstream ss;
    ss << L"session-" << ms;
    session_id_ = ss.str();
    started_at_ = timestamp;
    game_ = game;
}

void SessionManager::Stop() {
    active_ = false;
    session_id_.clear();
    started_at_.clear();
    game_.clear();
}

bool SessionManager::IsActive() const {
    return active_;
}

std::wstring SessionManager::CurrentId() const {
    return session_id_;
}

std::wstring SessionManager::StartedAt() const {
    return started_at_;
}

std::wstring SessionManager::Game() const {
    return game_;
}

} // namespace nlsi::session
