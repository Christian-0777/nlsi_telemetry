#pragma once

#include <string>

namespace nlsi::providers {

class RenCloudProvider {
public:
    RenCloudProvider();
    ~RenCloudProvider();

    bool Connect();
    bool IsConnected() const;
    std::wstring Name() const;

private:
    bool connected_ = false;
};

} // namespace nlsi::providers
