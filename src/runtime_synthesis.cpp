#include "runtime.hpp"

#include "runtime_internal.hpp"
#include "native_audio_query.hpp"
#include "native_audio_query_validation.hpp"
#include "native_text_query.hpp"
#include "preset_store.hpp"
#include "streaming_audio.hpp"

#include <stdexcept>
#include <string>
#include <vector>

std::string analyzeText(RuntimeState &runtimeState, const std::string &text) {
    reloadUserDict(runtimeState);
    if (isNativeRuntimeBackend(runtimeState)) {
        return createNativeAccentPhrasesJsonFromText(runtimeState.dictionaryDirectory, runtimeState.coreBackend.userDictPath, text);
    }
    return analyzeCoreBackendText(runtimeState.coreBackend, text);
}

std::string createAudioQuery(RuntimeState &runtimeState, const std::string &text, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeAudioQueryMoraData(runtimeState, createNativeAudioQueryFromText(runtimeState.dictionaryDirectory, runtimeState.coreBackend.userDictPath, text), styleId);
    }
    return createCoreBackendAudioQuery(runtimeState.coreBackend, text, styleId);
}

std::string createAudioQueryFromKana(RuntimeState &runtimeState, const std::string &kana, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeAudioQueryMoraData(runtimeState, createNativeAudioQueryFromKana(kana), styleId);
    }
    return createCoreBackendAudioQueryFromKana(runtimeState.coreBackend, kana, styleId);
}

void validateKana(RuntimeState &runtimeState, const std::string &kana) {
    if (isNativeRuntimeBackend(runtimeState)) {
        createNativeAccentPhrasesJsonFromKana(kana);
        return;
    }
    for (const VoiceModelRecord &modelRecord : runtimeState.voiceModels) {
        for (const StyleRecord &styleRecord : modelRecord.styles) {
            if (styleRecord.styleType == "talk") {
                createCoreBackendAccentPhrasesFromKana(runtimeState.coreBackend, kana, styleRecord.styleId);
                return;
            }
        }
    }
    throw std::runtime_error("talk style がありません");
}

std::string createAudioQueryFromPreset(RuntimeState &runtimeState, const std::string &text, int32_t presetId) {
    std::string presetJson = findPresetJson(runtimeState.presetPath, presetId);
    uint32_t styleId = getPresetStyleId(presetJson);
    return applyPresetToAudioQueryJson(createAudioQuery(runtimeState, text, styleId), presetJson);
}

std::string createAudioQueryFromAccentPhrases(RuntimeState &runtimeState, const std::string &accentPhrasesJson) {
    if (isNativeRuntimeBackend(runtimeState)) {
        return createNativeAudioQueryFromAccentPhrasesJson(accentPhrasesJson);
    }
    return createCoreBackendAudioQueryFromAccentPhrases(runtimeState.coreBackend, accentPhrasesJson);
}

void validateAudioQuery(RuntimeState &runtimeState, const std::string &audioQueryJson) {
    if (isNativeRuntimeBackend(runtimeState)) {
        validateNativeAudioQuery(audioQueryJson);
        return;
    }
    validateCoreBackendAudioQuery(runtimeState.coreBackend, audioQueryJson);
}

void validateFrameAudioQuery(RuntimeState &runtimeState, const std::string &frameAudioQueryJson) {
    if (isNativeRuntimeBackend(runtimeState)) {
        validateNativeOnnxFrameAudioQuery(frameAudioQueryJson);
        return;
    }
    validateCoreBackendFrameAudioQuery(runtimeState.coreBackend, frameAudioQueryJson);
}

std::string createAccentPhrases(RuntimeState &runtimeState, const std::string &text, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeMoraData(runtimeState, createNativeAccentPhrasesJsonFromText(runtimeState.dictionaryDirectory, runtimeState.coreBackend.userDictPath, text), styleId);
    }
    return createCoreBackendAccentPhrases(runtimeState.coreBackend, text, styleId);
}

std::string createAccentPhrasesFromKana(RuntimeState &runtimeState, const std::string &kana, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeMoraData(runtimeState, createNativeAccentPhrasesJsonFromKana(kana), styleId);
    }
    return createCoreBackendAccentPhrasesFromKana(runtimeState.coreBackend, kana, styleId);
}

std::string replaceMoraData(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeMoraData(runtimeState, accentPhrasesJson, styleId);
    }
    return replaceCoreBackendMoraData(runtimeState.coreBackend, accentPhrasesJson, styleId);
}

std::string replacePhonemeLength(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimePhonemeLength(runtimeState, accentPhrasesJson, styleId);
    }
    return replaceCoreBackendPhonemeLength(runtimeState.coreBackend, accentPhrasesJson, styleId);
}

std::string replaceMoraPitch(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return replaceNativeRuntimeMoraPitch(runtimeState, accentPhrasesJson, styleId);
    }
    return replaceCoreBackendMoraPitch(runtimeState.coreBackend, accentPhrasesJson, styleId);
}

std::vector<uint8_t> synthesizeAudioQuery(RuntimeState &runtimeState, const std::string &audioQueryJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        const VoiceModelRecord &modelRecord = getRuntimeVoiceModelForStyle(runtimeState, styleId);
        return synthesizeNativeOnnxModelAssetsAudioQuery(runtimeState.coreBackend.nativeOnnxRuntime, modelRecord.modelAssets, audioQueryJson, styleId, getCoreBackendCpuNumThreads(runtimeState.coreBackend), shouldUseNativeRuntimeVvBinConfig(runtimeState));
    }
    return synthesizeCoreBackendAudioQuery(runtimeState.coreBackend, audioQueryJson, styleId);
}

std::vector<uint8_t> synthesizeText(RuntimeState &runtimeState, const std::string &text, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return synthesizeAudioQuery(runtimeState, createNativeAudioQueryFromText(runtimeState.dictionaryDirectory, runtimeState.coreBackend.userDictPath, text), styleId);
    }
    return synthesizeCoreBackendText(runtimeState.coreBackend, text, styleId);
}

std::vector<uint8_t> synthesizeKana(RuntimeState &runtimeState, const std::string &kana, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        return synthesizeAudioQuery(runtimeState, createNativeAudioQueryFromKana(kana), styleId);
    }
    return synthesizeCoreBackendKana(runtimeState.coreBackend, kana, styleId);
}

std::string createSingFrameAudioQuery(RuntimeState &runtimeState, const std::string &scoreJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        const VoiceModelRecord &modelRecord = getRuntimeVoiceModelForStyle(runtimeState, styleId);
        return createNativeOnnxModelAssetsSingFrameAudioQuery(runtimeState.coreBackend.nativeOnnxRuntime, modelRecord.modelAssets, scoreJson, styleId, getCoreBackendCpuNumThreads(runtimeState.coreBackend), shouldUseNativeRuntimeVvBinConfig(runtimeState));
    }
    return createCoreBackendSingFrameAudioQuery(runtimeState.coreBackend, scoreJson, styleId);
}

std::string createSingFrameF0(RuntimeState &runtimeState, const std::string &scoreJson, const std::string &frameAudioQueryJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        const VoiceModelRecord &modelRecord = getRuntimeVoiceModelForStyle(runtimeState, styleId);
        return createNativeOnnxModelAssetsSingFrameF0(runtimeState.coreBackend.nativeOnnxRuntime, modelRecord.modelAssets, scoreJson, frameAudioQueryJson, styleId, getCoreBackendCpuNumThreads(runtimeState.coreBackend), shouldUseNativeRuntimeVvBinConfig(runtimeState));
    }
    return createCoreBackendSingFrameF0(runtimeState.coreBackend, scoreJson, frameAudioQueryJson, styleId);
}

std::string createSingFrameVolume(RuntimeState &runtimeState, const std::string &scoreJson, const std::string &frameAudioQueryJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        const VoiceModelRecord &modelRecord = getRuntimeVoiceModelForStyle(runtimeState, styleId);
        return createNativeOnnxModelAssetsSingFrameVolume(runtimeState.coreBackend.nativeOnnxRuntime, modelRecord.modelAssets, scoreJson, frameAudioQueryJson, styleId, getCoreBackendCpuNumThreads(runtimeState.coreBackend), shouldUseNativeRuntimeVvBinConfig(runtimeState));
    }
    return createCoreBackendSingFrameVolume(runtimeState.coreBackend, scoreJson, frameAudioQueryJson, styleId);
}

std::vector<uint8_t> synthesizeFrameAudioQuery(RuntimeState &runtimeState, const std::string &frameAudioQueryJson, uint32_t styleId) {
    ensureStyleLoaded(runtimeState, styleId);
    if (isNativeRuntimeBackend(runtimeState)) {
        const VoiceModelRecord &modelRecord = getRuntimeVoiceModelForStyle(runtimeState, styleId);
        return synthesizeNativeOnnxModelAssetsFrameAudioQuery(runtimeState.coreBackend.nativeOnnxRuntime, modelRecord.modelAssets, frameAudioQueryJson, styleId, getCoreBackendCpuNumThreads(runtimeState.coreBackend), shouldUseNativeRuntimeVvBinConfig(runtimeState));
    }
    return synthesizeCoreBackendFrameAudioQuery(runtimeState.coreBackend, frameAudioQueryJson, styleId);
}

std::vector<uint8_t> synthesizeMorphingAudioQuery(RuntimeState &runtimeState, const std::string &audioQueryJson, uint32_t baseStyleId, uint32_t targetStyleId, double morphRate) {
    std::vector<uint8_t> baseWavBytes = synthesizeAudioQuery(runtimeState, audioQueryJson, baseStyleId);
    std::vector<uint8_t> targetWavBytes = synthesizeAudioQuery(runtimeState, audioQueryJson, targetStyleId);
    return mixPcmWaveBytes(baseWavBytes, targetWavBytes, morphRate);
}
