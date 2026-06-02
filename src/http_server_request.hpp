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
