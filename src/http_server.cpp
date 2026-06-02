#include "http_server.hpp"

#include "api_schema.hpp"
#include "json_utility.hpp"
#include "socket_compat.hpp"
#include "setting_store.hpp"
#include "streaming_audio.hpp"
#include "utility.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;

struct HttpRequest {
    std::string method;
    std::string httpVersion;
    std::string path;
    std::map<std::string, std::string> queryParameters;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int statusCode = 200;
    std::string contentType = "application/json";
    std::map<std::string, std::string> headers;
    std::vector<uint8_t> bodyBytes;
    bool isChunked = false;
    size_t chunkBytes = 32768;
    std::function<void(const std::function<void(const uint8_t *, size_t)> &)> writeStream;
};

class HttpStatusError : public std::runtime_error {
public:
    HttpStatusError(int createdStatusCode, const std::string &messageText) : std::runtime_error(messageText), statusCode(createdStatusCode) {}
    HttpStatusError(int createdStatusCode, const std::string &messageText, const std::string &createdResponseBodyText) : std::runtime_error(messageText), statusCode(createdStatusCode), responseBodyText(createdResponseBodyText) {}
    int statusCode;
    std::string responseBodyText;
};

class HttpClientDisconnected : public std::runtime_error {
public:
    HttpClientDisconnected() : std::runtime_error("client disconnected") {}
};

enum class JsonBodyRoot {
    Object,
    Array
};

struct QueuedClientSocket {
    LitevoxSocket clientSocket = static_cast<LitevoxSocket>(-1);
    std::chrono::steady_clock::time_point queuedTime;
};

struct ClientSocketQueue {
    std::mutex mutex;
    std::condition_variable condition;
    std::queue<QueuedClientSocket> sockets;
    RuntimeRequestQueueMetrics requestQueueMetrics;
};

struct ZipFileEntry {
    std::string name;
    std::vector<uint8_t> bodyBytes;
};

static bool startsWithText(const std::string &text, const std::string &prefixText);
static bool isRuntimeCorsRequestAllowed(const HttpRequest &request);

static uint64_t getElapsedMillisecondsCount(std::chrono::steady_clock::time_point startTime, std::chrono::steady_clock::time_point endTime) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
}

static std::string getStatusText(int statusCode) {
    if (statusCode == 200) return "OK";
    if (statusCode == 204) return "No Content";
    if (statusCode == 400) return "Bad Request";
    if (statusCode == 403) return "Forbidden";
    if (statusCode == 404) return "Not Found";
    if (statusCode == 405) return "Method Not Allowed";
    if (statusCode == 422) return "Unprocessable Entity";
    if (statusCode == 500) return "Internal Server Error";
    if (statusCode == 501) return "Not Implemented";
    return "OK";
}

static std::string urlDecode(const std::string &encodedText) {
    std::string decodedText;
    decodedText.reserve(encodedText.size());
    for (size_t position = 0; position < encodedText.size(); position++) {
        char character = encodedText[position];
        if (character == '+') {
            decodedText.push_back(' ');
        } else if (character == '%' && position + 2 < encodedText.size()) {
            char hexText[3] = {encodedText[position + 1], encodedText[position + 2], '\0'};
            char *endPointer = nullptr;
            long codePoint = std::strtol(hexText, &endPointer, 16);
            if (endPointer && *endPointer == '\0') {
                decodedText.push_back(static_cast<char>(codePoint));
                position += 2;
            } else {
                decodedText.push_back(character);
            }
        } else {
            decodedText.push_back(character);
        }
    }
    return decodedText;
}

static std::map<std::string, std::string> parseQueryParameters(const std::string &queryText) {
    std::map<std::string, std::string> queryParameters;
    size_t startPosition = 0;
    while (startPosition <= queryText.size()) {
        size_t ampersandPosition = queryText.find('&', startPosition);
        std::string pairText = queryText.substr(startPosition, ampersandPosition == std::string::npos ? std::string::npos : ampersandPosition - startPosition);
        if (!pairText.empty()) {
            size_t equalsPosition = pairText.find('=');
            std::string keyText = urlDecode(pairText.substr(0, equalsPosition));
            std::string valueText = equalsPosition == std::string::npos ? "" : urlDecode(pairText.substr(equalsPosition + 1));
            queryParameters[keyText] = valueText;
        }
        if (ampersandPosition == std::string::npos) {
            break;
        }
        startPosition = ampersandPosition + 1;
    }
    return queryParameters;
}

static std::map<std::string, std::string> parseFormParameters(const std::string &formText) {
    return parseQueryParameters(formText);
}

static std::string createValidationErrorJson(const std::vector<std::string> &validationDetails) {
    std::string jsonText = "{\"detail\":[";
    for (size_t detailIndex = 0; detailIndex < validationDetails.size(); detailIndex++) {
        if (detailIndex > 0) {
            jsonText += ",";
        }
        jsonText += validationDetails[detailIndex];
    }
    jsonText += "]}";
    return jsonText;
}

static std::string createMissingQueryValidationDetail(const std::string &name) {
    return "{\"type\":\"missing\",\"loc\":[\"query\"," + quoteJsonString(name) + "],\"msg\":\"Field required\",\"input\":null}";
}

static std::string createMissingBodyValidationDetail() {
    return "{\"type\":\"missing\",\"loc\":[\"body\"],\"msg\":\"Field required\",\"input\":null}";
}

static std::string createMissingBodyFieldValidationDetail(const std::string &fieldName, const std::string &inputText) {
    return "{\"type\":\"missing\",\"loc\":[\"body\"," + quoteJsonString(fieldName) + "],\"msg\":\"Field required\",\"input\":" + inputText + "}";
}

static std::string createBodyRootTypeValidationDetail(JsonBodyRoot bodyRoot, const std::string &inputText) {
    if (bodyRoot == JsonBodyRoot::Object) {
        return "{\"type\":\"model_attributes_type\",\"loc\":[\"body\"],\"msg\":\"Input should be a valid dictionary or object to extract fields from\",\"input\":" + inputText + "}";
    }
    return "{\"type\":\"list_type\",\"loc\":[\"body\"],\"msg\":\"Input should be a valid list\",\"input\":" + inputText + "}";
}

static std::string createDictionaryBodyRootTypeValidationDetail(const std::string &inputText) {
    return "{\"type\":\"dict_type\",\"loc\":[\"body\"],\"msg\":\"Input should be a valid dictionary\",\"input\":" + inputText + "}";
}

static std::string createJsonInvalidBodyValidationDetail() {
    return "{\"type\":\"json_invalid\",\"loc\":[\"body\",0],\"msg\":\"JSON decode error\",\"input\":{},\"ctx\":{\"error\":\"Expecting value\"}}";
}

static std::string createQueryParsingValidationDetail(const std::string &name, const std::string &valueText, const std::string &typeText, const std::string &messageText) {
    return "{\"type\":" + quoteJsonString(typeText) + ",\"loc\":[\"query\"," + quoteJsonString(name) + "],\"msg\":" + quoteJsonString(messageText) + ",\"input\":" + quoteJsonString(valueText) + "}";
}

static std::string createBooleanQueryParsingValidationDetail(const std::string &name, const std::string &valueText) {
    return createQueryParsingValidationDetail(name, valueText, "bool_parsing", "Input should be a valid boolean, unable to interpret input");
}

static std::string createLiteralQueryValidationDetail(const std::string &name, const std::string &valueText, const std::string &expectedText) {
    return "{\"type\":\"literal_error\",\"loc\":[\"query\"," + quoteJsonString(name) + "],\"msg\":\"Input should be " + expectedText + "\",\"input\":" + quoteJsonString(valueText) + ",\"ctx\":{\"expected\":\"" + expectedText + "\"}}";
}

static std::string createUserDictWordTypeValidationDetail(const std::string &valueText) {
    return "{\"type\":\"enum\",\"loc\":[\"query\",\"word_type\",\"str-enum[WordTypes]\"],\"msg\":\"Input should be 'PROPER_NOUN', 'COMMON_NOUN', 'VERB', 'ADJECTIVE' or 'SUFFIX'\",\"input\":" + quoteJsonString(valueText) + ",\"ctx\":{\"expected\":\"'PROPER_NOUN', 'COMMON_NOUN', 'VERB', 'ADJECTIVE' or 'SUFFIX'\"}}";
}

static std::string createNoneRequiredQueryValidationDetail(const std::string &name, const std::string &valueText) {
    return "{\"type\":\"none_required\",\"loc\":[\"query\"," + quoteJsonString(name) + ",\"none\"],\"msg\":\"Input should be None\",\"input\":" + quoteJsonString(valueText) + "}";
}

static std::string createUnionIntegerQueryParsingValidationDetail(const std::string &name, const std::string &valueText) {
    return "{\"type\":\"int_parsing\",\"loc\":[\"query\"," + quoteJsonString(name) + ",\"int\"],\"msg\":\"Input should be a valid integer, unable to parse string as an integer\",\"input\":" + quoteJsonString(valueText) + "}";
}

static std::string createBodyEnumValidationDetail(const std::string &fieldName, const std::string &valueText, const std::string &expectedText) {
    return "{\"type\":\"enum\",\"loc\":[\"body\"," + quoteJsonString(fieldName) + "],\"msg\":\"Input should be " + expectedText + "\",\"input\":" + quoteJsonString(valueText) + ",\"ctx\":{\"expected\":\"" + expectedText + "\"}}";
}

static HttpStatusError createValidationStatusError(const std::string &messageText, const std::vector<std::string> &validationDetails) {
    return HttpStatusError(422, messageText, createValidationErrorJson(validationDetails));
}

static HttpStatusError createMissingQueryParameterError(const std::string &name) {
    return createValidationStatusError(name + " がありません", {createMissingQueryValidationDetail(name)});
}

static std::vector<std::string> collectMissingQueryValidationDetails(const HttpRequest &request, const std::vector<std::string> &parameterNames) {
    std::vector<std::string> validationDetails;
    for (const std::string &parameterName : parameterNames) {
        if (request.queryParameters.find(parameterName) == request.queryParameters.end()) {
            validationDetails.push_back(createMissingQueryValidationDetail(parameterName));
        }
    }
    return validationDetails;
}

static void requireQueryParameters(const HttpRequest &request, const std::vector<std::string> &parameterNames) {
    std::vector<std::string> validationDetails = collectMissingQueryValidationDetails(request, parameterNames);
    if (!validationDetails.empty()) {
        throw createValidationStatusError("query parameter がありません", validationDetails);
    }
}

static void appendIntegerQueryValidationDetail(const HttpRequest &request, const std::string &name, std::vector<std::string> &validationDetails) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        validationDetails.push_back(createMissingQueryValidationDetail(name));
        return;
    }
    try {
        size_t consumedLength = 0;
        std::stoi(valueIterator->second, &consumedLength);
        if (consumedLength != valueIterator->second.size()) {
            throw std::invalid_argument(name);
        }
    } catch (...) {
        validationDetails.push_back(createQueryParsingValidationDetail(name, valueIterator->second, "int_parsing", "Input should be a valid integer, unable to parse string as an integer"));
    }
}

static void appendDoubleQueryValidationDetail(const HttpRequest &request, const std::string &name, std::vector<std::string> &validationDetails) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        validationDetails.push_back(createMissingQueryValidationDetail(name));
        return;
    }
    try {
        size_t consumedLength = 0;
        std::stod(valueIterator->second, &consumedLength);
        if (consumedLength != valueIterator->second.size()) {
            throw std::invalid_argument(name);
        }
    } catch (...) {
        validationDetails.push_back(createQueryParsingValidationDetail(name, valueIterator->second, "float_parsing", "Input should be a valid number, unable to parse string as a number"));
    }
}

static HttpStatusError createMissingBodyError() {
    return createValidationStatusError("JSON body がありません", {createMissingBodyValidationDetail()});
}

static HttpStatusError createDetailStatusError(int statusCode, const std::string &detailText) {
    return HttpStatusError(statusCode, detailText, "{\"detail\":" + quoteJsonString(detailText) + "}");
}

static HttpStatusError createNamedParseKanaBadRequestError(const std::string &detailText, const std::string &errorName, const std::string &errorArgsJson) {
    return HttpStatusError(400, detailText, "{\"detail\":{\"text\":" + quoteJsonString(detailText) + ",\"error_name\":" + quoteJsonString(errorName) + ",\"error_args\":" + errorArgsJson + "}}");
}

static HttpStatusError createTextParseKanaBadRequestError(const std::string &detailText, const std::string &errorName, const std::string &text) {
    return createNamedParseKanaBadRequestError(detailText, errorName, "{\"text\":" + quoteJsonString(text) + "}");
}

static HttpStatusError createParseKanaBadRequestErrorFromMessage(const std::string &messageText) {
    const std::string unknownPrefix = "未知の kana です: ";
    const std::string accentTopPrefix = "accent をアクセント句の先頭に置けません: ";
    const std::string accentTwicePrefix = "accent が複数あります: ";
    const std::string accentNotFoundPrefix = "accent がありません: ";
    const std::string emptyPhrasePrefix = "空のアクセント句です: ";
    const std::string interrogationPrefix = "疑問符が末尾以外です: ";
    if (startsWithText(messageText, unknownPrefix)) {
        std::string text = messageText.substr(unknownPrefix.size());
        return createTextParseKanaBadRequestError("判別できない読み仮名があります: " + text, "UNKNOWN_TEXT", text);
    }
    if (startsWithText(messageText, accentTopPrefix)) {
        std::string text = messageText.substr(accentTopPrefix.size());
        return createTextParseKanaBadRequestError("句頭にアクセントは置けません: " + text, "ACCENT_TOP", text);
    }
    if (startsWithText(messageText, accentTwicePrefix)) {
        std::string text = messageText.substr(accentTwicePrefix.size());
        return createTextParseKanaBadRequestError("1つのアクセント句に二つ以上のアクセントは置けません: " + text, "ACCENT_TWICE", text);
    }
    if (startsWithText(messageText, accentNotFoundPrefix)) {
        std::string text = messageText.substr(accentNotFoundPrefix.size());
        return createTextParseKanaBadRequestError("アクセントを指定していないアクセント句があります: " + text, "ACCENT_NOTFOUND", text);
    }
    if (startsWithText(messageText, emptyPhrasePrefix)) {
        std::string positionText = messageText.substr(emptyPhrasePrefix.size());
        return createNamedParseKanaBadRequestError(positionText + "番目のアクセント句が空白です", "EMPTY_PHRASE", "{\"position\":" + quoteJsonString(positionText) + "}");
    }
    if (startsWithText(messageText, interrogationPrefix)) {
        std::string text = messageText.substr(interrogationPrefix.size());
        return createTextParseKanaBadRequestError("アクセント句末以外に「？」は置けません: " + text, "INTERROGATION_MARK_NOT_AT_END", text);
    }
    return HttpStatusError(400, messageText);
}

static HttpStatusError createBodyRootTypeError(JsonBodyRoot bodyRoot, const std::string &inputText) {
    return createValidationStatusError("JSON body type mismatch", {createBodyRootTypeValidationDetail(bodyRoot, inputText)});
}

static HttpStatusError createJsonInvalidBodyError() {
    return createValidationStatusError("JSON decode error", {createJsonInvalidBodyValidationDetail()});
}

static size_t findJsonStringEndPosition(const std::string &jsonText, size_t quotePosition) {
    bool isEscaped = false;
    for (size_t position = quotePosition + 1; position < jsonText.size(); position++) {
        char character = jsonText[position];
        if (isEscaped) {
            isEscaped = false;
        } else if (character == '\\') {
            isEscaped = true;
        } else if (character == '"') {
            return position;
        }
    }
    return std::string::npos;
}

static size_t findJsonValueEndPosition(const std::string &jsonText, size_t valueStartPosition) {
    if (valueStartPosition >= jsonText.size()) {
        return std::string::npos;
    }
    char valueStartToken = jsonText[valueStartPosition];
    if (valueStartToken == '"') {
        size_t stringEndPosition = findJsonStringEndPosition(jsonText, valueStartPosition);
        return stringEndPosition == std::string::npos ? std::string::npos : stringEndPosition + 1;
    }
    if (valueStartToken == '{') {
        size_t objectEndPosition = findJsonMatchingToken(jsonText, valueStartPosition, '{', '}');
        return objectEndPosition == std::string::npos ? std::string::npos : objectEndPosition + 1;
    }
    if (valueStartToken == '[') {
        size_t arrayEndPosition = findJsonMatchingToken(jsonText, valueStartPosition, '[', ']');
        return arrayEndPosition == std::string::npos ? std::string::npos : arrayEndPosition + 1;
    }
    size_t valueEndPosition = valueStartPosition;
    while (valueEndPosition < jsonText.size() && jsonText[valueEndPosition] != ',' && jsonText[valueEndPosition] != '}' && jsonText[valueEndPosition] != ']') {
        valueEndPosition++;
    }
    return valueEndPosition;
}

static size_t findJsonObjectFieldValuePosition(const std::string &jsonText, const std::string &fieldName) {
    std::string objectText = trimAscii(jsonText);
    if (objectText.size() < 2 || objectText.front() != '{' || objectText.back() != '}') {
        return std::string::npos;
    }
    size_t position = 1;
    while (position + 1 < objectText.size()) {
        while (position < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[position]))) {
            position++;
        }
        if (position >= objectText.size() || objectText[position] == '}') {
            break;
        }
        if (objectText[position] != '"') {
            return std::string::npos;
        }
        std::string parsedFieldName = decodeJsonString(objectText, position);
        size_t fieldEndPosition = findJsonStringEndPosition(objectText, position);
        if (fieldEndPosition == std::string::npos) {
            return std::string::npos;
        }
        position = fieldEndPosition + 1;
        while (position < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[position]))) {
            position++;
        }
        if (position >= objectText.size() || objectText[position] != ':') {
            return std::string::npos;
        }
        position++;
        while (position < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[position]))) {
            position++;
        }
        size_t valueStartPosition = position;
        if (parsedFieldName == fieldName) {
            return valueStartPosition;
        }
        size_t valueEndPosition = findJsonValueEndPosition(objectText, valueStartPosition);
        if (valueEndPosition == std::string::npos) {
            return std::string::npos;
        }
        position = valueEndPosition;
        while (position < objectText.size() && std::isspace(static_cast<unsigned char>(objectText[position]))) {
            position++;
        }
        if (position < objectText.size() && objectText[position] == ',') {
            position++;
        }
    }
    return std::string::npos;
}

static bool isJsonNumberText(const std::string &valueText) {
    errno = 0;
    char *numberEnd = nullptr;
    const char *numberStart = valueText.c_str();
    std::strtod(numberStart, &numberEnd);
    return numberStart != numberEnd && errno != ERANGE && numberEnd && *numberEnd == '\0';
}

static bool isCompleteQuotedJsonString(const std::string &valueText) {
    return valueText.size() >= 2 && valueText.front() == '"' && valueText.back() == '"';
}

static bool isCompleteJsonContainer(const std::string &valueText, char openToken, char closeToken) {
    if (valueText.size() < 2 || valueText.front() != openToken) {
        return false;
    }
    size_t closePosition = findJsonMatchingToken(valueText, 0, openToken, closeToken);
    return closePosition == valueText.size() - 1;
}

static bool isLikelyCompleteJsonValue(const std::string &valueText) {
    return isCompleteJsonContainer(valueText, '{', '}') || isCompleteJsonContainer(valueText, '[', ']') || isCompleteQuotedJsonString(valueText) || valueText == "true" || valueText == "false" || valueText == "null" || isJsonNumberText(valueText);
}

static uint32_t parseSpeakerParameter(const HttpRequest &request) {
    auto speakerIterator = request.queryParameters.find("speaker");
    if (speakerIterator == request.queryParameters.end()) {
        throw createMissingQueryParameterError("speaker");
    }
    try {
        size_t consumedLength = 0;
        uint32_t speakerId = static_cast<uint32_t>(std::stoul(speakerIterator->second, &consumedLength));
        if (consumedLength != speakerIterator->second.size()) {
            throw std::invalid_argument("speaker");
        }
        return speakerId;
    } catch (...) {
        throw createValidationStatusError("speaker が不正です: " + speakerIterator->second, {createQueryParsingValidationDetail("speaker", speakerIterator->second, "int_parsing", "Input should be a valid integer, unable to parse string as an integer")});
    }
}

static std::string getTextParameter(const HttpRequest &request) {
    auto textIterator = request.queryParameters.find("text");
    if (textIterator == request.queryParameters.end()) {
        throw createMissingQueryParameterError("text");
    }
    return textIterator->second;
}

static std::string getRequiredQueryParameter(const HttpRequest &request, const std::string &name) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        throw createMissingQueryParameterError(name);
    }
    return valueIterator->second;
}

static int32_t parseIntegerParameter(const HttpRequest &request, const std::string &name) {
    std::string numberText = getRequiredQueryParameter(request, name);
    try {
        size_t consumedLength = 0;
        int32_t numberValue = std::stoi(numberText, &consumedLength);
        if (consumedLength != numberText.size()) {
            throw std::invalid_argument(name);
        }
        return numberValue;
    } catch (...) {
        throw createValidationStatusError(name + " が不正です: " + numberText, {createQueryParsingValidationDetail(name, numberText, "int_parsing", "Input should be a valid integer, unable to parse string as an integer")});
    }
}

static double parseDoubleParameter(const HttpRequest &request, const std::string &name) {
    std::string numberText = getRequiredQueryParameter(request, name);
    try {
        size_t consumedLength = 0;
        double numberValue = std::stod(numberText, &consumedLength);
        if (consumedLength != numberText.size()) {
            throw std::invalid_argument(name);
        }
        return numberValue;
    } catch (...) {
        throw createValidationStatusError(name + " が不正です: " + numberText, {createQueryParsingValidationDetail(name, numberText, "float_parsing", "Input should be a valid number, unable to parse string as a number")});
    }
}

static bool parseBooleanParameter(const HttpRequest &request, const std::string &name) {
    std::string valueText = getRequiredQueryParameter(request, name);
    std::string normalizedText = lowercaseAscii(valueText);
    if (normalizedText == "true" || normalizedText == "1" || normalizedText == "yes" || normalizedText == "on") {
        return true;
    }
    if (normalizedText == "false" || normalizedText == "0" || normalizedText == "no" || normalizedText == "off") {
        return false;
    }
    throw createValidationStatusError(name + " が不正です: " + valueText, {createBooleanQueryParsingValidationDetail(name, valueText)});
}

static std::string getOptionalQueryParameter(const HttpRequest &request, const std::string &name, const std::string &fallbackText) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        return fallbackText;
    }
    return valueIterator->second;
}

static std::string getHttpHost(const HttpRequest &request) {
    auto hostIterator = request.headers.find("host");
    if (hostIterator == request.headers.end() || hostIterator->second.empty()) {
        return "127.0.0.1:50021";
    }
    return hostIterator->second;
}

static std::string createResourceBaseUrl(const HttpRequest &request) {
    return "http://" + getHttpHost(request) + "/_resources";
}

static bool isDecimalText(const std::string &numberText) {
    if (numberText.empty()) {
        return false;
    }
    for (char character : numberText) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

static VoicevoxUserDictWordType parseUserDictWordType(const std::string &wordTypeText) {
    std::string normalizedText = lowercaseAscii(wordTypeText);
    if (normalizedText.empty() || normalizedText == "proper_noun") {
        return 0;
    }
    if (normalizedText == "common_noun") {
        return 1;
    }
    if (normalizedText == "verb") {
        return 2;
    }
    if (normalizedText == "adjective") {
        return 3;
    }
    if (normalizedText == "suffix") {
        return 4;
    }
    if (isDecimalText(normalizedText)) {
        return static_cast<VoicevoxUserDictWordType>(std::stoi(normalizedText));
    }
    throw HttpStatusError(400, "word_type が不正です: " + wordTypeText);
}

static std::string jsonQuote(const std::string &text) {
    std::string quotedText = "\"";
    for (char character : text) {
        if (character == '"' || character == '\\') {
            quotedText.push_back('\\');
        }
        quotedText.push_back(character);
    }
    quotedText.push_back('"');
    return quotedText;
}

static bool startsWithText(const std::string &text, const std::string &prefixText) {
    return text.size() >= prefixText.size() && text.compare(0, prefixText.size(), prefixText) == 0;
}

static VoicevoxUserDictWordType getUserDictWordTypeParameter(const HttpRequest &request) {
    return parseUserDictWordType(getOptionalQueryParameter(request, "word_type", "proper_noun"));
}

static void validateOptionalResourceFormatParameter(const HttpRequest &request) {
    auto valueIterator = request.queryParameters.find("resource_format");
    if (valueIterator != request.queryParameters.end() && valueIterator->second != "base64" && valueIterator->second != "url") {
        throw createValidationStatusError("resource_format が不正です", {createLiteralQueryValidationDetail("resource_format", valueIterator->second, "'base64' or 'url'")});
    }
}

static bool isValidUserDictWordTypeText(const std::string &wordTypeText) {
    std::string normalizedText = lowercaseAscii(wordTypeText);
    return normalizedText == "proper_noun" || normalizedText == "common_noun" || normalizedText == "verb" || normalizedText == "adjective" || normalizedText == "suffix";
}

static void validateOptionalUserDictWordTypeParameter(const HttpRequest &request) {
    auto valueIterator = request.queryParameters.find("word_type");
    if (valueIterator != request.queryParameters.end() && !isValidUserDictWordTypeText(valueIterator->second)) {
        throw createValidationStatusError("word_type が不正です", {createUserDictWordTypeValidationDetail(valueIterator->second), createNoneRequiredQueryValidationDetail("word_type", valueIterator->second)});
    }
}

static uint32_t getUserDictPriorityParameter(const HttpRequest &request) {
    std::string priorityText = getOptionalQueryParameter(request, "priority", "5");
    try {
        size_t consumedLength = 0;
        uint32_t priority = static_cast<uint32_t>(std::stoul(priorityText, &consumedLength));
        if (consumedLength != priorityText.size()) {
            throw std::invalid_argument("priority");
        }
        return priority;
    } catch (...) {
        throw createValidationStatusError("priority が不正です: " + priorityText, {createUnionIntegerQueryParsingValidationDetail("priority", priorityText), createNoneRequiredQueryValidationDetail("priority", priorityText)});
    }
}

static uint8_t decodeBase64Character(char character) {
    if (character >= 'A' && character <= 'Z') {
        return static_cast<uint8_t>(character - 'A');
    }
    if (character >= 'a' && character <= 'z') {
        return static_cast<uint8_t>(character - 'a' + 26);
    }
    if (character >= '0' && character <= '9') {
        return static_cast<uint8_t>(character - '0' + 52);
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    throw HttpStatusError(400, "base64 が不正です");
}

static std::vector<uint8_t> decodeBase64Text(const std::string &base64Text) {
    std::vector<uint8_t> decodedBytes;
    uint32_t packedBits = 0;
    int packedBitCount = 0;
    for (char character : base64Text) {
        if (std::isspace(static_cast<unsigned char>(character))) {
            continue;
        }
        if (character == '=') {
            break;
        }
        packedBits = (packedBits << 6) | decodeBase64Character(character);
        packedBitCount += 6;
        if (packedBitCount >= 8) {
            packedBitCount -= 8;
            decodedBytes.push_back(static_cast<uint8_t>((packedBits >> packedBitCount) & 0xff));
        }
    }
    return decodedBytes;
}

static std::vector<std::string> parseJsonStringArray(const std::string &jsonText) {
    std::string trimmedText = trimAscii(jsonText);
    if (trimmedText.size() < 2 || trimmedText.front() != '[' || trimmedText.back() != ']') {
        throw HttpStatusError(400, "JSON array body が必要です");
    }
    std::vector<std::string> stringValues;
    size_t position = 1;
    while (position + 1 < trimmedText.size()) {
        while (position < trimmedText.size() && std::isspace(static_cast<unsigned char>(trimmedText[position]))) {
            position++;
        }
        if (position < trimmedText.size() && trimmedText[position] == ']') {
            break;
        }
        if (position >= trimmedText.size() || trimmedText[position] != '"') {
            throw HttpStatusError(400, "JSON string array が必要です");
        }
        stringValues.push_back(decodeJsonString(trimmedText, position));
        position++;
        bool isEscaped = false;
        while (position < trimmedText.size()) {
            char character = trimmedText[position++];
            if (isEscaped) {
                isEscaped = false;
            } else if (character == '\\') {
                isEscaped = true;
            } else if (character == '"') {
                break;
            }
        }
        while (position < trimmedText.size() && std::isspace(static_cast<unsigned char>(trimmedText[position]))) {
            position++;
        }
        if (position < trimmedText.size() && trimmedText[position] == ',') {
            position++;
        } else if (position < trimmedText.size() && trimmedText[position] == ']') {
            break;
        } else {
            throw HttpStatusError(400, "JSON string array の区切りが不正です");
        }
    }
    return stringValues;
}

static void appendZipUint16(std::vector<uint8_t> &zipBytes, uint16_t numberValue) {
    zipBytes.push_back(static_cast<uint8_t>(numberValue & 0xff));
    zipBytes.push_back(static_cast<uint8_t>((numberValue >> 8) & 0xff));
}

static void appendZipUint32(std::vector<uint8_t> &zipBytes, uint32_t numberValue) {
    zipBytes.push_back(static_cast<uint8_t>(numberValue & 0xff));
    zipBytes.push_back(static_cast<uint8_t>((numberValue >> 8) & 0xff));
    zipBytes.push_back(static_cast<uint8_t>((numberValue >> 16) & 0xff));
    zipBytes.push_back(static_cast<uint8_t>((numberValue >> 24) & 0xff));
}

static std::vector<uint8_t> createStoredZipBytes(const std::vector<ZipFileEntry> &zipFileEntries) {
    std::vector<uint8_t> zipBytes;
    std::vector<uint32_t> localHeaderOffsets;
    std::vector<uint32_t> entryChecksums;
    localHeaderOffsets.reserve(zipFileEntries.size());
    entryChecksums.reserve(zipFileEntries.size());
    for (const ZipFileEntry &zipFileEntry : zipFileEntries) {
        if (zipFileEntry.name.size() > std::numeric_limits<uint16_t>::max() || zipFileEntry.bodyBytes.size() > std::numeric_limits<uint32_t>::max() || zipBytes.size() > std::numeric_limits<uint32_t>::max()) {
            throw HttpStatusError(400, "zip entry が大きすぎます");
        }
        uint32_t checksum = crc32(0L, Z_NULL, 0);
        if (!zipFileEntry.bodyBytes.empty()) {
            checksum = crc32(checksum, reinterpret_cast<const Bytef *>(zipFileEntry.bodyBytes.data()), static_cast<uInt>(zipFileEntry.bodyBytes.size()));
        }
        localHeaderOffsets.push_back(static_cast<uint32_t>(zipBytes.size()));
        entryChecksums.push_back(checksum);
        appendZipUint32(zipBytes, 0x04034b50);
        appendZipUint16(zipBytes, 20);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint32(zipBytes, checksum);
        appendZipUint32(zipBytes, static_cast<uint32_t>(zipFileEntry.bodyBytes.size()));
        appendZipUint32(zipBytes, static_cast<uint32_t>(zipFileEntry.bodyBytes.size()));
        appendZipUint16(zipBytes, static_cast<uint16_t>(zipFileEntry.name.size()));
        appendZipUint16(zipBytes, 0);
        zipBytes.insert(zipBytes.end(), zipFileEntry.name.begin(), zipFileEntry.name.end());
        zipBytes.insert(zipBytes.end(), zipFileEntry.bodyBytes.begin(), zipFileEntry.bodyBytes.end());
    }
    if (zipBytes.size() > std::numeric_limits<uint32_t>::max() || zipFileEntries.size() > std::numeric_limits<uint16_t>::max()) {
        throw HttpStatusError(400, "zip が大きすぎます");
    }
    uint32_t centralDirectoryOffset = static_cast<uint32_t>(zipBytes.size());
    for (size_t entryIndex = 0; entryIndex < zipFileEntries.size(); entryIndex++) {
        const ZipFileEntry &zipFileEntry = zipFileEntries[entryIndex];
        appendZipUint32(zipBytes, 0x02014b50);
        appendZipUint16(zipBytes, 20);
        appendZipUint16(zipBytes, 20);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint32(zipBytes, entryChecksums[entryIndex]);
        appendZipUint32(zipBytes, static_cast<uint32_t>(zipFileEntry.bodyBytes.size()));
        appendZipUint32(zipBytes, static_cast<uint32_t>(zipFileEntry.bodyBytes.size()));
        appendZipUint16(zipBytes, static_cast<uint16_t>(zipFileEntry.name.size()));
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint16(zipBytes, 0);
        appendZipUint32(zipBytes, 0);
        appendZipUint32(zipBytes, localHeaderOffsets[entryIndex]);
        zipBytes.insert(zipBytes.end(), zipFileEntry.name.begin(), zipFileEntry.name.end());
    }
    if (zipBytes.size() > std::numeric_limits<uint32_t>::max()) {
        throw HttpStatusError(400, "zip が大きすぎます");
    }
    uint32_t centralDirectoryBytes = static_cast<uint32_t>(zipBytes.size()) - centralDirectoryOffset;
    appendZipUint32(zipBytes, 0x06054b50);
    appendZipUint16(zipBytes, 0);
    appendZipUint16(zipBytes, 0);
    appendZipUint16(zipBytes, static_cast<uint16_t>(zipFileEntries.size()));
    appendZipUint16(zipBytes, static_cast<uint16_t>(zipFileEntries.size()));
    appendZipUint32(zipBytes, centralDirectoryBytes);
    appendZipUint32(zipBytes, centralDirectoryOffset);
    appendZipUint16(zipBytes, 0);
    return zipBytes;
}

static bool isTruthyParameter(const HttpRequest &request, const std::string &name) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        return false;
    }
    std::string valueText = lowercaseAscii(valueIterator->second);
    return valueText == "1" || valueText == "true" || valueText == "yes";
}

static void requireMethod(const HttpRequest &request, const std::vector<std::string> &allowedMethods) {
    for (const std::string &allowedMethod : allowedMethods) {
        if (request.method == allowedMethod) {
            return;
        }
    }
    throw createDetailStatusError(405, "Method Not Allowed");
}

static void requireJsonBody(const HttpRequest &request, JsonBodyRoot bodyRoot) {
    std::string bodyText = trimAscii(request.body);
    if (bodyText.empty()) {
        throw createMissingBodyError();
    }
    if (bodyRoot == JsonBodyRoot::Object) {
        if (isCompleteJsonContainer(bodyText, '{', '}')) {
            return;
        }
        if (isLikelyCompleteJsonValue(bodyText)) {
            throw createBodyRootTypeError(bodyRoot, bodyText);
        }
        throw createJsonInvalidBodyError();
    }
    if (isCompleteJsonContainer(bodyText, '[', ']')) {
        return;
    }
    if (isLikelyCompleteJsonValue(bodyText)) {
        throw createBodyRootTypeError(bodyRoot, bodyText);
    }
    throw createJsonInvalidBodyError();
}

static bool appendJsonBodyRootValidationDetails(const HttpRequest &request, JsonBodyRoot bodyRoot, std::vector<std::string> &validationDetails) {
    std::string bodyText = trimAscii(request.body);
    if (bodyText.empty()) {
        validationDetails.push_back(createMissingBodyValidationDetail());
        return false;
    }
    if (bodyRoot == JsonBodyRoot::Object) {
        if (isCompleteJsonContainer(bodyText, '{', '}')) {
            return true;
        }
        if (isLikelyCompleteJsonValue(bodyText)) {
            validationDetails.push_back(createBodyRootTypeValidationDetail(bodyRoot, bodyText));
            return false;
        }
        validationDetails.push_back(createJsonInvalidBodyValidationDetail());
        return false;
    }
    if (isCompleteJsonContainer(bodyText, '[', ']')) {
        return true;
    }
    if (isLikelyCompleteJsonValue(bodyText)) {
        validationDetails.push_back(createBodyRootTypeValidationDetail(bodyRoot, bodyText));
        return false;
    }
    validationDetails.push_back(createJsonInvalidBodyValidationDetail());
    return false;
}

static void appendJsonObjectBodyFieldValidationDetails(const HttpRequest &request, const std::vector<std::string> &requiredFieldNames, const std::string &missingInputText, std::vector<std::string> &validationDetails) {
    std::string bodyText = trimAscii(request.body);
    for (const std::string &fieldName : requiredFieldNames) {
        if (findJsonObjectFieldValuePosition(bodyText, fieldName) == std::string::npos) {
            validationDetails.push_back(createMissingBodyFieldValidationDetail(fieldName, missingInputText));
        }
    }
}

static void requireQueryParametersAndJsonObjectBodyFields(const HttpRequest &request, const std::vector<std::string> &integerParameterNames, const std::vector<std::string> &doubleParameterNames, const std::vector<std::string> &requiredFieldNames, const std::string &messageText) {
    std::vector<std::string> validationDetails;
    for (const std::string &parameterName : integerParameterNames) {
        appendIntegerQueryValidationDetail(request, parameterName, validationDetails);
    }
    for (const std::string &parameterName : doubleParameterNames) {
        appendDoubleQueryValidationDetail(request, parameterName, validationDetails);
    }
    if (appendJsonBodyRootValidationDetails(request, JsonBodyRoot::Object, validationDetails)) {
        appendJsonObjectBodyFieldValidationDetails(request, requiredFieldNames, trimAscii(request.body), validationDetails);
    }
    if (!validationDetails.empty()) {
        throw createValidationStatusError(messageText, validationDetails);
    }
}

static void requireSpeakerParameterAndAudioQueryBodyFields(const HttpRequest &request) {
    static const std::vector<std::string> requiredFieldNames = {
        "accent_phrases",
        "speedScale",
        "pitchScale",
        "intonationScale",
        "volumeScale",
        "prePhonemeLength",
        "postPhonemeLength",
        "outputSamplingRate",
        "outputStereo"
    };
    requireQueryParametersAndJsonObjectBodyFields(request, {"speaker"}, {}, requiredFieldNames, "synthesis request が不完全です");
}

static void requireSynthesisMorphingRequestFields(const HttpRequest &request) {
    static const std::vector<std::string> requiredFieldNames = {
        "accent_phrases",
        "speedScale",
        "pitchScale",
        "intonationScale",
        "volumeScale",
        "prePhonemeLength",
        "postPhonemeLength",
        "outputSamplingRate",
        "outputStereo"
    };
    requireQueryParametersAndJsonObjectBodyFields(request, {"base_speaker", "target_speaker"}, {"morph_rate"}, requiredFieldNames, "synthesis morphing request が不完全です");
}

static void requireSpeakerParameterAndFrameAudioQueryBodyFields(const HttpRequest &request) {
    static const std::vector<std::string> requiredFieldNames = {
        "f0",
        "volume",
        "phonemes",
        "volumeScale",
        "outputSamplingRate",
        "outputStereo"
    };
    requireQueryParametersAndJsonObjectBodyFields(request, {"speaker"}, {}, requiredFieldNames, "frame audio query body が不完全です");
}

static bool appendBooleanQueryValidationDetail(const HttpRequest &request, const std::string &name, std::vector<std::string> &validationDetails) {
    auto valueIterator = request.queryParameters.find(name);
    if (valueIterator == request.queryParameters.end()) {
        validationDetails.push_back(createMissingQueryValidationDetail(name));
        return false;
    }
    std::string normalizedText = lowercaseAscii(valueIterator->second);
    if (normalizedText == "true" || normalizedText == "1" || normalizedText == "yes" || normalizedText == "on" || normalizedText == "false" || normalizedText == "0" || normalizedText == "no" || normalizedText == "off") {
        return true;
    }
    validationDetails.push_back(createBooleanQueryParsingValidationDetail(name, valueIterator->second));
    return false;
}

static void requireImportUserDictRequestFields(const HttpRequest &request) {
    std::vector<std::string> validationDetails;
    appendBooleanQueryValidationDetail(request, "override", validationDetails);
    std::string bodyText = trimAscii(request.body);
    if (bodyText.empty()) {
        validationDetails.push_back(createMissingBodyValidationDetail());
    } else if (isCompleteJsonContainer(bodyText, '{', '}')) {
    } else if (isLikelyCompleteJsonValue(bodyText)) {
        validationDetails.push_back(createDictionaryBodyRootTypeValidationDetail(bodyText));
    } else {
        validationDetails.push_back(createJsonInvalidBodyValidationDetail());
    }
    if (!validationDetails.empty()) {
        throw createValidationStatusError("import user dict request が不完全です", validationDetails);
    }
}

static void requireJsonObjectBodyFields(const HttpRequest &request, const std::vector<std::string> &requiredFieldNames, const std::string &messageText, const std::string &missingInputText) {
    std::string bodyText = trimAscii(request.body);
    std::vector<std::string> validationDetails;
    for (const std::string &fieldName : requiredFieldNames) {
        if (findJsonObjectFieldValuePosition(bodyText, fieldName) == std::string::npos) {
            validationDetails.push_back(createMissingBodyFieldValidationDetail(fieldName, missingInputText));
        }
    }
    if (!validationDetails.empty()) {
        throw createValidationStatusError(messageText, validationDetails);
    }
}

static void requireScoreBodyFields(const HttpRequest &request) {
    requireJsonObjectBodyFields(request, {"notes"}, "score body が不完全です", trimAscii(request.body));
}

static void requireSingFrameRequestBodyFields(const HttpRequest &request) {
    requireJsonObjectBodyFields(request, {"score", "frame_audio_query"}, "sing frame body が不完全です", "null");
}

static void requirePresetBodyFields(const HttpRequest &request) {
    static const std::vector<std::string> requiredFieldNames = {
        "id",
        "name",
        "speaker_uuid",
        "style_id",
        "speedScale",
        "pitchScale",
        "intonationScale",
        "volumeScale",
        "prePhonemeLength",
        "postPhonemeLength"
    };
    requireJsonObjectBodyFields(request, requiredFieldNames, "preset body が不完全です", trimAscii(request.body));
}

static void requireSettingFormFields(const std::map<std::string, std::string> &formParameters) {
    if (formParameters.find("cors_policy_mode") == formParameters.end()) {
        throw createValidationStatusError("setting form が不完全です", {createMissingBodyFieldValidationDetail("cors_policy_mode", "null")});
    }
}

static HttpRequest parseHttpRequestText(const std::string &requestText) {
    size_t headerEndPosition = requestText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        throw std::runtime_error("HTTP ヘッダーが不完全です");
    }
    std::string headerText = requestText.substr(0, headerEndPosition);
    std::string bodyText = requestText.substr(headerEndPosition + 4);
    std::istringstream headerStream(headerText);
    std::string requestLine;
    std::getline(headerStream, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') {
        requestLine.pop_back();
    }
    std::istringstream requestLineStream(requestLine);
    std::string targetText;
    HttpRequest request;
    requestLineStream >> request.method >> targetText >> request.httpVersion;
    size_t queryPosition = targetText.find('?');
    request.path = queryPosition == std::string::npos ? targetText : targetText.substr(0, queryPosition);
    if (queryPosition != std::string::npos) {
        request.queryParameters = parseQueryParameters(targetText.substr(queryPosition + 1));
    }
    std::string headerLine;
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        size_t colonPosition = headerLine.find(':');
        if (colonPosition != std::string::npos) {
            std::string keyText = lowercaseAscii(trimAscii(headerLine.substr(0, colonPosition)));
            std::string valueText = trimAscii(headerLine.substr(colonPosition + 1));
            request.headers[keyText] = valueText;
        }
    }
    request.body = bodyText;
    return request;
}

static bool readHttpRequest(LitevoxSocket clientSocket, std::string &pendingRequestText, HttpRequest *request) {
    char bufferBytes[8192];
    while (true) {
        size_t headerEndPosition = pendingRequestText.find("\r\n\r\n");
        if (headerEndPosition != std::string::npos) {
            HttpRequest partialRequest = parseHttpRequestText(pendingRequestText.substr(0, headerEndPosition + 4));
            auto contentLengthIterator = partialRequest.headers.find("content-length");
            size_t contentLength = 0;
            if (contentLengthIterator != partialRequest.headers.end()) {
                contentLength = static_cast<size_t>(std::stoul(contentLengthIterator->second));
            }
            size_t requestLength = headerEndPosition + 4 + contentLength;
            if (pendingRequestText.size() >= requestLength) {
                *request = parseHttpRequestText(pendingRequestText.substr(0, requestLength));
                pendingRequestText.erase(0, requestLength);
                return true;
            }
        }
        int receivedLength = static_cast<int>(recv(clientSocket, bufferBytes, sizeof(bufferBytes), 0));
        if (receivedLength < 0) {
            throw std::runtime_error("HTTP request 受信に失敗しました");
        }
        if (receivedLength == 0) {
            if (pendingRequestText.empty()) {
                return false;
            }
            throw std::runtime_error("HTTP request が途中で切断されました");
        }
        pendingRequestText.append(bufferBytes, bufferBytes + receivedLength);
    }
}

static HttpResponse makeTextResponse(int statusCode, const std::string &contentType, const std::string &bodyText) {
    HttpResponse response;
    response.statusCode = statusCode;
    response.contentType = contentType;
    response.bodyBytes = makeBodyBytes(bodyText);
    return response;
}

static HttpResponse makeJsonResponse(const std::string &bodyText) {
    return makeTextResponse(200, "application/json", bodyText);
}

static HttpResponse makeNoContentResponse() {
    return makeTextResponse(204, "application/json", "");
}

static HttpResponse makeErrorResponse(int statusCode, const std::string &messageText) {
    std::string escapedText;
    for (char character : messageText) {
        if (character == '"' || character == '\\') {
            escapedText.push_back('\\');
        }
        escapedText.push_back(character);
    }
    return makeTextResponse(statusCode, "application/json", "{\"error\":\"" + escapedText + "\"}");
}

static HttpResponse makeStatusErrorResponse(const HttpStatusError &statusError) {
    if (!statusError.responseBodyText.empty()) {
        return makeTextResponse(statusError.statusCode, "application/json", statusError.responseBodyText);
    }
    return makeErrorResponse(statusError.statusCode, statusError.what());
}

static HttpResponse makeUnsupportedFeatureResponse(const std::string &featureName) {
    return makeErrorResponse(501, featureName + " は現在の LiteVox backend では未対応です");
}

static HttpResponse makeExperimentalFeatureDisabledResponse() {
    return makeTextResponse(404, "application/json", "{\"detail\":\"実験的機能はデフォルトで無効になっています。使用するには引数を指定してください。\"}");
}

static bool isUnsupportedFeaturePath(const std::string &pathText) {
    return pathText == "/sing_frame_audio_query"
        || pathText == "/sing_frame_f0"
        || pathText == "/sing_frame_volume"
        || pathText == "/frame_synthesis";
}

static std::vector<uint32_t> parseJsonIntegerArrayText(const std::string &jsonText) {
    std::vector<uint32_t> numbers;
    uint64_t numberValue = 0;
    bool hasNumber = false;
    for (char character : jsonText) {
        if (character >= '0' && character <= '9') {
            hasNumber = true;
            numberValue = numberValue * 10 + static_cast<uint64_t>(character - '0');
        } else if (hasNumber) {
            numbers.push_back(static_cast<uint32_t>(numberValue));
            numberValue = 0;
            hasNumber = false;
        }
    }
    if (hasNumber) {
        numbers.push_back(static_cast<uint32_t>(numberValue));
    }
    return numbers;
}

static bool isRuntimeTalkStyle(const RuntimeState &runtimeState, uint32_t styleId) {
    for (const VoiceModelRecord &voiceModel : runtimeState.voiceModels) {
        for (const StyleRecord &styleRecord : voiceModel.styles) {
            if (styleRecord.styleId == styleId) {
                return styleRecord.styleType == "talk";
            }
        }
    }
    return false;
}

static bool isTalkStyleRecord(const StyleRecord &styleRecord) {
    return styleRecord.styleType.empty() || styleRecord.styleType == "talk";
}

static std::map<std::string, std::string> createRuntimeSpeakerMorphingPermissions(const RuntimeState &runtimeState, const std::vector<StyleRecord> &styleRecords) {
    std::map<std::string, bool> speakerHasTalkStyles;
    for (const StyleRecord &styleRecord : styleRecords) {
        if (isTalkStyleRecord(styleRecord)) {
            speakerHasTalkStyles[styleRecord.speakerUuid] = true;
        }
    }
    std::map<std::string, std::string> speakerPermissions;
    std::map<std::string, std::string> speakerSupportedFeaturesJsons = createCharacterSupportedFeaturesJsons(runtimeState.characterResources);
    bool supportsMorphing = getCoreBackendCapabilities(runtimeState.coreBackend).supportsMorphing;
    for (const StyleRecord &styleRecord : styleRecords) {
        if (speakerPermissions.find(styleRecord.speakerUuid) != speakerPermissions.end()) {
            continue;
        }
        auto featureEntry = speakerSupportedFeaturesJsons.find(styleRecord.speakerUuid);
        if (featureEntry != speakerSupportedFeaturesJsons.end()) {
            speakerPermissions[styleRecord.speakerUuid] = extractJsonStringField(featureEntry->second, "permitted_synthesis_morphing");
        }
        if (speakerPermissions[styleRecord.speakerUuid].empty()) {
            speakerPermissions[styleRecord.speakerUuid] = supportsMorphing && speakerHasTalkStyles[styleRecord.speakerUuid] ? "ALL" : "NOTHING";
        }
    }
    return speakerPermissions;
}

static bool isMorphableStylePair(const StyleRecord &baseStyleRecord, const StyleRecord &targetStyleRecord, const std::map<std::string, std::string> &speakerPermissions) {
    if (!isTalkStyleRecord(baseStyleRecord) || !isTalkStyleRecord(targetStyleRecord)) {
        return false;
    }
    auto basePermissionEntry = speakerPermissions.find(baseStyleRecord.speakerUuid);
    auto targetPermissionEntry = speakerPermissions.find(targetStyleRecord.speakerUuid);
    std::string basePermission = basePermissionEntry == speakerPermissions.end() ? "NOTHING" : basePermissionEntry->second;
    std::string targetPermission = targetPermissionEntry == speakerPermissions.end() ? "NOTHING" : targetPermissionEntry->second;
    if (basePermission == "NOTHING" || targetPermission == "NOTHING") {
        return false;
    }
    if (baseStyleRecord.speakerUuid == targetStyleRecord.speakerUuid) {
        return true;
    }
    return basePermission == "ALL" && targetPermission == "ALL";
}

static std::string createMorphableTargetsJson(const RuntimeState &runtimeState, const std::string &baseStyleIdsJson) {
    std::vector<uint32_t> baseStyleIds = parseJsonIntegerArrayText(baseStyleIdsJson);
    std::vector<StyleRecord> targetStyleRecords = extractOrderedStylesFromMetasJson(runtimeState.combinedMetasJson);
    std::map<uint32_t, StyleRecord> styleRecordsById;
    for (const StyleRecord &styleRecord : targetStyleRecords) {
        styleRecordsById[styleRecord.styleId] = styleRecord;
    }
    std::map<std::string, std::string> speakerPermissions = createRuntimeSpeakerMorphingPermissions(runtimeState, targetStyleRecords);
    std::ostringstream morphableTargetsStream;
    morphableTargetsStream << "[";
    for (size_t baseIndex = 0; baseIndex < baseStyleIds.size(); baseIndex++) {
        if (baseIndex > 0) {
            morphableTargetsStream << ",";
        }
        morphableTargetsStream << "{";
        auto baseStyleRecordEntry = styleRecordsById.find(baseStyleIds[baseIndex]);
        for (size_t targetIndex = 0; targetIndex < targetStyleRecords.size(); targetIndex++) {
            if (targetIndex > 0) {
                morphableTargetsStream << ",";
            }
            bool isMorphable = baseStyleRecordEntry != styleRecordsById.end() && isMorphableStylePair(baseStyleRecordEntry->second, targetStyleRecords[targetIndex], speakerPermissions);
            morphableTargetsStream << "\"" << targetStyleRecords[targetIndex].styleId << "\":{\"is_morphable\":" << (isMorphable ? "true" : "false") << "}";
        }
        morphableTargetsStream << "}";
    }
    morphableTargetsStream << "]";
    return morphableTargetsStream.str();
}

static AudioStreamFormat getAudioStreamFormatParameter(const HttpRequest &request) {
    return parseAudioStreamFormat(getOptionalQueryParameter(request, "format", "wav"));
}

static size_t getChunkSamplesParameter(const HttpRequest &request) {
    std::string chunkSamplesText = getOptionalQueryParameter(request, "chunk_samples", "1024");
    size_t chunkSamples = 0;
    try {
        chunkSamples = static_cast<size_t>(std::stoul(chunkSamplesText));
    } catch (...) {
        throw HttpStatusError(400, "chunk_samples が不正です: " + chunkSamplesText);
    }
    if (chunkSamples == 0) {
        throw HttpStatusError(400, "chunk_samples は 1 以上が必要です");
    }
    return chunkSamples;
}

static HttpResponse makeAudioResponse(const std::vector<uint8_t> &wavBytes, AudioStreamFormat audioStreamFormat) {
    AudioStreamPayload audioStreamPayload = createAudioStreamPayload(wavBytes, audioStreamFormat);
    HttpResponse response;
    response.statusCode = 200;
    response.contentType = audioStreamPayload.contentType;
    response.bodyBytes = std::move(audioStreamPayload.audioBytes);
    return response;
}

static std::string createHttpAudioStreamContentType(AudioStreamFormat audioStreamFormat) {
    if (audioStreamFormat == AudioStreamFormat::Pcm) {
        return "audio/L16; rate=24000; channels=1";
    }
    return "audio/wav";
}

static HttpResponse makeChunkedRuntimeStreamResponse(AudioStreamFormat audioStreamFormat, const std::function<void(const std::function<void(const uint8_t *, size_t)> &)> &writeStream) {
    HttpResponse response;
    response.statusCode = 200;
    response.contentType = createHttpAudioStreamContentType(audioStreamFormat);
    response.isChunked = true;
    response.writeStream = writeStream;
    return response;
}

static HttpResponse makeCancellableSynthesisResponse(RuntimeState &runtimeState, const HttpRequest &request) {
    uint32_t styleId = parseSpeakerParameter(request);
    AudioStreamFormat audioStreamFormat = getAudioStreamFormatParameter(request);
    CoreBackendCapabilities backendCapabilities = getCoreBackendCapabilities(runtimeState.coreBackend);
    if (backendCapabilities.supportsTrueStreaming) {
        std::string audioQueryJson = request.body;
        RuntimeAudioStreamOptions streamOptions;
        streamOptions.audioStreamFormat = audioStreamFormat;
        streamOptions.chunkSamples = getChunkSamplesParameter(request);
        HttpResponse response = makeChunkedRuntimeStreamResponse(audioStreamFormat, [&runtimeState, audioQueryJson, styleId, streamOptions](const std::function<void(const uint8_t *, size_t)> &writeChunk) {
            streamAudioQuery(runtimeState, audioQueryJson, styleId, streamOptions, writeChunk);
        });
        response.headers["X-LiteVox-Cancellable"] = "true";
        response.headers["X-LiteVox-Backend"] = getCoreBackendMode(runtimeState.coreBackend);
        return response;
    }
    HttpResponse response = makeAudioResponse(synthesizeAudioQuery(runtimeState, request.body, styleId), audioStreamFormat);
    response.headers["X-LiteVox-Cancellable"] = "false";
    response.headers["X-LiteVox-Backend"] = getCoreBackendMode(runtimeState.coreBackend);
    return response;
}

static void validateRequestAudioQuery(RuntimeState &runtimeState, const std::string &audioQueryJson) {
    try {
        validateAudioQuery(runtimeState, audioQueryJson);
    } catch (const std::runtime_error &validationError) {
        throw HttpStatusError(400, validationError.what());
    }
}

static void validateRequestFrameAudioQuery(RuntimeState &runtimeState, const std::string &frameAudioQueryJson) {
    try {
        validateFrameAudioQuery(runtimeState, frameAudioQueryJson);
    } catch (const std::runtime_error &validationError) {
        throw HttpStatusError(400, validationError.what());
    }
}

static std::string getJsonObjectBodyField(const HttpRequest &request, const std::string &fieldName) {
    std::string objectText = extractJsonObjectField(request.body, fieldName);
    if (objectText.empty()) {
        throw HttpStatusError(400, fieldName + " がありません");
    }
    return objectText;
}

static HttpResponse makePortalPageResponse() {
    return makeTextResponse(200, "text/html; charset=utf-8", "\n        <html>\n            <head>\n                <title>VOICEVOX Engine</title>\n            </head>\n            <body>\n                <h1>VOICEVOX Engine</h1>\n                VOICEVOX Engine へようこそ！\n                <ul>\n                    <li><a href='/setting'>設定</a></li>\n                    <li><a href='/docs'>API ドキュメント</a></li>\n        </ul></body></html>\n        ");
}

static HttpResponse makeDocsPageResponse() {
    return makeTextResponse(200, "text/html; charset=utf-8", R"litevox_html(
    <!DOCTYPE html>
    <html>
    <head>
    <link type="text/css" rel="stylesheet" href="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui.css">
    <link rel="shortcut icon" href="https://fastapi.tiangolo.com/img/favicon.png">
    <title>VOICEVOX Engine - Swagger UI</title>
    </head>
    <body>
    <div id="swagger-ui">
    </div>
    <script src="https://cdn.jsdelivr.net/npm/swagger-ui-dist@5/swagger-ui-bundle.js"></script>
    <!-- `SwaggerUIBundle` is now available on the page -->
    <script>
    const ui = SwaggerUIBundle({
        url: '/openapi.json',
    "dom_id": "#swagger-ui",
"layout": "BaseLayout",
"deepLinking": true,
"showExtensions": true,
"showCommonExtensions": true,
oauth2RedirectUrl: window.location.origin + '/docs/oauth2-redirect',
    presets: [
        SwaggerUIBundle.presets.apis,
        SwaggerUIBundle.SwaggerUIStandalonePreset
        ],
    })
    </script>
    </body>
    </html>
    )litevox_html");
}

static HttpResponse makeRedocPageResponse() {
    return makeTextResponse(200, "text/html; charset=utf-8", R"litevox_html(
    <!DOCTYPE html>
    <html>
    <head>
    <title>VOICEVOX Engine - ReDoc</title>
    <!-- needed for adaptive design -->
    <meta charset="utf-8"/>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    
    <link href="https://fonts.googleapis.com/css?family=Montserrat:300,400,700|Roboto:300,400,700" rel="stylesheet">
    
    <link rel="shortcut icon" href="https://fastapi.tiangolo.com/img/favicon.png">
    <!--
    ReDoc doesn't change outer page styles
    -->
    <style>
      body {
        margin: 0;
        padding: 0;
      }
    </style>
    </head>
    <body>
    <noscript>
        ReDoc requires Javascript to function. Please enable it to browse the documentation.
    </noscript>
    <redoc spec-url="/openapi.json"></redoc>
    <script src="https://cdn.jsdelivr.net/npm/redoc@next/bundles/redoc.standalone.js"> </script>
    </body>
    </html>
    )litevox_html");
}

static HttpResponse makeOauth2RedirectResponse() {
    return makeTextResponse(200, "text/html; charset=utf-8", R"litevox_html(
    <!doctype html>
    <html lang="en-US">
    <head>
        <title>Swagger UI: OAuth2 Redirect</title>
    </head>
    <body>
    <script>
        'use strict';
        function run () {
            var oauth2 = window.opener.swaggerUIRedirectOauth2;
            var sentState = oauth2.state;
            var redirectUrl = oauth2.redirectUrl;
            var isValid, qp, arr;

            if (/code|token|error/.test(window.location.hash)) {
                qp = window.location.hash.substring(1).replace('?', '&');
            } else {
                qp = location.search.substring(1);
            }

            arr = qp.split("&");
            arr.forEach(function (v,i,_arr) { _arr[i] = '"' + v.replace('=', '":"') + '"';});
            qp = qp ? JSON.parse('{' + arr.join() + '}',
                    function (key, value) {
                        return key === "" ? value : decodeURIComponent(value);
                    }
            ) : {};

            isValid = qp.state === sentState;

            if ((
              oauth2.auth.schema.get("flow") === "accessCode" ||
              oauth2.auth.schema.get("flow") === "authorizationCode" ||
              oauth2.auth.schema.get("flow") === "authorization_code"
            ) && !oauth2.auth.code) {
                if (!isValid) {
                    oauth2.errCb({
                        authId: oauth2.auth.name,
                        source: "auth",
                        level: "warning",
                        message: "Authorization may be unsafe, passed state was changed in server. The passed state wasn't returned from auth server."
                    });
                }

                if (qp.code) {
                    delete oauth2.state;
                    oauth2.auth.code = qp.code;
                    oauth2.callback({auth: oauth2.auth, redirectUrl: redirectUrl});
                } else {
                    let oauthErrorMsg;
                    if (qp.error) {
                        oauthErrorMsg = "["+qp.error+"]: " +
                            (qp.error_description ? qp.error_description+ ". " : "no accessCode received from the server. ") +
                            (qp.error_uri ? "More info: "+qp.error_uri : "");
                    }

                    oauth2.errCb({
                        authId: oauth2.auth.name,
                        source: "auth",
                        level: "error",
                        message: oauthErrorMsg || "[Authorization failed]: no accessCode received from the server."
                    });
                }
            } else {
                oauth2.callback({auth: oauth2.auth, token: qp, isValid: isValid, redirectUrl: redirectUrl});
            }
            window.close();
        }

        if (document.readyState !== 'loading') {
            run();
        } else {
            document.addEventListener('DOMContentLoaded', function () {
                run();
            });
        }
    </script>
    </body>
    </html>
        )litevox_html");
}

static HttpResponse makeConnectedWavesResponse(const HttpRequest &request) {
    std::vector<std::string> encodedWaves = parseJsonStringArray(request.body);
    std::vector<std::vector<uint8_t>> wavFiles;
    wavFiles.reserve(encodedWaves.size());
    for (const std::string &encodedWave : encodedWaves) {
        wavFiles.push_back(decodeBase64Text(encodedWave));
    }
    HttpResponse response;
    response.statusCode = 200;
    response.contentType = "audio/wav";
    response.bodyBytes = connectPcmWaveBytes(wavFiles);
    return response;
}

static std::string createHttpOpenApiJson(RuntimeState &runtimeState) {
    fs::path schemaPath = runtimeState.engineManifestAssetDirectory.parent_path() / "openapi.json";
    if (fs::exists(schemaPath)) {
        return readTextFile(schemaPath);
    }
    return createOpenApiJson();
}

static std::string formatMultiSynthesisFileName(size_t audioQueryIndex) {
    std::ostringstream fileNameStream;
    fileNameStream << std::setw(3) << std::setfill('0') << (audioQueryIndex + 1) << ".wav";
    return fileNameStream.str();
}

static HttpResponse makeMultiSynthesisResponse(RuntimeState &runtimeState, const HttpRequest &request) {
    std::vector<std::string> audioQueries = splitJsonObjects(request.body);
    if (audioQueries.empty()) {
        throw HttpStatusError(400, "audio query array が必要です");
    }
    uint32_t styleId = parseSpeakerParameter(request);
    std::vector<ZipFileEntry> zipFileEntries;
    zipFileEntries.reserve(audioQueries.size());
    for (size_t audioQueryIndex = 0; audioQueryIndex < audioQueries.size(); audioQueryIndex++) {
        validateRequestAudioQuery(runtimeState, audioQueries[audioQueryIndex]);
        zipFileEntries.push_back(ZipFileEntry{formatMultiSynthesisFileName(audioQueryIndex), synthesizeAudioQuery(runtimeState, audioQueries[audioQueryIndex], styleId)});
    }
    HttpResponse response;
    response.statusCode = 200;
    response.contentType = "application/zip";
    response.bodyBytes = createStoredZipBytes(zipFileEntries);
    return response;
}

static HttpResponse routeRequest(RuntimeState &runtimeState, const HttpRequest &request, const RuntimeRequestQueueMetrics *requestQueueMetrics) {
    if (request.method == "OPTIONS") {
        return makeNoContentResponse();
    }
    syncInstalledVoiceLibraries(runtimeState);
    if (request.path == "/") {
        requireMethod(request, {"GET"});
        return makePortalPageResponse();
    }
    if (request.path == "/docs") {
        requireMethod(request, {"GET"});
        return makeDocsPageResponse();
    }
    if (request.path == "/redoc") {
        requireMethod(request, {"GET"});
        return makeRedocPageResponse();
    }
    if (request.path == "/docs/oauth2-redirect") {
        requireMethod(request, {"GET"});
        return makeOauth2RedirectResponse();
    }
    if (request.path == "/health") {
        requireMethod(request, {"GET"});
        return makeJsonResponse("{\"status\":\"ok\"}");
    }
    if (request.path == "/request_queue") {
        requireMethod(request, {"GET"});
        if (!requestQueueMetrics) {
            RuntimeRequestQueueMetrics emptyRequestQueueMetrics;
            return makeJsonResponse(createRuntimeRequestQueueMetricsJson(emptyRequestQueueMetrics));
        }
        return makeJsonResponse(createRuntimeRequestQueueMetricsJson(*requestQueueMetrics));
    }
    if (request.path == "/version") {
        requireMethod(request, {"GET"});
        return makeJsonResponse("\"0.25.2\"");
    }
    if (request.path == "/core_versions") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(std::string("[\"") + getRuntimeCoreVersion(runtimeState) + "\"]");
    }
    if (request.path == "/runtime_info") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createRuntimeInfoJson(runtimeState));
    }
    if (request.path == "/model_assets") {
        requireMethod(request, {"GET"});
        return makeTextResponse(200, "text/tab-separated-values; charset=utf-8", createRuntimeModelAssetTable(runtimeState));
    }
    if (request.path == "/model_cache") {
        requireMethod(request, {"GET", "POST"});
        if (request.method == "POST") {
            loadRuntimeModelSessionCache(runtimeState);
        }
        return makeTextResponse(200, "text/tab-separated-values; charset=utf-8", createRuntimeModelSessionCacheSummary(runtimeState));
    }
    if (request.path == "/speakers") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createSpeakersJson(runtimeState.combinedMetasJson, getCoreBackendCapabilities(runtimeState.coreBackend).supportsMorphing, createCharacterSupportedFeaturesJsons(runtimeState.characterResources)));
    }
    if (request.path == "/singers") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createSingersJson(runtimeState.combinedMetasJson, createCharacterSupportedFeaturesJsons(runtimeState.characterResources)));
    }
    if (request.path == "/speaker_info") {
        requireMethod(request, {"GET"});
        std::string speakerUuid = getRequiredQueryParameter(request, "speaker_uuid");
        validateOptionalResourceFormatParameter(request);
        try {
            return makeJsonResponse(createRuntimeSpeakerInfoJson(runtimeState, speakerUuid, "talk", getOptionalQueryParameter(request, "resource_format", "base64"), createResourceBaseUrl(request)));
        } catch (const std::runtime_error &speakerInfoError) {
            if (startsWithText(speakerInfoError.what(), "resource_format")) {
                throw HttpStatusError(400, speakerInfoError.what());
            }
            throw HttpStatusError(404, speakerInfoError.what());
        }
    }
    if (request.path == "/singer_info") {
        requireMethod(request, {"GET"});
        std::string speakerUuid = getRequiredQueryParameter(request, "speaker_uuid");
        validateOptionalResourceFormatParameter(request);
        try {
            return makeJsonResponse(createRuntimeSpeakerInfoJson(runtimeState, speakerUuid, "sing", getOptionalQueryParameter(request, "resource_format", "base64"), createResourceBaseUrl(request)));
        } catch (const std::runtime_error &speakerInfoError) {
            if (startsWithText(speakerInfoError.what(), "resource_format")) {
                throw HttpStatusError(400, speakerInfoError.what());
            }
            throw HttpStatusError(404, speakerInfoError.what());
        }
    }
    if (startsWithText(request.path, "/_resources/")) {
        requireMethod(request, {"GET"});
        std::string resourceHash = request.path.substr(std::string("/_resources/").size());
        try {
            HttpResponse response;
            response.statusCode = 200;
            response.contentType = getRuntimeCharacterResourceContentType(runtimeState, resourceHash);
            response.bodyBytes = readRuntimeCharacterResource(runtimeState, resourceHash);
            response.headers["Cache-Control"] = "max-age=2592000";
            return response;
        } catch (const std::runtime_error &resourceError) {
            throw HttpStatusError(404, resourceError.what());
        }
    }
    if (request.path == "/models" || request.path == "/styles") {
        requireMethod(request, {"GET"});
        return makeTextResponse(200, "text/tab-separated-values; charset=utf-8", createModelTable(runtimeState));
    }
    if (request.path == "/engine_manifest") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(runtimeState.manifestJson);
    }
    if (request.path == "/supported_devices") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createSupportedDevicesJson(runtimeState));
    }
    if (request.path == "/setting") {
        requireMethod(request, {"GET", "POST"});
        if (request.method == "GET") {
            return makeTextResponse(200, "text/html; charset=utf-8", createSettingPageHtml(runtimeState));
        }
        std::map<std::string, std::string> formParameters = parseFormParameters(request.body);
        requireSettingFormFields(formParameters);
        auto corsPolicyIterator = formParameters.find("cors_policy_mode");
        if (corsPolicyIterator->second != "all" && corsPolicyIterator->second != "localapps") {
            throw createValidationStatusError("cors_policy_mode が不正です", {createBodyEnumValidationDetail("cors_policy_mode", corsPolicyIterator->second, "'all' or 'localapps'")});
        }
        std::string allowOrigin = "";
        auto allowOriginIterator = formParameters.find("allow_origin");
        if (allowOriginIterator != formParameters.end()) {
            allowOrigin = allowOriginIterator->second;
        }
        try {
            updateSetting(runtimeState, corsPolicyIterator->second, allowOrigin);
        } catch (const std::runtime_error &settingError) {
            throw HttpStatusError(400, settingError.what());
        }
        return makeNoContentResponse();
    }
    if (request.path == "/downloadable_libraries") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createDownloadableLibrariesJson(runtimeState));
    }
    if (request.path == "/installed_libraries") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createInstalledLibrariesJson(runtimeState));
    }
    if (startsWithText(request.path, "/install_library/")) {
        requireMethod(request, {"POST"});
        std::string libraryUuid = request.path.substr(std::string("/install_library/").size());
        try {
            installVoiceLibrary(runtimeState, libraryUuid, std::vector<uint8_t>(request.body.begin(), request.body.end()));
        } catch (const std::runtime_error &libraryError) {
            throw HttpStatusError(422, libraryError.what());
        }
        return makeNoContentResponse();
    }
    if (startsWithText(request.path, "/uninstall_library/")) {
        requireMethod(request, {"POST"});
        std::string libraryUuid = request.path.substr(std::string("/uninstall_library/").size());
        try {
            uninstallVoiceLibrary(runtimeState, libraryUuid);
        } catch (const std::runtime_error &libraryError) {
            if (startsWithText(libraryError.what(), "標準ライブラリ")) {
                throw HttpStatusError(403, libraryError.what());
            }
            throw HttpStatusError(404, libraryError.what());
        }
        return makeNoContentResponse();
    }
    if (request.path == "/presets") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createPresetsJson(runtimeState));
    }
    if (request.path == "/add_preset") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Object);
        requirePresetBodyFields(request);
        try {
            return makeJsonResponse(std::to_string(addPreset(runtimeState, request.body)));
        } catch (const std::runtime_error &presetError) {
            throw HttpStatusError(400, presetError.what());
        }
    }
    if (request.path == "/update_preset") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Object);
        requirePresetBodyFields(request);
        try {
            return makeJsonResponse(std::to_string(updatePreset(runtimeState, request.body)));
        } catch (const std::runtime_error &presetError) {
            std::string presetErrorText = presetError.what();
            if (presetErrorText.find("プリセットが見つかりません") == 0) {
                throw createDetailStatusError(422, "更新先のプリセットが存在しません");
            }
            throw HttpStatusError(400, presetErrorText);
        }
    }
    if (request.path == "/delete_preset") {
        requireMethod(request, {"POST"});
        int32_t presetId = parseIntegerParameter(request, "id");
        try {
            deletePreset(runtimeState, presetId);
        } catch (const std::runtime_error &presetError) {
            std::string presetErrorText = presetError.what();
            if (presetErrorText.find("プリセットが見つかりません") == 0) {
                throw createDetailStatusError(422, "削除対象のプリセットが存在しません");
            }
            throw HttpStatusError(400, presetErrorText);
        }
        return makeNoContentResponse();
    }
    if (request.path == "/audio_query_from_preset") {
        requireMethod(request, {"POST"});
        reloadUserDict(runtimeState);
        requireQueryParameters(request, {"text", "preset_id"});
        try {
            return makeJsonResponse(createAudioQueryFromPreset(runtimeState, getTextParameter(request), parseIntegerParameter(request, "preset_id")));
        } catch (const std::runtime_error &presetError) {
            std::string presetErrorText = presetError.what();
            if (presetErrorText.find("プリセットが見つかりません") == 0) {
                throw createDetailStatusError(422, "該当するプリセットIDが見つかりません");
            }
            throw HttpStatusError(400, presetErrorText);
        }
    }
    if (request.path == "/validate_kana") {
        requireMethod(request, {"POST"});
        std::string text = getTextParameter(request);
        try {
            validateKana(runtimeState, text);
        } catch (const std::runtime_error &kanaError) {
            throw createParseKanaBadRequestErrorFromMessage(kanaError.what());
        }
        return makeJsonResponse("true");
    }
    if (request.path == "/connect_waves") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeConnectedWavesResponse(request);
    }
    if (request.path == "/multi_synthesis") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeMultiSynthesisResponse(runtimeState, request);
    }
    if (request.path == "/sing_frame_audio_query") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Object);
        requireScoreBodyFields(request);
        if (!canCoreBackendSing(runtimeState.coreBackend)) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        return makeJsonResponse(createSingFrameAudioQuery(runtimeState, request.body, parseSpeakerParameter(request)));
    }
    if (request.path == "/sing_frame_f0") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Object);
        requireSingFrameRequestBodyFields(request);
        if (!canCoreBackendSing(runtimeState.coreBackend)) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        std::string scoreJson = getJsonObjectBodyField(request, "score");
        std::string frameAudioQueryJson = getJsonObjectBodyField(request, "frame_audio_query");
        return makeJsonResponse(createSingFrameF0(runtimeState, scoreJson, frameAudioQueryJson, parseSpeakerParameter(request)));
    }
    if (request.path == "/sing_frame_volume") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Object);
        requireSingFrameRequestBodyFields(request);
        if (!canCoreBackendSing(runtimeState.coreBackend)) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        std::string scoreJson = getJsonObjectBodyField(request, "score");
        std::string frameAudioQueryJson = getJsonObjectBodyField(request, "frame_audio_query");
        return makeJsonResponse(createSingFrameVolume(runtimeState, scoreJson, frameAudioQueryJson, parseSpeakerParameter(request)));
    }
    if (request.path == "/openapi.json") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(createHttpOpenApiJson(runtimeState));
    }
    if (request.path == "/user_dict") {
        requireMethod(request, {"GET"});
        reloadUserDict(runtimeState);
        return makeJsonResponse(createUserDictJson(runtimeState));
    }
    if (request.path == "/import_user_dict") {
        requireMethod(request, {"POST"});
        requireImportUserDictRequestFields(request);
        bool shouldOverride = parseBooleanParameter(request, "override");
        try {
            reloadUserDict(runtimeState);
            importUserDictJson(runtimeState, request.body, shouldOverride);
        } catch (const std::runtime_error &userDictError) {
            throw HttpStatusError(400, userDictError.what());
        }
        return makeNoContentResponse();
    }
    if (request.path == "/user_dict_word") {
        requireMethod(request, {"POST"});
        reloadUserDict(runtimeState);
        if (request.method == "POST") {
            requireQueryParameters(request, {"surface", "pronunciation", "accent_type"});
            validateOptionalUserDictWordTypeParameter(request);
            std::string surface = getRequiredQueryParameter(request, "surface");
            std::string pronunciation = getRequiredQueryParameter(request, "pronunciation");
            uintptr_t accentType = static_cast<uintptr_t>(parseIntegerParameter(request, "accent_type"));
            VoicevoxUserDictWordType wordType = getUserDictWordTypeParameter(request);
            uint32_t priority = getUserDictPriorityParameter(request);
            try {
                std::string wordUuid = addUserDictWord(
                    runtimeState,
                    surface,
                    pronunciation,
                    accentType,
                    wordType,
                    priority);
                return makeJsonResponse(jsonQuote(wordUuid));
            } catch (const std::runtime_error &userDictError) {
                throw HttpStatusError(400, userDictError.what());
            }
        }
    }
    if (startsWithText(request.path, "/user_dict_word/")) {
        requireMethod(request, {"PUT", "DELETE"});
        reloadUserDict(runtimeState);
        std::string wordUuid = request.path.substr(std::string("/user_dict_word/").size());
        if (request.method == "PUT") {
            requireQueryParameters(request, {"surface", "pronunciation", "accent_type"});
            validateOptionalUserDictWordTypeParameter(request);
            std::string surface = getRequiredQueryParameter(request, "surface");
            std::string pronunciation = getRequiredQueryParameter(request, "pronunciation");
            uintptr_t accentType = static_cast<uintptr_t>(parseIntegerParameter(request, "accent_type"));
            VoicevoxUserDictWordType wordType = getUserDictWordTypeParameter(request);
            uint32_t priority = getUserDictPriorityParameter(request);
            try {
                updateUserDictWord(
                    runtimeState,
                    wordUuid,
                    surface,
                    pronunciation,
                    accentType,
                    wordType,
                    priority);
            } catch (const std::runtime_error &userDictError) {
                throw HttpStatusError(400, userDictError.what());
            }
            return makeNoContentResponse();
        }
        if (request.method == "DELETE") {
            try {
                removeUserDictWord(runtimeState, wordUuid);
            } catch (const std::runtime_error &userDictError) {
                throw createDetailStatusError(422, "IDに該当するワードが見つかりませんでした");
            }
            return makeNoContentResponse();
        }
    }
    if (request.path == "/is_initialized_speaker") {
        requireMethod(request, {"GET"});
        return makeJsonResponse(isStyleLoaded(runtimeState, parseSpeakerParameter(request)) ? "true" : "false");
    }
    if (request.path == "/loaded_models") {
        requireMethod(request, {"GET"});
        return makeTextResponse(200, "text/tab-separated-values; charset=utf-8", createLoadedModelTable(runtimeState));
    }
    if (request.path == "/initialize_speaker") {
        requireMethod(request, {"POST"});
        ensureStyleLoaded(runtimeState, parseSpeakerParameter(request));
        return makeNoContentResponse();
    }
    if (request.path == "/unload_speaker") {
        requireMethod(request, {"POST"});
        unloadStyleModel(runtimeState, parseSpeakerParameter(request));
        return makeNoContentResponse();
    }
    if (request.path == "/open_jtalk/analyze") {
        requireMethod(request, {"POST"});
        return makeJsonResponse(analyzeText(runtimeState, getTextParameter(request)));
    }
    if (request.path == "/audio_query_from_accent_phrases") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeJsonResponse(createAudioQueryFromAccentPhrases(runtimeState, request.body));
    }
    if (request.path == "/morphable_targets") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        if (!getCoreBackendCapabilities(runtimeState.coreBackend).supportsMorphing) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        return makeJsonResponse(createMorphableTargetsJson(runtimeState, request.body));
    }
    if (request.path == "/synthesis_morphing") {
        requireMethod(request, {"POST"});
        requireSynthesisMorphingRequestFields(request);
        validateRequestAudioQuery(runtimeState, request.body);
        if (!getCoreBackendCapabilities(runtimeState.coreBackend).supportsMorphing) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        uint32_t baseStyleId = static_cast<uint32_t>(parseIntegerParameter(request, "base_speaker"));
        uint32_t targetStyleId = static_cast<uint32_t>(parseIntegerParameter(request, "target_speaker"));
        double morphRate = parseDoubleParameter(request, "morph_rate");
        if (morphRate < 0.0 || morphRate > 1.0) {
            throw HttpStatusError(400, "morph_rate は 0.0 以上 1.0 以下が必要です");
        }
        if (!isRuntimeTalkStyle(runtimeState, baseStyleId) || !isRuntimeTalkStyle(runtimeState, targetStyleId)) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        return makeAudioResponse(synthesizeMorphingAudioQuery(runtimeState, request.body, baseStyleId, targetStyleId, morphRate), getAudioStreamFormatParameter(request));
    }
    if (request.path == "/audio_query") {
        requireMethod(request, {"POST"});
        reloadUserDict(runtimeState);
        requireQueryParameters(request, {"text", "speaker"});
        uint32_t styleId = parseSpeakerParameter(request);
        return makeJsonResponse(createAudioQuery(runtimeState, getTextParameter(request), styleId));
    }
    if (request.path == "/accent_phrases") {
        requireMethod(request, {"POST"});
        reloadUserDict(runtimeState);
        requireQueryParameters(request, {"text", "speaker"});
        uint32_t styleId = parseSpeakerParameter(request);
        if (isTruthyParameter(request, "is_kana")) {
            try {
                return makeJsonResponse(createAccentPhrasesFromKana(runtimeState, getTextParameter(request), styleId));
            } catch (const std::runtime_error &kanaError) {
                throw createParseKanaBadRequestErrorFromMessage(kanaError.what());
            }
        }
        return makeJsonResponse(createAccentPhrases(runtimeState, getTextParameter(request), styleId));
    }
    if (request.path == "/mora_data") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeJsonResponse(replaceMoraData(runtimeState, request.body, parseSpeakerParameter(request)));
    }
    if (request.path == "/mora_length") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeJsonResponse(replacePhonemeLength(runtimeState, request.body, parseSpeakerParameter(request)));
    }
    if (request.path == "/mora_pitch") {
        requireMethod(request, {"POST"});
        requireJsonBody(request, JsonBodyRoot::Array);
        return makeJsonResponse(replaceMoraPitch(runtimeState, request.body, parseSpeakerParameter(request)));
    }
    if (request.path == "/synthesis") {
        requireMethod(request, {"POST"});
        requireSpeakerParameterAndAudioQueryBodyFields(request);
        validateRequestAudioQuery(runtimeState, request.body);
        return makeAudioResponse(synthesizeAudioQuery(runtimeState, request.body, parseSpeakerParameter(request)), getAudioStreamFormatParameter(request));
    }
    if (request.path == "/frame_synthesis") {
        requireMethod(request, {"POST"});
        requireSpeakerParameterAndFrameAudioQueryBodyFields(request);
        validateRequestFrameAudioQuery(runtimeState, request.body);
        if (!canCoreBackendFrameSynthesis(runtimeState.coreBackend)) {
            return makeUnsupportedFeatureResponse(request.path);
        }
        return makeAudioResponse(synthesizeFrameAudioQuery(runtimeState, request.body, parseSpeakerParameter(request)), getAudioStreamFormatParameter(request));
    }
    if (request.path == "/cancellable_synthesis") {
        requireMethod(request, {"POST"});
        requireSpeakerParameterAndAudioQueryBodyFields(request);
        validateRequestAudioQuery(runtimeState, request.body);
        if (!runtimeState.enableCancellableSynthesis) {
            return makeExperimentalFeatureDisabledResponse();
        }
        return makeCancellableSynthesisResponse(runtimeState, request);
    }
    if (request.path == "/synthesis_stream") {
        requireMethod(request, {"POST"});
        requireSpeakerParameterAndAudioQueryBodyFields(request);
        validateRequestAudioQuery(runtimeState, request.body);
        uint32_t styleId = parseSpeakerParameter(request);
        std::string audioQueryJson = request.body;
        RuntimeAudioStreamOptions streamOptions;
        streamOptions.audioStreamFormat = getAudioStreamFormatParameter(request);
        streamOptions.chunkSamples = getChunkSamplesParameter(request);
        return makeChunkedRuntimeStreamResponse(streamOptions.audioStreamFormat, [&runtimeState, audioQueryJson, styleId, streamOptions](const std::function<void(const uint8_t *, size_t)> &writeChunk) {
            streamAudioQuery(runtimeState, audioQueryJson, styleId, streamOptions, writeChunk);
        });
    }
    if (request.path == "/tts") {
        requireMethod(request, {"GET", "POST"});
        reloadUserDict(runtimeState);
        uint32_t styleId = parseSpeakerParameter(request);
        if (isTruthyParameter(request, "is_kana")) {
            return makeAudioResponse(synthesizeKana(runtimeState, getTextParameter(request), styleId), getAudioStreamFormatParameter(request));
        }
        return makeAudioResponse(synthesizeText(runtimeState, getTextParameter(request), styleId), getAudioStreamFormatParameter(request));
    }
    if (request.path == "/tts_stream") {
        requireMethod(request, {"GET", "POST"});
        reloadUserDict(runtimeState);
        uint32_t styleId = parseSpeakerParameter(request);
        std::string text = getTextParameter(request);
        RuntimeAudioStreamOptions streamOptions;
        streamOptions.audioStreamFormat = getAudioStreamFormatParameter(request);
        streamOptions.chunkSamples = getChunkSamplesParameter(request);
        if (isTruthyParameter(request, "is_kana")) {
            return makeChunkedRuntimeStreamResponse(streamOptions.audioStreamFormat, [&runtimeState, text, styleId, streamOptions](const std::function<void(const uint8_t *, size_t)> &writeChunk) {
                streamKana(runtimeState, text, styleId, streamOptions, writeChunk);
            });
        }
        return makeChunkedRuntimeStreamResponse(streamOptions.audioStreamFormat, [&runtimeState, text, styleId, streamOptions](const std::function<void(const uint8_t *, size_t)> &writeChunk) {
            streamText(runtimeState, text, styleId, streamOptions, writeChunk);
        });
    }
    if (isUnsupportedFeaturePath(request.path)) {
        return makeUnsupportedFeatureResponse(request.path);
    }
    return makeErrorResponse(404, "未実装のエンドポイントです: " + request.path);
}

static bool sendRawBytes(LitevoxSocket clientSocket, const uint8_t *bytePointer, size_t byteCount) {
    const uint8_t *bodyPointer = bytePointer;
    size_t bodyRemaining = byteCount;
    while (bodyRemaining > 0) {
        int sentLength = static_cast<int>(send(clientSocket, bodyPointer, static_cast<int>(bodyRemaining), 0));
        if (sentLength <= 0) {
            return false;
        }
        bodyPointer += sentLength;
        bodyRemaining -= static_cast<size_t>(sentLength);
    }
    return true;
}

static bool sendAllBytes(LitevoxSocket clientSocket, const std::string &headerText, const std::vector<uint8_t> &bodyBytes) {
    const char *headerPointer = headerText.data();
    size_t headerRemaining = headerText.size();
    while (headerRemaining > 0) {
        int sentLength = static_cast<int>(send(clientSocket, headerPointer, static_cast<int>(headerRemaining), 0));
        if (sentLength <= 0) {
            return false;
        }
        headerPointer += sentLength;
        headerRemaining -= static_cast<size_t>(sentLength);
    }
    if (!bodyBytes.empty()) {
        return sendRawBytes(clientSocket, bodyBytes.data(), bodyBytes.size());
    }
    return true;
}

static void sendHttpResponse(LitevoxSocket clientSocket, const HttpResponse &response, bool shouldKeepAlive) {
    std::ostringstream headerStream;
    headerStream << "HTTP/1.1 " << response.statusCode << " " << getStatusText(response.statusCode) << "\r\n";
    headerStream << "Content-Type: " << response.contentType << "\r\n";
    headerStream << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
    headerStream << "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto &responseHeader : response.headers) {
        if (responseHeader.first == "Access-Control-Allow-Origin" && responseHeader.second.empty()) {
            continue;
        }
        headerStream << responseHeader.first << ": " << responseHeader.second << "\r\n";
    }
    if (response.isChunked) {
        headerStream << "Transfer-Encoding: chunked\r\n";
        headerStream << "Connection: " << (shouldKeepAlive ? "keep-alive" : "close") << "\r\n\r\n";
        std::vector<uint8_t> emptyBody;
        if (!sendAllBytes(clientSocket, headerStream.str(), emptyBody)) {
            return;
        }
        auto writeHttpChunk = [clientSocket](const uint8_t *audioBytes, size_t byteCount) {
            std::ostringstream chunkHeaderStream;
            chunkHeaderStream << std::hex << byteCount << "\r\n";
            if (!sendAllBytes(clientSocket, chunkHeaderStream.str(), std::vector<uint8_t>())) {
                throw HttpClientDisconnected();
            }
            if (!sendRawBytes(clientSocket, audioBytes, byteCount)) {
                throw HttpClientDisconnected();
            }
            const uint8_t lineBreakBytes[] = {'\r', '\n'};
            if (!sendRawBytes(clientSocket, lineBreakBytes, sizeof(lineBreakBytes))) {
                throw HttpClientDisconnected();
            }
        };
        try {
            if (response.writeStream) {
                response.writeStream(writeHttpChunk);
            } else {
                AudioStreamPayload audioStreamPayload;
                audioStreamPayload.contentType = response.contentType;
                audioStreamPayload.audioBytes = response.bodyBytes;
                writeAudioStreamChunks(audioStreamPayload, response.chunkBytes, writeHttpChunk);
            }
        } catch (const HttpClientDisconnected &) {
            return;
        }
        std::vector<uint8_t> endBytes = {'0', '\r', '\n', '\r', '\n'};
        sendAllBytes(clientSocket, "", endBytes);
        return;
    }
    headerStream << "Content-Length: " << response.bodyBytes.size() << "\r\n";
    headerStream << "Connection: " << (shouldKeepAlive ? "keep-alive" : "close") << "\r\n\r\n";
    sendAllBytes(clientSocket, headerStream.str(), response.bodyBytes);
}

static void sendRuntimeHttpResponse(LitevoxSocket clientSocket, RuntimeState &runtimeState, const HttpResponse &response, bool shouldKeepAlive) {
    HttpResponse runtimeResponse = response;
    runtimeResponse.headers["X-LiteVox-Worker-Index"] = std::to_string(runtimeState.workerIndex);
    runtimeResponse.headers["X-LiteVox-Worker-Count"] = std::to_string(runtimeState.workerCount);
    sendHttpResponse(clientSocket, runtimeResponse, shouldKeepAlive);
}

static bool isRuntimeCorsRequestAllowed(const HttpRequest &request) {
    auto originIterator = request.headers.find("origin");
    if (originIterator == request.headers.end() || originIterator->second.empty()) {
        return true;
    }
    return isLiteVoxLocalAppOrigin(originIterator->second);
}

static bool shouldKeepHttpConnectionAlive(const HttpRequest &request) {
    auto connectionIterator = request.headers.find("connection");
    if (connectionIterator != request.headers.end()) {
        std::string connectionValue = lowercaseAscii(connectionIterator->second);
        if (connectionValue.find("close") != std::string::npos) {
            return false;
        }
        if (connectionValue.find("keep-alive") != std::string::npos) {
            return true;
        }
    }
    return request.httpVersion != "HTTP/1.0";
}

static LitevoxSocket openServerSocket(int port) {
    initializeSocketRuntime();
    LitevoxSocket serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidSocket(serverSocket)) {
        throw std::runtime_error("socket を作成できません");
    }
    int reuseEnabled = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuseEnabled), sizeof(reuseEnabled));

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddress.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(serverSocket, reinterpret_cast<sockaddr *>(&serverAddress), sizeof(serverAddress)) < 0) {
        closeSocket(serverSocket);
        throw std::runtime_error("bind に失敗しました: " + getSocketErrorText());
    }
    if (listen(serverSocket, 64) < 0) {
        closeSocket(serverSocket);
        throw std::runtime_error("listen に失敗しました: " + getSocketErrorText());
    }
    return serverSocket;
}

static void handleClient(RuntimeState &runtimeState, LitevoxSocket clientSocket, RuntimeRequestQueueMetrics *requestQueueMetrics, std::chrono::steady_clock::time_point queuedTime) {
    std::string pendingRequestText;
    std::chrono::steady_clock::time_point currentQueuedTime = queuedTime;
    while (true) {
        HttpRequest request;
        try {
            if (!readHttpRequest(clientSocket, pendingRequestText, &request)) {
                break;
            }
        } catch (const std::exception &exception) {
            sendHttpResponse(clientSocket, makeErrorResponse(400, exception.what()), false);
            break;
        }
        bool shouldKeepAlive = shouldKeepHttpConnectionAlive(request);
        auto serviceStartTime = std::chrono::steady_clock::now();
        if (requestQueueMetrics) {
            markRuntimeRequestAccepted(*requestQueueMetrics);
            uint64_t waitMilliseconds = getElapsedMillisecondsCount(currentQueuedTime, serviceStartTime);
            markRuntimeRequestStarted(*requestQueueMetrics, waitMilliseconds);
        }
        try {
            if (!isRuntimeCorsRequestAllowed(request)) {
                throw createDetailStatusError(403, "Origin not allowed");
            }
            sendRuntimeHttpResponse(clientSocket, runtimeState, routeRequest(runtimeState, request, requestQueueMetrics), shouldKeepAlive);
        } catch (const HttpStatusError &statusError) {
            sendHttpResponse(clientSocket, makeStatusErrorResponse(statusError), shouldKeepAlive);
        } catch (const std::invalid_argument &invalidArgument) {
            sendHttpResponse(clientSocket, makeErrorResponse(400, invalidArgument.what()), shouldKeepAlive);
        } catch (const std::out_of_range &outOfRange) {
            sendHttpResponse(clientSocket, makeErrorResponse(400, outOfRange.what()), shouldKeepAlive);
        } catch (const std::exception &exception) {
            sendHttpResponse(clientSocket, makeErrorResponse(500, exception.what()), false);
            shouldKeepAlive = false;
        }
        if (requestQueueMetrics) {
            uint64_t serviceMilliseconds = getElapsedMillisecondsCount(serviceStartTime, std::chrono::steady_clock::now());
            markRuntimeRequestCompleted(*requestQueueMetrics, serviceMilliseconds);
        }
        if (!shouldKeepAlive) {
            break;
        }
        currentQueuedTime = std::chrono::steady_clock::now();
    }
    closeSocket(clientSocket);
}

void serve(RuntimeState &runtimeState, int port) {
    LitevoxSocket serverSocket = openServerSocket(port);
    RuntimeRequestQueueMetrics requestQueueMetrics;
    std::cout << "litevox listening on http://127.0.0.1:" << port << "\n";
    while (true) {
        LitevoxSocket clientSocket = accept(serverSocket, nullptr, nullptr);
        if (!isValidSocket(clientSocket)) {
            continue;
        }
        auto queuedTime = std::chrono::steady_clock::now();
        markRuntimeConnectionAccepted(requestQueueMetrics);
        handleClient(runtimeState, clientSocket, &requestQueueMetrics, queuedTime);
    }
}

static void pushClientSocket(ClientSocketQueue &clientSocketQueue, LitevoxSocket clientSocket) {
    {
        std::lock_guard<std::mutex> queueLock(clientSocketQueue.mutex);
        clientSocketQueue.sockets.push(QueuedClientSocket{clientSocket, std::chrono::steady_clock::now()});
        markRuntimeConnectionAccepted(clientSocketQueue.requestQueueMetrics);
        markRuntimeConnectionQueued(clientSocketQueue.requestQueueMetrics);
    }
    clientSocketQueue.condition.notify_one();
}

static QueuedClientSocket waitClientSocket(ClientSocketQueue &clientSocketQueue) {
    std::unique_lock<std::mutex> queueLock(clientSocketQueue.mutex);
    clientSocketQueue.condition.wait(queueLock, [&clientSocketQueue]() {
        return !clientSocketQueue.sockets.empty();
    });
    QueuedClientSocket queuedClientSocket = clientSocketQueue.sockets.front();
    clientSocketQueue.sockets.pop();
    markRuntimeConnectionDequeued(clientSocketQueue.requestQueueMetrics);
    return queuedClientSocket;
}

static void runRuntimeWorkerLoop(RuntimeWorker runtimeWorker, ClientSocketQueue &clientSocketQueue) {
    while (true) {
        QueuedClientSocket queuedClientSocket = waitClientSocket(clientSocketQueue);
        handleClient(*runtimeWorker.runtimeState, queuedClientSocket.clientSocket, &clientSocketQueue.requestQueueMetrics, queuedClientSocket.queuedTime);
    }
}

static std::vector<std::thread> startRuntimeWorkerThreads(const std::vector<RuntimeWorker> &runtimeWorkers, ClientSocketQueue &clientSocketQueue) {
    std::vector<std::thread> workerThreads;
    workerThreads.reserve(runtimeWorkers.size());
    for (RuntimeWorker runtimeWorker : runtimeWorkers) {
        workerThreads.emplace_back([runtimeWorker, &clientSocketQueue]() {
            runRuntimeWorkerLoop(runtimeWorker, clientSocketQueue);
        });
    }
    return workerThreads;
}

static void acceptClientSockets(LitevoxSocket serverSocket, ClientSocketQueue &clientSocketQueue) {
    while (true) {
        LitevoxSocket clientSocket = accept(serverSocket, nullptr, nullptr);
        if (!isValidSocket(clientSocket)) {
            continue;
        }
        pushClientSocket(clientSocketQueue, clientSocket);
    }
}

void serveRuntimePool(std::vector<RuntimeWorker> runtimeWorkers, int port) {
    if (runtimeWorkers.empty()) {
        throw std::runtime_error("worker がありません");
    }
    LitevoxSocket serverSocket = openServerSocket(port);
    ClientSocketQueue clientSocketQueue;
    std::vector<std::thread> workerThreads = startRuntimeWorkerThreads(runtimeWorkers, clientSocketQueue);
    std::cout << "litevox listening on http://127.0.0.1:" << port << " workers=" << runtimeWorkers.size() << "\n";
    acceptClientSockets(serverSocket, clientSocketQueue);
}
