/*
    Copyright 2023 dragonflylee
*/

#pragma once

#include <borealis.hpp>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <memory>
#endif

class RecyclingGrid;
class AutoTabFrame;
class MediaFilter;

class MediaCollection : public brls::Box {
public:
    explicit MediaCollection(
        const std::string& itemId, const std::string& itemType = "", const std::string& genresId = "");
#ifdef PS5_NATIVE_GPU
    ~MediaCollection() override;
#endif

    brls::View* getDefaultFocus() override;

    static void clearPref() { customPrefs.clear(); }

private:
    BRLS_BIND(RecyclingGrid, recycler, "media/series");
    BRLS_BIND(AutoTabFrame, tabFrame, "media/tabFrame");

    /// @brief 获取显示配置
    void doPreferences();
#ifdef PS5_NATIVE_GPU
    void doRequest(bool refresh = false);
    void resetRequest();
#else
    void doRequest();
#endif

    void loadFilter();
    void saveFilter();

    std::string itemId;
    std::string genresId;
    std::string itemType;
    size_t pageSize;
    size_t startIndex;
#ifdef PS5_NATIVE_GPU
    size_t requestGeneration = 0;
    bool requestInFlight = false;
    bool awaitingPreferences = false;
    bool hasMore = true;
    std::shared_ptr<std::atomic_bool> requestCancelled;
    std::shared_ptr<std::atomic_bool> accountCancelled;
    std::map<std::string, std::string> queryParameters;
#endif

    std::string prefId;
    std::string prefKey;
    static std::map<std::string, std::string> customPrefs;
#ifdef PS5_NATIVE_GPU
};
#else
};
#endif
