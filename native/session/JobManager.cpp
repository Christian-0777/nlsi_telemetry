#include "JobManager.h"

namespace nlsi::session {

JobManager::JobManager() = default;
JobManager::~JobManager() = default;

void JobManager::SetCurrentJob(const std::wstring& job_name) {
    current_job_ = job_name;
}

void JobManager::ClearCurrentJob() {
    current_job_.clear();
}

bool JobManager::HasActiveJob() const {
    return !current_job_.empty();
}

std::wstring JobManager::CurrentJobName() const {
    return current_job_;
}

} // namespace nlsi::session
