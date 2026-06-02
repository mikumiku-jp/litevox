#pragma once

#include <filesystem>
#include <string>

struct LiteVoxSetting {
    std::string corsPolicyMode = "localapps";
    std::string allowOrigin = "";
};

LiteVoxSetting loadLiteVoxSetting(const std::filesystem::path &settingPath);
std::string createLiteVoxSettingJson(const LiteVoxSetting &setting);
std::string createLiteVoxSettingPageHtml(const LiteVoxSetting &setting);
std::string createLiteVoxSettingPageHtml(const LiteVoxSetting &setting, const std::string &templateHtml, const std::string &brandName);
void saveLiteVoxSetting(const std::filesystem::path &settingPath, const LiteVoxSetting &setting);
bool isLiteVoxLocalAppOrigin(const std::string &originText);
