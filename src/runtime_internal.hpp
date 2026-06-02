#pragma once

#include "runtime.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

bool isNativeRuntimeBackend(const RuntimeState &runtimeState);
bool shouldUseNativeRuntimeVvBinConfig(const RuntimeState &runtimeState);
std::string createRuntimeManifestJson(RuntimeState &runtimeState);
std::string readOptionalTextFile(const std::filesystem::path &filePath, const std::string &fallbackText);
std::string readOptionalJsonArrayFile(const std::filesystem::path &filePath);
void storeMaximumAtomicNumber(std::atomic<uint64_t> &targetNumber, uint64_t candidateNumber);
uint64_t countModelAssetBytes(const std::vector<ModelAssetRecord> &modelAssets);
std::string getVoiceModelLibraryUuid(const VoiceModelRecord &modelRecord);
std::string formatUuidBytes(const uint8_t uuidBytes[16]);
std::string formatUuidBytes(const std::array<uint8_t, 16> &uuidBytes);
const VoiceModelRecord &getRuntimeVoiceModelForStyle(RuntimeState &runtimeState, uint32_t styleId);
std::string replaceNativeRuntimeAudioQueryMoraData(RuntimeState &runtimeState, const std::string &audioQueryJson, uint32_t styleId);
std::string replaceNativeRuntimeMoraData(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
std::string replaceNativeRuntimePhonemeLength(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
std::string replaceNativeRuntimeMoraPitch(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
