#pragma once

#include "vvm_archive.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ModelAssetRecord {
    std::filesystem::path archivePath;
    std::string entryName;
    uint64_t dataOffset = 0;
    uint64_t compressedSize = 0;
    uint64_t uncompressedSize = 0;
    uint32_t crc32 = 0;
    uint16_t compressionMethod = 0;
};

struct ManifestModelRecord {
    std::filesystem::path archivePath;
    std::string entryName;
    std::string domainName;
    std::string operationName;
    std::string modelType;
};

std::vector<ModelAssetRecord> collectModelAssets(const std::vector<VvmArchiveSummary> &archiveSummaries);
std::vector<ModelAssetRecord> collectModelAssetsFromModelRoots(const std::vector<std::filesystem::path> &modelRoots);
std::vector<ManifestModelRecord> collectManifestModels(const VvmArchiveSummary &archiveSummary);
std::string createModelAssetTable(const std::vector<ModelAssetRecord> &modelAssets);
std::string createModelAssetJson(const std::vector<ModelAssetRecord> &modelAssets);
std::string createModelDumpText(const std::vector<VvmArchiveSummary> &archiveSummaries, size_t prefixBytes = 64, bool shouldScanModels = false);
std::string exportOnnxModelFiles(const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &outputDirectory);
