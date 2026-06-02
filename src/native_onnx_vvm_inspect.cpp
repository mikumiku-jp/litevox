#include "native_onnx_internal.hpp"

#include "dynamic_library.hpp"
#include "json_utility.hpp"
#include "model_asset.hpp"
#include "native_audio_query.hpp"
#include "streaming_audio.hpp"
#include "utility.hpp"

#include <atomic>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

static std::string findNativeOnnxFieldText(const std::string &tableText, const std::string &fieldName) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    const std::string fieldPrefix = fieldName + "\t";
    while (std::getline(inputStream, lineText)) {
        if (lineText.rfind(fieldPrefix, 0) == 0) {
            return lineText.substr(fieldPrefix.size());
        }
    }
    return "-";
}

void sanitizeNativeOnnxTableCell(std::string &cellText) {
    for (char &character : cellText) {
        if (character == '\t' || character == '\n' || character == '\r') {
            character = ' ';
        }
    }
}

static std::string summarizeNativeOnnxValueLines(const std::string &tableText) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    std::vector<std::string> valueTexts;
    bool isValueTable = false;
    while (std::getline(inputStream, lineText)) {
        if (lineText == "value\tname\ttype\tshape") {
            isValueTable = true;
            continue;
        }
        if (lineText.rfind("run_status\t", 0) == 0) {
            isValueTable = false;
        }
        if (!isValueTable) {
            continue;
        }
        bool isInputLine = lineText.rfind("input_", 0) == 0 && lineText.size() > 6 && std::isdigit(static_cast<unsigned char>(lineText[6]));
        bool isOutputLine = lineText.rfind("output_", 0) == 0 && lineText.size() > 7 && std::isdigit(static_cast<unsigned char>(lineText[7]));
        if (!isInputLine && !isOutputLine) {
            continue;
        }
        sanitizeNativeOnnxTableCell(lineText);
        valueTexts.push_back(lineText);
    }
    std::ostringstream summaryStream;
    for (size_t valueIndex = 0; valueIndex < valueTexts.size(); valueIndex++) {
        if (valueIndex > 0) {
            summaryStream << "; ";
        }
        summaryStream << valueTexts[valueIndex];
    }
    return summaryStream.str();
}

static std::string summarizeNativeOnnxTraceMatches(const std::string &tableText) {
    std::istringstream inputStream(tableText);
    std::string lineText;
    std::vector<std::string> matchTexts;
    bool isRunOutputTable = false;
    while (std::getline(inputStream, lineText)) {
        if (lineText == "run_output\tname\ttype\telements\tfirst\ttrace_match") {
            isRunOutputTable = true;
            continue;
        }
        if (!isRunOutputTable) {
            continue;
        }
        bool isOutputLine = lineText.rfind("output_", 0) == 0 && lineText.size() > 7 && std::isdigit(static_cast<unsigned char>(lineText[7]));
        if (!isOutputLine) {
            continue;
        }
        std::istringstream lineStream(lineText);
        std::string valueIdText;
        std::string nameText;
        std::string typeText;
        std::string elementsText;
        std::string firstText;
        std::string traceMatchText;
        std::getline(lineStream, valueIdText, '\t');
        std::getline(lineStream, nameText, '\t');
        std::getline(lineStream, typeText, '\t');
        std::getline(lineStream, elementsText, '\t');
        std::getline(lineStream, firstText, '\t');
        std::getline(lineStream, traceMatchText, '\t');
        if (!nameText.empty() && !traceMatchText.empty()) {
            matchTexts.push_back(nameText + "=" + traceMatchText);
        }
    }
    if (matchTexts.empty()) {
        return "-";
    }
    std::ostringstream summaryStream;
    for (size_t matchIndex = 0; matchIndex < matchTexts.size(); matchIndex++) {
        if (matchIndex > 0) {
            summaryStream << "; ";
        }
        summaryStream << matchTexts[matchIndex];
    }
    return summaryStream.str();
}


std::string createNativeOnnxVvmInspectText(const fs::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const fs::path &inputDirectory, uint16_t cpuThreadCount) {
    NativeOnnxApi nativeOnnxApi = loadNativeOnnxApi(onnxruntimeLibraryPath);
    try {
        std::vector<ModelAssetRecord> modelAssets = collectModelAssets(archiveSummaries);
        std::ostringstream inspectStream;
        inspectStream << "vvm\tasset\tbytes\tsession_status\tinput_count\toutput_count\trun_status\ttrace_match\tdetail\n";
        for (const ModelAssetRecord &modelAsset : modelAssets) {
            std::vector<uint8_t> modelBytes = extractVvmEntryBytesAt(
                modelAsset.archivePath,
                modelAsset.entryName,
                modelAsset.dataOffset,
                modelAsset.compressedSize,
                modelAsset.uncompressedSize,
                modelAsset.compressionMethod);
            std::ostringstream sessionStream;
            std::string sessionStatus = "created";
            std::string inputCountText = "-";
            std::string outputCountText = "-";
            std::string runStatusText = "-";
            std::string traceMatchText = "-";
            std::string detailText;
            try {
                appendNativeOnnxSessionInfoFromBytes(sessionStream, nativeOnnxApi, modelBytes, inputDirectory, cpuThreadCount, true, &modelAsset);
                std::string sessionText = sessionStream.str();
                inputCountText = findNativeOnnxFieldText(sessionText, "input_count");
                outputCountText = findNativeOnnxFieldText(sessionText, "output_count");
                runStatusText = findNativeOnnxFieldText(sessionText, "run_status");
                traceMatchText = summarizeNativeOnnxTraceMatches(sessionText);
                detailText = summarizeNativeOnnxValueLines(sessionText);
                if (detailText.empty()) {
                    detailText = "ok";
                }
            } catch (const std::exception &caughtException) {
                sessionStatus = "failed";
                detailText = caughtException.what();
            }
            sanitizeNativeOnnxTableCell(detailText);
            inspectStream << modelAsset.archivePath.filename().string() << "\t"
                          << modelAsset.entryName << "\t"
                          << modelAsset.uncompressedSize << "\t"
                          << sessionStatus << "\t"
                          << inputCountText << "\t"
                          << outputCountText << "\t"
                          << runStatusText << "\t"
                          << traceMatchText << "\t"
                          << detailText << "\n";
        }
        closeNativeOnnxApi(nativeOnnxApi);
        return inspectStream.str();
    } catch (...) {
        closeNativeOnnxApi(nativeOnnxApi);
        throw;
    }
}

const ModelAssetRecord &requireNativeOnnxModelAsset(const std::vector<ModelAssetRecord> &modelAssets, const std::string &entryName) {
    for (const ModelAssetRecord &modelAsset : modelAssets) {
        if (modelAsset.entryName == entryName) {
            return modelAsset;
        }
    }
    throw std::runtime_error("model asset がありません: " + entryName);
}

