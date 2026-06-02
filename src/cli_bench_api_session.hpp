static std::string chooseApiSessionFileExtension(const std::string &contentType, AudioStreamFormat audioStreamFormat) {
    if (contentType.find("application/json") != std::string::npos) {
        return ".json";
    }
    if (contentType.find("audio/wav") != std::string::npos || contentType.find("audio/wave") != std::string::npos) {
        return ".wav";
    }
    if (contentType.find("audio/pcm") != std::string::npos || contentType.find("application/octet-stream") != std::string::npos) {
        return audioStreamFormat == AudioStreamFormat::Pcm ? ".pcm" : ".wav";
    }
    return ".bin";
}

struct ApiSessionRequest {
    std::string method = "GET";
    std::string targetPath;
    std::vector<uint8_t> bodyBytes;
    std::string contentType;
};

static std::string readApiSessionInputText(const std::string &lineText) {
    if (lineText.size() > 1 && lineText.front() == '@') {
        return readTextFile(lineText.substr(1));
    }
    return lineText;
}

static std::vector<std::string> splitApiSessionInputFields(const std::string &lineText) {
    std::vector<std::string> fields;
    size_t fieldStart = 0;
    while (fieldStart <= lineText.size()) {
        size_t separatorPosition = lineText.find('\t', fieldStart);
        if (separatorPosition == std::string::npos) {
            fields.push_back(trimAscii(lineText.substr(fieldStart)));
            break;
        }
        fields.push_back(trimAscii(lineText.substr(fieldStart, separatorPosition - fieldStart)));
        fieldStart = separatorPosition + 1;
    }
    return fields;
}

static std::string joinApiSessionInputFields(const std::vector<std::string> &inputFields) {
    std::ostringstream textStream;
    for (size_t fieldIndex = 0; fieldIndex < inputFields.size(); fieldIndex++) {
        if (fieldIndex > 0) {
            textStream << '\t';
        }
        textStream << inputFields[fieldIndex];
    }
    return textStream.str();
}

static ApiSessionLineSpec parseApiSessionLineSpec(const CliOptions &cliOptions, const std::string &lineText) {
    ApiSessionLineSpec lineSpec;
    std::vector<std::string> inputFields = splitApiSessionInputFields(lineText);
    if (!inputFields.empty() && !inputFields.front().empty() && inputFields.front().front() == '/') {
        lineSpec.httpPath = inputFields.front();
        lineSpec.inputFields.assign(inputFields.begin() + 1, inputFields.end());
    } else {
        lineSpec.httpPath = cliOptions.httpPath;
        lineSpec.inputFields = std::move(inputFields);
    }
    return lineSpec;
}

static bool isSongApiSessionPath(const std::string &httpPath) {
    std::string normalizedPath = getHttpBenchPathname(httpPath);
    return normalizedPath == "/sing_frame_audio_query"
        || normalizedPath == "/sing_frame_f0"
        || normalizedPath == "/sing_frame_volume"
        || normalizedPath == "/frame_synthesis";
}

static ApiSessionRequest createApiSessionRequest(const CliOptions &cliOptions, const ApiSessionLineSpec &lineSpec) {
    ApiSessionRequest request;
    if (isSongApiSessionPath(lineSpec.httpPath)) {
        std::string normalizedPath = getHttpBenchPathname(lineSpec.httpPath);
        request.method = "POST";
        request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
        if ((normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") && lineSpec.inputFields.size() == 2) {
            request.bodyBytes = makeBodyBytes(createSongBenchRequestBody(readApiSessionInputText(lineSpec.inputFields[0]), readApiSessionInputText(lineSpec.inputFields[1])));
        } else {
            if ((normalizedPath == "/sing_frame_audio_query" || normalizedPath == "/frame_synthesis") && lineSpec.inputFields.size() > 1) {
                throw std::runtime_error("song api-session の入力列数が不正です");
            }
            request.bodyBytes = makeBodyBytes(readApiSessionInputText(joinApiSessionInputFields(lineSpec.inputFields)));
        }
        request.contentType = "application/json";
        return request;
    }
    std::string textValue = readApiSessionInputText(joinApiSessionInputFields(lineSpec.inputFields));
    request.targetPath = createHttpBenchTargetPath(cliOptions, lineSpec.httpPath, textValue, cliOptions.speaker);
    return request;
}

static bool isApiSessionSongScoreText(const std::string &jsonText) {
    return !extractJsonArrayField(jsonText, "notes").empty();
}

static bool isApiSessionFrameAudioQueryText(const std::string &jsonText) {
    return !extractJsonArrayField(jsonText, "phonemes").empty();
}

static bool hasApiSessionJsonField(const std::string &jsonText, const std::string &fieldName) {
    return findJsonFieldValuePosition(jsonText, fieldName) != std::string::npos;
}

static HttpBenchResponse executeApiSessionRequest(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const ApiSessionRequest &request) {
    return request.method == "POST"
        ? requestHttpBenchPostKeepAlive(cliOptions, socketDescriptor, pendingResponseText, request.targetPath, request.bodyBytes, request.contentType)
        : requestHttpBenchTargetKeepAlive(cliOptions, socketDescriptor, pendingResponseText, request.targetPath);
}

static std::string requestApiSessionSongFrameAudioQuery(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &scoreText, double &elapsedMilliseconds, double &firstResponseMilliseconds, double &firstBodyMilliseconds) {
    ApiSessionRequest request;
    request.method = "POST";
    request.targetPath = createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat);
    request.bodyBytes = makeBodyBytes(scoreText);
    request.contentType = "application/json";
    HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
    if (response.statusCode != 200) {
        throw std::runtime_error("song api-session の sing_frame_audio_query が失敗しました: status=" + std::to_string(response.statusCode));
    }
    elapsedMilliseconds += response.elapsedMilliseconds;
    firstResponseMilliseconds += response.firstResponseMilliseconds;
    firstBodyMilliseconds += response.firstBodyMilliseconds;
    return std::string(response.bodyBytesData.begin(), response.bodyBytesData.end());
}

static HttpBenchResponse executeApiSessionSongRequest(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const ApiSessionLineSpec &lineSpec) {
    std::string normalizedPath = getHttpBenchPathname(lineSpec.httpPath);
    const std::vector<std::string> &inputFields = lineSpec.inputFields;
    if (normalizedPath == "/sing_frame_audio_query") {
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
    }
    if (normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") {
        if (inputFields.size() > 2) {
            throw std::runtime_error("song api-session の入力列数が不正です");
        }
        if (inputFields.size() == 2) {
            return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
        }
        std::string inputText = readApiSessionInputText(joinApiSessionInputFields(inputFields));
        if (isApiSessionSongScoreText(inputText) && !hasApiSessionJsonField(inputText, "frame_audio_query")) {
            double elapsedMilliseconds = 0.0;
            double firstResponseMilliseconds = 0.0;
            double firstBodyMilliseconds = 0.0;
            std::string frameAudioQueryText = requestApiSessionSongFrameAudioQuery(cliOptions, socketDescriptor, pendingResponseText, inputText, elapsedMilliseconds, firstResponseMilliseconds, firstBodyMilliseconds);
            ApiSessionRequest request;
            request.method = "POST";
            request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
            request.bodyBytes = makeBodyBytes(createSongBenchRequestBody(inputText, frameAudioQueryText));
            request.contentType = "application/json";
            HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
            response.elapsedMilliseconds += elapsedMilliseconds;
            response.firstResponseMilliseconds += firstResponseMilliseconds;
            response.firstBodyMilliseconds += firstBodyMilliseconds;
            return response;
        }
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
    }
    if (normalizedPath == "/frame_synthesis") {
        if (inputFields.size() > 1) {
            throw std::runtime_error("song api-session の入力列数が不正です");
        }
        std::string inputText = readApiSessionInputText(joinApiSessionInputFields(inputFields));
        ApiSessionRequest request;
        request.method = "POST";
        request.targetPath = createHttpSongBenchTargetPath(lineSpec.httpPath, cliOptions.speaker, cliOptions.audioStreamFormat);
        request.contentType = "application/json";
        if (hasApiSessionJsonField(inputText, "frame_audio_query")) {
            request.bodyBytes = makeBodyBytes(extractJsonObjectField(inputText, "frame_audio_query"));
            return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
        }
        if (isApiSessionSongScoreText(inputText) && !isApiSessionFrameAudioQueryText(inputText)) {
            double elapsedMilliseconds = 0.0;
            double firstResponseMilliseconds = 0.0;
            double firstBodyMilliseconds = 0.0;
            std::string frameAudioQueryText = requestApiSessionSongFrameAudioQuery(cliOptions, socketDescriptor, pendingResponseText, inputText, elapsedMilliseconds, firstResponseMilliseconds, firstBodyMilliseconds);
            request.bodyBytes = makeBodyBytes(frameAudioQueryText);
            HttpBenchResponse response = executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
            response.elapsedMilliseconds += elapsedMilliseconds;
            response.firstResponseMilliseconds += firstResponseMilliseconds;
            response.firstBodyMilliseconds += firstBodyMilliseconds;
            return response;
        }
        request.bodyBytes = makeBodyBytes(inputText);
        return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, request);
    }
    return executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
}

static std::vector<std::string> getOptionalSongScoreTexts(const CliOptions &cliOptions) {
    if (cliOptions.scorePath.empty()) {
        if (!cliOptions.benchScorePaths.empty()) {
            throw std::runtime_error("--add-score の前に --score が必要です");
        }
        return {};
    }
    return getBenchScoreTexts(cliOptions);
}

static std::vector<std::string> createApiSessionDefaultSongBodies(const CliOptions &cliOptions) {
    std::string normalizedPath = getHttpBenchPathname(cliOptions.httpPath);
    std::vector<std::string> scoreTexts = getOptionalSongScoreTexts(cliOptions);
    std::vector<std::string> frameAudioQueryTexts = getBenchFrameAudioQueryTexts(cliOptions, scoreTexts.empty() ? 1 : scoreTexts.size());
    if (normalizedPath == "/sing_frame_audio_query") {
        return scoreTexts;
    }
    if (normalizedPath == "/sing_frame_f0" || normalizedPath == "/sing_frame_volume") {
        if (scoreTexts.empty()) {
            return {};
        }
        if (!frameAudioQueryTexts.empty()) {
            std::vector<std::string> requestBodies;
            requestBodies.reserve(scoreTexts.size());
            for (size_t scoreIndex = 0; scoreIndex < scoreTexts.size(); scoreIndex++) {
                requestBodies.push_back(scoreTexts[scoreIndex] + "\t" + frameAudioQueryTexts[scoreIndex]);
            }
            return requestBodies;
        }
        return scoreTexts;
    }
    if (normalizedPath == "/frame_synthesis") {
        if (!frameAudioQueryTexts.empty()) {
            return frameAudioQueryTexts;
        }
        fs::path frameAudioQueryPath = cliOptions.frameAudioQueryPath.empty() ? cliOptions.audioQueryPath : cliOptions.frameAudioQueryPath;
        if (!frameAudioQueryPath.empty()) {
            return {readTextFile(frameAudioQueryPath)};
        }
        return scoreTexts;
    }
    return {};
}

int runApiSessionCommand(const CliOptions &cliOptions) {
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    fs::path outputDirectory = cliOptions.outputPath;
    if (outputDirectory.empty() || outputDirectory == "-") {
        outputDirectory = "api-session-out";
    }
    fs::create_directories(outputDirectory);
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    std::string pendingResponseText;
    size_t completedCount = 0;
    try {
        std::vector<std::string> inputLines;
        std::string lineText;
        while (std::getline(std::cin, lineText)) {
            std::string textValue = trimAscii(lineText);
            if (textValue.empty()) {
                continue;
            }
            inputLines.push_back(textValue);
        }
        if (inputLines.empty()) {
            std::vector<std::string> defaultSongBodies = createApiSessionDefaultSongBodies(cliOptions);
            for (const std::string &defaultSongBody : defaultSongBodies) {
                if (!defaultSongBody.empty()) {
                    inputLines.push_back(defaultSongBody);
                }
            }
        }
        for (const std::string &inputLine : inputLines) {
            ApiSessionLineSpec lineSpec = parseApiSessionLineSpec(cliOptions, inputLine);
            HttpBenchResponse response = isSongApiSessionPath(lineSpec.httpPath)
                ? executeApiSessionSongRequest(cliOptions, socketDescriptor, pendingResponseText, lineSpec)
                : executeApiSessionRequest(cliOptions, socketDescriptor, pendingResponseText, createApiSessionRequest(cliOptions, lineSpec));
            if (response.statusCode != 200) {
                throw std::runtime_error("api-session request が失敗しました: status=" + std::to_string(response.statusCode));
            }
            std::string extension = chooseApiSessionFileExtension(response.contentType, cliOptions.audioStreamFormat);
            std::ostringstream fileNameStream;
            fileNameStream << std::setw(4) << std::setfill('0') << (completedCount + 1);
            fs::path outputFilePath = outputDirectory / (fileNameStream.str() + extension);
            writeBinaryFile(outputFilePath, response.bodyBytesData);
            std::cout << (completedCount + 1) << "\t" << outputFilePath.string() << "\t" << response.bodyBytesData.size() << "\t" << std::fixed << std::setprecision(3) << response.elapsedMilliseconds << "\t" << response.firstBodyMilliseconds << "\n";
            completedCount++;
        }
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
    closeSocket(socketDescriptor);
    return 0;
}
