#include "Logger.h"

#include <fstream>

namespace nlsi::logging {

Logger::Logger(const std::wstring& log_path) : log_path_(log_path) {
    std::wofstream stream(log_path_.c_str(), std::ios::app);
    if (stream.is_open()) {
        stream << L"[startup] NLSI Exclusive Logbook" << std::endl;
    }
}

Logger::~Logger() = default;

void Logger::Log(const std::wstring& message) {
    std::wofstream stream(log_path_.c_str(), std::ios::app);
    if (stream.is_open()) {
        stream << message << std::endl;
    }
}

std::wstring Logger::LogPath() const {
    return log_path_;
}

} // namespace nlsi::logging
