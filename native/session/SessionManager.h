#pragma once

#include <string>

namespace nlsi::session {

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    void Start();
    void Stop();
    bool IsActive() const;
    std::wstring CurrentId() const;

private:
    bool active_ = false;
    std::wstring session_id_;
};

} // namespace nlsi::session
