static std::string getCliAudioStreamFormatText(AudioStreamFormat audioStreamFormat) {
    if (audioStreamFormat == AudioStreamFormat::Pcm) {
        return "pcm";
    }
    return "wav";
}

static bool isUrlUnreservedByte(unsigned char byteValue) {
    return (byteValue >= 'A' && byteValue <= 'Z')
        || (byteValue >= 'a' && byteValue <= 'z')
        || (byteValue >= '0' && byteValue <= '9')
        || byteValue == '-'
        || byteValue == '.'
        || byteValue == '_'
        || byteValue == '~';
}

static std::string percentEncodeQueryValue(const std::string &plainText) {
    const char *hexDigits = "0123456789ABCDEF";
    std::string encodedText;
    for (unsigned char byteValue : plainText) {
        if (isUrlUnreservedByte(byteValue)) {
            encodedText.push_back(static_cast<char>(byteValue));
        } else {
            encodedText.push_back('%');
            encodedText.push_back(hexDigits[(byteValue >> 4) & 0x0f]);
            encodedText.push_back(hexDigits[byteValue & 0x0f]);
        }
    }
    return encodedText;
}

static std::string normalizeHttpBenchPath(const std::string &httpPath) {
    if (httpPath.empty()) {
        return "/tts";
    }
    if (httpPath.front() == '/') {
        return httpPath;
    }
    return "/" + httpPath;
}

static std::string getHttpBenchPathname(const std::string &httpPath) {
    std::string normalizedPath = normalizeHttpBenchPath(httpPath);
    size_t queryPosition = normalizedPath.find('?');
    if (queryPosition == std::string::npos) {
        return normalizedPath;
    }
    return normalizedPath.substr(0, queryPosition);
}

static bool hasHttpBenchQueryParameter(const std::string &targetPath, const std::string &parameterName) {
    size_t queryStart = targetPath.find('?');
    if (queryStart == std::string::npos) {
        return false;
    }
    size_t position = queryStart + 1;
    while (position <= targetPath.size()) {
        size_t separatorPosition = targetPath.find('&', position);
        std::string pairText = targetPath.substr(position, separatorPosition == std::string::npos ? std::string::npos : separatorPosition - position);
        size_t equalsPosition = pairText.find('=');
        std::string keyText = equalsPosition == std::string::npos ? pairText : pairText.substr(0, equalsPosition);
        if (keyText == parameterName) {
            return true;
        }
        if (separatorPosition == std::string::npos) {
            break;
        }
        position = separatorPosition + 1;
    }
    return false;
}

static std::string createHttpBenchRequestText(const CliOptions &cliOptions, const std::string &method, const std::string &targetPath, bool shouldKeepAlive, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    std::string requestText = method + " " + targetPath + " HTTP/1.1\r\n";
    requestText += "Host: " + cliOptions.httpHost + ":" + std::to_string(cliOptions.port) + "\r\n";
    requestText += "Connection: " + std::string(shouldKeepAlive ? "keep-alive" : "close") + "\r\n";
    if (!contentType.empty()) {
        requestText += "Content-Type: " + contentType + "\r\n";
    }
    if (!bodyBytes.empty()) {
        requestText += "Content-Length: " + std::to_string(bodyBytes.size()) + "\r\n";
    }
    requestText += "\r\n";
    requestText.append(reinterpret_cast<const char *>(bodyBytes.data()), bodyBytes.size());
    return requestText;
}

static std::string createHttpBenchTargetPath(const CliOptions &cliOptions, const std::string &httpPath, const std::string &textValue, uint32_t speakerId) {
    std::string targetPath = normalizeHttpBenchPath(httpPath);
    targetPath += targetPath.find('?') == std::string::npos ? "?" : "&";
    if (!hasHttpBenchQueryParameter(targetPath, "speaker")) {
        targetPath += "speaker=" + std::to_string(speakerId) + "&";
    }
    if (!hasHttpBenchQueryParameter(targetPath, "text")) {
        targetPath += "text=" + percentEncodeQueryValue(textValue) + "&";
    }
    if (!hasHttpBenchQueryParameter(targetPath, "format")) {
        targetPath += "format=" + getCliAudioStreamFormatText(cliOptions.audioStreamFormat) + "&";
    }
    if (targetPath.find("/tts_stream") == 0 && !hasHttpBenchQueryParameter(targetPath, "chunk_samples")) {
        targetPath += "chunk_samples=" + std::to_string(cliOptions.chunkSamples) + "&";
    }
    if (cliOptions.isKana && !hasHttpBenchQueryParameter(targetPath, "is_kana")) {
        targetPath += "is_kana=true&";
    }
    while (!targetPath.empty() && targetPath.back() == '&') {
        targetPath.pop_back();
    }
    return targetPath;
}

static LitevoxSocket openHttpBenchSocket(const std::string &httpHost, int port) {
    initializeSocketRuntime();
    addrinfo addressHints{};
    addressHints.ai_family = AF_UNSPEC;
    addressHints.ai_socktype = SOCK_STREAM;
    std::string portText = std::to_string(port);
    addrinfo *addressList = nullptr;
    int lookupCode = getaddrinfo(httpHost.c_str(), portText.c_str(), &addressHints, &addressList);
    if (lookupCode != 0) {
        throw std::runtime_error("HTTP bench host を解決できません: " + getAddrInfoErrorText(lookupCode));
    }
    for (addrinfo *addressPointer = addressList; addressPointer; addressPointer = addressPointer->ai_next) {
        LitevoxSocket socketDescriptor = socket(addressPointer->ai_family, addressPointer->ai_socktype, addressPointer->ai_protocol);
        if (!isValidSocket(socketDescriptor)) {
            continue;
        }
        if (connect(socketDescriptor, addressPointer->ai_addr, addressPointer->ai_addrlen) == 0) {
            freeaddrinfo(addressList);
            return socketDescriptor;
        }
        closeSocket(socketDescriptor);
    }
    freeaddrinfo(addressList);
    throw std::runtime_error("HTTP bench 接続に失敗しました: " + httpHost + ":" + portText);
}

static void writeHttpBenchRequest(LitevoxSocket socketDescriptor, const std::string &requestText) {
    const char *requestBytes = requestText.data();
    size_t writtenByteCount = 0;
    while (writtenByteCount < requestText.size()) {
        int currentWriteBytes = static_cast<int>(send(socketDescriptor, requestBytes + writtenByteCount, static_cast<int>(requestText.size() - writtenByteCount), 0));
        if (currentWriteBytes <= 0) {
            throw std::runtime_error("HTTP bench request 送信に失敗しました: " + getSocketErrorText());
        }
        writtenByteCount += static_cast<size_t>(currentWriteBytes);
    }
}

static std::map<std::string, std::string> parseHttpBenchHeaderMap(const std::string &headerText) {
    std::map<std::string, std::string> headers;
    std::istringstream headerStream(headerText);
    std::string headerLine;
    std::getline(headerStream, headerLine);
    while (std::getline(headerStream, headerLine)) {
        if (!headerLine.empty() && headerLine.back() == '\r') {
            headerLine.pop_back();
        }
        size_t colonPosition = headerLine.find(':');
        if (colonPosition == std::string::npos) {
            continue;
        }
        headers[lowercaseAscii(trimAscii(headerLine.substr(0, colonPosition)))] = trimAscii(headerLine.substr(colonPosition + 1));
    }
    return headers;
}

static size_t findHttpBenchChunkedResponseEnd(const std::string &responseText, size_t bodyOffset) {
    size_t cursor = bodyOffset;
    while (true) {
        size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
        if (chunkHeaderEnd == std::string::npos) {
            return std::string::npos;
        }
        std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
        size_t semicolonPosition = chunkSizeText.find(';');
        if (semicolonPosition != std::string::npos) {
            chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
        }
        if (chunkSizeText.empty()) {
            throw std::runtime_error("HTTP chunk size が不正です");
        }
        size_t chunkSize = 0;
        std::stringstream sizeStream;
        sizeStream << std::hex << chunkSizeText;
        sizeStream >> chunkSize;
        if (!sizeStream || !sizeStream.eof()) {
            throw std::runtime_error("HTTP chunk size を読めません");
        }
        size_t chunkDataOffset = chunkHeaderEnd + 2;
        if (chunkSize == 0) {
            if (responseText.size() < chunkDataOffset + 2) {
                return std::string::npos;
            }
            return chunkDataOffset + 2;
        }
        size_t chunkEnd = chunkDataOffset + chunkSize;
        if (responseText.size() < chunkEnd + 2) {
            return std::string::npos;
        }
        if (responseText.compare(chunkEnd, 2, "\r\n") != 0) {
            throw std::runtime_error("HTTP chunk 終端が不正です");
        }
        cursor = chunkEnd + 2;
    }
}

static size_t findHttpBenchCompleteResponseEnd(const std::string &responseText, size_t headerEndPosition, const std::map<std::string, std::string> &headers) {
    size_t bodyOffset = headerEndPosition + 4;
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        return findHttpBenchChunkedResponseEnd(responseText, bodyOffset);
    }
    auto contentLengthIterator = headers.find("content-length");
    if (contentLengthIterator == headers.end()) {
        return std::string::npos;
    }
    size_t contentLength = static_cast<size_t>(std::stoull(contentLengthIterator->second));
    size_t responseEndPosition = bodyOffset + contentLength;
    if (responseText.size() < responseEndPosition) {
        return std::string::npos;
    }
    return responseEndPosition;
}

static std::string readHttpBenchResponseText(LitevoxSocket socketDescriptor, std::string &pendingResponseText, std::chrono::steady_clock::time_point responseStartTime, double *firstResponseMilliseconds, double *firstBodyMilliseconds) {
    char responseBuffer[32768];
    while (true) {
        size_t headerEndPosition = pendingResponseText.find("\r\n\r\n");
        if (headerEndPosition != std::string::npos) {
            std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(pendingResponseText.substr(0, headerEndPosition));
            size_t responseEndPosition = findHttpBenchCompleteResponseEnd(pendingResponseText, headerEndPosition, headers);
            if (responseEndPosition != std::string::npos) {
                std::string responseText = pendingResponseText.substr(0, responseEndPosition);
                pendingResponseText.erase(0, responseEndPosition);
                return responseText;
            }
        }
        int currentReadBytes = static_cast<int>(recv(socketDescriptor, responseBuffer, sizeof(responseBuffer), 0));
        if (currentReadBytes < 0) {
            throw std::runtime_error("HTTP bench response 受信に失敗しました: " + getSocketErrorText());
        }
        if (currentReadBytes == 0) {
            throw std::runtime_error("HTTP bench response が途中で切断されました");
        }
        auto currentReadTime = std::chrono::steady_clock::now();
        if (*firstResponseMilliseconds <= 0.0) {
            *firstResponseMilliseconds = getElapsedMilliseconds(responseStartTime, currentReadTime);
        }
        pendingResponseText.append(responseBuffer, static_cast<size_t>(currentReadBytes));
        if (*firstBodyMilliseconds <= 0.0) {
            size_t bodyHeaderEndPosition = pendingResponseText.find("\r\n\r\n");
            if (bodyHeaderEndPosition != std::string::npos && pendingResponseText.size() > bodyHeaderEndPosition + 4) {
                *firstBodyMilliseconds = getElapsedMilliseconds(responseStartTime, currentReadTime);
            }
        }
    }
}

static int parseHttpBenchStatusCode(const std::string &responseText) {
    size_t firstSpacePosition = responseText.find(' ');
    if (firstSpacePosition == std::string::npos || firstSpacePosition + 4 > responseText.size()) {
        return 0;
    }
    return std::stoi(responseText.substr(firstSpacePosition + 1, 3));
}

static size_t countHttpBenchBodyBytes(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return 0;
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        size_t cursor = headerEndPosition + 4;
        size_t bodyBytes = 0;
        while (true) {
            size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
            if (chunkHeaderEnd == std::string::npos) {
                return bodyBytes;
            }
            std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
            size_t semicolonPosition = chunkSizeText.find(';');
            if (semicolonPosition != std::string::npos) {
                chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
            }
            size_t chunkSize = 0;
            std::stringstream sizeStream;
            sizeStream << std::hex << chunkSizeText;
            sizeStream >> chunkSize;
            if (!sizeStream || !sizeStream.eof()) {
                return bodyBytes;
            }
            if (chunkSize == 0) {
                return bodyBytes;
            }
            bodyBytes += chunkSize;
            cursor = chunkHeaderEnd + 2 + chunkSize + 2;
        }
    }
    auto contentLengthIterator = headers.find("content-length");
    if (contentLengthIterator == headers.end()) {
        return responseText.size() - headerEndPosition - 4;
    }
    return static_cast<size_t>(std::stoull(contentLengthIterator->second));
}

static std::vector<uint8_t> extractHttpBenchBodyBytes(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return {};
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto transferEncodingIterator = headers.find("transfer-encoding");
    if (transferEncodingIterator != headers.end() && lowercaseAscii(transferEncodingIterator->second).find("chunked") != std::string::npos) {
        std::vector<uint8_t> bodyBytes;
        size_t cursor = headerEndPosition + 4;
        while (true) {
            size_t chunkHeaderEnd = responseText.find("\r\n", cursor);
            if (chunkHeaderEnd == std::string::npos) {
                return bodyBytes;
            }
            std::string chunkSizeText = trimAscii(responseText.substr(cursor, chunkHeaderEnd - cursor));
            size_t semicolonPosition = chunkSizeText.find(';');
            if (semicolonPosition != std::string::npos) {
                chunkSizeText = chunkSizeText.substr(0, semicolonPosition);
            }
            size_t chunkSize = 0;
            std::stringstream sizeStream;
            sizeStream << std::hex << chunkSizeText;
            sizeStream >> chunkSize;
            if (!sizeStream || !sizeStream.eof() || chunkSize == 0) {
                return bodyBytes;
            }
            size_t chunkDataOffset = chunkHeaderEnd + 2;
            bodyBytes.insert(bodyBytes.end(), responseText.begin() + static_cast<std::string::difference_type>(chunkDataOffset), responseText.begin() + static_cast<std::string::difference_type>(chunkDataOffset + chunkSize));
            cursor = chunkDataOffset + chunkSize + 2;
        }
    }
    return std::vector<uint8_t>(responseText.begin() + static_cast<std::string::difference_type>(headerEndPosition + 4), responseText.end());
}

static std::string getHttpBenchContentType(const std::string &responseText) {
    size_t headerEndPosition = responseText.find("\r\n\r\n");
    if (headerEndPosition == std::string::npos) {
        return "";
    }
    std::map<std::string, std::string> headers = parseHttpBenchHeaderMap(responseText.substr(0, headerEndPosition));
    auto contentTypeIterator = headers.find("content-type");
    if (contentTypeIterator == headers.end()) {
        return "";
    }
    return lowercaseAscii(contentTypeIterator->second);
}

static HttpBenchResponse requestHttpBenchTarget(const CliOptions &cliOptions, const std::string &targetPath) {
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    try {
        std::string requestText = createHttpBenchRequestText(cliOptions, "GET", targetPath, false, {}, "");
        writeHttpBenchRequest(socketDescriptor, requestText);
        auto responseStartTime = std::chrono::steady_clock::now();
        double firstResponseMilliseconds = 0.0;
        double firstBodyMilliseconds = 0.0;
        std::string pendingResponseText;
        std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
        auto responseEndTime = std::chrono::steady_clock::now();
        closeSocket(socketDescriptor);
        HttpBenchResponse benchResponse;
        benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
        benchResponse.responseBytes = responseText.size();
        benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
        benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
        benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
        benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
        benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
        benchResponse.contentType = getHttpBenchContentType(responseText);
        return benchResponse;
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
}

static HttpBenchResponse requestHttpBenchTargetKeepAlive(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &targetPath) {
    std::string requestText = createHttpBenchRequestText(cliOptions, "GET", targetPath, true, {}, "");
    writeHttpBenchRequest(socketDescriptor, requestText);
    auto responseStartTime = std::chrono::steady_clock::now();
    double firstResponseMilliseconds = 0.0;
    double firstBodyMilliseconds = 0.0;
    std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
    auto responseEndTime = std::chrono::steady_clock::now();
    HttpBenchResponse benchResponse;
    benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
    benchResponse.responseBytes = responseText.size();
    benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
    benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
    benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
    benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
    benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
    benchResponse.contentType = getHttpBenchContentType(responseText);
    return benchResponse;
}

static HttpBenchResponse requestHttpBenchPost(const CliOptions &cliOptions, const std::string &targetPath, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    LitevoxSocket socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
    try {
        std::string requestText = createHttpBenchRequestText(cliOptions, "POST", targetPath, false, bodyBytes, contentType);
        writeHttpBenchRequest(socketDescriptor, requestText);
        auto responseStartTime = std::chrono::steady_clock::now();
        double firstResponseMilliseconds = 0.0;
        double firstBodyMilliseconds = 0.0;
        std::string pendingResponseText;
        std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
        auto responseEndTime = std::chrono::steady_clock::now();
        closeSocket(socketDescriptor);
        HttpBenchResponse benchResponse;
        benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
        benchResponse.responseBytes = responseText.size();
        benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
        benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
        benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
        benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
        benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
        benchResponse.contentType = getHttpBenchContentType(responseText);
        return benchResponse;
    } catch (...) {
        closeSocket(socketDescriptor);
        throw;
    }
}

static HttpBenchResponse requestHttpBenchPostKeepAlive(const CliOptions &cliOptions, LitevoxSocket socketDescriptor, std::string &pendingResponseText, const std::string &targetPath, const std::vector<uint8_t> &bodyBytes, const std::string &contentType) {
    std::string requestText = createHttpBenchRequestText(cliOptions, "POST", targetPath, true, bodyBytes, contentType);
    writeHttpBenchRequest(socketDescriptor, requestText);
    auto responseStartTime = std::chrono::steady_clock::now();
    double firstResponseMilliseconds = 0.0;
    double firstBodyMilliseconds = 0.0;
    std::string responseText = readHttpBenchResponseText(socketDescriptor, pendingResponseText, responseStartTime, &firstResponseMilliseconds, &firstBodyMilliseconds);
    auto responseEndTime = std::chrono::steady_clock::now();
    HttpBenchResponse benchResponse;
    benchResponse.statusCode = parseHttpBenchStatusCode(responseText);
    benchResponse.responseBytes = responseText.size();
    benchResponse.bodyBytes = countHttpBenchBodyBytes(responseText);
    benchResponse.elapsedMilliseconds = getElapsedMilliseconds(responseStartTime, responseEndTime);
    benchResponse.firstResponseMilliseconds = firstResponseMilliseconds;
    benchResponse.firstBodyMilliseconds = firstBodyMilliseconds > 0.0 ? firstBodyMilliseconds : firstResponseMilliseconds;
    benchResponse.bodyBytesData = extractHttpBenchBodyBytes(responseText);
    benchResponse.contentType = getHttpBenchContentType(responseText);
    return benchResponse;
}

static std::string createHttpSongBenchTargetPath(const std::string &httpPath, uint32_t speakerId, AudioStreamFormat audioStreamFormat) {
    std::string targetPath = normalizeHttpBenchPath(httpPath);
    targetPath += targetPath.find('?') == std::string::npos ? "?" : "&";
    if (!hasHttpBenchQueryParameter(targetPath, "speaker")) {
        targetPath += "speaker=" + std::to_string(speakerId) + "&";
    }
    if (targetPath.find("/frame_synthesis") == 0 && !hasHttpBenchQueryParameter(targetPath, "format")) {
        targetPath += "format=" + getCliAudioStreamFormatText(audioStreamFormat) + "&";
    }
    while (!targetPath.empty() && targetPath.back() == '&') {
        targetPath.pop_back();
    }
    return targetPath;
}

static std::string createSongBenchRequestBody(const std::string &scoreText, const std::string &frameAudioQueryText) {
    return std::string("{\"score\":") + scoreText + ",\"frame_audio_query\":" + frameAudioQueryText + "}";
}

static std::string detectHttpSongBenchMode(const CliOptions &cliOptions) {
    try {
        HttpBenchResponse runtimeInfoResponse = requestHttpBenchTarget(cliOptions, "/runtime_info");
        if (runtimeInfoResponse.statusCode != 200) {
            return "unknown";
        }
        std::string runtimeInfoText(runtimeInfoResponse.bodyBytesData.begin(), runtimeInfoResponse.bodyBytesData.end());
        size_t nativeSingTeacherPosition = runtimeInfoText.find("\"native_sing_teacher\":{");
        if (nativeSingTeacherPosition == std::string::npos) {
            return "unknown";
        }
        size_t modeFieldPosition = runtimeInfoText.find("\"mode\":\"", nativeSingTeacherPosition);
        if (modeFieldPosition == std::string::npos) {
            return "unknown";
        }
        modeFieldPosition += std::string("\"mode\":\"").size();
        size_t modeFieldEnd = runtimeInfoText.find('"', modeFieldPosition);
        if (modeFieldEnd == std::string::npos) {
            return "unknown";
        }
        std::string modeValue = runtimeInfoText.substr(modeFieldPosition, modeFieldEnd - modeFieldPosition);
        if (modeValue == "seeded_exported_onnx") {
            return "deterministic";
        }
        if (modeValue == "vv_bin") {
            return "vv-bin";
        }
        return modeValue;
    } catch (...) {
        return "unknown";
    }
}

static SongBenchResult benchmarkSongHttpOperation(const std::string &endpoint, size_t runCount, size_t workerCount, size_t scoreCount, const std::function<HttpBenchResponse(size_t, LitevoxSocket *, std::string &)> &runOperation) {
    if (runCount == 0) {
        throw std::runtime_error("runCount は 1 以上が必要です");
    }
    if (workerCount == 0) {
        throw std::runtime_error("workerCount は 1 以上が必要です");
    }
    SongBenchResult result;
    result.endpoint = endpoint;
    std::vector<double> elapsedValues(runCount, 0.0);
    std::vector<double> firstResponseValues(runCount, 0.0);
    std::vector<double> firstBodyValues(runCount, 0.0);
    std::vector<size_t> bodyByteSizes(runCount, 0);
    std::vector<std::string> sha256ByRun(runCount);
    result.completedRequestsByWorker.assign(workerCount, 0);
    result.averageMillisecondsByWorker.assign(workerCount, 0.0);
    result.averageFirstResponseMillisecondsByWorker.assign(workerCount, 0.0);
    result.averageFirstBodyMillisecondsByWorker.assign(workerCount, 0.0);
    std::atomic<size_t> nextRunIndex{0};
    std::atomic<bool> hasBenchError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<std::thread> benchmarkThreads;
    benchmarkThreads.reserve(workerCount);
    auto benchmarkStartTime = std::chrono::steady_clock::now();
    for (size_t workerIndex = 0; workerIndex < workerCount; workerIndex++) {
        benchmarkThreads.emplace_back([&, workerIndex]() {
            LitevoxSocket socketDescriptor = static_cast<LitevoxSocket>(-1);
            std::string pendingResponseText;
            size_t workerCompletedRequests = 0;
            double workerTotalElapsedMilliseconds = 0.0;
            double workerTotalFirstResponseMilliseconds = 0.0;
            double workerTotalFirstBodyMilliseconds = 0.0;
            try {
                while (!hasBenchError.load()) {
                    size_t runIndex = nextRunIndex.fetch_add(1);
                    if (runIndex >= runCount) {
                        break;
                    }
                    HttpBenchResponse benchResponse = runOperation(runIndex, &socketDescriptor, pendingResponseText);
                    if (benchResponse.statusCode != 200) {
                        throw std::runtime_error(endpoint + " が失敗しました: status=" + std::to_string(benchResponse.statusCode));
                    }
                    elapsedValues[runIndex] = benchResponse.elapsedMilliseconds;
                    firstResponseValues[runIndex] = benchResponse.firstResponseMilliseconds;
                    firstBodyValues[runIndex] = benchResponse.firstBodyMilliseconds;
                    bodyByteSizes[runIndex] = benchResponse.bodyBytesData.size();
                    sha256ByRun[runIndex] = createSha256Hex(benchResponse.bodyBytesData.data(), benchResponse.bodyBytesData.size());
                    workerCompletedRequests++;
                    workerTotalElapsedMilliseconds += benchResponse.elapsedMilliseconds;
                    workerTotalFirstResponseMilliseconds += benchResponse.firstResponseMilliseconds;
                    workerTotalFirstBodyMilliseconds += benchResponse.firstBodyMilliseconds;
                }
            } catch (const std::exception &caughtException) {
                if (!hasBenchError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = caughtException.what();
                }
            } catch (...) {
                if (!hasBenchError.exchange(true)) {
                    std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                    firstErrorMessage = endpoint + " の bench 実行中に不明なエラーが発生しました";
                }
            }
            if (isValidSocket(socketDescriptor)) {
                closeSocket(socketDescriptor);
            }
            result.completedRequestsByWorker[workerIndex] = workerCompletedRequests;
            if (workerCompletedRequests > 0) {
                result.averageMillisecondsByWorker[workerIndex] = workerTotalElapsedMilliseconds / static_cast<double>(workerCompletedRequests);
                result.averageFirstResponseMillisecondsByWorker[workerIndex] = workerTotalFirstResponseMilliseconds / static_cast<double>(workerCompletedRequests);
                result.averageFirstBodyMillisecondsByWorker[workerIndex] = workerTotalFirstBodyMilliseconds / static_cast<double>(workerCompletedRequests);
            }
        });
    }
    for (std::thread &benchmarkThread : benchmarkThreads) {
        benchmarkThread.join();
    }
    if (hasBenchError.load()) {
        throw std::runtime_error(firstErrorMessage);
    }
    auto benchmarkEndTime = std::chrono::steady_clock::now();
    std::set<std::string> sha256Values(sha256ByRun.begin(), sha256ByRun.end());
    std::vector<std::set<std::string>> sha256ValuesByScore(scoreCount);
    for (size_t runIndex = 0; runIndex < runCount; runIndex++) {
        sha256ValuesByScore[runIndex % scoreCount].insert(sha256ByRun[runIndex]);
    }
    result.totalElapsedMilliseconds = getElapsedMilliseconds(benchmarkStartTime, benchmarkEndTime);
    result.firstMilliseconds = elapsedValues.front();
    result.minimumMilliseconds = *std::min_element(elapsedValues.begin(), elapsedValues.end());
    result.maximumMilliseconds = *std::max_element(elapsedValues.begin(), elapsedValues.end());
    result.averageMilliseconds = std::accumulate(elapsedValues.begin(), elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size());
    result.averageFirstResponseMilliseconds = std::accumulate(firstResponseValues.begin(), firstResponseValues.end(), 0.0) / static_cast<double>(firstResponseValues.size());
    result.averageFirstBodyMilliseconds = std::accumulate(firstBodyValues.begin(), firstBodyValues.end(), 0.0) / static_cast<double>(firstBodyValues.size());
    result.throughputPerSecond = result.totalElapsedMilliseconds > 0.0 ? static_cast<double>(runCount) * 1000.0 / result.totalElapsedMilliseconds : 0.0;
    result.activeWorkers = countActiveBenchWorkers(result.completedRequestsByWorker);
    result.bytes = bodyByteSizes.front();
    result.firstSha256 = sha256ByRun.front();
    result.lastSha256 = sha256ByRun.back();
    if (elapsedValues.size() > 1) {
        result.averageWarmMilliseconds = std::accumulate(elapsedValues.begin() + 1, elapsedValues.end(), 0.0) / static_cast<double>(elapsedValues.size() - 1);
    } else {
        result.averageWarmMilliseconds = result.averageMilliseconds;
    }
    result.uniqueShaCount = sha256Values.size();
    result.repeatStatus = "exact";
    for (const std::set<std::string> &scoreShaValues : sha256ValuesByScore) {
        if (scoreShaValues.size() > 1) {
            result.repeatStatus = "different";
            break;
        }
    }
    return result;
}

int runHttpSongBenchCommand(const CliOptions &cliOptions) {
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    std::vector<std::string> scoreTexts = getBenchScoreTexts(cliOptions);
    std::vector<std::string> frameAudioQueryTexts = getBenchFrameAudioQueryTexts(cliOptions, scoreTexts.size());
    std::vector<size_t> scoreCompletedRuns = createRoundRobinCounts(cliOptions.runs, scoreTexts.size());
    std::vector<std::vector<uint8_t>> scoreBytesList;
    scoreBytesList.reserve(scoreTexts.size());
    for (const std::string &scoreText : scoreTexts) {
        scoreBytesList.push_back(makeBodyBytes(scoreText));
    }
    if (cliOptions.frameAudioQueryPath.empty()) {
        for (const std::vector<uint8_t> &scoreBytes : scoreBytesList) {
            HttpBenchResponse frameAudioQueryResponse = requestHttpBenchPost(
                cliOptions,
                createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat),
                scoreBytes,
                "application/json");
            if (frameAudioQueryResponse.statusCode != 200) {
                throw std::runtime_error("sing_frame_audio_query の初期化に失敗しました: status=" + std::to_string(frameAudioQueryResponse.statusCode));
            }
            frameAudioQueryTexts.emplace_back(frameAudioQueryResponse.bodyBytesData.begin(), frameAudioQueryResponse.bodyBytesData.end());
        }
    }
    std::vector<std::vector<uint8_t>> frameAudioQueryBytesList;
    std::vector<std::vector<uint8_t>> pairBodyBytesList;
    frameAudioQueryBytesList.reserve(frameAudioQueryTexts.size());
    pairBodyBytesList.reserve(frameAudioQueryTexts.size());
    for (size_t scoreIndex = 0; scoreIndex < scoreTexts.size(); scoreIndex++) {
        frameAudioQueryBytesList.push_back(makeBodyBytes(frameAudioQueryTexts[scoreIndex]));
        pairBodyBytesList.push_back(makeBodyBytes(createSongBenchRequestBody(scoreTexts[scoreIndex], frameAudioQueryTexts[scoreIndex])));
    }
    std::vector<SongBenchResult> results;
    results.push_back(benchmarkSongHttpOperation("sing_frame_audio_query", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &scoreBytes = scoreBytesList[runIndex % scoreBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat), scoreBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_audio_query", cliOptions.speaker, cliOptions.audioStreamFormat), scoreBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("sing_frame_f0", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &pairBodyBytes = pairBodyBytesList[runIndex % pairBodyBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_f0", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_f0", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("sing_frame_volume", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &pairBodyBytes = pairBodyBytesList[runIndex % pairBodyBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/sing_frame_volume", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/sing_frame_volume", cliOptions.speaker, cliOptions.audioStreamFormat), pairBodyBytes, "application/json");
    }));
    results.push_back(benchmarkSongHttpOperation("frame_synthesis", cliOptions.runs, cliOptions.workers, scoreTexts.size(), [&](size_t runIndex, LitevoxSocket *socketDescriptor, std::string &pendingResponseText) {
        const std::vector<uint8_t> &frameAudioQueryBytes = frameAudioQueryBytesList[runIndex % frameAudioQueryBytesList.size()];
        if (cliOptions.httpKeepAlive) {
            if (!isValidSocket(*socketDescriptor)) {
                *socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                pendingResponseText.clear();
            }
            return requestHttpBenchPostKeepAlive(cliOptions, *socketDescriptor, pendingResponseText, createHttpSongBenchTargetPath("/frame_synthesis", cliOptions.speaker, cliOptions.audioStreamFormat), frameAudioQueryBytes, "application/json");
        }
        return requestHttpBenchPost(cliOptions, createHttpSongBenchTargetPath("/frame_synthesis", cliOptions.speaker, cliOptions.audioStreamFormat), frameAudioQueryBytes, "application/json");
    }));
    std::cout << "mode\tendpoint\truns\tworkers\tkeep_alive\tscore_mode\tscore_count\tscore_utf8_bytes\tscore_completed_requests\telapsed_ms\tthroughput_rps\tfirst_ms\tavg_ms\tavg_warm_ms\tmin_ms\tmax_ms\tavg_first_response_ms\tavg_first_body_ms\tactive_workers\tworker_completed_requests\tworker_avg_ms\tworker_avg_first_response_ms\tworker_avg_first_body_ms\tbytes\trepeat_status\tunique_sha_count\tsha256_first\tsha256_last\n";
    std::cout << std::fixed << std::setprecision(3);
    std::string modeText = detectHttpSongBenchMode(cliOptions);
    for (const SongBenchResult &result : results) {
        std::cout << modeText << "\t"
                  << result.endpoint << "\t"
                  << cliOptions.runs << "\t"
                  << cliOptions.workers << "\t"
                  << (cliOptions.httpKeepAlive ? "yes" : "no") << "\t"
                  << getRoundRobinModeText(scoreTexts.size()) << "\t"
                  << scoreTexts.size() << "\t"
                  << joinTextByteLengths(scoreTexts) << "\t"
                  << joinSizeValues(scoreCompletedRuns) << "\t"
                  << result.totalElapsedMilliseconds << "\t"
                  << result.throughputPerSecond << "\t"
                  << result.firstMilliseconds << "\t"
                  << result.averageMilliseconds << "\t"
                  << result.averageWarmMilliseconds << "\t"
                  << result.minimumMilliseconds << "\t"
                  << result.maximumMilliseconds << "\t"
                  << result.averageFirstResponseMilliseconds << "\t"
                  << result.averageFirstBodyMilliseconds << "\t"
                  << result.activeWorkers << "\t"
                  << joinSizeValues(result.completedRequestsByWorker) << "\t"
                  << joinDoubleValues(result.averageMillisecondsByWorker) << "\t"
                  << joinDoubleValues(result.averageFirstResponseMillisecondsByWorker) << "\t"
                  << joinDoubleValues(result.averageFirstBodyMillisecondsByWorker) << "\t"
                  << result.bytes << "\t"
                  << result.repeatStatus << "\t"
                  << result.uniqueShaCount << "\t"
                  << result.firstSha256 << "\t"
                  << result.lastSha256 << "\n";
    }
    return 0;
}

int runHttpBenchCommand(const CliOptions &cliOptions) {
    std::vector<std::string> benchTexts = getBenchTexts(cliOptions);
    if (benchTexts.empty() || benchTexts.front().empty()) {
        throw std::runtime_error("--text が必要です");
    }
    if (cliOptions.port <= 0) {
        throw std::runtime_error("--port は 1 以上が必要です");
    }
    std::vector<uint32_t> benchSpeakerIds = getBenchSpeakerIds(cliOptions);
    std::vector<std::string> benchHttpPaths = getBenchHttpPaths(cliOptions);
    std::atomic<size_t> nextRunIndex{0};
    std::atomic<size_t> completedRequestCount{0};
    std::atomic<size_t> failedRequestCount{0};
    std::atomic<size_t> bodyBytesSize{0};
    std::atomic<uint64_t> totalBodyBytesSize{0};
    std::atomic<bool> hasBenchError{false};
    std::string firstErrorMessage;
    std::mutex firstErrorMessageMutex;
    std::vector<size_t> completedRequestsByWorker(cliOptions.workers, 0);
    std::vector<size_t> failedRequestsByWorker(cliOptions.workers, 0);
    std::vector<uint64_t> totalBodyBytesByWorker(cliOptions.workers, 0);
    std::vector<size_t> completedRequestsBySpeaker(benchSpeakerIds.size(), 0);
    std::vector<size_t> failedRequestsBySpeaker(benchSpeakerIds.size(), 0);
    std::vector<size_t> completedRequestsByText(benchTexts.size(), 0);
    std::vector<size_t> failedRequestsByText(benchTexts.size(), 0);
    std::vector<size_t> completedRequestsByPath(benchHttpPaths.size(), 0);
    std::vector<size_t> failedRequestsByPath(benchHttpPaths.size(), 0);
    std::vector<size_t> completedRequestsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size()), 0);
    std::vector<size_t> failedRequestsByCombination(getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size()), 0);
    std::mutex speakerCountMutex;
    std::vector<double> totalElapsedMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> totalFirstResponseMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> totalFirstBodyMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<std::thread> benchThreads;
    benchThreads.reserve(cliOptions.workers);
    auto requestStartTime = std::chrono::steady_clock::now();
    for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
        benchThreads.emplace_back([&, workerIndex]() {
            size_t workerCompletedRequests = 0;
            size_t workerFailedRequests = 0;
            uint64_t workerTotalBodyBytes = 0;
            LitevoxSocket socketDescriptor = static_cast<LitevoxSocket>(-1);
            std::string pendingResponseText;
            std::vector<size_t> workerCompletedRequestsBySpeaker(benchSpeakerIds.size(), 0);
            std::vector<size_t> workerFailedRequestsBySpeaker(benchSpeakerIds.size(), 0);
            std::vector<size_t> workerCompletedRequestsByText(benchTexts.size(), 0);
            std::vector<size_t> workerFailedRequestsByText(benchTexts.size(), 0);
            std::vector<size_t> workerCompletedRequestsByPath(benchHttpPaths.size(), 0);
            std::vector<size_t> workerFailedRequestsByPath(benchHttpPaths.size(), 0);
            std::vector<size_t> workerCompletedRequestsByCombination(completedRequestsByCombination.size(), 0);
            std::vector<size_t> workerFailedRequestsByCombination(failedRequestsByCombination.size(), 0);
            double workerTotalElapsedMilliseconds = 0.0;
            double workerTotalFirstResponseMilliseconds = 0.0;
            double workerTotalFirstBodyMilliseconds = 0.0;
            while (true) {
                size_t runIndex = nextRunIndex.fetch_add(1);
                if (runIndex >= cliOptions.runs) {
                    break;
                }
                BenchCaseSelection selection = getBenchCaseSelection(runIndex, benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size());
                std::string targetPath = createHttpBenchTargetPath(cliOptions, benchHttpPaths[selection.pathIndex], benchTexts[selection.textIndex], benchSpeakerIds[selection.speakerIndex]);
                try {
                    HttpBenchResponse benchResponse;
                    if (cliOptions.httpKeepAlive) {
                        if (!isValidSocket(socketDescriptor)) {
                            socketDescriptor = openHttpBenchSocket(cliOptions.httpHost, cliOptions.port);
                            pendingResponseText.clear();
                        }
                        benchResponse = requestHttpBenchTargetKeepAlive(cliOptions, socketDescriptor, pendingResponseText, targetPath);
                    } else {
                        benchResponse = requestHttpBenchTarget(cliOptions, targetPath);
                    }
                    if (benchResponse.statusCode == 200) {
                        completedRequestCount.fetch_add(1);
                        bodyBytesSize.store(benchResponse.bodyBytes);
                        totalBodyBytesSize.fetch_add(static_cast<uint64_t>(benchResponse.bodyBytes));
                        workerCompletedRequests++;
                        workerTotalBodyBytes += static_cast<uint64_t>(benchResponse.bodyBytes);
                        workerTotalElapsedMilliseconds += benchResponse.elapsedMilliseconds;
                        workerTotalFirstResponseMilliseconds += benchResponse.firstResponseMilliseconds;
                        workerTotalFirstBodyMilliseconds += benchResponse.firstBodyMilliseconds;
                        workerCompletedRequestsBySpeaker[selection.speakerIndex]++;
                        workerCompletedRequestsByText[selection.textIndex]++;
                        workerCompletedRequestsByPath[selection.pathIndex]++;
                        workerCompletedRequestsByCombination[selection.combinationIndex]++;
                    } else {
                        failedRequestCount.fetch_add(1);
                        workerFailedRequests++;
                        workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                        workerFailedRequestsByText[selection.textIndex]++;
                        workerFailedRequestsByPath[selection.pathIndex]++;
                        workerFailedRequestsByCombination[selection.combinationIndex]++;
                    }
                } catch (const std::exception &caughtException) {
                    if (isValidSocket(socketDescriptor)) {
                        closeSocket(socketDescriptor);
                        socketDescriptor = static_cast<LitevoxSocket>(-1);
                    }
                    pendingResponseText.clear();
                    failedRequestCount.fetch_add(1);
                    workerFailedRequests++;
                    workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                    workerFailedRequestsByText[selection.textIndex]++;
                    workerFailedRequestsByPath[selection.pathIndex]++;
                    workerFailedRequestsByCombination[selection.combinationIndex]++;
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = caughtException.what();
                    }
                } catch (...) {
                    if (isValidSocket(socketDescriptor)) {
                        closeSocket(socketDescriptor);
                        socketDescriptor = static_cast<LitevoxSocket>(-1);
                    }
                    pendingResponseText.clear();
                    failedRequestCount.fetch_add(1);
                    workerFailedRequests++;
                    workerFailedRequestsBySpeaker[selection.speakerIndex]++;
                    workerFailedRequestsByText[selection.textIndex]++;
                    workerFailedRequestsByPath[selection.pathIndex]++;
                    workerFailedRequestsByCombination[selection.combinationIndex]++;
                    if (!hasBenchError.exchange(true)) {
                        std::lock_guard<std::mutex> firstErrorLock(firstErrorMessageMutex);
                        firstErrorMessage = "HTTP bench 実行中に不明なエラーが発生しました";
                    }
                }
            }
            if (isValidSocket(socketDescriptor)) {
                closeSocket(socketDescriptor);
            }
            completedRequestsByWorker[workerIndex] = workerCompletedRequests;
            failedRequestsByWorker[workerIndex] = workerFailedRequests;
            totalBodyBytesByWorker[workerIndex] = workerTotalBodyBytes;
            totalElapsedMillisecondsByWorker[workerIndex] = workerTotalElapsedMilliseconds;
            totalFirstResponseMillisecondsByWorker[workerIndex] = workerTotalFirstResponseMilliseconds;
            totalFirstBodyMillisecondsByWorker[workerIndex] = workerTotalFirstBodyMilliseconds;
            std::lock_guard<std::mutex> speakerCountLock(speakerCountMutex);
            for (size_t speakerIndex = 0; speakerIndex < benchSpeakerIds.size(); speakerIndex++) {
                completedRequestsBySpeaker[speakerIndex] += workerCompletedRequestsBySpeaker[speakerIndex];
                failedRequestsBySpeaker[speakerIndex] += workerFailedRequestsBySpeaker[speakerIndex];
            }
            for (size_t textIndex = 0; textIndex < benchTexts.size(); textIndex++) {
                completedRequestsByText[textIndex] += workerCompletedRequestsByText[textIndex];
                failedRequestsByText[textIndex] += workerFailedRequestsByText[textIndex];
            }
            for (size_t pathIndex = 0; pathIndex < benchHttpPaths.size(); pathIndex++) {
                completedRequestsByPath[pathIndex] += workerCompletedRequestsByPath[pathIndex];
                failedRequestsByPath[pathIndex] += workerFailedRequestsByPath[pathIndex];
            }
            for (size_t combinationIndex = 0; combinationIndex < completedRequestsByCombination.size(); combinationIndex++) {
                completedRequestsByCombination[combinationIndex] += workerCompletedRequestsByCombination[combinationIndex];
                failedRequestsByCombination[combinationIndex] += workerFailedRequestsByCombination[combinationIndex];
            }
        });
    }
    for (std::thread &benchThread : benchThreads) {
        benchThread.join();
    }
    auto requestEndTime = std::chrono::steady_clock::now();
    size_t completedRequests = completedRequestCount.load();
    size_t failedRequests = failedRequestCount.load();
    if (completedRequests == 0 && failedRequests > 0 && !firstErrorMessage.empty()) {
        throw std::runtime_error(firstErrorMessage);
    }
    double requestMilliseconds = getElapsedMilliseconds(requestStartTime, requestEndTime);
    double requestThroughputPerSecond = requestMilliseconds > 0.0 ? static_cast<double>(completedRequests + failedRequests) * 1000.0 / requestMilliseconds : 0.0;
    double completedThroughputPerSecond = requestMilliseconds > 0.0 ? static_cast<double>(completedRequests) * 1000.0 / requestMilliseconds : 0.0;
    double totalElapsedMilliseconds = 0.0;
    double totalFirstResponseMilliseconds = 0.0;
    double totalFirstBodyMilliseconds = 0.0;
    std::vector<double> averageElapsedMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> averageFirstResponseMillisecondsByWorker(cliOptions.workers, 0.0);
    std::vector<double> averageFirstBodyMillisecondsByWorker(cliOptions.workers, 0.0);
    for (size_t workerIndex = 0; workerIndex < cliOptions.workers; workerIndex++) {
        totalElapsedMilliseconds += totalElapsedMillisecondsByWorker[workerIndex];
        totalFirstResponseMilliseconds += totalFirstResponseMillisecondsByWorker[workerIndex];
        totalFirstBodyMilliseconds += totalFirstBodyMillisecondsByWorker[workerIndex];
        if (completedRequestsByWorker[workerIndex] == 0) {
            continue;
        }
        double completedRequestsPerWorker = static_cast<double>(completedRequestsByWorker[workerIndex]);
        averageElapsedMillisecondsByWorker[workerIndex] = totalElapsedMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
        averageFirstResponseMillisecondsByWorker[workerIndex] = totalFirstResponseMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
        averageFirstBodyMillisecondsByWorker[workerIndex] = totalFirstBodyMillisecondsByWorker[workerIndex] / completedRequestsPerWorker;
    }
    double averageElapsedMilliseconds = completedRequests > 0 ? totalElapsedMilliseconds / static_cast<double>(completedRequests) : 0.0;
    double averageFirstResponseMilliseconds = completedRequests > 0 ? totalFirstResponseMilliseconds / static_cast<double>(completedRequests) : 0.0;
    double averageFirstBodyMilliseconds = completedRequests > 0 ? totalFirstBodyMilliseconds / static_cast<double>(completedRequests) : 0.0;
    size_t combinationCycle = getCombinationCycleSize(benchSpeakerIds.size(), benchTexts.size(), benchHttpPaths.size());
    std::string targetPath = benchHttpPaths.size() > 1 || benchTexts.size() > 1
        ? "(mixed)"
        : createHttpBenchTargetPath(cliOptions, benchHttpPaths.front(), benchTexts.front(), benchSpeakerIds.front());
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "host\t" << cliOptions.httpHost << "\n";
    std::cout << "port\t" << cliOptions.port << "\n";
    std::cout << "path\t" << targetPath << "\n";
    std::cout << "elapsed_ms\t" << requestMilliseconds << "\n";
    std::cout << "runs\t" << cliOptions.runs << "\n";
    std::cout << "workers\t" << cliOptions.workers << "\n";
    std::cout << "workload_mode\t" << getCartesianModeText(combinationCycle) << "\n";
    std::cout << "combination_cycle\t" << combinationCycle << "\n";
    std::cout << "speaker_mode\t" << getRoundRobinModeText(benchSpeakerIds.size()) << "\n";
    std::cout << "speakers\t" << joinSpeakerIds(benchSpeakerIds) << "\n";
    std::cout << "text_mode\t" << getRoundRobinModeText(benchTexts.size()) << "\n";
    std::cout << "text_count\t" << benchTexts.size() << "\n";
    std::cout << "text_utf8_bytes\t" << joinTextByteLengths(benchTexts) << "\n";
    std::cout << "http_path_mode\t" << getRoundRobinModeText(benchHttpPaths.size()) << "\n";
    std::cout << "http_paths\t" << joinStringValues(benchHttpPaths) << "\n";
    std::cout << "keep_alive\t" << (cliOptions.httpKeepAlive ? "yes" : "no") << "\n";
    std::cout << "completed_requests\t" << completedRequests << "\n";
    std::cout << "failed_requests\t" << failedRequests << "\n";
    std::cout << "speaker_completed_requests\t" << joinSpeakerCounts(benchSpeakerIds, completedRequestsBySpeaker) << "\n";
    std::cout << "speaker_failed_requests\t" << joinSpeakerCounts(benchSpeakerIds, failedRequestsBySpeaker) << "\n";
    std::cout << "text_completed_requests\t" << joinSizeValues(completedRequestsByText) << "\n";
    std::cout << "text_failed_requests\t" << joinSizeValues(failedRequestsByText) << "\n";
    std::cout << "path_completed_requests\t" << joinStringCounts(benchHttpPaths, completedRequestsByPath) << "\n";
    std::cout << "path_failed_requests\t" << joinStringCounts(benchHttpPaths, failedRequestsByPath) << "\n";
    std::cout << "combination_completed_requests\t" << joinCombinationCounts(benchSpeakerIds, benchTexts.size(), benchHttpPaths.size(), completedRequestsByCombination) << "\n";
    std::cout << "combination_failed_requests\t" << joinCombinationCounts(benchSpeakerIds, benchTexts.size(), benchHttpPaths.size(), failedRequestsByCombination) << "\n";
    std::cout << "body_bytes\t" << bodyBytesSize.load() << "\n";
    std::cout << "total_body_bytes\t" << totalBodyBytesSize.load() << "\n";
    std::cout << "request_rps\t" << requestThroughputPerSecond << "\n";
    std::cout << "completed_rps\t" << completedThroughputPerSecond << "\n";
    std::cout << "avg_request_ms\t" << averageElapsedMilliseconds << "\n";
    std::cout << "avg_first_response_ms\t" << averageFirstResponseMilliseconds << "\n";
    std::cout << "avg_first_body_ms\t" << averageFirstBodyMilliseconds << "\n";
    std::cout << "active_workers\t" << countActiveBenchWorkers(completedRequestsByWorker) << "\n";
    std::cout << "worker_completed_requests\t" << joinSizeValues(completedRequestsByWorker) << "\n";
    std::cout << "worker_failed_requests\t" << joinSizeValues(failedRequestsByWorker) << "\n";
    std::cout << "worker_total_body_bytes\t" << joinUint64Values(totalBodyBytesByWorker) << "\n";
    std::cout << "worker_avg_request_ms\t" << joinDoubleValues(averageElapsedMillisecondsByWorker) << "\n";
    std::cout << "worker_avg_first_response_ms\t" << joinDoubleValues(averageFirstResponseMillisecondsByWorker) << "\n";
    std::cout << "worker_avg_first_body_ms\t" << joinDoubleValues(averageFirstBodyMillisecondsByWorker) << "\n";
    std::cout << "max_rss_bytes\t" << getPeakResidentBytes() << "\n";
    return 0;
}
