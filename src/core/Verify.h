#pragma once

#include <filesystem>
#include <string>

namespace core {

struct VerifyOptions {
    bool enablePersistence = true;
    std::filesystem::path persistenceRoot;
};

struct VerifyResult {
    bool ok = true;
    std::string message;
};

VerifyResult RunAll(const VerifyOptions& options);

} // namespace core
