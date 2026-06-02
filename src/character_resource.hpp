#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct CharacterResourceManager {
    std::filesystem::path rootDirectory;
    std::vector<std::pair<std::filesystem::path, std::string>> pathHashes;
};

CharacterResourceManager createCharacterResourceManager(const std::filesystem::path &rootDirectory);
std::map<std::string, std::string> createCharacterSupportedFeaturesJsons(const CharacterResourceManager &resourceManager);
std::string createCharacterInfoJson(const std::string &metasJson, const std::string &speakerUuid, const std::string &styleGroup, const CharacterResourceManager &resourceManager, const std::string &resourceBaseUrl, const std::string &resourceFormat);
std::filesystem::path getCharacterResourcePath(const CharacterResourceManager &resourceManager, const std::string &resourceHash);
std::string getCharacterResourceContentType(const std::filesystem::path &resourcePath);
