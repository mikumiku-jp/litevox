#include "json_utility.hpp"

#include "utility.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>

size_t findJsonMatchingToken(const std::string &jsonText, size_t openPosition, char openToken, char closeToken) {
    bool isString = false;
    bool isEscaped = false;
    int depth = 0;
    for (size_t position = openPosition; position < jsonText.size(); position++) {
        char character = jsonText[position];
        if (isString) {
            if (isEscaped) {
                isEscaped = false;
            } else if (character == '\\') {
                isEscaped = true;
            } else if (character == '"') {
                isString = false;
            }
            continue;
        }
        if (character == '"') {
            isString = true;
        } else if (character == openToken) {
            depth++;
        } else if (character == closeToken) {
            depth--;
            if (depth == 0) {
                return position;
            }
        }
    }
    return std::string::npos;
}

std::vector<std::string> splitJsonObjects(const std::string &jsonArrayText) {
    std::vector<std::string> objectTexts;
    size_t searchPosition = 0;
    while (true) {
        size_t openPosition = jsonArrayText.find('{', searchPosition);
        if (openPosition == std::string::npos) {
            break;
        }
        size_t closePosition = findJsonMatchingToken(jsonArrayText, openPosition, '{', '}');
        if (closePosition == std::string::npos) {
            break;
        }
        objectTexts.push_back(jsonArrayText.substr(openPosition, closePosition - openPosition + 1));
        searchPosition = closePosition + 1;
    }
    return objectTexts;
}

std::string decodeJsonString(const std::string &jsonText, size_t quotePosition) {
    std::string decodedText;
    for (size_t position = quotePosition + 1; position < jsonText.size(); position++) {
        char character = jsonText[position];
        if (character == '\\' && position + 1 < jsonText.size()) {
            decodedText.push_back(jsonText[position + 1]);
            position++;
        } else if (character == '"') {
            return decodedText;
        } else {
            decodedText.push_back(character);
        }
    }
    return decodedText;
}

size_t findJsonFieldValuePosition(const std::string &jsonText, const std::string &fieldName) {
    std::string fieldPattern = "\"" + fieldName + "\"";
    size_t fieldPosition = jsonText.find(fieldPattern);
    if (fieldPosition == std::string::npos) {
        return std::string::npos;
    }
    size_t colonPosition = jsonText.find(':', fieldPosition + fieldPattern.size());
    if (colonPosition == std::string::npos) {
        return std::string::npos;
    }
    size_t valuePosition = colonPosition + 1;
    while (valuePosition < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[valuePosition]))) {
        valuePosition++;
    }
    return valuePosition;
}

std::string extractJsonStringField(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size() || jsonText[valuePosition] != '"') {
        return "";
    }
    return decodeJsonString(jsonText, valuePosition);
}

bool extractJsonNumberField(const std::string &jsonText, const std::string &fieldName, uint32_t &numberValue) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size() || !std::isdigit(static_cast<unsigned char>(jsonText[valuePosition]))) {
        return false;
    }
    uint32_t parsedNumber = 0;
    while (valuePosition < jsonText.size() && std::isdigit(static_cast<unsigned char>(jsonText[valuePosition]))) {
        parsedNumber = parsedNumber * 10 + static_cast<uint32_t>(jsonText[valuePosition] - '0');
        valuePosition++;
    }
    numberValue = parsedNumber;
    return true;
}

std::string extractJsonArrayField(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size() || jsonText[valuePosition] != '[') {
        return "";
    }
    size_t closePosition = findJsonMatchingToken(jsonText, valuePosition, '[', ']');
    if (closePosition == std::string::npos) {
        return "";
    }
    return jsonText.substr(valuePosition, closePosition - valuePosition + 1);
}

std::string extractJsonObjectField(const std::string &jsonText, const std::string &fieldName) {
    size_t valuePosition = findJsonFieldValuePosition(jsonText, fieldName);
    if (valuePosition == std::string::npos || valuePosition >= jsonText.size() || jsonText[valuePosition] != '{') {
        return "";
    }
    size_t closePosition = findJsonMatchingToken(jsonText, valuePosition, '{', '}');
    if (closePosition == std::string::npos) {
        return "";
    }
    return jsonText.substr(valuePosition, closePosition - valuePosition + 1);
}

std::string stripJsonArrayEnvelope(const std::string &jsonText) {
    std::string trimmedText = trimAscii(jsonText);
    if (trimmedText.size() < 2 || trimmedText.front() != '[' || trimmedText.back() != ']') {
        return "";
    }
    return trimAscii(trimmedText.substr(1, trimmedText.size() - 2));
}

std::string quoteJsonString(const std::string &text) {
    std::ostringstream quotedStream;
    quotedStream << "\"";
    for (unsigned char character : text) {
        if (character == '\\' || character == '"') {
            quotedStream << "\\" << static_cast<char>(character);
        } else if (character == '\n') {
            quotedStream << "\\n";
        } else if (character == '\r') {
            quotedStream << "\\r";
        } else if (character == '\t') {
            quotedStream << "\\t";
        } else if (character < 0x20) {
            quotedStream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(character) << std::dec;
        } else {
            quotedStream << static_cast<char>(character);
        }
    }
    quotedStream << "\"";
    return quotedStream.str();
}
