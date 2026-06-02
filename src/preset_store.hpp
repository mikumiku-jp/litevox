#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

std::string loadPresetsJson(const std::filesystem::path &presetPath);
int32_t addPresetJson(const std::filesystem::path &presetPath, const std::string &presetJson);
int32_t updatePresetJson(const std::filesystem::path &presetPath, const std::string &presetJson);
void deletePresetJson(const std::filesystem::path &presetPath, int32_t presetId);
std::string findPresetJson(const std::filesystem::path &presetPath, int32_t presetId);
uint32_t getPresetStyleId(const std::string &presetJson);
std::string applyPresetToAudioQueryJson(const std::string &audioQueryJson, const std::string &presetJson);
