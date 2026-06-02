#pragma once

#include "cli_shared.hpp"
#include "vvm_archive.hpp"

#include <filesystem>
#include <string>
#include <vector>

std::vector<VvmArchiveSummary> collectArchiveSummaries(const std::vector<std::filesystem::path> &modelRoots);
std::string createArchiveInspectionText(const std::vector<VvmArchiveSummary> &archiveSummaries);
std::string createArchiveValidationText(const std::vector<VvmArchiveSummary> &archiveSummaries);
std::string createArchiveInventoryText(const std::vector<VvmArchiveSummary> &archiveSummaries);
int runExtractCommand(const CliOptions &cliOptions);
int runCacheCommand(const CliOptions &cliOptions);
std::string createResourceExtractionText(const CliOptions &cliOptions);
std::string createEngineAssetExtractionText(const CliOptions &cliOptions);
std::string createOnnxruntimeExtractionText(const CliOptions &cliOptions);
std::string createRuntimeExtractionText(const CliOptions &cliOptions);
std::filesystem::path resolveRuntimeRootPath(const std::filesystem::path &runtimeInputPath);
