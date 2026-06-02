#pragma once

#include "runtime.hpp"

#include <string>

bool isNativeRuntimeBackend(const RuntimeState &runtimeState);
bool shouldUseNativeRuntimeVvBinConfig(const RuntimeState &runtimeState);
const VoiceModelRecord &getRuntimeVoiceModelForStyle(RuntimeState &runtimeState, uint32_t styleId);
std::string replaceNativeRuntimeAudioQueryMoraData(RuntimeState &runtimeState, const std::string &audioQueryJson, uint32_t styleId);
std::string replaceNativeRuntimeMoraData(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
std::string replaceNativeRuntimePhonemeLength(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
std::string replaceNativeRuntimeMoraPitch(RuntimeState &runtimeState, const std::string &accentPhrasesJson, uint32_t styleId);
