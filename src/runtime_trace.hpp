#pragma once

#include "runtime.hpp"

#include <filesystem>
#include <string>

struct RuntimeTraceOptions {
    std::filesystem::path outputDirectory;
    std::string text;
    uint32_t styleId = 3;
    bool isKana = false;
};

std::filesystem::path writeRuntimeTrace(RuntimeState &runtimeState, const RuntimeTraceOptions &traceOptions);
