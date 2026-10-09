#pragma once

#include <string>

namespace nlsi::session {

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    void Start();
    void Start(const std::wstring& timestamp, const std::wstring& game);
    void Stop();
    bool IsActive() const;
    std::wstring CurrentId() const;
    std::wstring StartedAt() const;
    std::wstring Game() const;

private:
    bool active_ = false;
    std::wstring session_id_;
    std::wstring started_at_;
    std::wstring game_;
};

} // namespace nlsi::session
