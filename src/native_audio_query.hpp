#pragma once

#include <string>
#include <vector>

struct NativeAudioQueryMora {
    std::string text;
    std::string consonant;
    std::string vowel;
    bool hasConsonant = false;
};

struct NativeAudioQueryAccentPhrase {
    std::vector<NativeAudioQueryMora> moras;
    NativeAudioQueryMora pauseMora;
    bool hasPauseMora = false;
    bool isInterrogative = false;
    size_t accent = 1;
};

NativeAudioQueryMora createNativeAudioQueryMora(const std::string &consonant, const std::string &vowel);
NativeAudioQueryMora createNativeAudioQueryMoraFromText(const std::string &moraText);
std::string createNativeAccentPhrasesJson(const std::vector<NativeAudioQueryAccentPhrase> &accentPhrases);
std::string createNativeKanaFromAccentPhrases(const std::vector<NativeAudioQueryAccentPhrase> &accentPhrases);
std::string createNativeAudioQueryJson(const std::vector<NativeAudioQueryAccentPhrase> &accentPhrases, const std::string &kana);
std::string createNativeAccentPhrasesJsonFromKana(const std::string &kana);
std::string createNativeAudioQueryFromAccentPhrasesJson(const std::string &accentPhrasesJson);
std::string createNativeAudioQueryFromKana(const std::string &kana);
