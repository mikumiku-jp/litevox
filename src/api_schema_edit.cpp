#include "api_schema_internal.hpp"

#include <string>

size_t findSchemaMatchingBrace(const std::string &jsonText, size_t openPosition) {
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
        } else if (character == '{') {
            depth++;
        } else if (character == '}') {
            depth--;
            if (depth == 0) {
                return position;
            }
        }
    }
    return std::string::npos;
}

size_t findSchemaMatchingBracket(const std::string &jsonText, size_t openPosition) {
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
        } else if (character == '[') {
            depth++;
        } else if (character == ']') {
            depth--;
            if (depth == 0) {
                return position;
            }
        }
    }
    return std::string::npos;
}

size_t findOperationResponsesOpenPosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText) {
    size_t pathPosition = jsonText.find("    \"" + pathText + "\": {");
    if (pathPosition == std::string::npos) {
        return std::string::npos;
    }
    size_t methodPosition = jsonText.find("      \"" + methodText + "\": {", pathPosition);
    if (methodPosition == std::string::npos) {
        return std::string::npos;
    }
    size_t responsesPosition = jsonText.find("        \"responses\": {", methodPosition);
    if (responsesPosition == std::string::npos) {
        return std::string::npos;
    }
    return jsonText.find('{', responsesPosition);
}

size_t findOperationOpenPosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText) {
    size_t pathPosition = jsonText.find("    \"" + pathText + "\": {");
    if (pathPosition == std::string::npos) {
        return std::string::npos;
    }
    size_t methodPosition = jsonText.find("      \"" + methodText + "\": {", pathPosition);
    if (methodPosition == std::string::npos) {
        return std::string::npos;
    }
    return jsonText.find('{', methodPosition);
}

size_t findOperationClosePosition(const std::string &jsonText, const std::string &pathText, const std::string &methodText) {
    size_t operationOpenPosition = findOperationOpenPosition(jsonText, pathText, methodText);
    if (operationOpenPosition == std::string::npos) {
        return std::string::npos;
    }
    return findSchemaMatchingBrace(jsonText, operationOpenPosition);
}

void setSchemaRequestBody(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &requestBodyText) {
    size_t operationOpenPosition = findOperationOpenPosition(jsonText, pathText, methodText);
    size_t operationClosePosition = findOperationClosePosition(jsonText, pathText, methodText);
    if (operationOpenPosition == std::string::npos || operationClosePosition == std::string::npos) {
        return;
    }
    size_t requestBodyPosition = jsonText.find("        \"requestBody\": {", operationOpenPosition);
    if (requestBodyPosition != std::string::npos && requestBodyPosition < operationClosePosition) {
        size_t requestBodyOpenPosition = jsonText.find('{', requestBodyPosition);
        size_t requestBodyClosePosition = findSchemaMatchingBrace(jsonText, requestBodyOpenPosition);
        if (requestBodyOpenPosition == std::string::npos || requestBodyClosePosition == std::string::npos) {
            return;
        }
        size_t lineStartPosition = jsonText.rfind('\n', requestBodyPosition);
        lineStartPosition = lineStartPosition == std::string::npos ? requestBodyPosition : lineStartPosition + 1;
        size_t nextLinePosition = jsonText.find('\n', requestBodyClosePosition);
        size_t eraseEndPosition = nextLinePosition == std::string::npos ? requestBodyClosePosition + 1 : nextLinePosition + 1;
        jsonText.replace(lineStartPosition, eraseEndPosition - lineStartPosition, requestBodyText + ",\n");
        return;
    }
    size_t responsesPosition = jsonText.find("        \"responses\": {", operationOpenPosition);
    if (responsesPosition == std::string::npos || responsesPosition >= operationClosePosition) {
        return;
    }
    jsonText.insert(responsesPosition, requestBodyText + ",\n");
}

void setSchemaParameters(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &parametersText) {
    size_t operationOpenPosition = findOperationOpenPosition(jsonText, pathText, methodText);
    size_t operationClosePosition = findOperationClosePosition(jsonText, pathText, methodText);
    if (operationOpenPosition == std::string::npos || operationClosePosition == std::string::npos) {
        return;
    }
    size_t parametersPosition = jsonText.find("        \"parameters\": [", operationOpenPosition);
    if (parametersPosition != std::string::npos && parametersPosition < operationClosePosition) {
        size_t parametersOpenPosition = jsonText.find('[', parametersPosition);
        size_t parametersClosePosition = findSchemaMatchingBracket(jsonText, parametersOpenPosition);
        if (parametersOpenPosition == std::string::npos || parametersClosePosition == std::string::npos) {
            return;
        }
        size_t lineStartPosition = jsonText.rfind('\n', parametersPosition);
        lineStartPosition = lineStartPosition == std::string::npos ? parametersPosition : lineStartPosition + 1;
        size_t nextLinePosition = jsonText.find('\n', parametersClosePosition);
        size_t eraseEndPosition = nextLinePosition == std::string::npos ? parametersClosePosition + 1 : nextLinePosition + 1;
        jsonText.replace(lineStartPosition, eraseEndPosition - lineStartPosition, parametersText + ",\n");
        return;
    }
    size_t requestBodyPosition = jsonText.find("        \"requestBody\": {", operationOpenPosition);
    if (requestBodyPosition != std::string::npos && requestBodyPosition < operationClosePosition) {
        jsonText.insert(requestBodyPosition, parametersText + ",\n");
        return;
    }
    size_t responsesPosition = jsonText.find("        \"responses\": {", operationOpenPosition);
    if (responsesPosition != std::string::npos && responsesPosition < operationClosePosition) {
        jsonText.insert(responsesPosition, parametersText + ",\n");
    }
}

void addSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode, const std::string &descriptionText) {
    size_t responsesOpenPosition = findOperationResponsesOpenPosition(jsonText, pathText, methodText);
    if (responsesOpenPosition == std::string::npos) {
        return;
    }
    size_t responsesClosePosition = findSchemaMatchingBrace(jsonText, responsesOpenPosition);
    if (responsesClosePosition == std::string::npos) {
        return;
    }
    if (jsonText.find("          \"" + statusCode + "\": {", responsesOpenPosition) < responsesClosePosition) {
        return;
    }
    std::string responseText = ",\n          \"" + statusCode + "\": {\n            \"description\": \"" + descriptionText + "\"\n          }";
    jsonText.insert(responsesClosePosition, responseText);
}

void removeSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode) {
    size_t responsesOpenPosition = findOperationResponsesOpenPosition(jsonText, pathText, methodText);
    if (responsesOpenPosition == std::string::npos) {
        return;
    }
    size_t responsesClosePosition = findSchemaMatchingBrace(jsonText, responsesOpenPosition);
    if (responsesClosePosition == std::string::npos) {
        return;
    }
    size_t statusPosition = jsonText.find("          \"" + statusCode + "\": {", responsesOpenPosition);
    if (statusPosition == std::string::npos || statusPosition >= responsesClosePosition) {
        return;
    }
    size_t statusOpenPosition = jsonText.find('{', statusPosition);
    size_t statusClosePosition = findSchemaMatchingBrace(jsonText, statusOpenPosition);
    if (statusOpenPosition == std::string::npos || statusClosePosition == std::string::npos) {
        return;
    }
    size_t lineStartPosition = jsonText.rfind('\n', statusPosition);
    lineStartPosition = lineStartPosition == std::string::npos ? statusPosition : lineStartPosition + 1;
    size_t nextLinePosition = jsonText.find('\n', statusClosePosition);
    size_t eraseEndPosition = nextLinePosition == std::string::npos ? statusClosePosition + 1 : nextLinePosition + 1;
    size_t nextNonSpacePosition = statusClosePosition + 1;
    while (nextNonSpacePosition < jsonText.size() && (jsonText[nextNonSpacePosition] == ' ' || jsonText[nextNonSpacePosition] == '\n')) {
        nextNonSpacePosition++;
    }
    if (nextNonSpacePosition < jsonText.size() && jsonText[nextNonSpacePosition] == ',') {
        size_t commaLineEndPosition = jsonText.find('\n', nextNonSpacePosition);
        eraseEndPosition = commaLineEndPosition == std::string::npos ? nextNonSpacePosition + 1 : commaLineEndPosition + 1;
        jsonText.erase(lineStartPosition, eraseEndPosition - lineStartPosition);
        return;
    }
    size_t previousCommaPosition = jsonText.rfind(',', lineStartPosition);
    if (previousCommaPosition != std::string::npos && previousCommaPosition > responsesOpenPosition) {
        jsonText.erase(previousCommaPosition, eraseEndPosition - previousCommaPosition);
        return;
    }
    jsonText.erase(lineStartPosition, eraseEndPosition - lineStartPosition);
}

void setSchemaResponseStatus(std::string &jsonText, const std::string &pathText, const std::string &methodText, const std::string &statusCode, const std::string &responseText) {
    addSchemaResponseStatus(jsonText, pathText, methodText, statusCode, "temporary");
    size_t responsesOpenPosition = findOperationResponsesOpenPosition(jsonText, pathText, methodText);
    if (responsesOpenPosition == std::string::npos) {
        return;
    }
    size_t responsesClosePosition = findSchemaMatchingBrace(jsonText, responsesOpenPosition);
    if (responsesClosePosition == std::string::npos) {
        return;
    }
    size_t statusPosition = jsonText.find("          \"" + statusCode + "\": {", responsesOpenPosition);
    if (statusPosition == std::string::npos || statusPosition >= responsesClosePosition) {
        return;
    }
    size_t statusOpenPosition = jsonText.find('{', statusPosition);
    size_t statusClosePosition = findSchemaMatchingBrace(jsonText, statusOpenPosition);
    if (statusOpenPosition == std::string::npos || statusClosePosition == std::string::npos) {
        return;
    }
    jsonText.replace(statusPosition, statusClosePosition - statusPosition + 1, "          \"" + statusCode + "\": " + responseText);
}
