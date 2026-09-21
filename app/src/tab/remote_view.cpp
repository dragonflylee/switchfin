#include "tab/remote_view.hpp"
#include "activity/gallery_activity.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/ebook_view.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/mpv_core.hpp"
#include "view/music_view.hpp"
#include "view/player_setting.hpp"
#include "client/local.hpp"
#include "utils/thread.hpp"
#include "utils/misc.hpp"
#include "utils/config.hpp"
#ifdef PS5_NATIVE_GPU
#include "utils/dialog.hpp"
#endif
#include "api/jellyfin.hpp"
#ifdef PS5_NATIVE_GPU
#include <memory>
#include <optional>
#endif

using namespace brls::literals;

class RemotePlayer : public brls::Box {
public:
    RemotePlayer(const remote::DirEntry& item, const std::string& itemId = "") : itemId(itemId) {
        float width = brls::Application::contentWidth;
        float height = brls::Application::contentHeight;
        view->setDimensions(width, height);
        view->setWidthPercentage(100);
        view->setHeightPercentage(100);
        view->setId("video");
        view->setTitie(item.name);
        view->hideVideoQuality();
        this->setDimensions(width, height);
        this->addView(view);

        if (item.type == remote::EntryType::PLAYLIST) {
            view->hideVideoProgressSlider();
        } else if (item.name.size() > 0) {
            titles.push_back(item.name);
        }

        auto& mpv = MPVCore::instance();
#ifdef PS5_NATIVE_GPU
        try {
            eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
                auto& mpv = MPVCore::instance();
                switch (event) {
                case MpvEventEnum::MPV_LOADED: {
                    if (!playbackReady || mpv.playlistGeneration() != loadPlaylistGeneration) break;
                    if (titles.empty() && !this->loadList()) break;
                    view->getProfile()->init("Local");
                    const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
                    for (auto& it : this->subtitles) {
                        mpv.command("sub-add", it.second.c_str(), flag, it.first.c_str());
                    }
#else
        eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
            auto& mpv = MPVCore::instance();
            switch (event) {
            case MpvEventEnum::MPV_LOADED: {
                if (titles.empty()) this->loadList();
                view->getProfile()->init("Local");
                const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
                for (auto& it : this->subtitles) {
                    mpv.command("sub-add", it.second.c_str(), flag, it.first.c_str());
                }
#endif

#ifdef PS5_NATIVE_GPU
                    // If item has a Jellyfin item ID and user is logged in, attach remote server subtitles
                    if (!this->itemId.empty()) {
                        const auto requestedItem = this->itemId;
                        const auto loadGeneration = lifetime->generation;
                        const auto playlistGeneration = loadPlaylistGeneration;
                        const auto server = AppConfig::instance().getUrl();
                        const auto user = AppConfig::instance().getUserId();
                        const auto authToken = AppConfig::instance().getToken();
                        const auto cancellation = AppConfig::instance().requestCancellation();
                        ASYNC_RETAIN
                        jellyfin::getJSON<jellyfin::Detail>(
                            [ASYNC_TOKEN, flag, requestedItem, loadGeneration, playlistGeneration,
                                server, user, authToken, cancellation](const jellyfin::Detail& detail) {
                                ASYNC_RELEASE
                                if (!lifetime->active || lifetime->generation != loadGeneration ||
                                    this->itemId != requestedItem || !playbackReady ||
                                    MPVCore::instance().playlistGeneration() != playlistGeneration ||
                                    (cancellation && cancellation->load()) ||
                                    !AppConfig::instance().matchesPlaybackSession(server, user, authToken)) return;
                                for (const auto& src : detail.MediaSources) {
                                    for (const auto& s : src.MediaStreams) {
                                        if (s.Type != jellyfin::streamTypeSubtitle) continue;
                                        std::string subUrl = misc::buildSubtitleUrl(
                                            server, requestedItem, src.Id, s.Index, s.Codec, s.IsExternal, s.DeliveryUrl);
                                        if (!subUrl.empty()) {
                                            auto& mpv = MPVCore::instance();
                                            mpv.command("sub-add", subUrl.c_str(), flag, s.DisplayTitle.c_str());
                                        }
#else
                // If item has a Jellyfin item ID and user is logged in, attach remote server subtitles
                if (!this->itemId.empty()) {
                    ASYNC_RETAIN
                    jellyfin::getJSON<jellyfin::Detail>(
                        [ASYNC_TOKEN, flag](const jellyfin::Detail& detail) {
                            ASYNC_RELEASE
                            auto& svr = AppConfig::instance().getUrl();
                            for (const auto& src : detail.MediaSources) {
                                for (const auto& s : src.MediaStreams) {
                                    if (s.Type != jellyfin::streamTypeSubtitle) continue;
                                    std::string subUrl = misc::buildSubtitleUrl(
                                        svr, this->itemId, src.Id, s.Index, s.Codec, s.IsExternal, s.DeliveryUrl);
                                    if (!subUrl.empty()) {
                                        auto& mpv = MPVCore::instance();
                                        mpv.command("sub-add", subUrl.c_str(), flag, s.DisplayTitle.c_str());
#endif
                                    }
                                }
#ifdef PS5_NATIVE_GPU
                            },
                            [ASYNC_TOKEN](const std::string&) { ASYNC_RELEASE },
                            jellyfin::apiUserItem, user, requestedItem);
                    }
                    break;
#else
                            }
                        },
                        nullptr, jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->itemId);
#endif
                }
#ifdef PS5_NATIVE_GPU
                default:;
                }
            });
            settingSubscribeID = view->getSettingEvent()->subscribe([]() {
                brls::View* setting = new PlayerSetting();
                brls::Application::pushActivity(new brls::Activity(setting));
            });
        } catch (...) {
            releaseSubscriptions();
            throw;
        }
#else
                break;
            }
            default:;
            }
        });
        settingSubscribeID = view->getSettingEvent()->subscribe([]() {
            brls::View* setting = new PlayerSetting();
            brls::Application::pushActivity(new brls::Activity(setting));
        });
#endif
    }

    ~RemotePlayer() override {
#ifdef PS5_NATIVE_GPU
        lifetime->active = false;
        failureIntent.reset();
#endif
        auto& mpv = MPVCore::instance();
#ifdef PS5_NATIVE_GPU
        releaseSubscriptions();
#else
        mpv.getEvent()->unsubscribe(eventSubscribeID);
        view->getPlayEvent()->unsubscribe(playSubscribeID);
        view->getSettingEvent()->unsubscribe(settingSubscribeID);
#endif
        mpv.command("write-watch-later-config");
#ifdef PS5_NATIVE_GPU
    }

    void frame(brls::FrameContext* ctx) override {
        brls::Box::frame(ctx);
        pumpLoadFailure();
#endif
    }

#ifdef ANDROID
    void willDisappear(bool resetState) override {
        if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT)
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(235, 235, 235, 255));
        else
            brls::Application::getTheme().addColor("brls/clear", nvgRGBA(45, 45, 45, 255));
    }

    void willAppear(bool resetState) override {
        brls::Application::getTheme().addColor("brls/clear", nvgRGBA(0, 0, 0, 0));
    }
#endif

    void setList(const DirList& list, size_t index, const std::string& extra) {
        // 播放列表
#ifdef PS5_NATIVE_GPU
        clearPlaySubscription();
        titles.clear();
#endif
        DirList urls;
        for (size_t i = 1; i < list.size(); i++) {
            auto& it = list.at(i);
            if (it.type == remote::EntryType::VIDEO) {
                if (i == index) index = urls.size();
                titles.push_back(it.name);
                urls.push_back(it);
            }
        }
        if (titles.size() > 1) view->setList(titles, index);

        playSubscribeID = view->getPlayEvent()->subscribe([this, list, urls, extra](int index) {
            if (index < 0 || index >= (int)urls.size()) {
                return VideoView::close(true);
            }
            MPVCore::instance().reset();
            auto& item = urls.at(index);

            std::string name = item.name;
            auto pos = name.find_last_of(".");
            if (pos != std::string::npos) {
                name = name.substr(0, pos);
            }

            this->subtitles.clear();
            for (auto& s : list) {
                if (s.type == remote::EntryType::SUBTITLE) {
                    if (!s.name.rfind(name, 0)) {
                        this->subtitles.insert(std::make_pair(s.name.substr(pos), s.url()));
                    }
                }
            }
            this->url = item.url();
#ifdef PS5_NATIVE_GPU
            this->submitUrl(this->url, extra);
#else
            MPVCore::instance().setUrl(this->url, extra);
#endif
            view->setTitie(name);
            return true;
        });

        view->getPlayEvent()->fire(index);
    }

#ifdef PS5_NATIVE_GPU
    void setUrl(const std::string& path, const std::string& extra = "", bool playlist = false) {
        clearPlaySubscription();
        if (!playlist)
            playSubscribeID = view->getPlayEvent()->subscribe([](int) { return VideoView::close(true); });
        this->submitUrl(path, extra);
#else
    void setUrl(const std::string& path) {
        playSubscribeID = view->getPlayEvent()->subscribe([](int index) { return VideoView::close(true); });
        MPVCore::instance().setUrl(path);
#endif
    }

#ifdef PS5_NATIVE_GPU
    bool loadList() {
#else
    void loadList() {
#endif
        auto& mpv = MPVCore::instance();
        int64_t count = mpv.getInt("playlist-count");
#ifdef PS5_NATIVE_GPU
        if (count <= 0) {
            playbackReady = false;
            this->deferLoadFailure();
            return false;
        }
        titles.clear();
#endif
        for (int64_t n = 0; n < count; n++) {
            auto key = fmt::format("playlist/{}/title", n);
            titles.push_back(mpv.getString(key));
        }
        if (titles.size() > 1) view->setList(titles, 0);
        view->setTitie(titles.front());

#ifdef PS5_NATIVE_GPU
        clearPlaySubscription();
#endif
        playSubscribeID = view->getPlayEvent()->subscribe([this, &mpv](int index) {
            if (index < 0 || index >= (int)titles.size()) {
                return VideoView::close();
            }
#ifndef PS5_NATIVE_GPU
            MPVCore::instance().reset();
#endif
            view->setTitie(titles.at(index));
            mpv.command("playlist-play-index", std::to_string(index).c_str());
#ifdef PS5_NATIVE_GPU
            loadPlaylistGeneration = mpv.playlistGeneration();
#endif
            return true;
        });
#ifdef PS5_NATIVE_GPU
        return true;
#endif
    }

private:
#ifdef PS5_NATIVE_GPU
    void clearPlaySubscription() {
        if (!playSubscribeID) return;
        view->getPlayEvent()->unsubscribe(*playSubscribeID);
        playSubscribeID.reset();
    }

    void releaseSubscriptions() {
        clearPlaySubscription();
        auto& mpv = MPVCore::instance();
        if (eventSubscribeID) { mpv.getEvent()->unsubscribe(*eventSubscribeID); eventSubscribeID.reset(); }
        if (settingSubscribeID) { view->getSettingEvent()->unsubscribe(*settingSubscribeID); settingSubscribeID.reset(); }
    }

    void submitUrl(const std::string& path, const std::string& extra) {
        ++lifetime->generation;
        failureIntent.reset();
        playbackReady = false;
        const int result = MPVCore::instance().setUrl(path, extra);
        loadPlaylistGeneration = MPVCore::instance().playlistGeneration();
        if (result < 0) {
            brls::Logger::error("mpv: remote load submission rejected code={}", result);
            this->deferLoadFailure();
            return;
        }
        playbackReady = true;
    }

    void deferLoadFailure() {
        if (failureIntent) return;
        const auto generation = lifetime->generation;
        const auto playlistGeneration = loadPlaylistGeneration;
        const auto server = AppConfig::instance().getUrl();
        const auto user = AppConfig::instance().getUserId();
        const auto authToken = AppConfig::instance().getToken();
        const auto cancellation = AppConfig::instance().requestCancellation();
        std::weak_ptr<PlaybackLifetime> owner = lifetime;
        auto valid = [owner, generation, playlistGeneration, server, user, authToken, cancellation]() {
            const auto state = owner.lock();
            return state && state->active && state->generation == generation &&
                MPVCore::instance().playlistGeneration() == playlistGeneration &&
                !(cancellation && cancellation->load()) &&
                AppConfig::instance().getUrl() == server &&
                AppConfig::instance().getUserId() == user &&
                AppConfig::instance().getToken() == authToken;
        };
        failureIntent = std::make_shared<FailureIntent>(FailureIntent{"main/player/error"_i18n, std::move(valid)});
        pumpLoadFailure();
    }

    void pumpLoadFailure() {
        const auto intent = failureIntent;
        if (!intent || intent->queued) return;
        std::weak_ptr<PlaybackLifetime> owner = lifetime;
        intent->queued = true;
        try {
            brls::sync([this, owner, intent]() {
                intent->queued = false;
                const auto state = owner.lock();
                if (!state || !state->active || failureIntent != intent) return;
                if (!intent->valid()) { failureIntent.reset(); return; }
                const auto activities = brls::Application::getActivitiesStack();
                if (activities.empty() || activities.back()->getContentView() != this) return;
                failureIntent.reset(); // Consume before opening the owned dialog.
                try {
                    Dialog::show(intent->message, [this, owner, intent]() {
                        const auto state = owner.lock();
                        if (!state || !state->active || !intent->valid()) return;
                        const auto activities = brls::Application::getActivitiesStack();
                        if (!activities.empty() && activities.back()->getContentView() == this) VideoView::close(true);
                    });
                } catch (...) {}
            });
        } catch (...) {
            intent->queued = false;
        }
    }

#endif
    std::string itemId;
    VideoView* view = new VideoView();
    std::string url;
    std::vector<std::string> titles;
    std::unordered_map<std::string, std::string> subtitles;
#ifdef PS5_NATIVE_GPU
    std::optional<MPVEvent::Subscription> eventSubscribeID;
    std::optional<brls::Event<int>::Subscription> playSubscribeID;
    std::optional<brls::VoidEvent::Subscription> settingSubscribeID;
    struct PlaybackLifetime { uint64_t generation = 0; bool active = true; };
    std::shared_ptr<PlaybackLifetime> lifetime = std::make_shared<PlaybackLifetime>();
    uint64_t loadPlaylistGeneration = 0;
    bool playbackReady = false;
    struct FailureIntent { std::string message; std::function<bool()> valid; bool queued = false; };
    std::shared_ptr<FailureIntent> failureIntent;
#else
    MPVEvent::Subscription eventSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
#endif
};

class FileCard : public RecyclingGridItem {
public:
    FileCard() { this->inflateFromXMLRes("xml/view/dir_entry.xml"); }

    void setCard(const remote::DirEntry& item) {
        if (item.type == remote::EntryType::UP) {
            this->icon->setImageFromSVGRes("icon/ico-folder-up.svg");
            this->name->setText("main/remote/up"_i18n);
            this->size->setText("");
            return;
        }
        this->name->setText(item.name);
        if (item.type == remote::EntryType::DIR) {
            this->icon->setImageFromSVGRes("icon/ico-folder.svg");
            this->size->setText("main/remote/folder"_i18n);
            return;
        }
        if (item.type == remote::EntryType::DEVICE) {
            this->icon->setImageFromSVGRes("icon/ico-folder.svg");
            this->size->setText(item.path);
            return;
        }
        this->size->setText(misc::formatSize(item.fileSize));
        switch (item.type) {
        case remote::EntryType::VIDEO:
            this->icon->setImageFromSVGRes("icon/ico-file-video.svg");
            break;
        case remote::EntryType::AUDIO:
            this->icon->setImageFromSVGRes("icon/ico-file-audio.svg");
            break;
        case remote::EntryType::IMAGE:
            this->icon->setImageFromSVGRes("icon/ico-file-image.svg");
            break;
        case remote::EntryType::BOOK:
            this->icon->setImageFromSVGRes("icon/ico-file-book.svg");
            break;
        case remote::EntryType::PLAYLIST:
            this->icon->setImageFromSVGRes("icon/ico-list.svg");
            break;
        default:
            this->icon->setImageFromSVGRes("icon/ico-file.svg");
        }
    }

private:
    BRLS_BIND(SVGImage, icon, "file/icon");
    BRLS_BIND(brls::Label, name, "file/name");
    BRLS_BIND(brls::Label, size, "file/misc");
};

static std::set<std::string> videoExt = {
    ".mp4", ".mkv", ".avi", ".flv", ".mov", ".wmv", ".webm", ".rm", ".rmvb", ".mpg"};
static std::set<std::string> audioExt = {".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".wma", ".ape"};
static std::set<std::string> imageExt = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp"};
static std::set<std::string> booktExt = {".pdf", ".epub", ".mobi", ".azw3", ".txt"};
static std::set<std::string> playlistExt = {".m3u", ".m3u8"};
static std::set<std::string> subtitleExt = {".srt", ".ass", ".ssa", ".sub", ".smi", ".vtt"};

class FileDataSource : public RecyclingGridDataSource {
public:
    FileDataSource(const DirList& r, RemoteView::Client c) : list(std::move(r)), client(c) {
        for (auto& it : this->list) {
            if (it.type != remote::EntryType::FILE) continue;

            auto pos = it.name.find_last_of('.');
            if (pos == std::string::npos) continue;
            std::string ext = it.name.substr(pos);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (videoExt.count(ext)) {
                it.type = remote::EntryType::VIDEO;
            } else if (audioExt.count(ext)) {
                it.type = remote::EntryType::AUDIO;
            } else if (imageExt.count(ext)) {
                it.type = remote::EntryType::IMAGE;
            } else if (booktExt.count(ext)) {
                it.type = remote::EntryType::BOOK;
            } else if (subtitleExt.count(ext)) {
                it.type = remote::EntryType::SUBTITLE;
            } else if (playlistExt.count(ext)) {
                it.type = remote::EntryType::PLAYLIST;
            }
        }
    }

    size_t getItemCount() override { return this->list.size(); }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        FileCard* cell = dynamic_cast<FileCard*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCard(item);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        if (item.type == remote::EntryType::UP) {
            recycler->getParent()->dismiss();
            return;
        }

        if (item.type == remote::EntryType::DIR || item.type == remote::EntryType::DEVICE) {
            auto* view = dynamic_cast<RemoteView*>(recycler->getParent());
            if (view) view->push(item.path);
            return;
        }

        if (item.type == remote::EntryType::VIDEO) {
            RemotePlayer* view = new RemotePlayer(item);
            view->setList(this->list, index, client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
            return;
        }

        if (item.type == remote::EntryType::AUDIO) {
            DirList urls;
            for (size_t i = 1; i < this->list.size(); i++) {
                auto& it = this->list.at(i);
                if (it.type == remote::EntryType::AUDIO) {
                    if (i == index) index = urls.size();
                    urls.push_back(it);
                }
            }
            MusicView::instance().load(urls, index, client->extraOption());
            return;
        }

        if (item.type == remote::EntryType::IMAGE) {
            brls::Application::pushActivity(new GalleryActivity(item.url(), client->getHeaders()));
            return;
        }
#ifdef USE_MUPDF
        if (item.type == remote::EntryType::BOOK) {
            EBookView* view = new EBookView();
            view->open(item.url(), 0, client->getHeaders());
            brls::Application::pushActivity(new brls::Activity(view));
            return;
        }
#endif
        if (item.type == remote::EntryType::PLAYLIST) {
            RemotePlayer* view = new RemotePlayer(item);
#ifndef PS5_NATIVE_GPU
            MPVCore::instance().setUrl(item.url(), client->extraOption());
#endif
            brls::Application::pushActivity(new brls::Activity(view));
#ifdef PS5_NATIVE_GPU
            view->setUrl(item.url(), client->extraOption(), true);
#endif
        }
    }

    void clearData() override { this->list.clear(); }

private:
    DirList list;
    RemoteView::Client client;
};

UmsView::UmsView() : RemoteView(std::make_shared<remote::Local>()) {
    RecyclingGrid* view = this->newRecycler();
    this->stack.push_back(view);
    this->setContent(view);

    auto ev = Ums::instance().getEvent();
    deviceSubscribeID = ev->subscribe([this, view](const Ums::DeviceList& r) {
        DirList dirs;
        dirs.reserve(r.size());
        for (auto& it : r) {
            remote::DirEntry entry;
            entry.type = it.id < 0 ? remote::EntryType::DIR : remote::EntryType::DEVICE;
            entry.name = it.name;
            entry.path = it.mount + "/";
            dirs.push_back(entry);
        }
        view->setDataSource(new FileDataSource(dirs, this->client));
    });
    ev->fire(Ums::instance().getDevice());
}

UmsView::~UmsView() { Ums::instance().getEvent()->unsubscribe(deviceSubscribeID); }

RemoteView::RemoteView(Client c) : client(c) { brls::Logger::debug("RemoteView: create"); }

RemoteView::~RemoteView() {
    brls::Logger::debug("RemoteView: deleted");
#ifdef PS5_NATIVE_GPU
    for (const auto& pending : listingCancellation) pending.second->store(true);
    // setContent detaches the previous directory; Box owns only the active one.
    for (auto* view : stack)
        if (view != recycler) view->freeView();
#endif
    this->setDimensions(View::AUTO, View::AUTO);
#ifdef PS5_NATIVE_GPU
    PlayerSetting::resetTrackSelections();
#else
    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;
#endif

    /// 通知 MusicView 已关闭
    MusicView::instance().setParent(nullptr);
}

brls::View* RemoteView::getDefaultFocus() { return this->recycler; }

void RemoteView::push(const std::string& path) {
    RecyclingGrid* view = this->newRecycler();
    this->stack.push_back(view);
    this->setContent(view);
#ifdef PS5_NATIVE_GPU
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    listingCancellation[view] = cancelled;
    const auto requestClient = client;
    const auto requestMutex = listingMutex;
    auto* initialFocus = brls::Application::getCurrentFocus();
#endif

    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    ThreadPool::instance().submit([ASYNC_TOKEN, path, requestClient, requestMutex, view, cancelled, initialFocus](HTTP&) {
        DirList entries;
        std::string error;
#else
    // path 按值捕获：submit 异步执行，引用捕获会在本函数返回后悬垂
    ThreadPool::instance().submit([ASYNC_TOKEN, path](HTTP&) {
#endif
        try {
#ifdef PS5_NATIVE_GPU
            // Apache/WebDAV keep a single HTTP handle per client. Waiting here
            // never blocks the UI; popped requests skip transport after waiting.
            std::lock_guard<std::mutex> lock(*requestMutex);
            if (!cancelled->load()) entries = requestClient->list(path);
#else
            auto r = client->list(path);
            brls::sync([ASYNC_TOKEN, r]() {
                ASYNC_RELEASE
                this->recycler->setDataSource(new FileDataSource(r, client));
                if (this->stack.size() > 1) brls::Application::giveFocus(this->recycler);
            });
#endif
        } catch (const std::exception& ex) {
#ifdef PS5_NATIVE_GPU
            error = ex.what();
#else
            std::string error = ex.what();
            brls::sync([ASYNC_TOKEN, error]() {
                ASYNC_RELEASE
                this->recycler->setError(error);
            });
#endif
        }
#ifdef PS5_NATIVE_GPU
        brls::sync([ASYNC_TOKEN, entries = std::move(entries), error, requestClient, view, cancelled, initialFocus]() {
            ASYNC_RELEASE
            // A popped target may have been deleted and its address reused.
            // Check its unique cancellation token before touching the pointer.
            if (cancelled->load()) return;
            listingCancellation.erase(view);
            if (!error.empty()) {
                view->setError(error);
                return;
            }
            bool restoreFocus = brls::Application::getCurrentFocus() == initialFocus;
            for (auto* focus = brls::Application::getCurrentFocus(); focus; focus = focus->getParent())
                if (focus == view) restoreFocus = true;
            view->setDataSource(new FileDataSource(entries, requestClient));
            if (this->recycler == view && restoreFocus && this->stack.size() > 1)
                brls::Application::giveFocus(view);
        });
#endif
    });
}

void RemoteView::dismiss(std::function<void(void)> cb) {
    if (this->stack.size() > 1) {
        brls::View* lastView = this->recycler;
#ifdef PS5_NATIVE_GPU
        auto pending = listingCancellation.find(this->recycler);
        if (pending != listingCancellation.end()) {
            pending->second->store(true);
            listingCancellation.erase(pending);
        }
#endif
        this->stack.pop_back();
        this->setContent(this->stack.back());
        cb();
        lastView->freeView();
    } else if (brls::Application::getInputType() == brls::InputType::TOUCH) {
        brls::View::dismiss(cb);
    } else {
        AutoTabFrame::focus2Sidebar(this);
    }
}

void RemoteView::setContent(RecyclingGrid* view) {
    if (this->recycler) {
        this->removeView(this->recycler, false);
        this->recycler = nullptr;
    }

    this->recycler = view;
    this->recycler->setDimensions(View::AUTO, View::AUTO);
    this->recycler->setGrow(1.0f);
    this->addView(this->recycler);
    brls::Application::giveFocus(this->recycler);
}

RecyclingGrid* RemoteView::newRecycler() {
    RecyclingGrid* view = new RecyclingGrid();
    view->spanCount = 1;
    view->estimatedRowHeight = 48;
    view->estimatedRowSpace = 10;
    view->setDefaultCellFocus(1);
    view->registerCell("Cell", []() { return new FileCard(); });
    view->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });
    return view;
}

void RemoteView::play(const std::string& path, const std::string& name, const std::string& itemId) {
    RemotePlayer* view = new RemotePlayer({remote::EntryType::VIDEO, name, path}, itemId);
    brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
    view->setUrl(path);
#ifdef PS5_NATIVE_GPU
}

#else
}
#endif
