#include "vv_bin_inspect.hpp"

#include "model_asset.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <limits>
#include <sstream>
#include <utility>

struct ByteStatistics {
    size_t byteCount = 0;
    double entropy = 0.0;
    double zeroRatio = 0.0;
    double printableRatio = 0.0;
};

struct MarkerScanRecord {
    bool hasMarker = false;
    std::string markerText;
    uint64_t markerOffset = 0;
};

static std::string createVvBinHexUint32(uint32_t numberValue) {
    std::ostringstream hexStream;
    hexStream << std::hex << std::setfill('0') << std::setw(8) << numberValue;
    return hexStream.str();
}

static std::string createVvBinPreviewHexText(const std::vector<uint8_t> &previewBytes) {
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

static ByteStatistics calculateByteStatistics(const std::vector<uint8_t> &sampleBytes) {
    ByteStatistics byteStatistics;
    byteStatistics.byteCount = sampleBytes.size();
    if (sampleBytes.empty()) {
        return byteStatistics;
    }
    std::array<size_t, 256> byteCounts{};
    size_t zeroCount = 0;
    size_t printableCount = 0;
    for (uint8_t sampleByte : sampleBytes) {
        byteCounts[sampleByte]++;
        if (sampleByte == 0) {
            zeroCount++;
        }
        if ((sampleByte >= 0x20 && sampleByte <= 0x7e) || sampleByte == '\n' || sampleByte == '\r' || sampleByte == '\t') {
            printableCount++;
        }
    }
    double entropy = 0.0;
    for (size_t byteCount : byteCounts) {
        if (byteCount == 0) {
            continue;
        }
        double probability = static_cast<double>(byteCount) / static_cast<double>(sampleBytes.size());
        entropy -= probability * std::log2(probability);
    }
    byteStatistics.entropy = entropy;
    byteStatistics.zeroRatio = static_cast<double>(zeroCount) / static_cast<double>(sampleBytes.size());
    byteStatistics.printableRatio = static_cast<double>(printableCount) / static_cast<double>(sampleBytes.size());
    return byteStatistics;
}

static MarkerScanRecord findAsciiMarker(const std::vector<uint8_t> &modelBytes) {
    const std::vector<std::string> markerTexts = {"onnx", "ir_version", "producer", "opset_import"};
    MarkerScanRecord markerScanRecord;
    markerScanRecord.markerOffset = std::numeric_limits<uint64_t>::max();
    for (const std::string &markerText : markerTexts) {
        if (modelBytes.size() < markerText.size()) {
            continue;
        }
        for (size_t byteIndex = 0; byteIndex + markerText.size() <= modelBytes.size(); byteIndex++) {
            bool isMatched = true;
            for (size_t markerIndex = 0; markerIndex < markerText.size(); markerIndex++) {
                unsigned char modelCharacter = modelBytes[byteIndex + markerIndex];
                unsigned char markerCharacter = static_cast<unsigned char>(markerText[markerIndex]);
                if (std::tolower(modelCharacter) != std::tolower(markerCharacter)) {
                    isMatched = false;
                    break;
                }
            }
            if (isMatched && (!markerScanRecord.hasMarker || byteIndex < markerScanRecord.markerOffset)) {
                markerScanRecord.hasMarker = true;
                markerScanRecord.markerText = markerText;
                markerScanRecord.markerOffset = byteIndex;
            }
        }
    }
    if (!markerScanRecord.hasMarker) {
        markerScanRecord.markerOffset = 0;
    }
    return markerScanRecord;
}

static std::string detectPrefixClass(const ByteStatistics &byteStatistics) {
    if (byteStatistics.zeroRatio > 0.8) {
        return "zero_heavy";
    }
    if (byteStatistics.printableRatio > 0.7) {
        return "text_like";
    }
    if (byteStatistics.entropy > 7.5) {
        return "high_entropy";
    }
    if (byteStatistics.entropy > 6.0) {
        return "mixed_binary";
    }
    return "structured_binary";
}

static std::string detectWholeAssetClass(const ByteStatistics &wholeAssetStatistics, const MarkerScanRecord &markerScanRecord) {
    if (wholeAssetStatistics.byteCount == 0) {
        return "not_scanned";
    }
    if (markerScanRecord.hasMarker) {
        return "contains_onnx_marker";
    }
    if (wholeAssetStatistics.zeroRatio > 0.8) {
        return "zero_heavy";
    }
    if (wholeAssetStatistics.printableRatio > 0.7) {
        return "text_like";
    }
    if (wholeAssetStatistics.entropy > 7.8) {
        return "opaque_high_entropy";
    }
    if (wholeAssetStatistics.entropy > 6.0) {
        return "opaque_mixed_entropy";
    }
    return "structured_binary";
}

static std::map<std::string, ManifestModelRecord> createVvBinManifestModelMap(const std::vector<VvmArchiveSummary> &archiveSummaries) {
    std::map<std::string, ManifestModelRecord> manifestModelMap;
    for (const VvmArchiveSummary &archiveSummary : archiveSummaries) {
        for (ManifestModelRecord manifestModel : collectManifestModels(archiveSummary)) {
            manifestModelMap[manifestModel.archivePath.string() + "\t" + manifestModel.entryName] = std::move(manifestModel);
        }
    }
    return manifestModelMap;
}

static const ManifestModelRecord *findVvBinManifestModel(const std::map<std::string, ManifestModelRecord> &manifestModelMap, const ModelAssetRecord &modelAsset) {
    auto manifestModelIterator = manifestModelMap.find(modelAsset.archivePath.string() + "\t" + modelAsset.entryName);
    if (manifestModelIterator == manifestModelMap.end()) {
        return nullptr;
    }
    return &manifestModelIterator->second;
}

std::string createVvBinInspectText(const std::vector<VvmArchiveSummary> &archiveSummaries, size_t sampleBytes, bool shouldScanWholeAsset) {
    std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
    std::map<std::string, ManifestModelRecord> manifestModelMap = createVvBinManifestModelMap(archiveSummaries);
    std::ostringstream inspectStream;
    inspectStream << "vvm\tasset\tdomain\toperation\tmodel_type\tbytes\tcrc32\tmethod\tsample_bytes\tentropy\tzero_ratio\tprintable_ratio\tprefix_class\tfull_bytes\tfull_entropy\tfull_zero_ratio\tfull_printable_ratio\tonnx_scan\tonnx_marker\tonnx_offset\tasset_class\tpreview_hex\n";
    inspectStream << std::fixed << std::setprecision(6);
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        const ManifestModelRecord *manifestModel = findVvBinManifestModel(manifestModelMap, modelAsset);
        if (!manifestModel || manifestModel->modelType != "vv_bin") {
            continue;
        }
        std::vector<uint8_t> prefixBytes = readVvmEntryPrefixBytesAt(
            modelAsset.archivePath,
            modelAsset.entryName,
            modelAsset.dataOffset,
            modelAsset.compressedSize,
            modelAsset.uncompressedSize,
            modelAsset.compressionMethod,
            sampleBytes);
        ByteStatistics prefixStatistics = calculateByteStatistics(prefixBytes);
        ByteStatistics wholeAssetStatistics;
        MarkerScanRecord markerScanRecord;
        std::string onnxScanText = "not_scanned";
        std::string onnxMarkerText;
        std::string onnxOffsetText;
        if (shouldScanWholeAsset) {
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            wholeAssetStatistics = calculateByteStatistics(modelBytes);
            markerScanRecord = findAsciiMarker(modelBytes);
            onnxScanText = markerScanRecord.hasMarker ? "marker_found" : "not_found";
            if (markerScanRecord.hasMarker) {
                onnxMarkerText = markerScanRecord.markerText;
                onnxOffsetText = std::to_string(markerScanRecord.markerOffset);
            }
        }
        inspectStream << modelAsset.archivePath.filename().string() << "\t"
                      << modelAsset.entryName << "\t"
                      << manifestModel->domainName << "\t"
                      << manifestModel->operationName << "\t"
                      << manifestModel->modelType << "\t"
                      << modelAsset.uncompressedSize << "\t"
                      << createVvBinHexUint32(modelAsset.crc32) << "\t"
                      << modelAsset.compressionMethod << "\t"
                      << prefixStatistics.byteCount << "\t"
                      << prefixStatistics.entropy << "\t"
                      << prefixStatistics.zeroRatio << "\t"
                      << prefixStatistics.printableRatio << "\t"
                      << detectPrefixClass(prefixStatistics) << "\t"
                      << wholeAssetStatistics.byteCount << "\t"
                      << wholeAssetStatistics.entropy << "\t"
                      << wholeAssetStatistics.zeroRatio << "\t"
                      << wholeAssetStatistics.printableRatio << "\t"
                      << onnxScanText << "\t"
                      << onnxMarkerText << "\t"
                      << onnxOffsetText << "\t"
                      << detectWholeAssetClass(wholeAssetStatistics, markerScanRecord) << "\t"
                      << createVvBinPreviewHexText(prefixBytes) << "\n";
    }
    return inspectStream.str();
}
