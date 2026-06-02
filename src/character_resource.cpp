#include "character_resource.hpp"

#include "json_utility.hpp"
#include "utility.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

static std::string quoteResourceJsonString(const std::string &text) {
    return quoteJsonString(text);
}

static bool isSingStyleType(const std::string &styleType) {
    return styleType == "singing_teacher" || styleType == "frame_decode" || styleType == "sing";
}

static bool shouldIncludeStyleType(const std::string &styleType, const std::string &styleGroup) {
    std::string normalizedType = styleType.empty() ? "talk" : styleType;
    if (styleGroup == "sing") {
        return isSingStyleType(normalizedType);
    }
    return normalizedType == "talk";
}

static std::string readResourceFileBytes(const fs::path &resourcePath) {
    std::ifstream inputStream(resourcePath, std::ios::binary);
    if (!inputStream) {
        throw std::runtime_error("resource を読めません: " + resourcePath.string());
    }
    std::ostringstream byteStream;
    byteStream << inputStream.rdbuf();
    return byteStream.str();
}

static std::string encodeBase64Bytes(const std::string &bytes) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encodedText;
    encodedText.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t byteIndex = 0; byteIndex < bytes.size(); byteIndex += 3) {
        uint32_t firstByte = static_cast<unsigned char>(bytes[byteIndex]);
        uint32_t secondByte = byteIndex + 1 < bytes.size() ? static_cast<unsigned char>(bytes[byteIndex + 1]) : 0;
        uint32_t thirdByte = byteIndex + 2 < bytes.size() ? static_cast<unsigned char>(bytes[byteIndex + 2]) : 0;
        uint32_t packedBytes = (firstByte << 16) | (secondByte << 8) | thirdByte;
        encodedText.push_back(table[(packedBytes >> 18) & 0x3f]);
        encodedText.push_back(table[(packedBytes >> 12) & 0x3f]);
        encodedText.push_back(byteIndex + 1 < bytes.size() ? table[(packedBytes >> 6) & 0x3f] : '=');
        encodedText.push_back(byteIndex + 2 < bytes.size() ? table[packedBytes & 0x3f] : '=');
    }
    return encodedText;
}

static std::string createResourceHash(const fs::path &resourcePath) {
    std::string resourceBytes = readResourceFileBytes(resourcePath);
    return createSha256Hex(reinterpret_cast<const uint8_t *>(resourceBytes.data()), resourceBytes.size());
}

static void registerResourcePath(CharacterResourceManager &resourceManager, const fs::path &resourcePath, const std::string &resourceHash) {
    if (!fs::exists(resourcePath) || !fs::is_regular_file(resourcePath)) {
        return;
    }
    resourceManager.pathHashes.push_back({fs::weakly_canonical(resourcePath), resourceHash});
}

static size_t findJsonStringEndPosition(const std::string &jsonText, size_t quotePosition) {
    bool isEscaped = false;
    for (size_t position = quotePosition + 1; position < jsonText.size(); position++) {
        char character = jsonText[position];
        if (isEscaped) {
            isEscaped = false;
            continue;
        }
        if (character == '\\') {
            isEscaped = true;
            continue;
        }
        if (character == '"') {
            return position;
        }
    }
    throw std::runtime_error("JSON string が壊れています");
}

static std::map<std::string, std::string> parseFilemapJson(const std::string &filemapJson) {
    std::map<std::string, std::string> filemapEntries;
    size_t position = 0;
    while (position < filemapJson.size()) {
        size_t keyPosition = filemapJson.find('"', position);
        if (keyPosition == std::string::npos) {
            break;
        }
        std::string keyText = decodeJsonString(filemapJson, keyPosition);
        size_t keyEndPosition = findJsonStringEndPosition(filemapJson, keyPosition);
        size_t colonPosition = filemapJson.find(':', keyEndPosition + 1);
        if (colonPosition == std::string::npos) {
            break;
        }
        size_t valuePosition = filemapJson.find('"', colonPosition + 1);
        if (valuePosition == std::string::npos) {
            break;
        }
        std::string valueText = decodeJsonString(filemapJson, valuePosition);
        size_t valueEndPosition = findJsonStringEndPosition(filemapJson, valuePosition);
        filemapEntries[keyText] = valueText;
        position = valueEndPosition + 1;
    }
    return filemapEntries;
}

CharacterResourceManager createCharacterResourceManager(const fs::path &rootDirectory) {
    CharacterResourceManager resourceManager;
    resourceManager.rootDirectory = rootDirectory;
    if (rootDirectory.empty() || !fs::exists(rootDirectory)) {
        return resourceManager;
    }
    fs::path filemapPath = rootDirectory / "filemap.json";
    if (fs::exists(filemapPath)) {
        std::map<std::string, std::string> filemapEntries = parseFilemapJson(readTextFile(filemapPath));
        for (const auto &filemapEntry : filemapEntries) {
            registerResourcePath(resourceManager, rootDirectory / filemapEntry.first, filemapEntry.second);
        }
        return resourceManager;
    }
    for (const fs::directory_entry &directoryEntry : fs::recursive_directory_iterator(rootDirectory)) {
        if (!directoryEntry.is_regular_file()) {
            continue;
        }
        fs::path resourcePath = directoryEntry.path();
        if (resourcePath.filename() == "filemap.json") {
            continue;
        }
        registerResourcePath(resourceManager, resourcePath, createResourceHash(resourcePath));
    }
    return resourceManager;
}

std::map<std::string, std::string> createCharacterSupportedFeaturesJsons(const CharacterResourceManager &resourceManager) {
    std::map<std::string, std::string> supportedFeaturesJsons;
    if (resourceManager.rootDirectory.empty() || !fs::exists(resourceManager.rootDirectory)) {
        return supportedFeaturesJsons;
    }
    for (const fs::directory_entry &directoryEntry : fs::directory_iterator(resourceManager.rootDirectory)) {
        if (!directoryEntry.is_directory()) {
            continue;
        }
        fs::path metasPath = directoryEntry.path() / "metas.json";
        if (!fs::exists(metasPath)) {
            continue;
        }
        std::string metadataJson = readTextFile(metasPath);
        std::string supportedFeaturesJson = extractJsonObjectField(metadataJson, "supported_features");
        if (supportedFeaturesJson.empty() || trimAscii(supportedFeaturesJson) == "{}") {
            continue;
        }
        std::string permittedMorphingText = extractJsonStringField(supportedFeaturesJson, "permitted_synthesis_morphing");
        if (!permittedMorphingText.empty()) {
            supportedFeaturesJsons[directoryEntry.path().filename().string()] = "{\"permitted_synthesis_morphing\":" + quoteJsonString(permittedMorphingText) + "}";
        }
    }
    return supportedFeaturesJsons;
}

static std::string createResourceString(const CharacterResourceManager &resourceManager, const fs::path &resourcePath, const std::string &resourceBaseUrl, const std::string &resourceFormat) {
    if (!fs::exists(resourcePath)) {
        return "";
    }
    if (resourceFormat == "base64") {
        return encodeBase64Bytes(readResourceFileBytes(resourcePath));
    }
    fs::path canonicalPath = fs::weakly_canonical(resourcePath);
    for (const auto &pathHash : resourceManager.pathHashes) {
        if (pathHash.first == canonicalPath) {
            return resourceBaseUrl + "/" + pathHash.second;
        }
    }
    return "";
}

static std::string getStylePortraitJsonValue(const CharacterResourceManager &resourceManager, const fs::path &stylePortraitPath, const std::string &resourceBaseUrl, const std::string &resourceFormat) {
    if (!fs::exists(stylePortraitPath)) {
        return "null";
    }
    std::string stylePortraitText = createResourceString(resourceManager, stylePortraitPath, resourceBaseUrl, resourceFormat);
    if (stylePortraitText.empty()) {
        return "null";
    }
    return quoteResourceJsonString(stylePortraitText);
}

std::string createCharacterInfoJson(const std::string &metasJson, const std::string &speakerUuid, const std::string &styleGroup, const CharacterResourceManager &resourceManager, const std::string &resourceBaseUrl, const std::string &resourceFormat) {
    if (resourceFormat != "base64" && resourceFormat != "url") {
        throw std::runtime_error("resource_format が不正です: " + resourceFormat);
    }
    bool hasSpeaker = false;
    std::set<uint32_t> includedStyleIds;
    std::ostringstream styleInfosStream;
    bool hasPreviousStyle = false;
    fs::path characterPath = resourceManager.rootDirectory / speakerUuid;
    for (const std::string &speakerObject : splitJsonObjects(metasJson)) {
        if (extractJsonStringField(speakerObject, "speaker_uuid") != speakerUuid) {
            continue;
        }
        if (!hasSpeaker) {
            hasSpeaker = true;
        }
        for (const std::string &styleObject : splitJsonObjects(extractJsonArrayField(speakerObject, "styles"))) {
            uint32_t styleId = 0;
            if (!extractJsonNumberField(styleObject, "id", styleId)) {
                continue;
            }
            if (includedStyleIds.find(styleId) != includedStyleIds.end()) {
                continue;
            }
            std::string styleType = extractJsonStringField(styleObject, "type");
            if (!shouldIncludeStyleType(styleType, styleGroup)) {
                continue;
            }
            includedStyleIds.insert(styleId);
            fs::path iconPath = characterPath / "icons" / (std::to_string(styleId) + ".png");
            fs::path stylePortraitPath = characterPath / "portraits" / (std::to_string(styleId) + ".png");
            if (hasPreviousStyle) {
                styleInfosStream << ",";
            }
            styleInfosStream << "{\"id\":" << styleId << ",";
            styleInfosStream << "\"icon\":" << quoteResourceJsonString(createResourceString(resourceManager, iconPath, resourceBaseUrl, resourceFormat)) << ",";
            styleInfosStream << "\"portrait\":" << getStylePortraitJsonValue(resourceManager, stylePortraitPath, resourceBaseUrl, resourceFormat) << ",";
            styleInfosStream << "\"voice_samples\":[";
            for (uint32_t sampleIndex = 1; sampleIndex <= 3; sampleIndex++) {
                if (sampleIndex > 1) {
                    styleInfosStream << ",";
                }
                std::ostringstream sampleNameStream;
                sampleNameStream << styleId << "_" << std::setw(3) << std::setfill('0') << sampleIndex << ".wav";
                fs::path voiceSamplePath = characterPath / "voice_samples" / sampleNameStream.str();
                styleInfosStream << quoteResourceJsonString(createResourceString(resourceManager, voiceSamplePath, resourceBaseUrl, resourceFormat));
            }
            styleInfosStream << "]}";
            hasPreviousStyle = true;
        }
    }
    if (!hasSpeaker) {
        throw std::runtime_error("speaker_uuid が見つかりません: " + speakerUuid);
    }
    fs::path policyPath = characterPath / "policy.md";
    fs::path portraitPath = characterPath / "portrait.png";
    std::string policyText = fs::exists(policyPath) ? readTextFile(policyPath) : "";
    std::string portraitText = createResourceString(resourceManager, portraitPath, resourceBaseUrl, resourceFormat);
    std::ostringstream speakerInfoStream;
    speakerInfoStream << "{\"policy\":" << quoteResourceJsonString(policyText) << ",";
    speakerInfoStream << "\"portrait\":" << quoteResourceJsonString(portraitText) << ",";
    speakerInfoStream << "\"style_infos\":[" << styleInfosStream.str() << "]}";
    return speakerInfoStream.str();
}

fs::path getCharacterResourcePath(const CharacterResourceManager &resourceManager, const std::string &resourceHash) {
    for (const auto &pathHash : resourceManager.pathHashes) {
        if (pathHash.second == resourceHash) {
            return pathHash.first;
        }
    }
    throw std::runtime_error("resource が見つかりません: " + resourceHash);
}

std::string getCharacterResourceContentType(const fs::path &resourcePath) {
    std::string extensionText = lowercaseAscii(resourcePath.extension().string());
    if (extensionText == ".png") {
        return "image/png";
    }
    if (extensionText == ".jpg" || extensionText == ".jpeg") {
        return "image/jpeg";
    }
    if (extensionText == ".wav") {
        return "audio/wav";
    }
    if (extensionText == ".md" || extensionText == ".txt") {
        return "text/plain; charset=utf-8";
    }
    return "application/octet-stream";
}
