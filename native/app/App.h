#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <string>

#include "telemetry/TelemetryCore.h"

class App {
public:
    App();
    ~App();

    int Run();
    std::wstring ProductName() const;
    std::wstring VersionLabel() const;

private:
    void Initialize();
    void Shutdown();

    std::wstring product_name_ = L"NLSI Exclusive Logbook";
    std::wstring version_label_ = L"v1.3.3 Alpha";
    nlsi::telemetry::TelemetryCore telemetry_core_;
};
