#include "model_asset.hpp"

#include "json_utility.hpp"
#include "model_metadata.hpp"
#include "utility.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

static bool hasPrefixText(const std::string &text, const std::string &prefixText) {
    return text.size() >= prefixText.size() && text.compare(0, prefixText.size(), prefixText) == 0;
}

static bool hasSuffixText(const std::string &text, const std::string &suffixText) {
    return text.size() >= suffixText.size() && text.compare(text.size() - suffixText.size(), suffixText.size(), suffixText) == 0;
}

static std::string createAssetHexUint32(uint32_t numberValue) {
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0') << std::setw(8) << numberValue;
    return hexStream.str();
}

static std::string createPreviewHexText(const std::vector<uint8_t> &previewBytes) {
    std::ostringstream previewStream;
    previewStream << std::hex << std::setfill('0');
    for (size_t byteIndex = 0; byteIndex < previewBytes.size(); byteIndex++) {
        if (byteIndex > 0) {
            previewStream << " ";
        }
        previewStream << std::setw(2) << static_cast<unsigned int>(previewBytes[byteIndex]);
    }
    return previewStream.str();
}

static bool containsAsciiMarker(const std::vector<uint8_t> &previewBytes, const std::string &markerText) {
    if (markerText.empty() || previewBytes.size() < markerText.size()) {
        return false;
    }
    for (size_t byteIndex = 0; byteIndex + markerText.size() <= previewBytes.size(); byteIndex++) {
        bool isMatch = true;
        for (size_t markerIndex = 0; markerIndex < markerText.size(); markerIndex++) {
            unsigned char previewCharacter = previewBytes[byteIndex + markerIndex];
            unsigned char markerCharacter = static_cast<unsigned char>(markerText[markerIndex]);
            if (std::tolower(previewCharacter) != std::tolower(markerCharacter)) {
                isMatch = false;
                break;
            }
        }
        if (isMatch) {
            return true;
        }
    }
    return false;
}

static std::string detectOnnxHint(const std::vector<uint8_t> &previewBytes) {
    if (containsAsciiMarker(previewBytes, "onnx") || containsAsciiMarker(previewBytes, "ir_version") || containsAsciiMarker(previewBytes, "producer")) {
        return "marker_in_prefix";
    }
    return "not_in_prefix";
}

static std::pair<std::string, uint64_t> findFirstOnnxMarker(const std::vector<uint8_t> &modelBytes) {
    const std::vector<std::string> markerTexts = {"onnx", "ir_version", "producer", "opset_import"};
    uint64_t bestOffset = 0;
    std::string bestMarker;
    bool hasMarker = false;
    for (const std::string &markerText : markerTexts) {
        if (markerText.empty() || modelBytes.size() < markerText.size()) {
            continue;
        }
        for (size_t byteIndex = 0; byteIndex + markerText.size() <= modelBytes.size(); byteIndex++) {
            bool isMatch = true;
            for (size_t markerIndex = 0; markerIndex < markerText.size(); markerIndex++) {
                unsigned char modelCharacter = modelBytes[byteIndex + markerIndex];
                unsigned char markerCharacter = static_cast<unsigned char>(markerText[markerIndex]);
                if (std::tolower(modelCharacter) != std::tolower(markerCharacter)) {
                    isMatch = false;
                    break;
                }
            }
            if (isMatch && (!hasMarker || byteIndex < bestOffset)) {
                bestOffset = byteIndex;
                bestMarker = markerText;
                hasMarker = true;
            }
        }
    }
    if (!hasMarker) {
        return {"", 0};
    }
    return {bestMarker, bestOffset};
}

static std::string quoteAssetJsonString(const std::string &text) {
    std::string quotedText = "\"";
    for (char character : text) {
        if (character == '\\' || character == '"') {
            quotedText.push_back('\\');
        }
        quotedText.push_back(character);
    }
    quotedText.push_back('"');
    return quotedText;
}

static void appendManifestModelRecord(std::vector<ManifestModelRecord> &manifestModels, const VvmArchiveSummary &archiveSummary, const std::string &domainName, const std::string &operationName) {
    std::string domainObject = extractJsonObjectField(archiveSummary.manifestJson, domainName);
    if (domainObject.empty()) {
        return;
    }
    std::string operationObject = extractJsonObjectField(domainObject, operationName);
    if (operationObject.empty()) {
        return;
    }
    std::string filename = extractJsonStringField(operationObject, "filename");
    if (filename.empty()) {
        return;
    }
    ManifestModelRecord manifestModel;
    manifestModel.archivePath = archiveSummary.archivePath;
    manifestModel.entryName = filename;
    manifestModel.domainName = domainName;
    manifestModel.operationName = operationName;
    manifestModel.modelType = extractJsonStringField(operationObject, "type");
    manifestModels.push_back(std::move(manifestModel));
}

std::vector<ManifestModelRecord> collectManifestModels(const VvmArchiveSummary &archiveSummary) {
    std::vector<ManifestModelRecord> manifestModels;
    appendManifestModelRecord(manifestModels, archiveSummary, "talk", "predict_duration");
    appendManifestModelRecord(manifestModels, archiveSummary, "talk", "predict_intonation");
    appendManifestModelRecord(manifestModels, archiveSummary, "talk", "decode");
    appendManifestModelRecord(manifestModels, archiveSummary, "experimental_talk", "predict_duration");
    appendManifestModelRecord(manifestModels, archiveSummary, "experimental_talk", "predict_intonation");
    appendManifestModelRecord(manifestModels, archiveSummary, "experimental_talk", "generate_full_intermediate");
    appendManifestModelRecord(manifestModels, archiveSummary, "experimental_talk", "render_audio_segment");
    appendManifestModelRecord(manifestModels, archiveSummary, "singing_teacher", "predict_sing_consonant_length");
    appendManifestModelRecord(manifestModels, archiveSummary, "singing_teacher", "predict_sing_f0");
    appendManifestModelRecord(manifestModels, archiveSummary, "singing_teacher", "predict_sing_volume");
    appendManifestModelRecord(manifestModels, archiveSummary, "frame_decode", "sf_decode");
    return manifestModels;
}

static std::map<std::string, ManifestModelRecord> createManifestModelMap(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::map<std::string, ManifestModelRecord> manifestModelMap;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (ManifestModelRecord &manifestModel : collectManifestModels(archiveSummary)) {
            std::string manifestKey = manifestModel.archivePath.string() + "\t" + manifestModel.entryName;
            manifestModelMap[manifestKey] = std::move(manifestModel);
        }
    }
    return manifestModelMap;
}

static const ManifestModelRecord *findManifestModel(const std::map<std::string, ManifestModelRecord> &manifestModelMap, const ModelAssetRecord &modelAsset) {
    std::string manifestKey = modelAsset.archivePath.string() + "\t" + modelAsset.entryName;
    auto manifestModelIterator = manifestModelMap.find(manifestKey);
    if (manifestModelIterator == manifestModelMap.end()) {
        return nullptr;
    }
    return &manifestModelIterator->second;
}

static bool isKnownOnnxModel(const ManifestModelRecord &manifestModel) {
    return manifestModel.modelType == "onnx" || hasSuffixText(manifestModel.entryName, ".onnx");
}

static std::string createOnnxScanText(const std::vector<uint8_t> &modelBytes) {
    std::pair<std::string, uint64_t> firstMarker = findFirstOnnxMarker(modelBytes);
    if (firstMarker.first.empty()) {
        return "not_found\t\t";
    }
    std::ostringstream markerStream;
    markerStream << "marker_found\t" << firstMarker.first << "\t" << firstMarker.second;
    return markerStream.str();
}

static bool parseNumericModelStem(const fs::path &modelPath, uint64_t &modelNumber) {
    std::string stemText = modelPath.stem().string();
    if (stemText.empty()) {
        return false;
    }
    uint64_t parsedNumber = 0;
    for (char character : stemText) {
        if (!std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }
        parsedNumber = parsedNumber * 10 + static_cast<uint64_t>(character - '0');
    }
    modelNumber = parsedNumber;
    return true;
}

static bool compareModelAsset(const ModelAssetRecord &leftAsset, const ModelAssetRecord &rightAsset) {
    uint64_t leftNumber = 0;
    uint64_t rightNumber = 0;
    bool leftHasNumber = parseNumericModelStem(leftAsset.archivePath, leftNumber);
    bool rightHasNumber = parseNumericModelStem(rightAsset.archivePath, rightNumber);
    if (leftHasNumber && rightHasNumber && leftNumber != rightNumber) {
        return leftNumber < rightNumber;
    }
    if (leftHasNumber != rightHasNumber) {
        return leftHasNumber;
    }
    if (leftAsset.archivePath == rightAsset.archivePath) {
        return leftAsset.entryName < rightAsset.entryName;
    }
    return leftAsset.archivePath < rightAsset.archivePath;
}

std::vector<ModelAssetRecord> collectModelAssets(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::vector<ModelAssetRecord> modelAssets;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (const VvmEntryRecord &archiveEntry : archiveSummary.entries) {
            if (!hasPrefixText(archiveEntry.name, "models/") || !hasSuffixText(archiveEntry.name, ".bin")) {
                continue;
            }
            ModelAssetRecord modelAsset;
            modelAsset.archivePath = archiveSummary.archivePath;
            modelAsset.entryName = archiveEntry.name;
            modelAsset.dataOffset = archiveEntry.dataOffset;
            modelAsset.compressedSize = archiveEntry.compressedSize;
            modelAsset.uncompressedSize = archiveEntry.uncompressedSize;
            modelAsset.crc32 = archiveEntry.crc32;
            modelAsset.compressionMethod = archiveEntry.compressionMethod;
            modelAssets.push_back(std::move(modelAsset));
        }
    }
    std::sort(modelAssets.begin(), modelAssets.end(), compareModelAsset);
    return modelAssets;
}

std::vector<ModelAssetRecord> collectModelAssetsFromModelRoots(const std::vector<fs::path> &modelRoots) {
    std::vector<VvmArchiveSummary> archiveSummaries;
    for (const fs::path &modelPath : collectVvmModelFiles(modelRoots)) {
        archiveSummaries.push_back(inspectVvmArchive(modelPath));
    }
    return collectModelAssets(archiveSummaries);
}

std::string createModelAssetTable(const std::vector<ModelAssetRecord> &modelAssets) {
    std::ostringstream tableStream;
    tableStream << "vvm\tasset\toffset\tcompressed_bytes\tuncompressed_bytes\tcrc32\tmethod\n";
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        tableStream << modelAsset.archivePath.filename().string() << "\t"
                    << modelAsset.entryName << "\t"
                    << modelAsset.dataOffset << "\t"
                    << modelAsset.compressedSize << "\t"
                    << modelAsset.uncompressedSize << "\t"
                    << createAssetHexUint32(modelAsset.crc32) << "\t"
                    << modelAsset.compressionMethod << "\n";
    }
    return tableStream.str();
}

std::string createModelAssetJson(const std::vector<ModelAssetRecord> &modelAssets) {
    std::ostringstream jsonStream;
    jsonStream << "{\"assets\":[";
    for (size_t assetIndex = 0; assetIndex < modelAssets.size(); assetIndex++) {
        const ModelAssetRecord &modelAsset = modelAssets[assetIndex];
        if (assetIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << "{\"vvm_path\":" << quoteAssetJsonString(modelAsset.archivePath.string()) << ",";
        jsonStream << "\"entry\":" << quoteAssetJsonString(modelAsset.entryName) << ",";
        jsonStream << "\"data_offset\":" << modelAsset.dataOffset << ",";
        jsonStream << "\"compressed_bytes\":" << modelAsset.compressedSize << ",";
        jsonStream << "\"uncompressed_bytes\":" << modelAsset.uncompressedSize << ",";
        jsonStream << "\"crc32\":" << modelAsset.crc32 << ",";
        jsonStream << "\"crc32_hex\":" << quoteAssetJsonString(createAssetHexUint32(modelAsset.crc32)) << ",";
        jsonStream << "\"method\":" << modelAsset.compressionMethod << "}";
    }
    jsonStream << "]}";
    return jsonStream.str();
}

std::string createModelDumpText(const std::vector<VvmArchiveSummary> &archiveSummaries, size_t prefixBytes, bool shouldScanModels) {
    std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
    std::map<std::string, ManifestModelRecord> manifestModelMap = createManifestModelMap(archiveSummaries);
    std::ostringstream dumpStream;
    dumpStream << "section\tvvm\tvvm_path\tentries\tmanifest_models\tmodel_bins\tstyles\tmanifest_bytes\tmetas_bytes\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        std::vector<StyleRecord> styleRecords = extractStylesFromMetasJson(archiveSummary.metasJson);
        std::vector<ManifestModelRecord> manifestModels = collectManifestModels(archiveSummary);
        dumpStream << "archive\t"
                   << archiveSummary.archivePath.filename().string() << "\t"
                   << archiveSummary.archivePath.string() << "\t"
                   << archiveSummary.entries.size() << "\t"
                   << manifestModels.size() << "\t"
                   << archiveSummary.modelBinNames.size() << "\t"
                   << styleRecords.size() << "\t"
                   << archiveSummary.manifestJson.size() << "\t"
                   << archiveSummary.metasJson.size() << "\n";
    }
    dumpStream << "section\tvvm\tvvm_path\tasset\tdomain\toperation\tmodel_type\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (const ManifestModelRecord &manifestModel : collectManifestModels(archiveSummary)) {
            dumpStream << "manifest_model\t"
                       << archiveSummary.archivePath.filename().string() << "\t"
                       << archiveSummary.archivePath.string() << "\t"
                       << manifestModel.entryName << "\t"
                       << manifestModel.domainName << "\t"
                       << manifestModel.operationName << "\t"
                       << manifestModel.modelType << "\n";
        }
    }
    dumpStream << "section\tvvm\tvvm_path\tasset\tdomain\toperation\tmodel_type\toffset\tcompressed_bytes\tuncompressed_bytes\tcrc32\tmethod\tonnx_hint\tfull_scan\tmarker\tonnx_offset\tpreview_hex\n";
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        const ManifestModelRecord *manifestModel = findManifestModel(manifestModelMap, modelAsset);
        std::vector<uint8_t> previewBytes = readVvmEntryPrefixBytesAt(
            modelAsset.archivePath,
            modelAsset.entryName,
            modelAsset.dataOffset,
            modelAsset.compressedSize,
            modelAsset.uncompressedSize,
            modelAsset.compressionMethod,
            prefixBytes);
        std::string fullScanStatus = "not_scanned\t\t";
        if (shouldScanModels) {
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            fullScanStatus = createOnnxScanText(modelBytes);
        }
        dumpStream << "asset\t"
                   << modelAsset.archivePath.filename().string() << "\t"
                   << modelAsset.archivePath.string() << "\t"
                   << modelAsset.entryName << "\t"
                   << (manifestModel ? manifestModel->domainName : "") << "\t"
                   << (manifestModel ? manifestModel->operationName : "") << "\t"
                   << (manifestModel ? manifestModel->modelType : "") << "\t"
                   << modelAsset.dataOffset << "\t"
                   << modelAsset.compressedSize << "\t"
                   << modelAsset.uncompressedSize << "\t"
                   << createAssetHexUint32(modelAsset.crc32) << "\t"
                   << modelAsset.compressionMethod << "\t"
                   << detectOnnxHint(previewBytes) << "\t"
                   << fullScanStatus << "\t"
                   << createPreviewHexText(previewBytes) << "\n";
    }
    dumpStream << "section\tvvm\tvvm_path\tstyle_id\ttype\tspeaker_uuid\tspeaker\tstyle\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (const StyleRecord &styleRecord : extractStylesFromMetasJson(archiveSummary.metasJson)) {
            dumpStream << "style\t"
                       << archiveSummary.archivePath.filename().string() << "\t"
                       << archiveSummary.archivePath.string() << "\t"
                       << styleRecord.styleId << "\t"
                       << styleRecord.styleType << "\t"
                       << styleRecord.speakerUuid << "\t"
                       << styleRecord.speakerName << "\t"
                       << styleRecord.styleName << "\n";
        }
    }
    return dumpStream.str();
}

std::string exportOnnxModelFiles(const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &outputDirectory) {
    std::ostringstream exportStream;
    exportStream << "vvm\tasset\tdomain\toperation\tmodel_type\tstatus\toutput\n";
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (const ManifestModelRecord &manifestModel : collectManifestModels(archiveSummary)) {
            if (!isKnownOnnxModel(manifestModel)) {
                exportStream << archiveSummary.archivePath.filename().string() << "\t"
                             << manifestModel.entryName << "\t"
                             << manifestModel.domainName << "\t"
                             << manifestModel.operationName << "\t"
                             << manifestModel.modelType << "\t"
                             << "skipped\t\n";
                continue;
            }
            fs::path modelOutputDirectory = outputDirectory / archiveSummary.archivePath.stem();
            fs::path outputPath = modelOutputDirectory / fs::path(manifestModel.entryName).filename();
            writeBinaryFile(outputPath, extractVvmEntryBytes(archiveSummary.archivePath, manifestModel.entryName));
            exportStream << archiveSummary.archivePath.filename().string() << "\t"
                         << manifestModel.entryName << "\t"
                         << manifestModel.domainName << "\t"
                         << manifestModel.operationName << "\t"
                         << manifestModel.modelType << "\t"
                         << "exported\t"
                         << outputPath.string() << "\n";
        }
    }
    return exportStream.str();
}
