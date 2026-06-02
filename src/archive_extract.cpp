#include "archive_extract.hpp"

#include "utility.hpp"
#include "vvm_archive.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;

static bool hasSuffixText(const std::string &text, const std::string &suffixText) {
    return text.size() >= suffixText.size() && text.compare(text.size() - suffixText.size(), suffixText.size(), suffixText) == 0;
}

static bool isSupportedZipArchive(const fs::path &archivePath) {
    return lowercaseAscii(archivePath.extension().string()) == ".zip";
}

static bool isSupportedTarArchive(const fs::path &archivePath) {
    std::string archiveText = lowercaseAscii(archivePath.filename().string());
    return hasSuffixText(archiveText, ".tgz") || hasSuffixText(archiveText, ".tar.gz") || hasSuffixText(archiveText, ".tar");
}

static fs::path createSafeArchiveOutputPath(const fs::path &outputDirectory, const std::string &entryName) {
    fs::path entryPath(entryName);
    if (entryPath.is_absolute()) {
        throw std::runtime_error("絶対パス entry は展開できません: " + entryName);
    }
    fs::path safePath = outputDirectory;
    for (const fs::path &pathPart : entryPath) {
        std::string pathText = pathPart.string();
        if (pathText.empty() || pathText == ".") {
            continue;
        }
        if (pathText == "..") {
            throw std::runtime_error("不正な entry パスです: " + entryName);
        }
        safePath /= pathPart;
    }
    return safePath;
}

static std::string readTarHeaderText(const uint8_t *headerBytes, size_t offset, size_t byteCount) {
    size_t endPosition = 0;
    while (endPosition < byteCount && headerBytes[offset + endPosition] != '\0') {
        endPosition++;
    }
    return std::string(reinterpret_cast<const char *>(headerBytes + offset), endPosition);
}

static uint64_t parseTarOctalNumber(const uint8_t *headerBytes, size_t offset, size_t byteCount) {
    std::string numberText = readTarHeaderText(headerBytes, offset, byteCount);
    size_t firstDigit = numberText.find_first_of("01234567");
    if (firstDigit == std::string::npos) {
        return 0;
    }
    size_t lastDigit = numberText.find_last_not_of("01234567");
    std::string trimmedNumber = lastDigit == std::string::npos
        ? numberText.substr(firstDigit)
        : numberText.substr(firstDigit, lastDigit - firstDigit + 1);
    if (trimmedNumber.empty()) {
        return 0;
    }
    return std::stoull(trimmedNumber, nullptr, 8);
}

static bool isZeroTarBlock(const uint8_t *headerBytes, size_t byteCount) {
    for (size_t byteIndex = 0; byteIndex < byteCount; byteIndex++) {
        if (headerBytes[byteIndex] != 0) {
            return false;
        }
    }
    return true;
}

static std::vector<uint8_t> decompressGzipBytes(const fs::path &archivePath) {
    std::vector<uint8_t> compressedBytes = readBinaryFile(archivePath);
    z_stream inflateStream{};
    inflateStream.next_in = reinterpret_cast<Bytef *>(compressedBytes.data());
    inflateStream.avail_in = static_cast<uInt>(compressedBytes.size());
    int initStatus = inflateInit2(&inflateStream, 16 + MAX_WBITS);
    if (initStatus != Z_OK) {
        throw std::runtime_error("gzip 初期化に失敗しました: " + archivePath.string());
    }
    std::vector<uint8_t> expandedBytes;
    std::array<uint8_t, 1 << 15> chunkBytes{};
    while (true) {
        inflateStream.next_out = chunkBytes.data();
        inflateStream.avail_out = static_cast<uInt>(chunkBytes.size());
        int inflateStatus = inflate(&inflateStream, Z_NO_FLUSH);
        size_t producedBytes = chunkBytes.size() - inflateStream.avail_out;
        expandedBytes.insert(expandedBytes.end(), chunkBytes.begin(), chunkBytes.begin() + static_cast<std::ptrdiff_t>(producedBytes));
        if (inflateStatus == Z_STREAM_END) {
            break;
        }
        if (inflateStatus != Z_OK) {
            inflateEnd(&inflateStream);
            throw std::runtime_error("gzip 展開に失敗しました: " + archivePath.string());
        }
    }
    inflateEnd(&inflateStream);
    return expandedBytes;
}

static ArchiveExtractionSummary extractTarBytesPreservingPaths(const std::vector<uint8_t> &tarBytes, const fs::path &outputDirectory) {
    ArchiveExtractionSummary extractionSummary;
    size_t offset = 0;
    while (offset + 512 <= tarBytes.size()) {
        const uint8_t *headerBytes = tarBytes.data() + offset;
        if (isZeroTarBlock(headerBytes, 512)) {
            break;
        }
        std::string nameText = readTarHeaderText(headerBytes, 0, 100);
        std::string prefixText = readTarHeaderText(headerBytes, 345, 155);
        std::string entryName = prefixText.empty() ? nameText : prefixText + "/" + nameText;
        char typeFlag = static_cast<char>(headerBytes[156]);
        uint64_t entrySize = parseTarOctalNumber(headerBytes, 124, 12);
        size_t dataOffset = offset + 512;
        if (dataOffset + entrySize > tarBytes.size()) {
            throw std::runtime_error("tar entry が壊れています: " + entryName);
        }
        if (!entryName.empty() && (typeFlag == '\0' || typeFlag == '0')) {
            std::vector<uint8_t> entryBytes(tarBytes.begin() + static_cast<std::ptrdiff_t>(dataOffset), tarBytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + entrySize));
            writeBinaryFile(createSafeArchiveOutputPath(outputDirectory, entryName), entryBytes);
            extractionSummary.fileCount++;
            extractionSummary.byteCount += entrySize;
        }
        size_t paddedSize = static_cast<size_t>(((entrySize + 511) / 512) * 512);
        offset = dataOffset + paddedSize;
    }
    if (extractionSummary.fileCount == 0) {
        throw std::runtime_error("展開対象 entry が見つかりません");
    }
    return extractionSummary;
}

static ArchiveExtractionSummary extractTarArchivePreservingPaths(const fs::path &archivePath, const fs::path &outputDirectory) {
    std::string archiveText = lowercaseAscii(archivePath.filename().string());
    std::vector<uint8_t> tarBytes = hasSuffixText(archiveText, ".tar")
        ? readBinaryFile(archivePath)
        : decompressGzipBytes(archivePath);
    return extractTarBytesPreservingPaths(tarBytes, outputDirectory);
}

ArchiveExtractionSummary extractArchivePreservingPaths(const fs::path &archivePath, const fs::path &outputDirectory) {
    ensurePathExists(archivePath, "archive");
    if (isSupportedZipArchive(archivePath)) {
        ArchiveExtractionSummary extractionSummary;
        ZipDirectoryExtractionSummary zipSummary = extractZipArchivePreservingPaths(archivePath, outputDirectory);
        extractionSummary.fileCount = zipSummary.fileCount;
        extractionSummary.byteCount = zipSummary.byteCount;
        return extractionSummary;
    }
    if (isSupportedTarArchive(archivePath)) {
        return extractTarArchivePreservingPaths(archivePath, outputDirectory);
    }
    throw std::runtime_error("未対応の archive です: " + archivePath.string());
}
