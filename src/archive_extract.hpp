#pragma once

#include <cstdint>
#include <filesystem>

struct ArchiveExtractionSummary {
    size_t fileCount = 0;
    uint64_t byteCount = 0;
};

ArchiveExtractionSummary extractArchivePreservingPaths(const std::filesystem::path &archivePath, const std::filesystem::path &outputDirectory);
