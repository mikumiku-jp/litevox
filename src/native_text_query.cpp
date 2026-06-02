#include "native_text_query.hpp"

#include "native_user_dict.hpp"
#include "utility.hpp"

#include <cstdio>

#include <openjtalk/jpcommon.h>
#include <openjtalk/mecab.h>
#include <openjtalk/njd.h>
#include <openjtalk/mecab2njd.h>
#include <openjtalk/njd2jpcommon.h>
#include <openjtalk/njd_set_accent_phrase.h>
#include <openjtalk/njd_set_accent_type.h>
#include <openjtalk/njd_set_digit.h>
#include <openjtalk/njd_set_long_vowel.h>
#include <openjtalk/njd_set_pronunciation.h>
#include <openjtalk/njd_set_unvoiced_vowel.h>
#include <openjtalk/text2mecab.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace fs = std::filesystem;

struct NativeOpenJtalkState {
    Mecab mecab{};
    NJD njd{};
    JPCommon jpcommon{};
    bool hasMecab = false;
    bool hasNjd = false;
    bool hasJpcommon = false;
};

struct NativeCompiledUserDictFiles {
    fs::path csvPath;
    fs::path dictionaryPath;
    bool hasDictionary = false;
    bool shouldRemoveFiles = false;
};

struct NativeParsedLabelMora {
    int32_t relativeAccentPosition = 0;
    uint32_t positionForward = 0;
    uint32_t positionBackward = 0;
    bool hasMora = false;
};

struct NativeParsedLabelAccentPhrase {
    uint32_t moraCount = 0;
    uint32_t accentPosition = 0;
    bool isInterrogative = false;
    uint32_t accentPhrasePositionForward = 0;
    uint32_t accentPhrasePositionBackward = 0;
    uint32_t moraPositionForward = 0;
    uint32_t moraPositionBackward = 0;
    bool hasAccentPhrase = false;
};

struct NativeParsedLabelBreathGroup {
    uint32_t accentPhraseCount = 0;
    uint32_t moraCount = 0;
    uint32_t breathGroupPositionForward = 0;
    uint32_t breathGroupPositionBackward = 0;
    uint32_t accentPhrasePositionForward = 0;
    uint32_t accentPhrasePositionBackward = 0;
    uint32_t moraPositionForward = 0;
    uint32_t moraPositionBackward = 0;
    bool hasBreathGroup = false;
};

struct NativeParsedLabel {
    std::string phoneme;
    NativeParsedLabelMora mora;
    NativeParsedLabelAccentPhrase accentPhrase;
    NativeParsedLabelBreathGroup breathGroup;
};

class NativeLabelCursor {
public:
    explicit NativeLabelCursor(const std::string &labelText) : labelText(labelText) {}

    std::string readUntil(const std::string &symbolText) {
        size_t foundPosition = labelText.find(symbolText, byteOffset);
        if (foundPosition == std::string::npos) {
            throw std::runtime_error("full-context label の記号が見つかりません: " + symbolText);
        }
        std::string segmentText = labelText.substr(byteOffset, foundPosition - byteOffset);
        byteOffset = foundPosition + symbolText.size();
        return segmentText;
    }

private:
    const std::string &labelText;
    size_t byteOffset = 0;
};

static uint64_t calculateNativeUserDictCacheHash(const fs::path &dictionaryDirectory, const std::string &mecabCsv) {
    uint64_t hashValue = 1469598103934665603ull;
    auto appendHashByte = [&hashValue](uint8_t byteValue) {
        hashValue ^= byteValue;
        hashValue *= 1099511628211ull;
    };
    std::string dictionaryPathText = dictionaryDirectory.string();
    for (unsigned char character : dictionaryPathText) {
        appendHashByte(character);
    }
    appendHashByte(0);
    for (unsigned char character : mecabCsv) {
        appendHashByte(character);
    }
    return hashValue;
}

static fs::path createNativeCachedUserDictPath(uint64_t cacheHash, const std::string &suffixText) {
    return fs::temp_directory_path() / ("litevox-native-user-dict-" + std::to_string(cacheHash) + suffixText);
}

static void removeNativeCompiledUserDictFiles(const NativeCompiledUserDictFiles &compiledFiles) {
    if (!compiledFiles.shouldRemoveFiles) {
        return;
    }
    if (!compiledFiles.csvPath.empty()) {
        fs::remove(compiledFiles.csvPath);
    }
    if (!compiledFiles.dictionaryPath.empty()) {
        fs::remove(compiledFiles.dictionaryPath);
    }
}

static NativeCompiledUserDictFiles compileNativeUserDict(const fs::path &dictionaryDirectory, const fs::path &userDictPath) {
    std::string mecabCsv;
    fs::path defaultCsvPath = dictionaryDirectory.parent_path() / "resources" / "default.csv";
    if (fs::exists(defaultCsvPath)) {
        mecabCsv = trimAscii(readTextFile(defaultCsvPath));
    }
    std::string userMecabCsv = trimAscii(createNativeUserDictMecabCsv(userDictPath));
    if (!userMecabCsv.empty()) {
        if (!mecabCsv.empty()) {
            mecabCsv += "\n";
        }
        mecabCsv += userMecabCsv;
    }
    if (mecabCsv.empty()) {
        return {};
    }
    NativeCompiledUserDictFiles compiledFiles;
    uint64_t cacheHash = calculateNativeUserDictCacheHash(dictionaryDirectory, mecabCsv);
    compiledFiles.csvPath = createNativeCachedUserDictPath(cacheHash, ".csv");
    compiledFiles.dictionaryPath = createNativeCachedUserDictPath(cacheHash, ".dic");
    static std::mutex compileMutex;
    std::lock_guard<std::mutex> compileLock(compileMutex);
    if (fs::exists(compiledFiles.dictionaryPath)) {
        compiledFiles.hasDictionary = true;
        return compiledFiles;
    }
    writeTextFile(compiledFiles.csvPath, mecabCsv);
    std::vector<std::string> argumentTexts = {
        "mecab-dict-index",
        "-d",
        dictionaryDirectory.string(),
        "-u",
        compiledFiles.dictionaryPath.string(),
        "-f",
        "utf-8",
        "-t",
        "utf-8",
        compiledFiles.csvPath.string(),
        "-q"
    };
    std::vector<char *> arguments;
    arguments.reserve(argumentTexts.size());
    for (std::string &argumentText : argumentTexts) {
        arguments.push_back(argumentText.data());
    }
    int indexStatus = mecab_dict_index(static_cast<int>(arguments.size()), arguments.data());
    if (indexStatus != 0) {
        compiledFiles.shouldRemoveFiles = true;
        removeNativeCompiledUserDictFiles(compiledFiles);
        throw std::runtime_error("ユーザー辞書のコンパイルに失敗しました");
    }
    compiledFiles.hasDictionary = true;
    return compiledFiles;
}

static void initializeNativeOpenJtalkState(NativeOpenJtalkState &openJtalkState, const fs::path &dictionaryDirectory, const fs::path &compiledUserDictPath) {
    Mecab_initialize(&openJtalkState.mecab);
    openJtalkState.hasMecab = true;
    NJD_initialize(&openJtalkState.njd);
    openJtalkState.hasNjd = true;
    JPCommon_initialize(&openJtalkState.jpcommon);
    openJtalkState.hasJpcommon = true;
    ensurePathExists(dictionaryDirectory, "OpenJTalk 辞書");
    std::string dictionaryPathText = dictionaryDirectory.string();
    bool hasLoadedMecab = false;
    if (compiledUserDictPath.empty()) {
        hasLoadedMecab = Mecab_load(&openJtalkState.mecab, dictionaryPathText.c_str());
    } else {
        std::string userDictPathText = compiledUserDictPath.string();
        hasLoadedMecab = Mecab_load_with_userdic(&openJtalkState.mecab, dictionaryPathText.c_str(), userDictPathText.c_str());
    }
    if (!hasLoadedMecab) {
        throw std::runtime_error("OpenJTalk 辞書をロードできません: " + dictionaryPathText);
    }
}

static void destroyNativeOpenJtalkState(NativeOpenJtalkState &openJtalkState) {
    if (openJtalkState.hasMecab) {
        Mecab_clear(&openJtalkState.mecab);
        openJtalkState.hasMecab = false;
    }
    if (openJtalkState.hasNjd) {
        NJD_clear(&openJtalkState.njd);
        openJtalkState.hasNjd = false;
    }
    if (openJtalkState.hasJpcommon) {
        JPCommon_clear(&openJtalkState.jpcommon);
        openJtalkState.hasJpcommon = false;
    }
}

static std::string convertTextToMecabInput(const std::string &text) {
    size_t bufferSize = std::max<size_t>(text.size() * 4 + 16, 64);
    std::vector<char> mecabInput(bufferSize, '\0');
    while (true) {
        text2mecab_result_t text2mecabStatus = text2mecab(mecabInput.data(), mecabInput.size(), text.c_str());
        if (text2mecabStatus == TEXT2MECAB_RESULT_SUCCESS) {
            return std::string(mecabInput.data());
        }
        if (text2mecabStatus != TEXT2MECAB_RESULT_RANGE_ERROR) {
            throw std::runtime_error("text2mecab に失敗しました");
        }
        if (mecabInput.size() > std::numeric_limits<size_t>::max() / 2) {
            throw std::runtime_error("text2mecab 入力バッファを確保できません");
        }
        mecabInput.assign(mecabInput.size() * 2, '\0');
    }
}

static std::vector<std::string> createNativeOpenJtalkLabelTexts(const fs::path &dictionaryDirectory, const fs::path &userDictPath, const std::string &text) {
    NativeCompiledUserDictFiles compiledUserDictFiles = compileNativeUserDict(dictionaryDirectory, userDictPath);
    NativeOpenJtalkState openJtalkState;
    initializeNativeOpenJtalkState(openJtalkState, dictionaryDirectory, compiledUserDictFiles.hasDictionary ? compiledUserDictFiles.dictionaryPath : fs::path{});
    try {
        std::string mecabInput = convertTextToMecabInput(text);
        JPCommon_refresh(&openJtalkState.jpcommon);
        NJD_refresh(&openJtalkState.njd);
        Mecab_refresh(&openJtalkState.mecab);
        if (!Mecab_analysis(&openJtalkState.mecab, mecabInput.c_str())) {
            throw std::runtime_error("Mecab_analysis に失敗しました");
        }
        char **mecabFeatures = Mecab_get_feature(&openJtalkState.mecab);
        if (!mecabFeatures) {
            throw std::runtime_error("Mecab_get_feature に失敗しました");
        }
        mecab2njd(&openJtalkState.njd, mecabFeatures, Mecab_get_size(&openJtalkState.mecab));
        njd_set_pronunciation(&openJtalkState.njd);
        njd_set_digit(&openJtalkState.njd);
        njd_set_accent_phrase(&openJtalkState.njd);
        njd_set_accent_type(&openJtalkState.njd);
        njd_set_unvoiced_vowel(&openJtalkState.njd);
        njd_set_long_vowel(&openJtalkState.njd);
        njd2jpcommon(&openJtalkState.jpcommon, &openJtalkState.njd);
        JPCommon_make_label(&openJtalkState.jpcommon);
        int labelCount = JPCommon_get_label_size(&openJtalkState.jpcommon);
        char **labelFeatures = JPCommon_get_label_feature(&openJtalkState.jpcommon);
        if (!labelFeatures) {
            throw std::runtime_error("JPCommon_get_label_feature に失敗しました");
        }
        std::vector<std::string> labelTexts;
        labelTexts.reserve(static_cast<size_t>(std::max(0, labelCount)));
        for (int labelIndex = 0; labelIndex < labelCount; labelIndex++) {
            labelTexts.emplace_back(labelFeatures[labelIndex]);
        }
        destroyNativeOpenJtalkState(openJtalkState);
        removeNativeCompiledUserDictFiles(compiledUserDictFiles);
        return labelTexts;
    } catch (...) {
        destroyNativeOpenJtalkState(openJtalkState);
        removeNativeCompiledUserDictFiles(compiledUserDictFiles);
        throw;
    }
}

static bool parseOptionalInteger(const std::string &fieldText, int32_t &parsedNumber) {
    if (fieldText == "xx") {
        return false;
    }
    size_t parsedLength = 0;
    int parsedValue = std::stoi(fieldText, &parsedLength);
    if (parsedLength != fieldText.size()) {
        throw std::runtime_error("full-context label の整数が不正です: " + fieldText);
    }
    parsedNumber = static_cast<int32_t>(parsedValue);
    return true;
}

static bool parseOptionalBool(const std::string &fieldText, bool &parsedBool) {
    if (fieldText == "xx") {
        return false;
    }
    if (fieldText == "0") {
        parsedBool = false;
        return true;
    }
    if (fieldText == "1") {
        parsedBool = true;
        return true;
    }
    throw std::runtime_error("full-context label の bool が不正です: " + fieldText);
}

static bool parseOptionalUnsigned(const std::string &fieldText, uint32_t &parsedNumber) {
    int32_t signedNumber = 0;
    if (!parseOptionalInteger(fieldText, signedNumber)) {
        return false;
    }
    if (signedNumber < 0) {
        throw std::runtime_error("full-context label の符号なし整数が不正です: " + fieldText);
    }
    parsedNumber = static_cast<uint32_t>(signedNumber);
    return true;
}

static NativeParsedLabelMora parseNativeLabelMora(NativeLabelCursor &labelCursor) {
    int32_t relativeAccentPosition = 0;
    uint32_t positionForward = 0;
    uint32_t positionBackward = 0;
    bool hasRelativeAccentPosition = parseOptionalInteger(labelCursor.readUntil("+"), relativeAccentPosition);
    bool hasPositionForward = parseOptionalUnsigned(labelCursor.readUntil("+"), positionForward);
    bool hasPositionBackward = parseOptionalUnsigned(labelCursor.readUntil("/B:"), positionBackward);
    NativeParsedLabelMora mora;
    if (hasRelativeAccentPosition && hasPositionForward && hasPositionBackward) {
        mora.relativeAccentPosition = relativeAccentPosition;
        mora.positionForward = positionForward;
        mora.positionBackward = positionBackward;
        mora.hasMora = true;
    }
    return mora;
}

static NativeParsedLabelAccentPhrase parseNativeLabelAccentPhrase(NativeLabelCursor &labelCursor) {
    uint32_t moraCount = 0;
    uint32_t accentPosition = 0;
    bool isInterrogative = false;
    uint32_t accentPhrasePositionForward = 0;
    uint32_t accentPhrasePositionBackward = 0;
    uint32_t moraPositionForward = 0;
    uint32_t moraPositionBackward = 0;
    bool hasMoraCount = parseOptionalUnsigned(labelCursor.readUntil("_"), moraCount);
    bool hasAccentPosition = parseOptionalUnsigned(labelCursor.readUntil("#"), accentPosition);
    bool hasIsInterrogative = parseOptionalBool(labelCursor.readUntil("_"), isInterrogative);
    if (labelCursor.readUntil("@") != "xx") {
        throw std::runtime_error("full-context label の F4 が不正です");
    }
    bool hasAccentPhrasePositionForward = parseOptionalUnsigned(labelCursor.readUntil("_"), accentPhrasePositionForward);
    bool hasAccentPhrasePositionBackward = parseOptionalUnsigned(labelCursor.readUntil("|"), accentPhrasePositionBackward);
    bool hasMoraPositionForward = parseOptionalUnsigned(labelCursor.readUntil("_"), moraPositionForward);
    bool hasMoraPositionBackward = parseOptionalUnsigned(labelCursor.readUntil("/G:"), moraPositionBackward);
    NativeParsedLabelAccentPhrase accentPhrase;
    if (hasMoraCount && hasAccentPosition && hasIsInterrogative && hasAccentPhrasePositionForward && hasAccentPhrasePositionBackward && hasMoraPositionForward && hasMoraPositionBackward) {
        accentPhrase.moraCount = moraCount;
        accentPhrase.accentPosition = accentPosition;
        accentPhrase.isInterrogative = isInterrogative;
        accentPhrase.accentPhrasePositionForward = accentPhrasePositionForward;
        accentPhrase.accentPhrasePositionBackward = accentPhrasePositionBackward;
        accentPhrase.moraPositionForward = moraPositionForward;
        accentPhrase.moraPositionBackward = moraPositionBackward;
        accentPhrase.hasAccentPhrase = true;
    }
    return accentPhrase;
}

static NativeParsedLabelBreathGroup parseNativeLabelBreathGroup(NativeLabelCursor &labelCursor) {
    uint32_t accentPhraseCount = 0;
    uint32_t moraCount = 0;
    uint32_t breathGroupPositionForward = 0;
    uint32_t breathGroupPositionBackward = 0;
    uint32_t accentPhrasePositionForward = 0;
    uint32_t accentPhrasePositionBackward = 0;
    uint32_t moraPositionForward = 0;
    uint32_t moraPositionBackward = 0;
    bool hasAccentPhraseCount = parseOptionalUnsigned(labelCursor.readUntil("-"), accentPhraseCount);
    bool hasMoraCount = parseOptionalUnsigned(labelCursor.readUntil("@"), moraCount);
    bool hasBreathGroupPositionForward = parseOptionalUnsigned(labelCursor.readUntil("+"), breathGroupPositionForward);
    bool hasBreathGroupPositionBackward = parseOptionalUnsigned(labelCursor.readUntil("&"), breathGroupPositionBackward);
    bool hasAccentPhrasePositionForward = parseOptionalUnsigned(labelCursor.readUntil("-"), accentPhrasePositionForward);
    bool hasAccentPhrasePositionBackward = parseOptionalUnsigned(labelCursor.readUntil("|"), accentPhrasePositionBackward);
    bool hasMoraPositionForward = parseOptionalUnsigned(labelCursor.readUntil("+"), moraPositionForward);
    bool hasMoraPositionBackward = parseOptionalUnsigned(labelCursor.readUntil("/J:"), moraPositionBackward);
    NativeParsedLabelBreathGroup breathGroup;
    if (hasAccentPhraseCount && hasMoraCount && hasBreathGroupPositionForward && hasBreathGroupPositionBackward && hasAccentPhrasePositionForward && hasAccentPhrasePositionBackward && hasMoraPositionForward && hasMoraPositionBackward) {
        breathGroup.accentPhraseCount = accentPhraseCount;
        breathGroup.moraCount = moraCount;
        breathGroup.breathGroupPositionForward = breathGroupPositionForward;
        breathGroup.breathGroupPositionBackward = breathGroupPositionBackward;
        breathGroup.accentPhrasePositionForward = accentPhrasePositionForward;
        breathGroup.accentPhrasePositionBackward = accentPhrasePositionBackward;
        breathGroup.moraPositionForward = moraPositionForward;
        breathGroup.moraPositionBackward = moraPositionBackward;
        breathGroup.hasBreathGroup = true;
    }
    return breathGroup;
}

static NativeParsedLabel parseNativeLabel(const std::string &labelText) {
    NativeLabelCursor labelCursor(labelText);
    labelCursor.readUntil("^");
    labelCursor.readUntil("-");
    NativeParsedLabel parsedLabel;
    parsedLabel.phoneme = labelCursor.readUntil("+");
    labelCursor.readUntil("=");
    labelCursor.readUntil("/A:");
    parsedLabel.mora = parseNativeLabelMora(labelCursor);
    labelCursor.readUntil("/F:");
    parsedLabel.accentPhrase = parseNativeLabelAccentPhrase(labelCursor);
    labelCursor.readUntil("/I:");
    parsedLabel.breathGroup = parseNativeLabelBreathGroup(labelCursor);
    return parsedLabel;
}

static bool hasSameNativeMora(const NativeParsedLabelMora &leftMora, const NativeParsedLabelMora &rightMora) {
    return leftMora.hasMora == rightMora.hasMora
        && leftMora.relativeAccentPosition == rightMora.relativeAccentPosition
        && leftMora.positionForward == rightMora.positionForward
        && leftMora.positionBackward == rightMora.positionBackward;
}

static bool hasSameNativeAccentPhrase(const NativeParsedLabelAccentPhrase &leftAccentPhrase, const NativeParsedLabelAccentPhrase &rightAccentPhrase) {
    return leftAccentPhrase.hasAccentPhrase == rightAccentPhrase.hasAccentPhrase
        && leftAccentPhrase.moraCount == rightAccentPhrase.moraCount
        && leftAccentPhrase.accentPosition == rightAccentPhrase.accentPosition
        && leftAccentPhrase.isInterrogative == rightAccentPhrase.isInterrogative
        && leftAccentPhrase.accentPhrasePositionForward == rightAccentPhrase.accentPhrasePositionForward
        && leftAccentPhrase.accentPhrasePositionBackward == rightAccentPhrase.accentPhrasePositionBackward
        && leftAccentPhrase.moraPositionForward == rightAccentPhrase.moraPositionForward
        && leftAccentPhrase.moraPositionBackward == rightAccentPhrase.moraPositionBackward;
}

static bool hasSameNativeBreathGroup(const NativeParsedLabelBreathGroup &leftBreathGroup, const NativeParsedLabelBreathGroup &rightBreathGroup) {
    return leftBreathGroup.hasBreathGroup == rightBreathGroup.hasBreathGroup
        && leftBreathGroup.accentPhraseCount == rightBreathGroup.accentPhraseCount
        && leftBreathGroup.moraCount == rightBreathGroup.moraCount
        && leftBreathGroup.breathGroupPositionForward == rightBreathGroup.breathGroupPositionForward
        && leftBreathGroup.breathGroupPositionBackward == rightBreathGroup.breathGroupPositionBackward
        && leftBreathGroup.accentPhrasePositionForward == rightBreathGroup.accentPhrasePositionForward
        && leftBreathGroup.accentPhrasePositionBackward == rightBreathGroup.accentPhrasePositionBackward
        && leftBreathGroup.moraPositionForward == rightBreathGroup.moraPositionForward
        && leftBreathGroup.moraPositionBackward == rightBreathGroup.moraPositionBackward;
}

static bool hasSameNativeLabelGroup(const NativeParsedLabel &leftLabel, const NativeParsedLabel &rightLabel) {
    return hasSameNativeAccentPhrase(leftLabel.accentPhrase, rightLabel.accentPhrase)
        && hasSameNativeBreathGroup(leftLabel.breathGroup, rightLabel.breathGroup);
}

static NativeAudioQueryMora createNativeMoraFromParsedLabels(const std::vector<const NativeParsedLabel *> &moraLabels) {
    if (moraLabels.size() == 1) {
        return createNativeAudioQueryMora("", moraLabels[0]->phoneme);
    }
    if (moraLabels.size() == 2) {
        return createNativeAudioQueryMora(moraLabels[0]->phoneme, moraLabels[1]->phoneme);
    }
    if (!moraLabels.empty() && moraLabels[0]->mora.positionForward == 49 && moraLabels[0]->mora.positionBackward == 49) {
        return NativeAudioQueryMora{};
    }
    throw std::runtime_error("モーラ内の音素数が不正です");
}

static std::vector<NativeAudioQueryMora> createNativeMorasFromParsedLabels(const std::vector<NativeParsedLabel> &parsedLabels, size_t beginIndex, size_t endIndex) {
    std::vector<NativeAudioQueryMora> moras;
    size_t moraBeginIndex = beginIndex;
    while (moraBeginIndex < endIndex) {
        size_t moraEndIndex = moraBeginIndex + 1;
        while (moraEndIndex < endIndex && hasSameNativeMora(parsedLabels[moraBeginIndex].mora, parsedLabels[moraEndIndex].mora)) {
            moraEndIndex++;
        }
        std::vector<const NativeParsedLabel *> moraLabels;
        for (size_t labelIndex = moraBeginIndex; labelIndex < moraEndIndex; labelIndex++) {
            if (parsedLabels[labelIndex].mora.hasMora) {
                moraLabels.push_back(&parsedLabels[labelIndex]);
            }
        }
        if (!moraLabels.empty()) {
            NativeAudioQueryMora mora = createNativeMoraFromParsedLabels(moraLabels);
            if (!mora.vowel.empty()) {
                moras.push_back(std::move(mora));
            }
        }
        moraBeginIndex = moraEndIndex;
    }
    return moras;
}

static std::vector<NativeAudioQueryAccentPhrase> createNativeAccentPhrasesFromParsedLabels(const std::vector<NativeParsedLabel> &parsedLabels) {
    std::vector<NativeAudioQueryAccentPhrase> accentPhrases;
    size_t groupBeginIndex = 0;
    while (groupBeginIndex < parsedLabels.size()) {
        size_t groupEndIndex = groupBeginIndex + 1;
        while (groupEndIndex < parsedLabels.size() && hasSameNativeLabelGroup(parsedLabels[groupBeginIndex], parsedLabels[groupEndIndex])) {
            groupEndIndex++;
        }
        const NativeParsedLabel &firstLabel = parsedLabels[groupBeginIndex];
        if (firstLabel.accentPhrase.hasAccentPhrase && firstLabel.breathGroup.hasBreathGroup) {
            NativeAudioQueryAccentPhrase accentPhrase;
            accentPhrase.moras = createNativeMorasFromParsedLabels(parsedLabels, groupBeginIndex, groupEndIndex);
            if (!accentPhrase.moras.empty()) {
                accentPhrase.accent = std::min<size_t>(firstLabel.accentPhrase.accentPosition, accentPhrase.moras.size());
                accentPhrase.isInterrogative = firstLabel.accentPhrase.isInterrogative;
                if (firstLabel.accentPhrase.accentPhrasePositionBackward == 1 && firstLabel.breathGroup.breathGroupPositionBackward != 1) {
                    accentPhrase.pauseMora.text = "、";
                    accentPhrase.pauseMora.vowel = "pau";
                    accentPhrase.hasPauseMora = true;
                }
                accentPhrases.push_back(std::move(accentPhrase));
            }
        }
        groupBeginIndex = groupEndIndex;
    }
    return accentPhrases;
}

std::vector<NativeAudioQueryAccentPhrase> createNativeAccentPhrasesFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text) {
    return createNativeAccentPhrasesFromText(dictionaryDirectory, fs::path{}, text);
}

std::vector<NativeAudioQueryAccentPhrase> createNativeAccentPhrasesFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text) {
    std::vector<std::string> labelTexts = createNativeOpenJtalkLabelTexts(dictionaryDirectory, userDictPath, text);
    std::vector<NativeParsedLabel> parsedLabels;
    parsedLabels.reserve(labelTexts.size());
    for (const std::string &labelText : labelTexts) {
        parsedLabels.push_back(parseNativeLabel(labelText));
    }
    return createNativeAccentPhrasesFromParsedLabels(parsedLabels);
}

std::string createNativeAccentPhrasesJsonFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text) {
    return createNativeAccentPhrasesJson(createNativeAccentPhrasesFromText(dictionaryDirectory, text));
}

std::string createNativeAccentPhrasesJsonFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text) {
    return createNativeAccentPhrasesJson(createNativeAccentPhrasesFromText(dictionaryDirectory, userDictPath, text));
}

std::string createNativeAudioQueryFromText(const std::filesystem::path &dictionaryDirectory, const std::string &text) {
    std::vector<NativeAudioQueryAccentPhrase> accentPhrases = createNativeAccentPhrasesFromText(dictionaryDirectory, text);
    return createNativeAudioQueryJson(accentPhrases, createNativeKanaFromAccentPhrases(accentPhrases));
}

std::string createNativeAudioQueryFromText(const std::filesystem::path &dictionaryDirectory, const std::filesystem::path &userDictPath, const std::string &text) {
    std::vector<NativeAudioQueryAccentPhrase> accentPhrases = createNativeAccentPhrasesFromText(dictionaryDirectory, userDictPath, text);
    return createNativeAudioQueryJson(accentPhrases, createNativeKanaFromAccentPhrases(accentPhrases));
}
