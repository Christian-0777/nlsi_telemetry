#include "RenCloudProvider.h"

namespace nlsi::providers {

RenCloudProvider::RenCloudProvider() = default;
RenCloudProvider::~RenCloudProvider() = default;

bool RenCloudProvider::Connect() {
    connected_ = true;
    return connected_;
}

bool RenCloudProvider::IsConnected() const {
    return connected_;
}

std::wstring RenCloudProvider::Name() const {
    return L"RenCloud";
}

} // namespace nlsi::providers
