#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "model_asset.hpp"
#include "vvm_archive.hpp"

struct NativeOnnxRuntimeState {
    void *libraryHandle = nullptr;
    std::filesystem::path libraryPath;
    std::string version;
    std::vector<std::string> availableProviders;
    std::string requestedAccelerationMode = "auto";
    std::string selectedExecutionProvider = "CPUExecutionProvider";
    uint32_t apiVersion = 0;
    bool isLoaded = false;
    bool hasUsableGpuProvider = false;
    bool isGpuExecutionProviderSelected = false;
};

struct NativeOnnxPcmStreamInfo {
    uint32_t sampleRate = 24000;
    uint16_t channels = 1;
    uint16_t bitsPerSample = 16;
    uintptr_t pcmBytes = 0;
};

NativeOnnxRuntimeState createNativeOnnxRuntimeState(const std::filesystem::path &onnxruntimeLibraryPath, const std::string &requestedAccelerationMode = "auto");
void destroyNativeOnnxRuntimeState(NativeOnnxRuntimeState &runtimeState);
void clearNativeOnnxCaches();
std::string createNativeOnnxSessionCacheInfoJson();
std::string createNativeOnnxExportedModelCacheInfoJson();
std::string createNativeOnnxDeterministicSingTeacherInfoJson(bool shouldUseVvBinConfig = true);
std::string createNativeOnnxProviderInfoJson(const NativeOnnxRuntimeState &runtimeState);
std::string createNativeOnnxSupportedDevicesJson(const NativeOnnxRuntimeState &runtimeState);
bool hasNativeOnnxGpuProvider(const NativeOnnxRuntimeState &runtimeState);
std::string createNativeOnnxInspectText(const std::filesystem::path &onnxruntimeLibraryPath, const std::filesystem::path &modelPath, const std::filesystem::path &inputDirectory, uint16_t cpuThreadCount);
std::string patchNativeOnnxRandomSeed(const std::filesystem::path &modelPath, const std::filesystem::path &outputPath, float seedValue);
std::string createNativeOnnxVvmInspectText(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &inputDirectory, uint16_t cpuThreadCount);
std::string createNativeOnnxVvmModelInfoText(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, uint16_t cpuThreadCount);
std::string createNativeOnnxVvmOperatorText(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, uint16_t cpuThreadCount);
std::string exportNativeOnnxVvmOnnxFiles(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &outputDirectory, uint16_t cpuThreadCount, bool shouldPatchRandomSeed, float randomSeedValue);
std::string createNativeOnnxVvmCompareText(const std::filesystem::path &vvBinOnnxruntimeLibraryPath, const std::filesystem::path &exportedOnnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &inputDirectory, const std::filesystem::path &audioQueryPath, uint32_t styleId, const std::string &assetFilter, uint16_t cpuThreadCount, size_t runCount);
std::string createNativeOnnxVvmChainText(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &inputDirectory, const std::filesystem::path &audioQueryPath, uint32_t styleId, uint16_t cpuThreadCount);
std::string replaceNativeOnnxModelAssetsMoraData(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsPhonemeLength(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsMoraPitch(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsAudioQueryMoraData(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::vector<uint8_t> synthesizeNativeOnnxModelAssetsAudioQuery(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
void streamNativeOnnxModelAssetsAudioQueryPcm(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, size_t chunkFrames, const std::function<void(const NativeOnnxPcmStreamInfo &)> &startStream, const std::function<void(const uint8_t *, size_t)> &writeChunk, bool shouldUseVvBinConfig = true);
void validateNativeOnnxFrameAudioQuery(const std::string &frameAudioQueryText);
std::string createNativeOnnxModelAssetsSingFrameAudioQuery(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string createNativeOnnxModelAssetsSingFrameF0(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string createNativeOnnxModelAssetsSingFrameVolume(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::vector<uint8_t> synthesizeNativeOnnxModelAssetsFrameAudioQuery(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<ModelAssetRecord> &modelAssets, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsMoraData(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsPhonemeLength(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsMoraPitch(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &accentPhrasesJson, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string replaceNativeOnnxModelAssetsAudioQueryMoraData(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::vector<uint8_t> synthesizeNativeOnnxModelAssetsAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
void streamNativeOnnxModelAssetsAudioQueryPcm(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &audioQueryText, uint32_t styleId, uint16_t cpuThreadCount, size_t chunkFrames, const std::function<void(const NativeOnnxPcmStreamInfo &)> &startStream, const std::function<void(const uint8_t *, size_t)> &writeChunk, bool shouldUseVvBinConfig = true);
std::string createNativeOnnxModelAssetsSingFrameAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string createNativeOnnxModelAssetsSingFrameF0(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::string createNativeOnnxModelAssetsSingFrameVolume(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &scoreText, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::vector<uint8_t> synthesizeNativeOnnxModelAssetsFrameAudioQuery(const NativeOnnxRuntimeState &runtimeState, const std::vector<ModelAssetRecord> &modelAssets, const std::string &frameAudioQueryText, uint32_t styleId, uint16_t cpuThreadCount, bool shouldUseVvBinConfig = true);
std::vector<uint8_t> synthesizeNativeOnnxVvmAudioQuery(const std::filesystem::path &onnxruntimeLibraryPath, const std::vector<VvmArchiveSummary> &archiveSummaries, const std::filesystem::path &audioQueryPath, uint32_t styleId, uint16_t cpuThreadCount);
