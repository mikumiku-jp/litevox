#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

size_t findJsonMatchingToken(const std::string &jsonText, size_t openPosition, char openToken, char closeToken);
std::vector<std::string> splitJsonObjects(const std::string &jsonArrayText);
std::string decodeJsonString(const std::string &jsonText, size_t quotePosition);
size_t findJsonFieldValuePosition(const std::string &jsonText, const std::string &fieldName);
std::string extractJsonStringField(const std::string &jsonText, const std::string &fieldName);
bool extractJsonNumberField(const std::string &jsonText, const std::string &fieldName, uint32_t &numberValue);
std::string extractJsonArrayField(const std::string &jsonText, const std::string &fieldName);
std::string extractJsonObjectField(const std::string &jsonText, const std::string &fieldName);
std::string stripJsonArrayEnvelope(const std::string &jsonText);
std::string quoteJsonString(const std::string &text);
