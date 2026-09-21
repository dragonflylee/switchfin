/*
    Copyright 2023 dragonflylee
*/

#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "api/jellyfin.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/home.xml");

    this->userResume->onQuery([](size_t start, size_t pageSize) {
        std::string query = HTTP::encode_form({
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"mediaTypes", "Video"},
            {"fields", "BasicSyncInfo,Chapters"},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiUserResume), AppConfig::instance().getUserId(), query);
    });

    this->showNextup->onQuery([](size_t start, size_t pageSize) {
        char cutoff[21] = {};
        const int maxNextup = AppConfig::instance().getItem(AppConfig::MAXDAY_NEXTUP, 365);
        const time_t tt = std::time(nullptr) - maxNextup * 24 * 3600;
        std::strftime(cutoff, sizeof(cutoff), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&tt));
        std::string query = HTTP::encode_form({
            {"userId", AppConfig::instance().getUserId()},
            {"fields", "BasicSyncInfo,Chapters"},
            {"enableImageTypes", "Primary,Backdrop,Thumb"},
            {"enableResumable", "false"},
            {"enableRewatching", "false"},
            {"nextUpDateCutoff", cutoff},
            {"limit", std::to_string(pageSize)},
            {"startIndex", std::to_string(start)},
        });
        return fmt::format(fmt::runtime(jellyfin::apiShowNextUp), query);
    });
}

#ifdef PS5_NATIVE_GPU
HomeTab::~HomeTab() {
    libraryLifetime->active = false;
    if (libraryCancelled) libraryCancelled->store(true);
    brls::Logger::debug("View HomeTab: delete");
}
#else
HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }
#endif

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doRequest() {
    this->userResume->reset();
    this->showNextup->reset();
    this->userResume->doRequest();
    this->showNextup->doRequest();
}

void HomeTab::onCreate() {
#ifdef PS5_NATIVE_GPU
    if (actionsRegistered) { loadLibraries(); return; }
    auto actionRefresh = [this](brls::View*) {
        this->loadLibraries();
#else
    auto actionRefresh = [this](brls::View* view) {
#endif
        this->userResume->doRequest(true);
        this->showNextup->doRequest(true);
        for (auto recyler : this->latest) {
            recyler->doLatest(true);
        }
        return true;
    };

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);
#ifdef PS5_NATIVE_GPU
    actionsRegistered = true;
    // Resume and Next Up do not depend on the library enumeration response.
    // Start them once now so a slow library list cannot delay these rows.
    this->doRequest();
    loadLibraries();
}
#endif

#ifdef PS5_NATIVE_GPU
void HomeTab::loadLibraries() {
    // AttachedView initializes once for its account. Retry cannot append the
    // same libraries again or register another pair of refresh actions.
    if (librariesLoaded) return;
    if (librariesLoading) {
        // Removing another saved account invalidates the request epoch while
        // retaining this Home. Refresh may arrive before that cancelled reply.
        if (!libraryAccountCancelled || !libraryAccountCancelled->load()) return;
        if (libraryCancelled) libraryCancelled->store(true);
    }
    const auto generation = ++libraryLifetime->generation;
    libraryCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    libraryAccountCancelled = context.cancel;
    context.ownerCancel = libraryCancelled;
    const auto path = fmt::format(fmt::runtime(jellyfin::apiUserViews), context.user);
    librariesLoading = true;
#endif
    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<jellyfin::Result<jellyfin::Collection>>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Collection>>(request, path);
        },
        [ASYNC_TOKEN, generation](const jellyfin::Result<jellyfin::Collection>& r) {
#else
    jellyfin::getJSON<jellyfin::Result<jellyfin::Collection>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Collection>& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != libraryLifetime->generation || !librariesLoading) return;
            librariesLoading = false;
            librariesLoaded = true;
#else
            this->userResume->doRequest();
            this->showNextup->doRequest();

#endif
            auto& excludes = AppConfig::instance().userConfig().LatestItemsExcludes;

            for (auto& item : r.Items) {
                auto it = std::find(excludes.begin(), excludes.end(), item.Id);
                if (it != excludes.end()) continue;

                RecylingVideo* recyler = new RecylingVideo();
                recyler->setTitle(item.Name);

                if (item.CollectionType == "livetv") {
                    recyler->setFrameHeight(150);
                    recyler->setItemWidth(200);
                    recyler->setPageSize(24);
                    recyler->onQuery([](size_t start, size_t pageSize) {
                        std::string query = HTTP::encode_form({
                            {"fields", "ChannelInfo"},
                            {"enableImageTypes", "Primary"},
                            {"isAiring", "true"},
                            {"userId", AppConfig::instance().getUserId()},
                            {"startIndex", std::to_string(start)},
                            {"limit", std::to_string(pageSize)},
                        });
                        return fmt::format(fmt::runtime(jellyfin::apiProgramRecommend), query);
                    });
                    recyler->doLiveTV();

                } else {
#ifdef PS5_NATIVE_GPU
                    const bool series = item.CollectionType == "tvshows";
                    recyler->setLatestSeries(series);
#endif
                    std::string itemId = std::move(item.Id);
                    if (item.CollectionType == "music") {
                        recyler->setFrameHeight(225);
                    } else if (item.CollectionType == "books") {
                        recyler->setFrameHeight(280);
                    } else {
                        recyler->setFrameHeight(300);
                    }
                    recyler->setItemWidth(175);
#ifdef PS5_NATIVE_GPU
                    recyler->onQuery([itemId, series](size_t, size_t pageSize) {
#else
                    recyler->onQuery([itemId](size_t start, size_t pageSize) {
#endif
                        std::string userId = AppConfig::instance().getUserId();
#ifdef PS5_NATIVE_GPU
                        HTTP::Form parameters = {
#else
                        std::string query = HTTP::encode_form({
#endif
                            {"enableImageTypes", "Primary"},
                            {"parentId", itemId},
                            {"fields", "BasicSyncInfo,Chapters"},
                            {"limit", std::to_string(pageSize)},
#ifdef PS5_NATIVE_GPU
                            {"groupItems", "true"},
                        };
                        if (series) parameters["includeItemTypes"] = jellyfin::mediaTypeEpisode;
                        std::string query = HTTP::encode_form(parameters);
#else
                        });
#endif
                        return fmt::format(fmt::runtime(jellyfin::apiUserLatest), userId, query);
                    });
                    recyler->doLatest();
                    this->latest.push_back(recyler);
                }
                boxHome->addView(recyler);
            }
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != libraryLifetime->generation || !librariesLoading) return;
            librariesLoading = false;
            if (context.cancelled()) return;
            std::weak_ptr<LibraryLifetime> lifetime = libraryLifetime;
            auto current = [lifetime, generation, context]() {
                const auto owner = lifetime.lock();
                return owner && owner->active && owner->generation == generation && !context.cancelled();
            };
#endif
            auto dialog = new brls::Dialog(ex);
#ifdef PS5_NATIVE_GPU
            dialog->addButton("hints/retry"_i18n, [this, current]() {
                if (!current()) return;
                brls::sync([this, current]() { if (current()) this->loadLibraries(); });
            });
#else
            dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->onCreate(); }); });
#endif
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
#ifdef PS5_NATIVE_GPU
        });
}
#else
        },
        jellyfin::apiUserViews, AppConfig::instance().getUserId());
}
#endif
