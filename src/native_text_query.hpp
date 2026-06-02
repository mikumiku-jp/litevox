#pragma once

#include "native_audio_query.hpp"

#include <filesystem>
#include <string>
#include <vector>

std::vector<NativeAudioQueryAccentPhrase> createNativeAccentPhrasesFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text);
std::vector<NativeAudioQueryAccentPhrase> createNativeAccentPhrasesFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text);
std::string createNativeAccentPhrasesJsonFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text);
std::string createNativeAccentPhrasesJsonFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text);
std::string createNativeAudioQueryFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text);
std::string createNativeAudioQueryFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text);
