#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct VvmEntryRecord {
    std::string name;
    uint16_t generalPurposeFlags = 0;
    uint16_t compressionMethod = 0;
    uint32_t crc32 = 0;
    uint64_t compressedSize = 0;
    uint64_t uncompressedSize = 0;
    uint64_t localHeaderOffset = 0;
    uint64_t dataOffset = 0;
};

struct VvmArchiveSummary {
    std::filesystem::path archivePath;
    std::vector<VvmEntryRecord> entries;
    std::string manifestJson;
    std::string metasJson;
    std::vector<std::string> modelBinNames;
};

struct VvmEntryValidationRecord {
    std::string name;
    uint64_t uncompressedSize = 0;
    uint32_t expectedCrc32 = 0;
    uint32_t actualCrc32 = 0;
    bool isValid = false;
};

struct ZipDirectoryExtractionSummary {
    size_t fileCount = 0;
    uint64_t byteCount = 0;
};

std::vector<std::filesystem::path> collectVvmModelFiles(const std::vector<std::filesystem::path> &modelRoots);
VvmArchiveSummary inspectVvmArchive(const std::filesystem::path &archivePath);
std::vector<uint8_t> extractVvmEntryBytes(const std::filesystem::path &archivePath, const std::string &entryName);
std::vector<uint8_t> extractVvmEntryBytesAt(const std::filesystem::path &archivePath, const std::string &entryName, uint64_t dataOffset, uint64_t compressedSize, uint64_t uncompressedSize, uint16_t compressionMethod);
std::vector<uint8_t> readVvmEntryPrefixBytesAt(const std::filesystem::path &archivePath, const std::string &entryName, uint64_t dataOffset, uint64_t compressedSize, uint64_t uncompressedSize, uint16_t compressionMethod, size_t prefixBytes);
std::vector<VvmEntryValidationRecord> validateVvmArchiveEntries(const std::filesystem::path &archivePath);
void extractVvmArchive(const std::filesystem::path &archivePath, const std::filesystem::path &outputDirectory);
ZipDirectoryExtractionSummary extractZipDirectoryEntries(const std::filesystem::path &archivePath, const std::string &directoryMarker, const std::filesystem::path &outputDirectory);
ZipDirectoryExtractionSummary extractZipArchivePreservingPaths(const std::filesystem::path &archivePath, const std::filesystem::path &outputDirectory);
bool extractZipEntryByMarker(const std::filesystem::path &archivePath, const std::string &entryMarker, const std::filesystem::path &outputPath, uint64_t &byteCount);
bool zipArchiveHasEntryMarker(const std::filesystem::path &archivePath, const std::string &entryMarker);
