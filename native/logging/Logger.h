#pragma once

#include <string>

namespace nlsi::logging {

class Logger {
public:
    Logger(const std::wstring& log_path = L"NLSI-Exclusive-Logbook.log");
    ~Logger();

    void Log(const std::wstring& message);
    std::wstring LogPath() const;

private:
    std::wstring log_path_;
};

} // namespace nlsi::logging
