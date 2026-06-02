#include "cli_archive.hpp"

#include "archive_extract.hpp"
#include "model_asset.hpp"
#include "model_metadata.hpp"
#include "model_session_cache.hpp"
#include "utility.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

std::vector<VvmArchiveSummary> collectArchiveSummaries(const std::vector<fs::path> &modelRoots) {
    std::vector<VvmArchiveSummary> archiveSummaries;
    for (const fs::path &modelPath : collectVvmModelFiles(modelRoots)) {
        archiveSummaries.push_back(inspectVvmArchive(modelPath));
    }
    return archiveSummaries;
}

std::string createArchiveInspectionText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    size_t styleCount = 0;
    size_t modelBinCount = 0;
    std::vector<std::pair<fs::path, std::vector<StyleRecord>>> modelStyleGroups;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::vector<StyleRecord> styleRecords = extractStylesFromMetasJson(archiveSummary.metasJson);
        styleCount += styleRecords.size();
        modelBinCount += archiveSummary.modelBinNames.size();
        modelStyleGroups.push_back({archiveSummary.archivePath, std::move(styleRecords)});
    }
    std::ostringstream inspectionStream;
    inspectionStream << "models\t" << archiveSummaries.size() << "\n";
    inspectionStream << "styles\t" << styleCount << "\n";
    inspectionStream << "model_bins\t" << modelBinCount << "\n";
    inspectionStream << createStyleTable(modelStyleGroups);
    return inspectionStream.str();
}

std::string createArchiveValidationText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::ostringstream validationStream;
    validationStream << "vvm\tstatus\tentries\tchecked_entries\tmodel_bins\tstyles\tmanifest_bytes\tmetas_bytes\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::vector<StyleRecord> styleRecords = extractStylesFromMetasJson(archiveSummary.metasJson);
        if (styleRecords.empty()) {
            throw std::runtime_error("style がありません: " + archiveSummary.archivePath.string());
        }
        std::vector<VvmEntryValidationRecord> validationRecords = validateVvmArchiveEntries(archiveSummary.archivePath);
        for (const VvmEntryValidationRecord &validationRecord : validationRecords) {
            if (!validationRecord.isValid) {
                throw std::runtime_error("VVM entry 検証に失敗しました: " + archiveSummary.archivePath.string() + ":" + validationRecord.name);
            }
        }
        validationStream << archiveSummary.archivePath.filename().string() << "\t"
                         << "ok\t"
                         << archiveSummary.entries.size() << "\t"
                         << validationRecords.size() << "\t"
                         << archiveSummary.modelBinNames.size() << "\t"
                         << styleRecords.size() << "\t"
                         << archiveSummary.manifestJson.size() << "\t"
                         << archiveSummary.metasJson.size() << "\n";
    }
    return validationStream.str();
}

static std::string createHexUint32(uint32_t numberValue) {
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0') << std::setw(8) << numberValue;
    return hexStream.str();
}

std::string createArchiveInventoryText(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::ostringstream inventoryStream;
    inventoryStream << "vvm\tentry\tdata_offset\tmethod\tcompressed_bytes\tuncompressed_bytes\tcrc32\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::string archiveName = archiveSummary.archivePath.filename().string();
        for (const VvmEntryRecord &archiveEntry : archiveSummary.entries) {
            inventoryStream << archiveName << "\t"
                            << archiveEntry.name << "\t"
                            << archiveEntry.dataOffset << "\t"
                            << archiveEntry.compressionMethod << "\t"
                            << archiveEntry.compressedSize << "\t"
                            << archiveEntry.uncompressedSize << "\t"
                            << createHexUint32(archiveEntry.crc32) << "\n";
        }
    }
    return inventoryStream.str();
}

int runExtractCommand(const CliOptions &cliOptions) {
    if (cliOptions.outputPath.empty() || cliOptions.outputPath == "out.wav" || cliOptions.outputPath == "-") {
        throw std::runtime_error("extract には --out DIR が必要です");
    }
    std::vector<fs::path> modelFiles = collectVvmModelFiles(cliOptions.runtimePaths.modelPaths);
    for (const fs::path &modelPath : modelFiles) {
        fs::path outputDirectory = modelFiles.size() == 1 ? cliOptions.outputPath : cliOptions.outputPath / modelPath.stem();
        extractVvmArchive(modelPath, outputDirectory);
        std::cout << outputDirectory.string() << "\n";
    }
    return 0;
}

static bool isSupportedArchivePath(const fs::path &archivePath);
static std::vector<fs::path> collectArchivePaths(const std::vector<fs::path> &inputPaths);

static std::vector<std::string> createVoicevoxArchiveEntryMarkers(const std::vector<std::string> &fileNames) {
    std::vector<std::string> entryMarkers;
    entryMarkers.reserve(fileNames.size());
    for (const std::string &fileName : fileNames) {
        entryMarkers.push_back("vv-engine/" + fileName);
    }
    return entryMarkers;
}

static bool zipArchiveHasAnyEntryMarker(const fs::path &zipPath, const std::vector<std::string> &entryMarkers) {
    for (const std::string &entryMarker : entryMarkers) {
        if (zipArchiveHasEntryMarker(zipPath, entryMarker)) {
            return true;
        }
    }
    return false;
}

static std::string findZipEntryMarker(const fs::path &zipPath, const std::vector<std::string> &entryMarkers) {
    for (const std::string &entryMarker : entryMarkers) {
        if (zipArchiveHasEntryMarker(zipPath, entryMarker)) {
            return entryMarker;
        }
    }
    return "";
}

static std::vector<fs::path> collectVoicevoxZipPaths(const CliOptions &cliOptions) {
    std::vector<fs::path> zipPaths;
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    auto appendVoicevoxZipIfPresent = [&](const fs::path &zipPath) {
        if (lowercaseAscii(zipPath.extension().string()) != ".zip") {
            return;
        }
        if (zipArchiveHasAnyEntryMarker(zipPath, coreEntryMarkers)) {
            zipPaths.push_back(zipPath);
        }
    };
    for (const fs::path &modelRoot : cliOptions.runtimePaths.modelPaths) {
        ensurePathExists(modelRoot, "ZIP パス");
        if (fs::is_regular_file(modelRoot)) {
            appendVoicevoxZipIfPresent(modelRoot);
            continue;
        }
        if (fs::is_directory(modelRoot)) {
            for (const fs::directory_entry &directoryEntry : fs::directory_iterator(modelRoot)) {
                if (directoryEntry.is_regular_file()) {
                    appendVoicevoxZipIfPresent(directoryEntry.path());
                }
            }
        }
    }
    if (zipPaths.empty()) {
        throw std::runtime_error("VOICEVOX zip が見つかりません");
    }
    return zipPaths;
}

static bool isStandardOnnxruntimeArchivePath(const fs::path &archivePath) {
    if (!isSupportedArchivePath(archivePath)) {
        return false;
    }
    std::string filenameText = lowercaseAscii(archivePath.filename().string());
    return filenameText.find("onnxruntime") != std::string::npos;
}

static std::vector<fs::path> collectOnnxruntimeArchivePaths(const CliOptions &cliOptions) {
    std::vector<fs::path> archivePaths;
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    for (const fs::path &archivePath : collectArchivePaths(cliOptions.runtimePaths.modelPaths)) {
        if (!isStandardOnnxruntimeArchivePath(archivePath)) {
            continue;
        }
        if (lowercaseAscii(archivePath.extension().string()) == ".zip" && zipArchiveHasAnyEntryMarker(archivePath, coreEntryMarkers)) {
            continue;
        }
        archivePaths.push_back(archivePath);
    }
    return archivePaths;
}

static bool hasTarGzExtension(const fs::path &archivePath) {
    std::string archiveText = lowercaseAscii(archivePath.filename().string());
    return (archiveText.size() >= 4 && archiveText.substr(archiveText.size() - 4) == ".tgz")
        || (archiveText.size() >= 7 && archiveText.substr(archiveText.size() - 7) == ".tar.gz");
}

static bool isSupportedArchivePath(const fs::path &archivePath) {
    std::string extensionText = lowercaseAscii(archivePath.extension().string());
    return extensionText == ".zip" || extensionText == ".tar" || hasTarGzExtension(archivePath);
}

static std::vector<fs::path> collectArchivePaths(const std::vector<fs::path> &inputPaths) {
    std::vector<fs::path> archivePaths;
    for (const fs::path &inputPath : inputPaths) {
        ensurePathExists(inputPath, "archive パス");
        if (fs::is_regular_file(inputPath)) {
            if (isSupportedArchivePath(inputPath)) {
                archivePaths.push_back(inputPath);
            }
            continue;
        }
        if (!fs::is_directory(inputPath)) {
            continue;
        }
        for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(inputPath)) {
            if (!directoryEntry.is_regular_file()) {
                continue;
            }
            if (isSupportedArchivePath(directoryEntry.path())) {
                archivePaths.push_back(directoryEntry.path());
            }
        }
    }
    if (archivePaths.empty()) {
        throw std::runtime_error("archive が見つかりません");
    }
    std::sort(archivePaths.begin(), archivePaths.end());
    archivePaths.erase(std::unique(archivePaths.begin(), archivePaths.end()), archivePaths.end());
    return archivePaths;
}

static std::string createZipDirectoryExtractionText(const CliOptions &cliOptions, const std::string &directoryMarker, const fs::path &outputDirectory) {
    std::ostringstream extractionStream;
    extractionStream << "zip\tfiles\tbytes\tout\n";
    for (const fs::path &zipPath : collectVoicevoxZipPaths(cliOptions)) {
        ZipDirectoryExtractionSummary extractionSummary = extractZipDirectoryEntries(zipPath, directoryMarker, outputDirectory);
        extractionStream << zipPath.filename().string() << "\t"
                         << extractionSummary.fileCount << "\t"
                         << extractionSummary.byteCount << "\t"
                         << outputDirectory.string() << "\n";
    }
    return extractionStream.str();
}

std::string createResourceExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.resourceOutputDirectory.empty()) {
        throw std::runtime_error("--extract-resources DIR が必要です");
    }
    return createZipDirectoryExtractionText(cliOptions, "resources/character_info/", cliOptions.resourceOutputDirectory);
}

std::string createEngineAssetExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.engineAssetOutputDirectory.empty()) {
        throw std::runtime_error("--extract-engine-assets DIR が必要です");
    }
    std::ostringstream extractionStream;
    extractionStream << createZipDirectoryExtractionText(cliOptions, "resources/engine_manifest_assets/", cliOptions.engineAssetOutputDirectory);
    fs::path defaultCsvPath = cliOptions.engineAssetOutputDirectory.parent_path() / "default.csv";
    for (const fs::path &zipPath : collectVoicevoxZipPaths(cliOptions)) {
        uint64_t byteCount = 0;
        if (extractZipEntryByMarker(zipPath, "resources/default.csv", defaultCsvPath, byteCount)) {
            extractionStream << zipPath.filename().string() << "\t1\t" << byteCount << "\t" << defaultCsvPath.string() << "\n";
        }
        fs::path settingTemplatePath = cliOptions.engineAssetOutputDirectory.parent_path() / "setting_ui_template.html";
        if (extractZipEntryByMarker(zipPath, "resources/setting_ui_template.html", settingTemplatePath, byteCount)) {
            extractionStream << zipPath.filename().string() << "\t1\t" << byteCount << "\t" << settingTemplatePath.string() << "\n";
        }
    }
    return extractionStream.str();
}

std::string createOnnxruntimeExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.onnxruntimeOutputDirectory.empty()) {
        throw std::runtime_error("--extract-onnxruntime DIR が必要です");
    }
    std::vector<fs::path> archivePaths = collectOnnxruntimeArchivePaths(cliOptions);
    if (archivePaths.empty()) {
        throw std::runtime_error("標準 ONNX Runtime archive が見つかりません");
    }
    std::ostringstream extractionStream;
    extractionStream << "archive\tfiles\tbytes\tout\n";
    for (const fs::path &archivePath : archivePaths) {
        ArchiveExtractionSummary extractionSummary = extractArchivePreservingPaths(archivePath, cliOptions.onnxruntimeOutputDirectory);
        extractionStream << archivePath.filename().string() << "\t"
                         << extractionSummary.fileCount << "\t"
                         << extractionSummary.byteCount << "\t"
                         << cliOptions.onnxruntimeOutputDirectory.string() << "\n";
    }
    return extractionStream.str();
}

static void appendRuntimeExtractionRecord(std::ostream *extractionStream, const std::string &componentName, size_t fileCount, uint64_t byteCount, const fs::path &outputPath) {
    if (extractionStream == nullptr) {
        return;
    }
    *extractionStream << componentName << "\t"
                      << fileCount << "\t"
                      << byteCount << "\t"
                      << outputPath.string() << "\n";
}

static size_t countRegularFilesInDirectory(const fs::path &directoryPath) {
    if (!fs::exists(directoryPath)) {
        return 0;
    }
    size_t fileCount = 0;
    for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(directoryPath)) {
        if (directoryEntry.is_regular_file()) {
            fileCount++;
        }
    }
    return fileCount;
}

static uint64_t sumRegularFileBytesInDirectory(const fs::path &directoryPath) {
    if (!fs::exists(directoryPath)) {
        return 0;
    }
    uint64_t byteCount = 0;
    for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(directoryPath)) {
        if (directoryEntry.is_regular_file()) {
            byteCount += static_cast<uint64_t>(directoryEntry.file_size());
        }
    }
    return byteCount;
}

static void copyRuntimeTree(const fs::path &sourcePath, const fs::path &destinationPath) {
    std::error_code copyError;
    fs::create_directories(destinationPath.parent_path());
    fs::copy(sourcePath, destinationPath, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("runtime resource をコピーできません: " + sourcePath.string());
    }
}

static void copyRuntimeFile(const fs::path &sourcePath, const fs::path &destinationPath) {
    std::error_code copyError;
    fs::create_directories(destinationPath.parent_path());
    fs::copy_file(sourcePath, destinationPath, fs::copy_options::overwrite_existing, copyError);
    if (copyError) {
        throw std::runtime_error("runtime file をコピーできません: " + sourcePath.string());
    }
    std::error_code permissionError;
    fs::permissions(destinationPath, fs::status(sourcePath).permissions(), fs::perm_options::replace, permissionError);
#if defined(_WIN32)
    permissionError.clear();
#endif
    if (permissionError) {
        throw std::runtime_error("runtime file 権限を設定できません: " + destinationPath.string());
    }
}

static void assembleRuntimeRoot(const fs::path &runtimeRoot, const std::vector<fs::path> &voicevoxZipPaths, const std::vector<fs::path> &onnxruntimeArchivePaths, std::ostream *extractionStream) {
    fs::path sourceRoot = getExecutableDirectory();
    fs::path sourceBinaryPath = sourceRoot / getPlatformExecutableFilename("litevox");
    fs::path sourceResourcesPath = sourceRoot / "resources";
    ensurePathExists(sourceBinaryPath, "litevox binary");
    ensurePathExists(sourceResourcesPath, "litevox resources");
    fs::create_directories(runtimeRoot);
    copyRuntimeFile(sourceBinaryPath, runtimeRoot / getPlatformExecutableFilename("litevox"));
    appendRuntimeExtractionRecord(extractionStream, "litevox", 1, fs::file_size(sourceBinaryPath), runtimeRoot / getPlatformExecutableFilename("litevox"));
    copyRuntimeTree(sourceResourcesPath, runtimeRoot / "resources");
    appendRuntimeExtractionRecord(extractionStream, "resources", countRegularFilesInDirectory(sourceResourcesPath), sumRegularFileBytesInDirectory(sourceResourcesPath), runtimeRoot / "resources");
    fs::create_directories(runtimeRoot / "core_libraries");
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    const std::vector<std::string> onnxruntimeEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxOnnxruntimeLibraryFileNames());
    for (const fs::path &zipPath : voicevoxZipPaths) {
        uint64_t byteCount = 0;
        std::string coreEntryMarker = findZipEntryMarker(zipPath, coreEntryMarkers);
        if (coreEntryMarker.empty() || !extractZipEntryByMarker(zipPath, coreEntryMarker, runtimeRoot / fs::path(coreEntryMarker).filename(), byteCount)) {
            throw std::runtime_error("voicevox_core library が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "voicevox_core", 1, byteCount, runtimeRoot / fs::path(coreEntryMarker).filename());
        std::string onnxruntimeEntryMarker = findZipEntryMarker(zipPath, onnxruntimeEntryMarkers);
        if (onnxruntimeEntryMarker.empty() || !extractZipEntryByMarker(zipPath, onnxruntimeEntryMarker, runtimeRoot / fs::path(onnxruntimeEntryMarker).filename(), byteCount)) {
            throw std::runtime_error("voicevox_onnxruntime library が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "voicevox_onnxruntime", 1, byteCount, runtimeRoot / fs::path(onnxruntimeEntryMarker).filename());
        if (!extractZipEntryByMarker(zipPath, "vv-engine/engine_manifest.json", runtimeRoot / "engine_manifest.json", byteCount)) {
            throw std::runtime_error("engine_manifest.json が見つかりません: " + zipPath.string());
        }
        appendRuntimeExtractionRecord(extractionStream, "engine_manifest", 1, byteCount, runtimeRoot / "engine_manifest.json");
        ZipDirectoryExtractionSummary modelSummary = extractZipDirectoryEntries(zipPath, "vv-engine/model/", runtimeRoot / "model-vvm");
        appendRuntimeExtractionRecord(extractionStream, "model_vvm", modelSummary.fileCount, modelSummary.byteCount, runtimeRoot / "model-vvm");
        ZipDirectoryExtractionSummary dictSummary = extractZipDirectoryEntries(zipPath, "open_jtalk_dic_utf_8-1.11/", runtimeRoot / "open_jtalk_dic_utf_8-1.11");
        appendRuntimeExtractionRecord(extractionStream, "open_jtalk_dict", dictSummary.fileCount, dictSummary.byteCount, runtimeRoot / "open_jtalk_dic_utf_8-1.11");
        ZipDirectoryExtractionSummary characterSummary = extractZipDirectoryEntries(zipPath, "resources/character_info/", runtimeRoot / "resources" / "character_info");
        appendRuntimeExtractionRecord(extractionStream, "character_info", characterSummary.fileCount, characterSummary.byteCount, runtimeRoot / "resources" / "character_info");
        ZipDirectoryExtractionSummary engineAssetSummary = extractZipDirectoryEntries(zipPath, "resources/engine_manifest_assets/", runtimeRoot / "resources" / "engine_manifest_assets");
        appendRuntimeExtractionRecord(extractionStream, "engine_manifest_assets", engineAssetSummary.fileCount, engineAssetSummary.byteCount, runtimeRoot / "resources" / "engine_manifest_assets");
        if (extractZipEntryByMarker(zipPath, "resources/default.csv", runtimeRoot / "resources" / "default.csv", byteCount)) {
            appendRuntimeExtractionRecord(extractionStream, "default_csv", 1, byteCount, runtimeRoot / "resources" / "default.csv");
        }
        if (extractZipEntryByMarker(zipPath, "resources/setting_ui_template.html", runtimeRoot / "resources" / "setting_ui_template.html", byteCount)) {
            appendRuntimeExtractionRecord(extractionStream, "setting_ui_template", 1, byteCount, runtimeRoot / "resources" / "setting_ui_template.html");
        }
    }
    for (const fs::path &archivePath : onnxruntimeArchivePaths) {
        ArchiveExtractionSummary extractionSummary = extractArchivePreservingPaths(archivePath, runtimeRoot);
        appendRuntimeExtractionRecord(extractionStream, "standard_onnxruntime", extractionSummary.fileCount, extractionSummary.byteCount, runtimeRoot);
    }
}

static uint64_t getPathFileSizeOrZero(const fs::path &filePath) {
    std::error_code fileError;
    uint64_t fileSize = fs::is_regular_file(filePath, fileError) ? static_cast<uint64_t>(fs::file_size(filePath, fileError)) : 0;
    if (fileError) {
        return 0;
    }
    return fileSize;
}

static int64_t getPathWriteTimeOrZero(const fs::path &filePath) {
    std::error_code timeError;
    fs::file_time_type writeTime = fs::last_write_time(filePath, timeError);
    if (timeError) {
        return 0;
    }
    return static_cast<int64_t>(writeTime.time_since_epoch().count());
}

static std::string createRuntimeCacheKey(const fs::path &runtimeArchivePath) {
    fs::path sourceRoot = getExecutableDirectory();
    fs::path sourceBinaryPath = sourceRoot / getPlatformExecutableFilename("litevox");
    fs::path sourceResourcesPath = sourceRoot / "resources";
    fs::path sourceOpenApiPath = sourceResourcesPath / "openapi.json";
    std::ostringstream cacheKeyStream;
    cacheKeyStream << fs::weakly_canonical(runtimeArchivePath).string() << "\n"
                   << getPathFileSizeOrZero(runtimeArchivePath) << "\n"
                   << getPathWriteTimeOrZero(runtimeArchivePath) << "\n"
                   << fs::weakly_canonical(sourceRoot).string() << "\n"
                   << getPathFileSizeOrZero(sourceBinaryPath) << "\n"
                   << getPathWriteTimeOrZero(sourceBinaryPath) << "\n"
                   << countRegularFilesInDirectory(sourceResourcesPath) << "\n"
                   << sumRegularFileBytesInDirectory(sourceResourcesPath) << "\n"
                   << getPathFileSizeOrZero(sourceOpenApiPath) << "\n"
                   << getPathWriteTimeOrZero(sourceOpenApiPath) << "\n";
    std::string cacheSeed = cacheKeyStream.str();
    return createSha256Hex(reinterpret_cast<const uint8_t *>(cacheSeed.data()), cacheSeed.size());
}

static bool isAssembledRuntimeRootReady(const fs::path &runtimeRoot) {
    return fs::exists(runtimeRoot / getPlatformExecutableFilename("litevox"))
        && !findFirstExistingPath(runtimeRoot, getVoicevoxCoreLibraryFileNames()).empty()
        && !findFirstExistingPath(runtimeRoot, getVoicevoxOnnxruntimeLibraryFileNames()).empty()
        && fs::exists(runtimeRoot / "engine_manifest.json")
        && fs::exists(runtimeRoot / "model-vvm")
        && fs::exists(runtimeRoot / "open_jtalk_dic_utf_8-1.11")
        && fs::exists(runtimeRoot / "resources");
}

static bool isVoicevoxRuntimeZipPath(const fs::path &archivePath) {
    const std::vector<std::string> coreEntryMarkers = createVoicevoxArchiveEntryMarkers(getVoicevoxCoreLibraryFileNames());
    return lowercaseAscii(archivePath.extension().string()) == ".zip"
        && zipArchiveHasAnyEntryMarker(archivePath, coreEntryMarkers);
}

fs::path resolveRuntimeRootPath(const fs::path &runtimeInputPath) {
    ensurePathExists(runtimeInputPath, "runtime パス");
    if (!fs::is_regular_file(runtimeInputPath) || !isVoicevoxRuntimeZipPath(runtimeInputPath)) {
        return runtimeInputPath;
    }
    fs::path parentPath = runtimeInputPath.parent_path();
    fs::path cacheBasePath = (parentPath.empty() ? fs::path(".") : parentPath) / ".litevox-runtime-cache";
    std::string cacheKey = createRuntimeCacheKey(runtimeInputPath);
    fs::path runtimeRoot = cacheBasePath / (runtimeInputPath.stem().string() + "-" + cacheKey.substr(0, 16));
    if (isAssembledRuntimeRootReady(runtimeRoot)) {
        return runtimeRoot;
    }
    std::error_code removeError;
    fs::remove_all(runtimeRoot, removeError);
    if (removeError) {
        throw std::runtime_error("runtime cache を初期化できません: " + runtimeRoot.string());
    }
    assembleRuntimeRoot(runtimeRoot, {runtimeInputPath}, {}, nullptr);
    return runtimeRoot;
}

std::string createRuntimeExtractionText(const CliOptions &cliOptions) {
    if (cliOptions.runtimeOutputDirectory.empty()) {
        throw std::runtime_error("--extract-runtime DIR が必要です");
    }
    std::ostringstream extractionStream;
    extractionStream << "component\tfiles\tbytes\tout\n";
    assembleRuntimeRoot(cliOptions.runtimeOutputDirectory, collectVoicevoxZipPaths(cliOptions), collectOnnxruntimeArchivePaths(cliOptions), &extractionStream);
    return extractionStream.str();
}

int runCacheCommand(const CliOptions &cliOptions) {
    std::vector<ModelAssetRecord> modelAssets = collectModelAssetsFromModelRoots(cliOptions.runtimePaths.modelPaths);
    ModelSessionCache modelSessionCache = loadModelSessionCache(modelAssets);
    std::cout << createModelSessionCacheSummary(modelSessionCache);
    return 0;
}
