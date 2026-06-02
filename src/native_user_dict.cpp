#include "native_user_dict.hpp"

#include "json_utility.hpp"
#include "utility.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

struct NativeUserDictEntry {
    std::string wordUuid;
    std::string wordJson;
};

struct NativePartOfSpeechFields {
    const char *partOfSpeech;
    const char *partOfSpeechDetail1;
    const char *partOfSpeechDetail2;
    const char *partOfSpeechDetail3;
    int contextId;
};

static constexpr VoicevoxUserDictWordType properNounWordType = 0;
static constexpr VoicevoxUserDictWordType commonNounWordType = 1;
static constexpr VoicevoxUserDictWordType verbWordType = 2;
static constexpr VoicevoxUserDictWordType adjectiveWordType = 3;
static constexpr VoicevoxUserDictWordType suffixWordType = 4;

static constexpr int properNounContextId = 1348;
static constexpr int commonNounContextId = 1345;
static constexpr int verbContextId = 642;
static constexpr int adjectiveContextId = 20;
static constexpr int suffixContextId = 1358;
static constexpr uint32_t minUserDictPriority = 0;
static constexpr uint32_t maxUserDictPriority = 10;

static constexpr std::array<int, 11> properNounCosts = {-988, 3488, 4768, 6048, 7328, 8609, 8734, 8859, 8984, 9110, 14176};
static constexpr std::array<int, 11> commonNounCosts = {-4445, 49, 1473, 2897, 4321, 5746, 6554, 7362, 8170, 8979, 15001};
static constexpr std::array<int, 11> verbCosts = {3100, 6160, 6360, 6561, 6761, 6962, 7414, 7866, 8318, 8771, 13433};
static constexpr std::array<int, 11> adjectiveCosts = {1527, 3266, 3561, 3857, 4153, 4449, 5149, 5849, 6549, 7250, 10001};
static constexpr std::array<int, 11> suffixCosts = {4399, 5373, 6041, 6710, 7378, 8047, 9440, 10834, 12228, 13622, 15847};

static constexpr const char *nativeUserDictMoraPatterns[] = {
    "ヴョ", "ヴュ", "ヴャ", "ドゥ", "デョ", "デュ", "デャ", "ディ", "デェ", "トゥ", "テョ", "テュ", "テャ", "ティ",
    "グヮ", "クヮ", "リョ", "リュ", "リャ", "リェ", "ミョ", "ミュ", "ミャ", "ミェ", "ピョ", "ピュ", "ピャ", "ピェ",
    "ビョ", "ビュ", "ビャ", "ビェ", "ヒョ", "ヒュ", "ヒャ", "ヒェ", "ニョ", "ニュ", "ニャ", "ニェ", "チョ", "チュ",
    "チャ", "チェ", "ジョ", "ジュ", "ジャ", "ジェ", "ショ", "シュ", "シャ", "シェ", "ギョ", "ギュ", "ギャ", "ギェ",
    "キョ", "キュ", "キャ", "キェ", "ヴァ", "ヴォ", "ヴェ", "ヴィ", "ツァ", "ツォ", "ツェ", "ツィ", "ファ", "フォ",
    "フェ", "フィ", "ウィ", "ウォ", "ウェ", "イェ", "スィ", "ズィ"
};

static size_t getNativeUtf8CodepointBytes(unsigned char firstByte);
static size_t countNativeUserDictMoras(const std::string &pronunciation);
static uint32_t requireNativeUserDictNumberField(const std::string &wordJson, const std::string &fieldName);
static std::string requireNativeUserDictStringField(const std::string &wordJson, const std::string &fieldName);

static void appendNativeUtf8Codepoint(std::string &text, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        text.push_back(static_cast<char>(codepoint));
        return;
    }
    if (codepoint <= 0x7ff) {
        text.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        return;
    }
    if (codepoint <= 0xffff) {
        text.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        return;
    }
    text.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
}

static std::string normalizeNativeUserDictSurface(const std::string &surface) {
    std::string normalizedSurface;
    for (size_t position = 0; position < surface.size();) {
        unsigned char firstByte = static_cast<unsigned char>(surface[position]);
        if (firstByte < 0x80) {
            if (firstByte == 0x20) {
                appendNativeUtf8Codepoint(normalizedSurface, 0x3000);
            } else if (firstByte >= 0x21 && firstByte <= 0x7e) {
                appendNativeUtf8Codepoint(normalizedSurface, static_cast<uint32_t>(firstByte) + 0xfee0);
            } else {
                normalizedSurface.push_back(static_cast<char>(firstByte));
            }
            position++;
            continue;
        }
        size_t codepointBytes = getNativeUtf8CodepointBytes(firstByte);
        normalizedSurface.append(surface, position, codepointBytes);
        position += codepointBytes;
    }
    return normalizedSurface;
}

static size_t skipJsonWhitespace(const std::string &jsonText, size_t position) {
    while (position < jsonText.size() && std::isspace(static_cast<unsigned char>(jsonText[position]))) {
        position++;
    }
    return position;
}

static std::string readNativeUserDictJson(const fs::path &userDictPath) {
    if (userDictPath.empty() || !fs::exists(userDictPath)) {
        return "{}";
    }
    std::string userDictJson = trimAscii(readTextFile(userDictPath));
    return userDictJson.empty() ? "{}" : userDictJson;
}

static void ensureNativeUserDictObject(const std::string &userDictJson) {
    std::string trimmedJson = trimAscii(userDictJson);
    if (trimmedJson.size() < 2 || trimmedJson.front() != '{' || trimmedJson.back() != '}') {
        throw std::runtime_error("ユーザー辞書 JSON は object が必要です");
    }
}

static std::vector<NativeUserDictEntry> parseNativeUserDictEntries(const std::string &userDictJson) {
    ensureNativeUserDictObject(userDictJson);
    std::vector<NativeUserDictEntry> entries;
    std::string trimmedJson = trimAscii(userDictJson);
    size_t position = skipJsonWhitespace(trimmedJson, 1);
    while (position < trimmedJson.size() && trimmedJson[position] != '}') {
        if (trimmedJson[position] == ',') {
            position = skipJsonWhitespace(trimmedJson, position + 1);
        }
        if (position >= trimmedJson.size() || trimmedJson[position] != '"') {
            throw std::runtime_error("ユーザー辞書 JSON の UUID key が不正です");
        }
        std::string wordUuid = decodeJsonString(trimmedJson, position);
        size_t keyEndPosition = position + 1;
        bool isEscaped = false;
        while (keyEndPosition < trimmedJson.size()) {
            char keyCharacter = trimmedJson[keyEndPosition];
            if (isEscaped) {
                isEscaped = false;
            } else if (keyCharacter == '\\') {
                isEscaped = true;
            } else if (keyCharacter == '"') {
                keyEndPosition++;
                break;
            }
            keyEndPosition++;
        }
        position = skipJsonWhitespace(trimmedJson, keyEndPosition);
        if (position >= trimmedJson.size() || trimmedJson[position] != ':') {
            throw std::runtime_error("ユーザー辞書 JSON の区切りが不正です");
        }
        position = skipJsonWhitespace(trimmedJson, position + 1);
        if (position >= trimmedJson.size() || trimmedJson[position] != '{') {
            throw std::runtime_error("ユーザー辞書単語 JSON は object が必要です");
        }
        size_t wordClosePosition = findJsonMatchingToken(trimmedJson, position, '{', '}');
        if (wordClosePosition == std::string::npos) {
            throw std::runtime_error("ユーザー辞書単語 JSON が閉じていません");
        }
        entries.push_back({wordUuid, trimmedJson.substr(position, wordClosePosition - position + 1)});
        position = skipJsonWhitespace(trimmedJson, wordClosePosition + 1);
        if (position < trimmedJson.size() && trimmedJson[position] == ',') {
            position = skipJsonWhitespace(trimmedJson, position + 1);
        }
    }
    return entries;
}

static std::string joinNativeUserDictEntries(const std::vector<NativeUserDictEntry> &entries) {
    std::ostringstream userDictStream;
    userDictStream << "{";
    for (size_t entryIndex = 0; entryIndex < entries.size(); entryIndex++) {
        if (entryIndex > 0) {
            userDictStream << ",";
        }
        userDictStream << quoteJsonString(entries[entryIndex].wordUuid) << ":" << entries[entryIndex].wordJson;
    }
    userDictStream << "}";
    return userDictStream.str();
}

static void writeNativeUserDictJson(const fs::path &userDictPath, const std::string &userDictJson) {
    ensureNativeUserDictObject(userDictJson);
    if (!userDictPath.empty()) {
        writeTextFile(userDictPath, userDictJson);
    }
}

static std::string formatNativeUuidBytes(const std::array<uint8_t, 16> &uuidBytes) {
    std::ostringstream uuidStream;
    uuidStream << std::hex << std::setfill('0');
    for (size_t byteIndex = 0; byteIndex < uuidBytes.size(); byteIndex++) {
        if (byteIndex == 4 || byteIndex == 6 || byteIndex == 8 || byteIndex == 10) {
            uuidStream << "-";
        }
        uuidStream << std::setw(2) << static_cast<unsigned int>(uuidBytes[byteIndex]);
    }
    return uuidStream.str();
}

static std::string createNativeUserDictUuid(const std::vector<NativeUserDictEntry> &entries) {
    std::random_device randomDevice;
    std::mt19937_64 randomEngine(randomDevice() ^ static_cast<unsigned long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    while (true) {
        std::array<uint8_t, 16> uuidBytes{};
        for (size_t byteIndex = 0; byteIndex < uuidBytes.size(); byteIndex += 8) {
            uint64_t randomBits = randomEngine();
            for (size_t bitIndex = 0; bitIndex < 8 && byteIndex + bitIndex < uuidBytes.size(); bitIndex++) {
                uuidBytes[byteIndex + bitIndex] = static_cast<uint8_t>((randomBits >> (bitIndex * 8)) & 0xff);
            }
        }
        uuidBytes[6] = static_cast<uint8_t>((uuidBytes[6] & 0x0f) | 0x40);
        uuidBytes[8] = static_cast<uint8_t>((uuidBytes[8] & 0x3f) | 0x80);
        std::string wordUuid = formatNativeUuidBytes(uuidBytes);
        bool hasCollision = std::any_of(entries.begin(), entries.end(), [&wordUuid](const NativeUserDictEntry &entry) {
            return entry.wordUuid == wordUuid;
        });
        if (!hasCollision) {
            return wordUuid;
        }
    }
}

static NativePartOfSpeechFields getNativePartOfSpeechFields(VoicevoxUserDictWordType wordType) {
    if (wordType == properNounWordType) {
        return {"名詞", "固有名詞", "一般", "*", properNounContextId};
    }
    if (wordType == commonNounWordType) {
        return {"名詞", "一般", "*", "*", commonNounContextId};
    }
    if (wordType == verbWordType) {
        return {"動詞", "自立", "*", "*", verbContextId};
    }
    if (wordType == adjectiveWordType) {
        return {"形容詞", "自立", "*", "*", adjectiveContextId};
    }
    if (wordType == suffixWordType) {
        return {"名詞", "接尾", "一般", "*", suffixContextId};
    }
    throw std::runtime_error("未対応のユーザー辞書品詞です: " + std::to_string(wordType));
}

static std::string createNativeUserDictWordJson(const std::string &surface, const std::string &pronunciation, uintptr_t accentType, VoicevoxUserDictWordType wordType, uint32_t priority) {
    NativePartOfSpeechFields partOfSpeechFields = getNativePartOfSpeechFields(wordType);
    std::string normalizedSurface = normalizeNativeUserDictSurface(surface);
    size_t moraCount = countNativeUserDictMoras(pronunciation);
    std::ostringstream wordStream;
    wordStream << "{";
    wordStream << "\"surface\":" << quoteJsonString(normalizedSurface) << ",";
    wordStream << "\"priority\":" << priority << ",";
    wordStream << "\"context_id\":" << partOfSpeechFields.contextId << ",";
    wordStream << "\"part_of_speech\":" << quoteJsonString(partOfSpeechFields.partOfSpeech) << ",";
    wordStream << "\"part_of_speech_detail_1\":" << quoteJsonString(partOfSpeechFields.partOfSpeechDetail1) << ",";
    wordStream << "\"part_of_speech_detail_2\":" << quoteJsonString(partOfSpeechFields.partOfSpeechDetail2) << ",";
    wordStream << "\"part_of_speech_detail_3\":" << quoteJsonString(partOfSpeechFields.partOfSpeechDetail3) << ",";
    wordStream << "\"inflectional_type\":\"*\",";
    wordStream << "\"inflectional_form\":\"*\",";
    wordStream << "\"stem\":\"*\",";
    wordStream << "\"yomi\":" << quoteJsonString(pronunciation) << ",";
    wordStream << "\"pronunciation\":" << quoteJsonString(pronunciation) << ",";
    wordStream << "\"accent_type\":" << accentType << ",";
    wordStream << "\"mora_count\":" << moraCount << ",";
    wordStream << "\"accent_associative_rule\":\"*\"";
    wordStream << "}";
    return wordStream.str();
}

static std::string normalizeNativeUserDictWordJson(const std::string &wordJson) {
    std::string surface = requireNativeUserDictStringField(wordJson, "surface");
    uint32_t priority = requireNativeUserDictNumberField(wordJson, "priority");
    uint32_t contextId = requireNativeUserDictNumberField(wordJson, "context_id");
    std::string partOfSpeech = requireNativeUserDictStringField(wordJson, "part_of_speech");
    std::string partOfSpeechDetail1 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_1");
    std::string partOfSpeechDetail2 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_2");
    std::string partOfSpeechDetail3 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_3");
    std::string inflectionalType = requireNativeUserDictStringField(wordJson, "inflectional_type");
    std::string inflectionalForm = requireNativeUserDictStringField(wordJson, "inflectional_form");
    std::string stem = requireNativeUserDictStringField(wordJson, "stem");
    std::string yomi = requireNativeUserDictStringField(wordJson, "yomi");
    std::string pronunciation = requireNativeUserDictStringField(wordJson, "pronunciation");
    uint32_t accentType = requireNativeUserDictNumberField(wordJson, "accent_type");
    uint32_t moraCount = 0;
    if (!extractJsonNumberField(wordJson, "mora_count", moraCount)) {
        moraCount = static_cast<uint32_t>(countNativeUserDictMoras(pronunciation));
    }
    std::string accentAssociativeRule = requireNativeUserDictStringField(wordJson, "accent_associative_rule");
    std::ostringstream wordStream;
    wordStream << "{";
    wordStream << "\"surface\":" << quoteJsonString(normalizeNativeUserDictSurface(surface)) << ",";
    wordStream << "\"priority\":" << priority << ",";
    wordStream << "\"context_id\":" << contextId << ",";
    wordStream << "\"part_of_speech\":" << quoteJsonString(partOfSpeech) << ",";
    wordStream << "\"part_of_speech_detail_1\":" << quoteJsonString(partOfSpeechDetail1) << ",";
    wordStream << "\"part_of_speech_detail_2\":" << quoteJsonString(partOfSpeechDetail2) << ",";
    wordStream << "\"part_of_speech_detail_3\":" << quoteJsonString(partOfSpeechDetail3) << ",";
    wordStream << "\"inflectional_type\":" << quoteJsonString(inflectionalType) << ",";
    wordStream << "\"inflectional_form\":" << quoteJsonString(inflectionalForm) << ",";
    wordStream << "\"stem\":" << quoteJsonString(stem) << ",";
    wordStream << "\"yomi\":" << quoteJsonString(yomi) << ",";
    wordStream << "\"pronunciation\":" << quoteJsonString(pronunciation) << ",";
    wordStream << "\"accent_type\":" << accentType << ",";
    wordStream << "\"mora_count\":" << moraCount << ",";
    wordStream << "\"accent_associative_rule\":" << quoteJsonString(accentAssociativeRule);
    wordStream << "}";
    return wordStream.str();
}

static size_t getNativeUtf8CodepointBytes(unsigned char firstByte) {
    if (firstByte < 0x80) {
        return 1;
    }
    if ((firstByte & 0xe0) == 0xc0) {
        return 2;
    }
    if ((firstByte & 0xf0) == 0xe0) {
        return 3;
    }
    if ((firstByte & 0xf8) == 0xf0) {
        return 4;
    }
    throw std::runtime_error("ユーザー辞書の読みが UTF-8 として不正です");
}

static uint32_t decodeNativeUtf8Codepoint(const std::string &text, size_t position, size_t &codepointBytes) {
    if (position >= text.size()) {
        throw std::runtime_error("ユーザー辞書の読みが UTF-8 として不正です");
    }
    unsigned char firstByte = static_cast<unsigned char>(text[position]);
    codepointBytes = getNativeUtf8CodepointBytes(firstByte);
    if (position + codepointBytes > text.size()) {
        throw std::runtime_error("ユーザー辞書の読みが UTF-8 として不正です");
    }
    if (codepointBytes == 1) {
        return firstByte;
    }
    uint32_t codepoint = firstByte & ((1u << (7 - codepointBytes)) - 1u);
    for (size_t byteOffset = 1; byteOffset < codepointBytes; byteOffset++) {
        unsigned char nextByte = static_cast<unsigned char>(text[position + byteOffset]);
        if ((nextByte & 0xc0) != 0x80) {
            throw std::runtime_error("ユーザー辞書の読みが UTF-8 として不正です");
        }
        codepoint = (codepoint << 6) | (nextByte & 0x3f);
    }
    return codepoint;
}

static bool startsWithNativeUserDictText(const std::string &text, size_t position, const std::string &prefixText) {
    return position + prefixText.size() <= text.size() && text.compare(position, prefixText.size(), prefixText) == 0;
}

static size_t countNativeUserDictMoras(const std::string &pronunciation) {
    size_t moraCount = 0;
    size_t position = 0;
    while (position < pronunciation.size()) {
        size_t matchedBytes = 0;
        for (const char *moraPattern : nativeUserDictMoraPatterns) {
            std::string patternText = moraPattern;
            if (patternText.size() > matchedBytes && startsWithNativeUserDictText(pronunciation, position, patternText)) {
                matchedBytes = patternText.size();
            }
        }
        if (matchedBytes > 0) {
            position += matchedBytes;
            moraCount++;
            continue;
        }
        size_t codepointBytes = 0;
        uint32_t codepoint = decodeNativeUtf8Codepoint(pronunciation, position, codepointBytes);
        if ((codepoint >= 0x30a1 && codepoint <= 0x30f4) || codepoint == 0x30fc) {
            position += codepointBytes;
            moraCount++;
            continue;
        }
        throw std::runtime_error("ユーザー辞書の読みはカタカナが必要です: " + pronunciation);
    }
    return moraCount;
}

static const std::array<int, 11> &getNativeCostCandidates(uint32_t contextId) {
    if (contextId == properNounContextId) {
        return properNounCosts;
    }
    if (contextId == commonNounContextId) {
        return commonNounCosts;
    }
    if (contextId == verbContextId) {
        return verbCosts;
    }
    if (contextId == adjectiveContextId) {
        return adjectiveCosts;
    }
    if (contextId == suffixContextId) {
        return suffixCosts;
    }
    throw std::runtime_error("未対応のユーザー辞書 context_id です: " + std::to_string(contextId));
}

static int getNativePriorityCost(uint32_t contextId, uint32_t priority) {
    if (priority < minUserDictPriority || priority > maxUserDictPriority) {
        throw std::runtime_error("ユーザー辞書 priority は 0 以上 10 以下が必要です");
    }
    return getNativeCostCandidates(contextId)[maxUserDictPriority - priority];
}

static uint32_t requireNativeUserDictNumberField(const std::string &wordJson, const std::string &fieldName) {
    uint32_t numberValue = 0;
    if (!extractJsonNumberField(wordJson, fieldName, numberValue)) {
        throw std::runtime_error("ユーザー辞書単語に数値 field がありません: " + fieldName);
    }
    return numberValue;
}

static std::string requireNativeUserDictStringField(const std::string &wordJson, const std::string &fieldName) {
    std::string fieldText = extractJsonStringField(wordJson, fieldName);
    if (fieldText.empty()) {
        throw std::runtime_error("ユーザー辞書単語に文字列 field がありません: " + fieldName);
    }
    return fieldText;
}

static std::string createNativeUserDictEntryMecabCsvLine(const NativeUserDictEntry &entry) {
    const std::string &wordJson = entry.wordJson;
    std::string surface = requireNativeUserDictStringField(wordJson, "surface");
    uint32_t priority = requireNativeUserDictNumberField(wordJson, "priority");
    uint32_t contextId = requireNativeUserDictNumberField(wordJson, "context_id");
    std::string partOfSpeech = requireNativeUserDictStringField(wordJson, "part_of_speech");
    std::string partOfSpeechDetail1 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_1");
    std::string partOfSpeechDetail2 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_2");
    std::string partOfSpeechDetail3 = requireNativeUserDictStringField(wordJson, "part_of_speech_detail_3");
    std::string inflectionalType = requireNativeUserDictStringField(wordJson, "inflectional_type");
    std::string inflectionalForm = requireNativeUserDictStringField(wordJson, "inflectional_form");
    std::string stem = requireNativeUserDictStringField(wordJson, "stem");
    std::string yomi = requireNativeUserDictStringField(wordJson, "yomi");
    std::string pronunciation = requireNativeUserDictStringField(wordJson, "pronunciation");
    uint32_t accentType = requireNativeUserDictNumberField(wordJson, "accent_type");
    uint32_t moraCount = 0;
    if (!extractJsonNumberField(wordJson, "mora_count", moraCount)) {
        moraCount = static_cast<uint32_t>(countNativeUserDictMoras(pronunciation));
    }
    std::string accentAssociativeRule = requireNativeUserDictStringField(wordJson, "accent_associative_rule");
    if (accentType > moraCount) {
        throw std::runtime_error("ユーザー辞書 accent_type が mora_count を超えています: " + entry.wordUuid);
    }
    std::ostringstream csvStream;
    csvStream << surface << ","
              << contextId << ","
              << contextId << ","
              << getNativePriorityCost(contextId, priority) << ","
              << partOfSpeech << ","
              << partOfSpeechDetail1 << ","
              << partOfSpeechDetail2 << ","
              << partOfSpeechDetail3 << ","
              << inflectionalType << ","
              << inflectionalForm << ","
              << stem << ","
              << yomi << ","
              << pronunciation << ","
              << accentType << "/"
              << moraCount << ","
              << accentAssociativeRule;
    return csvStream.str();
}

static void upsertNativeUserDictEntry(std::vector<NativeUserDictEntry> &entries, const NativeUserDictEntry &updatedEntry) {
    auto entryIterator = std::find_if(entries.begin(), entries.end(), [&updatedEntry](const NativeUserDictEntry &entry) {
        return entry.wordUuid == updatedEntry.wordUuid;
    });
    if (entryIterator == entries.end()) {
        entries.push_back(updatedEntry);
        return;
    }
    entryIterator->wordJson = updatedEntry.wordJson;
}

std::string createNativeUserDictJson(const fs::path &userDictPath) {
    std::string userDictJson = readNativeUserDictJson(userDictPath);
    ensureNativeUserDictObject(userDictJson);
    return userDictJson;
}

std::string addNativeUserDictWord(const fs::path &userDictPath, const std::string &surface, const std::string &pronunciation, uintptr_t accentType, VoicevoxUserDictWordType wordType, uint32_t priority) {
    std::vector<NativeUserDictEntry> entries = parseNativeUserDictEntries(readNativeUserDictJson(userDictPath));
    std::string wordUuid = createNativeUserDictUuid(entries);
    entries.push_back({wordUuid, createNativeUserDictWordJson(surface, pronunciation, accentType, wordType, priority)});
    writeNativeUserDictJson(userDictPath, joinNativeUserDictEntries(entries));
    return wordUuid;
}

void updateNativeUserDictWord(const fs::path &userDictPath, const std::string &wordUuid, const std::string &surface, const std::string &pronunciation, uintptr_t accentType, VoicevoxUserDictWordType wordType, uint32_t priority) {
    std::vector<NativeUserDictEntry> entries = parseNativeUserDictEntries(readNativeUserDictJson(userDictPath));
    auto entryIterator = std::find_if(entries.begin(), entries.end(), [&wordUuid](const NativeUserDictEntry &entry) {
        return entry.wordUuid == wordUuid;
    });
    if (entryIterator == entries.end()) {
        throw std::runtime_error("ユーザー辞書単語がありません: " + wordUuid);
    }
    entryIterator->wordJson = createNativeUserDictWordJson(surface, pronunciation, accentType, wordType, priority);
    writeNativeUserDictJson(userDictPath, joinNativeUserDictEntries(entries));
}

void removeNativeUserDictWord(const fs::path &userDictPath, const std::string &wordUuid) {
    std::vector<NativeUserDictEntry> entries = parseNativeUserDictEntries(readNativeUserDictJson(userDictPath));
    auto entryIterator = std::find_if(entries.begin(), entries.end(), [&wordUuid](const NativeUserDictEntry &entry) {
        return entry.wordUuid == wordUuid;
    });
    if (entryIterator == entries.end()) {
        throw std::runtime_error("ユーザー辞書単語がありません: " + wordUuid);
    }
    entries.erase(entryIterator);
    writeNativeUserDictJson(userDictPath, joinNativeUserDictEntries(entries));
}

void importNativeUserDictJson(const fs::path &userDictPath, const std::string &userDictJson, bool shouldOverride) {
    std::vector<NativeUserDictEntry> importedEntries = parseNativeUserDictEntries(userDictJson);
    for (NativeUserDictEntry &importedEntry : importedEntries) {
        importedEntry.wordJson = normalizeNativeUserDictWordJson(importedEntry.wordJson);
    }
    std::vector<NativeUserDictEntry> entries = parseNativeUserDictEntries(readNativeUserDictJson(userDictPath));
    for (const NativeUserDictEntry &importedEntry : importedEntries) {
        upsertNativeUserDictEntry(entries, importedEntry);
    }
    (void)shouldOverride;
    writeNativeUserDictJson(userDictPath, joinNativeUserDictEntries(entries));
}

std::string createNativeUserDictMecabCsv(const fs::path &userDictPath) {
    std::vector<NativeUserDictEntry> entries = parseNativeUserDictEntries(readNativeUserDictJson(userDictPath));
    std::ostringstream csvStream;
    for (size_t entryIndex = 0; entryIndex < entries.size(); entryIndex++) {
        if (entryIndex > 0) {
            csvStream << "\n";
        }
        csvStream << createNativeUserDictEntryMecabCsvLine(entries[entryIndex]);
    }
    return csvStream.str();
}
