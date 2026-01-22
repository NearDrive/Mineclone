#pragma once

#include <string>

namespace core {

struct WorldTestResult {
    bool ok = false;
    std::string message;
    std::string checksum;
};

WorldTestResult RunWorldTest();

} // namespace core
