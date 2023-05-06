#pragma once

#include <borealis/core/singleton.hpp>
#include <borealis/core/logger.hpp>
#include <nlohmann/json.hpp>

class AppVersion : public brls::Singleton<AppVersion> {
public:
    std::string git_commit, git_tag;
    AppVersion();

    std::string getPlatform();
    bool needUpdate(std::string latestVersion);
    void checkUpdate(int delay = 2000, bool showUpToDateDialog = false);
};

class AppConfig : public brls::Singleton<AppConfig> {
public:
    AppConfig();

    void init();
    void save();
    std::string configDir();

    template <typename T>
    T getItem(const std::string& key, T defaultValue) {
        try {
            if (!setting.contains(key)) return defaultValue;
            return this->setting.at(key).get<T>();
        } catch (const std::exception& e) {
            brls::Logger::error("Damaged config found: {}/{}", key, e.what());
            return defaultValue;
        }
    }

    template <typename T>
    void setItem(const std::string& key, T data) {
        this->setting[key] = data;
        this->save();
    }

private:
    nlohmann::json setting;
};