#pragma once

#include "voicevox_types.hpp"

#include <filesystem>
#include <string>

std::string createNativeUserDictJson(const std::filesystem::path &userDictPath);
std::string addNativeUserDictWord(const std::filesystem::path &userDictPath, const std::string &surface, const std::string &pronunciation, uintptr_t accentType, VoicevoxUserDictWordType wordType, uint32_t priority);
void updateNativeUserDictWord(const std::filesystem::path &userDictPath, const std::string &wordUuid, const std::string &surface, const std::string &pronunciation, uintptr_t accentType, VoicevoxUserDictWordType wordType, uint32_t priority);
void removeNativeUserDictWord(const std::filesystem::path &userDictPath, const std::string &wordUuid);
void importNativeUserDictJson(const std::filesystem::path &userDictPath, const std::string &userDictJson, bool shouldOverride);
std::string createNativeUserDictMecabCsv(const std::filesystem::path &userDictPath);
