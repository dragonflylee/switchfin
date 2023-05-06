#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
#include <unistd.h>
#include <borealis/platforms/desktop/desktop_platform.hpp>
#endif

#include <fstream>
#include <filesystem>
#include <set>
#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>
#include "utils/config.hpp"

constexpr uint32_t MINIMUM_WINDOW_WIDTH = 640;
constexpr uint32_t MINIMUM_WINDOW_HEIGHT = 360;

AppConfig::AppConfig() = default;

void AppConfig::init() {
    brls::Logger::info("Switchfin {}-{}", AppVersion::instance().git_tag, AppVersion::instance().git_commit);

    const std::string path = this->configDir() + "/config.json";
    std::ifstream readFile(path);
    if (readFile.is_open()) {
        try {
            this->setting = nlohmann::json::parse(readFile);
            brls::Logger::info("Load config from: {}", path);
        } catch (const std::exception& e) {
            brls::Logger::error("AppConfig::load: {}", e.what());
        }
    }

    // 初始化i18n
#ifndef __SWITCH__
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ZH_HANS;
#endif

    // init custom font path
    brls::FontLoader::USER_FONT_PATH = configDir() + "/font.ttf";
    brls::FontLoader::USER_ICON_PATH = configDir() + "/icon.ttf";

    if (access(brls::FontLoader::USER_ICON_PATH.c_str(), F_OK) == -1) {
        brls::FontLoader::USER_ICON_PATH = BRLS_ASSET("font/keymap_keyboard.ttf");
    }
}

void AppConfig::save() {
    const std::string path = this->configDir() + "/config.json";
    std::filesystem::create_directories(this->configDir());
    std::ofstream writeFile(path);
    if (writeFile.is_open()) {
        writeFile << this->setting;
    }
}

std::string AppConfig::configDir() {
#if __SWITCH__
    return "sdmc:/switch/Jellyfin";
#elif _WIN32
    return std::string(getenv("LOCALAPPDATA")) + "\\Jellyfin";
#elif __linux__
    std::string(getenv("HOME")) + "/.config/Jellyfin";
#elif __APPLE__
    return std::string(getenv("HOME")) + "/Library/Application Support/Jellyfin";
#else
#endif
}