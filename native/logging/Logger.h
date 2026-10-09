#pragma once

#include <mutex>
#include <string>

#include <QStringList>

namespace nlsi::logging {

class Logger {
public:
    explicit Logger(const std::wstring& log_path = L"NLSI-Exclusive-Logbook.log");
    ~Logger();

    bool Log(const std::wstring& message, std::wstring* error = nullptr);
    std::wstring LogPath() const;
    std::wstring NlsiLogPath() const;
    static bool ReadNlsiLog(
        const std::wstring& path,
        QStringList* messages,
        std::wstring* error = nullptr);

private:
    bool EnsureNlsiLogValid(std::wstring* error);
    bool WriteNlsiRecord(const std::wstring& message, std::wstring* error);

    std::wstring log_path_;
    std::wstring nlsi_log_path_;
    bool nlsi_log_validated_ = false;
    std::mutex mutex_;
};

} // namespace nlsi::logging
