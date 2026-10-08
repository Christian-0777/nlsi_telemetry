#pragma once

#include <string>

namespace nlsi::session {

class JobManager {
public:
    JobManager();
    ~JobManager();

    void SetCurrentJob(const std::wstring& job_name);
    void ClearCurrentJob();
    bool HasActiveJob() const;
    std::wstring CurrentJobName() const;

private:
    std::wstring current_job_;
};

} // namespace nlsi::session
