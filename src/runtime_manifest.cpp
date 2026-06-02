#include "runtime.hpp"

#include "runtime_internal.hpp"
#include "native_audio_query.hpp"
#include "native_audio_query_validation.hpp"
#include "native_text_query.hpp"
#include "native_user_dict.hpp"
#include "json_utility.hpp"
#include "preset_store.hpp"
#include "setting_store.hpp"
#include "utility.hpp"
#include "vvm_archive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fs = std::filesystem;

static void appendManifestFeature(std::ostringstream &jsonStream, const std::string &keyText, bool featureValue, bool &hasPreviousFeature) {
    if (hasPreviousFeature) {
        jsonStream << ",";
    }
    jsonStream << "\"" << keyText << "\":" << (featureValue ? "true" : "false");
    hasPreviousFeature = true;
}

std::string readOptionalTextFile(const fs::path &filePath, const std::string &fallbackText) {
    if (filePath.empty() || !fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        return fallbackText;
    }
    return readTextFile(filePath);
}

static int parseHexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool parseJsonUnicodeEscape(const std::string &jsonText, size_t position, uint32_t &codepoint) {
    if (position + 4 >= jsonText.size()) {
        return false;
    }
    uint32_t parsedCodepoint = 0;
    for (size_t digitIndex = 1; digitIndex <= 4; digitIndex++) {
        int digitValue = parseHexDigit(jsonText[position + digitIndex]);
        if (digitValue < 0) {
            return false;
        }
        parsedCodepoint = (parsedCodepoint << 4) | static_cast<uint32_t>(digitValue);
    }
    codepoint = parsedCodepoint;
    return true;
}

static void appendJsonUtf8Codepoint(std::string &text, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        text.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

static std::string compactJsonText(const std::string &jsonText) {
    std::string compactText;
    compactText.reserve(jsonText.size());
    bool isString = false;
    bool isEscaped = false;
    for (size_t position = 0; position < jsonText.size(); position++) {
        char character = jsonText[position];
        if (isString) {
            if (isEscaped) {
                uint32_t codepoint = 0;
                if (character == 'u' && parseJsonUnicodeEscape(jsonText, position, codepoint)) {
                    appendJsonUtf8Codepoint(compactText, codepoint);
                    position += 4;
                } else {
                    compactText.push_back('\\');
                    compactText.push_back(character);
                }
                isEscaped = false;
            } else if (character == '\\') {
                isEscaped = true;
            } else {
                compactText.push_back(character);
                if (character == '"') {
                    isString = false;
                }
            }
            continue;
        }
        if (character == '"') {
            isString = true;
            compactText.push_back(character);
            continue;
        }
        if (!std::isspace(static_cast<unsigned char>(character))) {
            compactText.push_back(character);
        }
    }
    if (isEscaped) {
        compactText.push_back('\\');
    }
    return compactText;
}

std::string readOptionalJsonArrayFile(const fs::path &filePath) {
    std::string jsonText = trimAscii(readOptionalTextFile(filePath, "[]"));
    if (jsonText.empty() || jsonText.front() != '[') {
        return "[]";
    }
    return compactJsonText(jsonText);
}

static std::string encodeManifestBase64Bytes(const std::string &bytes) {
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

static std::string readOptionalBase64File(const fs::path &filePath) {
    if (filePath.empty() || !fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        return "";
    }
    return encodeManifestBase64Bytes(readTextFile(filePath));
}

std::string createRuntimeManifestJson(RuntimeState &runtimeState) {
    CoreBackendCapabilities backendCapabilities = getCoreBackendCapabilities(runtimeState.coreBackend);
    bool supportsSing = backendCapabilities.supportsSing && backendCapabilities.supportsFrameSynthesis;
    fs::path assetDirectory = runtimeState.engineManifestAssetDirectory;
    std::string iconBase64 = readOptionalBase64File(assetDirectory / "icon.png");
    std::string termsOfService = readOptionalTextFile(assetDirectory / "terms_of_service.md", "");
    std::string updateInfosJson = readOptionalJsonArrayFile(assetDirectory / "update_infos.json");
    std::string dependencyLicensesJson = readOptionalJsonArrayFile(assetDirectory / "dependency_licenses.json");
    std::ostringstream jsonStream;
    jsonStream << "{\"manifest_version\":\"0.13.1\",";
    jsonStream << "\"name\":\"VOICEVOX Engine\",";
    jsonStream << "\"brand_name\":\"VOICEVOX\",";
    jsonStream << "\"uuid\":\"074fc39e-678b-4c13-8916-ffca8d505d1d\",";
    jsonStream << "\"url\":\"https://github.com/VOICEVOX/voicevox_engine\",";
    jsonStream << "\"icon\":" << quoteJsonString(iconBase64) << ",";
    jsonStream << "\"default_sampling_rate\":24000,";
    jsonStream << "\"frame_rate\":93.75,";
    jsonStream << "\"terms_of_service\":" << quoteJsonString(termsOfService) << ",";
    jsonStream << "\"update_infos\":" << updateInfosJson << ",";
    jsonStream << "\"dependency_licenses\":" << dependencyLicensesJson << ",";
    jsonStream << "\"supported_vvlib_manifest_version\":null,";
    jsonStream << "\"supported_features\":{";
    bool hasPreviousFeature = false;
    appendManifestFeature(jsonStream, "adjust_mora_pitch", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_phoneme_length", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_speed_scale", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_pitch_scale", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_intonation_scale", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_volume_scale", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "adjust_pause_length", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "interrogative_upspeak", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "synthesis_morphing", backendCapabilities.supportsMorphing, hasPreviousFeature);
    appendManifestFeature(jsonStream, "sing", supportsSing, hasPreviousFeature);
    appendManifestFeature(jsonStream, "manage_library", false, hasPreviousFeature);
    appendManifestFeature(jsonStream, "return_resource_url", true, hasPreviousFeature);
    appendManifestFeature(jsonStream, "apply_katakana_english", true, hasPreviousFeature);
    jsonStream << "}}";
    return jsonStream.str();
}

void reloadUserDict(RuntimeState &runtimeState) {
    runtimeState.segmentedTextPrefetchRuntime.reset();
    if (isNativeRuntimeBackend(runtimeState)) {
        return;
    }
    std::unique_lock<std::mutex> userDictLock;
    if (runtimeState.sharedUserDictMutex) {
        userDictLock = std::unique_lock<std::mutex>(*runtimeState.sharedUserDictMutex);
    }
    reloadCoreBackendUserDict(runtimeState.coreBackend);
}
