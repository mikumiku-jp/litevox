#include "runtime_trace.hpp"

#include "json_utility.hpp"
#include "model_asset.hpp"
#include "utility.hpp"
#include "vvm_archive.hpp"

#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static bool hasTraceStyleId(const VoiceModelRecord &voiceModel, uint32_t styleId) {
    for (uint32_t modelStyleId : voiceModel.styleIds) {
        if (modelStyleId == styleId) {
            return true;
        }
    }
    for (const StyleRecord &styleRecord : voiceModel.styles) {
        if (styleRecord.styleId == styleId) {
            return true;
        }
    }
    return false;
}

static const VoiceModelRecord &findTraceVoiceModel(const RuntimeState &runtimeState, uint32_t styleId) {
    auto styleIterator = runtimeState.styleToModelIndex.find(styleId);
    if (styleIterator != runtimeState.styleToModelIndex.end()) {
        return runtimeState.voiceModels.at(styleIterator->second);
    }
    for (const VoiceModelRecord &voiceModel : runtimeState.voiceModels) {
        if (hasTraceStyleId(voiceModel, styleId)) {
            return voiceModel;
        }
    }
    throw std::runtime_error("未対応の speaker/style ID です: " + std::to_string(styleId));
}

static std::string createTraceRequestJson(const RuntimeState &runtimeState, const RuntimeTraceOptions &traceOptions) {
    std::ostringstream jsonStream;
    jsonStream << "{";
    jsonStream << "\"style_id\":" << traceOptions.styleId << ",";
    jsonStream << "\"is_kana\":" << (traceOptions.isKana ? "true" : "false") << ",";
    jsonStream << "\"text\":" << quoteJsonString(traceOptions.text) << ",";
    jsonStream << "\"backend\":" << quoteJsonString(getCoreBackendMode(runtimeState.coreBackend)) << ",";
    jsonStream << "\"core_profile\":" << quoteJsonString(getCoreBackendProfile(runtimeState.coreBackend)) << ",";
    jsonStream << "\"acceleration_mode\":" << quoteJsonString(getCoreBackendAccelerationMode(runtimeState.coreBackend));
    jsonStream << "}";
    return jsonStream.str();
}

static std::string createTraceOperationJson(const VoiceModelRecord &voiceModel) {
    VvmArchiveSummary archiveSummary = inspectVvmArchive(voiceModel.modelPath);
    std::vector<ManifestModelRecord> manifestModels = collectManifestModels(archiveSummary);
    std::ostringstream jsonStream;
    jsonStream << "[";
    for (size_t modelIndex = 0; modelIndex < manifestModels.size(); modelIndex++) {
        const ManifestModelRecord &manifestModel = manifestModels[modelIndex];
        if (modelIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << "{";
        jsonStream << "\"domain\":" << quoteJsonString(manifestModel.domainName) << ",";
        jsonStream << "\"operation\":" << quoteJsonString(manifestModel.operationName) << ",";
        jsonStream << "\"asset\":" << quoteJsonString(manifestModel.entryName) << ",";
        jsonStream << "\"model_type\":" << quoteJsonString(manifestModel.modelType);
        jsonStream << "}";
    }
    jsonStream << "]";
    return jsonStream.str();
}

static std::string createTraceManifestJson(const RuntimeTraceOptions &traceOptions, const VoiceModelRecord &voiceModel) {
    std::ostringstream jsonStream;
    jsonStream << "{";
    jsonStream << "\"trace_version\":1,";
    jsonStream << "\"style_id\":" << traceOptions.styleId << ",";
    jsonStream << "\"vvm\":" << quoteJsonString(voiceModel.modelPath.filename().string()) << ",";
    jsonStream << "\"model_loaded\":" << (voiceModel.isLoaded ? "true" : "false") << ",";
    jsonStream << "\"files\":[";
    const std::vector<std::string> fileNames = {
        "request.json",
        "runtime_info.before.json",
        "models.tsv",
        "assets.tsv",
        "operations.json",
        "audio_query.json",
        "loaded_models.tsv",
        "runtime_info.after_query.json",
        "synthesis.wav",
        "runtime_info.after_synthesis.json"
    };
    for (size_t fileIndex = 0; fileIndex < fileNames.size(); fileIndex++) {
        if (fileIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << quoteJsonString(fileNames[fileIndex]);
    }
    jsonStream << "]}";
    return jsonStream.str();
}

std::filesystem::path writeRuntimeTrace(RuntimeState &runtimeState, const RuntimeTraceOptions &traceOptions) {
    if (traceOptions.outputDirectory.empty()) {
        throw std::runtime_error("trace には --out DIR が必要です");
    }
    if (traceOptions.text.empty()) {
        throw std::runtime_error("trace には --text が必要です");
    }
    fs::create_directories(traceOptions.outputDirectory);
    const VoiceModelRecord &voiceModel = findTraceVoiceModel(runtimeState, traceOptions.styleId);
    writeTextFile(traceOptions.outputDirectory / "request.json", createTraceRequestJson(runtimeState, traceOptions));
    writeTextFile(traceOptions.outputDirectory / "runtime_info.before.json", createRuntimeInfoJson(runtimeState));
    writeTextFile(traceOptions.outputDirectory / "models.tsv", createModelTable(runtimeState));
    writeTextFile(traceOptions.outputDirectory / "assets.tsv", createRuntimeModelAssetTable(runtimeState));
    writeTextFile(traceOptions.outputDirectory / "operations.json", createTraceOperationJson(voiceModel));
    fs::path tensorDirectory = traceOptions.outputDirectory / "tensors";
    fs::create_directories(tensorDirectory);
    std::string tensorDirectoryText = tensorDirectory.string();
    setEnvironmentVariable("LITEVOX_TENSOR_TRACE_DIR", tensorDirectoryText);
    std::string audioQueryJson = traceOptions.isKana
        ? createAudioQueryFromKana(runtimeState, traceOptions.text, traceOptions.styleId)
        : createAudioQuery(runtimeState, traceOptions.text, traceOptions.styleId);
    writeTextFile(traceOptions.outputDirectory / "audio_query.json", audioQueryJson);
    writeTextFile(traceOptions.outputDirectory / "loaded_models.tsv", createLoadedModelTable(runtimeState));
    writeTextFile(traceOptions.outputDirectory / "runtime_info.after_query.json", createRuntimeInfoJson(runtimeState));
    std::vector<uint8_t> wavBytes = synthesizeAudioQuery(runtimeState, audioQueryJson, traceOptions.styleId);
    writeBinaryFile(traceOptions.outputDirectory / "synthesis.wav", wavBytes);
    writeTextFile(traceOptions.outputDirectory / "runtime_info.after_synthesis.json", createRuntimeInfoJson(runtimeState));
    writeTextFile(traceOptions.outputDirectory / "trace_manifest.json", createTraceManifestJson(traceOptions, findTraceVoiceModel(runtimeState, traceOptions.styleId)));
    return traceOptions.outputDirectory / "trace_manifest.json";
}
