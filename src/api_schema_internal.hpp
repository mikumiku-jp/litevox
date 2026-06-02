#pragma once

#include <cstddef>
#include <string>

size_t findSchemaMatchingBrace(const std::string &jsonText, size_t openPosition);
size_t findSchemaMatchingBracket(const std::string &jsonText, size_t openPosition);
size_t findOperationResponsesOpenPosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText);
size_t findOperationOpenPosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText);
size_t findOperationClosePosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText);
void setSchemaRequestBody(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &requestBodyText);
void setSchemaParameters(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &parametersText);
void addSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode, const std::string &descriptionText);
void removeSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode);
void setSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode, const std::string &responseText);
