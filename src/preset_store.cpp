#include "preset_store.hpp"

#include "json_utility.hpp"
#include "utility.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

static int32_t getPresetId(const std::string &presetJson) {
    uint32_t presetId = 0;
    if (!extractJsonNumberField(presetJson, "id", presetId)) {
        throw std::runtime_error("プリセット id がありません");
    }
    return static_cast<int32_t>(presetId);
}

static bool hasJsonStringField(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    return valuePosition != std::string::npos && valuePosition < jsonText.size() && jsonText[valuePosition] == '"';
}

static void validatePresetJson(const std::string &presetJson) {
    std::string trimmedText = trimAscii(presetJson);
    if (trimmedText.size() < 2 || trimmedText.front() != '{' || trimmedText.back() != '}') {
        throw std::runtime_error("プリセット JSON が object ではありません");
    }
    uint32_t presetId = 0;
    if (!extractJsonNumberField(trimmedText, "id", presetId)) {
        throw std::runtime_error("プリセット id がありません");
    }
    uint32_t styleId = 0;
    if (!extractJsonNumberField(trimmedText, "style_id", styleId)) {
        throw std::runtime_error("プリセット style_id がありません");
    }
    if (!hasJsonStringField(trimmedText, "name")) {
        throw std::runtime_error("プリセット name がありません");
    }
}

static std::vector<std::string> loadPresetObjects(const fs::path &presetPath) {
    std::string presetsJson = loadPresetsJson(presetPath);
    std::string trimmedText = trimAscii(presetsJson);
    if (trimmedText == "[]") {
        return {};
    }
    if (trimmedText.size() < 2 || trimmedText.front() != '[' || trimmedText.back() != ']') {
        throw std::runtime_error("プリセット JSON が配列ではありません: " + presetPath.string());
    }
    return splitJsonObjects(trimmedText);
}

static std::string joinPresetObjects(const std::vector<std::string> &presetObjects) {
    std::ostringstream jsonStream;
    jsonStream << "[";
    for (size_t presetIndex = 0; presetIndex < presetObjects.size(); presetIndex++) {
        if (presetIndex > 0) {
            jsonStream << ",";
        }
        jsonStream << presetObjects[presetIndex];
    }
    jsonStream << "]";
    return jsonStream.str();
}

static std::string replaceIntegerField(std::string jsonText, const std::string &fieldName, int32_t integerValue) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        return jsonText;
    }
    size_t endPosition = valuePosition;
    while (endPosition < jsonText.size() && std::isdigit(static_cast<unsigned char>(jsonText[endPosition]))) {
        endPosition++;
    }
    jsonText.replace(valuePosition, endPosition - valuePosition, std::to_string(integerValue));
    return jsonText;
}

static int32_t getNextPresetId(const std::vector<std::string> &presetObjects) {
    int32_t nextPresetId = 0;
    for (const std::string &presetObject : presetObjects) {
        int32_t presetId = getPresetId(presetObject);
        if (presetId >= nextPresetId) {
            nextPresetId = presetId + 1;
        }
    }
    return nextPresetId;
}

static void savePresetObjects(const fs::path &presetPath, const std::vector<std::string> &presetObjects) {
    writeTextFile(presetPath, joinPresetObjects(presetObjects));
}

std::string loadPresetsJson(const fs::path &presetPath) {
    if (presetPath.empty() || !fs::exists(presetPath)) {
        return "[]";
    }
    std::string presetsJson = trimAscii(readTextFile(presetPath));
    if (presetsJson.empty()) {
        return "[]";
    }
    return presetsJson;
}

int32_t addPresetJson(const fs::path &presetPath, const std::string &presetJson) {
    validatePresetJson(presetJson);
    int32_t presetId = getPresetId(presetJson);
    std::vector<std::string> presetObjects = loadPresetObjects(presetPath);
    std::string savedPresetJson = trimAscii(presetJson);
    for (const std::string &presetObject : presetObjects) {
        if (getPresetId(presetObject) == presetId) {
            presetId = getNextPresetId(presetObjects);
            savedPresetJson = replaceIntegerField(savedPresetJson, "id", presetId);
            break;
        }
    }
    presetObjects.push_back(savedPresetJson);
    savePresetObjects(presetPath, presetObjects);
    return presetId;
}

int32_t updatePresetJson(const fs::path &presetPath, const std::string &presetJson) {
    validatePresetJson(presetJson);
    int32_t presetId = getPresetId(presetJson);
    std::vector<std::string> presetObjects = loadPresetObjects(presetPath);
    for (std::string &presetObject : presetObjects) {
        if (getPresetId(presetObject) == presetId) {
            presetObject = trimAscii(presetJson);
            savePresetObjects(presetPath, presetObjects);
            return presetId;
        }
    }
    throw std::runtime_error("プリセットが見つかりません: " + std::to_string(presetId));
}

void deletePresetJson(const fs::path &presetPath, int32_t presetId) {
    std::vector<std::string> presetObjects = loadPresetObjects(presetPath);
    for (auto presetIterator = presetObjects.begin(); presetIterator != presetObjects.end(); ++presetIterator) {
        if (getPresetId(*presetIterator) == presetId) {
            presetObjects.erase(presetIterator);
            savePresetObjects(presetPath, presetObjects);
            return;
        }
    }
    throw std::runtime_error("プリセットが見つかりません: " + std::to_string(presetId));
}

std::string findPresetJson(const fs::path &presetPath, int32_t presetId) {
    for (const std::string &presetObject : loadPresetObjects(presetPath)) {
        if (getPresetId(presetObject) == presetId) {
            return presetObject;
        }
    }
    throw std::runtime_error("プリセットが見つかりません: " + std::to_string(presetId));
}

uint32_t getPresetStyleId(const std::string &presetJson) {
    uint32_t styleId = 0;
    if (!extractJsonNumberField(presetJson, "style_id", styleId)) {
        throw std::runtime_error("プリセット style_id がありません");
    }
    return styleId;
}

static std::string formatPresetNumberText(const std::string &rawNumberText) {
    double numberValue = std::stod(rawNumberText);
    std::ostringstream numberStream;
    numberStream << std::setprecision(15) << numberValue;
    std::string formattedNumberText = numberStream.str();
    if (formattedNumberText.find('.') == std::string::npos && formattedNumberText.find('e') == std::string::npos && formattedNumberText.find('E') == std::string::npos) {
        formattedNumberText += ".0";
    }
    return formattedNumberText;
}

static std::string replaceNumberField(std::string jsonText, const std::string &fieldName, const std::string &numberText) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        return jsonText;
    }
    size_t endPosition = valuePosition;
    bool isNullValue = jsonText.compare(valuePosition, 4, "null") == 0;
    if (isNullValue) {
        endPosition = valuePosition + 4;
    }
    while (!isNullValue && endPosition < jsonText.size()) {
        char character = jsonText[endPosition];
        if ((character >= '0' && character <= '9') || character == '-' || character == '+' || character == '.' || character == 'e' || character == 'E') {
            endPosition++;
            continue;
        }
        break;
    }
    jsonText.replace(valuePosition, endPosition - valuePosition, numberText);
    return jsonText;
}

static bool extractRawNumberField(const std::string &jsonText, const std::string &fieldName, std::string &numberText) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos) {
        return false;
    }
    size_t endPosition = valuePosition;
    while (endPosition < jsonText.size()) {
        char character = jsonText[endPosition];
        if ((character >= '0' && character <= '9') || character == '-' || character == '+' || character == '.' || character == 'e' || character == 'E') {
            endPosition++;
            continue;
        }
        break;
    }
    if (endPosition == valuePosition) {
        return false;
    }
    numberText = jsonText.substr(valuePosition, endPosition - valuePosition);
    return true;
}

std::string applyPresetToAudioQueryJson(const std::string &audioQueryJson, const std::string &presetJson) {
    std::string adjustedJson = audioQueryJson;
    const std::vector<std::string> fieldNames = {
        "speedScale",
        "pitchScale",
        "intonationScale",
        "volumeScale",
        "prePhonemeLength",
        "postPhonemeLength",
        "pauseLength",
        "pauseLengthScale"};
    for (const std::string &fieldName : fieldNames) {
        std::string numberText;
        if (extractRawNumberField(presetJson, fieldName, numberText)) {
            adjustedJson = replaceNumberField(adjustedJson, fieldName, formatPresetNumberText(numberText));
        }
    }
    return adjustedJson;
}
