#include "SessionManager.h"

#include <chrono>
#include <sstream>

namespace nlsi::session {

SessionManager::SessionManager() = default;
SessionManager::~SessionManager() = default;

void SessionManager::Start() {
    active_ = true;
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::wstringstream ss;
    ss << L"session-" << ms;
    session_id_ = ss.str();
}

void SessionManager::Stop() {
    active_ = false;
    session_id_.clear();
}

bool SessionManager::IsActive() const {
    return active_;
}

std::wstring SessionManager::CurrentId() const {
    return session_id_;
}

} // namespace nlsi::session
