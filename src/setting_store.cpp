#include "setting_store.hpp"

#include "json_utility.hpp"
#include "utility.hpp"

#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;

static void validateCorsPolicyMode(const std::string &corsPolicyMode) {
    if (corsPolicyMode != "all" && corsPolicyMode != "localapps") {
        throw std::runtime_error("cors_policy_mode が不正です: " + corsPolicyMode);
    }
}

static std::string escapeHtmlText(const std::string &text) {
    std::string escapedText;
    for (char character : text) {
        if (character == '&') {
            escapedText += "&amp;";
        } else if (character == '<') {
            escapedText += "&lt;";
        } else if (character == '>') {
            escapedText += "&gt;";
        } else if (character == '"') {
            escapedText += "&quot;";
        } else {
            escapedText.push_back(character);
        }
    }
    return escapedText;
}

static void replaceAllText(std::string &text, const std::string &fromText, const std::string &toText) {
    size_t position = 0;
    while ((position = text.find(fromText, position)) != std::string::npos) {
        text.replace(position, fromText.size(), toText);
        position += toText.size();
    }
}

static std::string createJsonStringContent(const std::string &text) {
    std::string quotedText = quoteJsonString(text);
    if (quotedText.size() < 2) {
        return "";
    }
    return quotedText.substr(1, quotedText.size() - 2);
}

LiteVoxSetting loadLiteVoxSetting(const fs::path &settingPath) {
    LiteVoxSetting setting;
    if (settingPath.empty() || !fs::exists(settingPath)) {
        return setting;
    }
    std::string settingJson = trimAscii(readTextFile(settingPath));
    if (settingJson.empty()) {
        return setting;
    }
    std::string corsPolicyMode = extractJsonStringField(settingJson, "cors_policy_mode");
    if (!corsPolicyMode.empty()) {
        validateCorsPolicyMode(corsPolicyMode);
        setting.corsPolicyMode = corsPolicyMode;
    }
    setting.allowOrigin = extractJsonStringField(settingJson, "allow_origin");
    return setting;
}

std::string createLiteVoxSettingJson(const LiteVoxSetting &setting) {
    validateCorsPolicyMode(setting.corsPolicyMode);
    return "{\"cors_policy_mode\":" + quoteJsonString(setting.corsPolicyMode) + ",\"allow_origin\":" + quoteJsonString(setting.allowOrigin) + "}";
}

std::string createLiteVoxSettingPageHtml(const LiteVoxSetting &setting) {
    std::string allSelectedText = setting.corsPolicyMode == "all" ? " selected" : "";
    std::string localAppsSelectedText = setting.corsPolicyMode == "localapps" ? " selected" : "";
    return "<!doctype html><html><head><meta charset=\"utf-8\"><title>LiteVox Setting</title></head><body><form method=\"post\" action=\"/setting\"><label>CORS policy <select name=\"cors_policy_mode\"><option value=\"all\"" + allSelectedText + ">all</option><option value=\"localapps\"" + localAppsSelectedText + ">localapps</option></select></label><label> Allow origin <input name=\"allow_origin\" value=\"" + escapeHtmlText(setting.allowOrigin) + "\"></label><button type=\"submit\">Save</button></form></body></html>";
}

std::string createLiteVoxSettingPageHtml(const LiteVoxSetting &setting, const std::string &templateHtml, const std::string &brandName) {
    if (templateHtml.empty()) {
        return createLiteVoxSettingPageHtml(setting);
    }
    std::string pageHtml = templateHtml;
    replaceAllText(pageHtml, "<JINJA_PRE>cors_policy_mode<JINJA_POST>", createJsonStringContent(setting.corsPolicyMode));
    replaceAllText(pageHtml, "<JINJA_PRE>allow_origin<JINJA_POST>", createJsonStringContent(setting.allowOrigin));
    replaceAllText(pageHtml, "<JINJA_PRE>brand_name<JINJA_POST>", createJsonStringContent(brandName));
    if (!pageHtml.empty() && pageHtml.back() == '\n') {
        pageHtml.pop_back();
    }
    return pageHtml;
}

void saveLiteVoxSetting(const fs::path &settingPath, const LiteVoxSetting &setting) {
    if (settingPath.empty()) {
        throw std::runtime_error("setting path がありません");
    }
    writeTextFile(settingPath, createLiteVoxSettingJson(setting));
}

bool isLiteVoxLocalAppOrigin(const std::string &originText) {
    std::string normalizedText = lowercaseAscii(originText);
    return normalizedText.rfind("app://.", 0) == 0
        || normalizedText.rfind("tauri://localhost", 0) == 0
        || normalizedText.rfind("http://localhost", 0) == 0
        || normalizedText.rfind("https://localhost", 0) == 0
        || normalizedText.rfind("http://127.0.0.1", 0) == 0
        || normalizedText.rfind("https://127.0.0.1", 0) == 0
        || normalizedText.rfind("http://[::1]", 0) == 0
        || normalizedText.rfind("https://[::1]", 0) == 0;
}
