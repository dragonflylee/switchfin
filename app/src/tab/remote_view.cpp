#include "tab/remote_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/icon_button.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/mpv_core.hpp"
#include "view/music_view.hpp"
#include "view/player_setting.hpp"
#include "client/local.hpp"
#include "view/remote_filter.hpp"
#include "utils/misc.hpp"
#include "utils/config.hpp"
#include "utils/dialog.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;

class RemotePlayer : public brls::Box {
public:
    RemotePlayer(const remote::DirEntry& item, const std::string& method = "") {
        float width = brls::Application::contentWidth;
        float height = brls::Application::contentHeight;
        view->setDimensions(width, height);
        view->setWidthPercentage(100);
        view->setHeightPercentage(100);
        view->setId("video");
        view->setTitie(item.name);
        view->hideVideoQuality();
        // local/remote playback: embedded mpv tracks only (no Plex Media). Same
        // wiring as the downloads player — without this the audio/subtitle OSD
        // buttons were shown but had no handler, so tracks looked unavailable.
        view->registerVideoSubtitle([](...) {
            PlayerSetting::showSubtitleMenu(nullptr);
            return true;
        });
        view->registerVideoAudio([](...) {
            PlayerSetting::showAudioMenu(nullptr);
            return true;
        });
        this->setDimensions(width, height);
        this->addView(view);

        if (item.type == remote::EntryType::PLAYLIST) {
            view->hideVideoProgressSlider();
        } else if (item.name.size() > 0) {
            titles.push_back(item.name);
        }

        auto& mpv = MPVCore::instance();
        eventSubscribeID = mpv.getEvent()->subscribe([this, method](MpvEventEnum event) {
            auto& mpv = MPVCore::instance();
            switch (event) {
            case MpvEventEnum::MPV_LOADED: {
                if (titles.empty()) this->loadList();
                view->getProfile()->init(method);
                const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
                for (auto& it : this->subtitles) {
                    mpv.command("sub-add", it.second.c_str(), flag, it.first.c_str());
                }
                break;
            }
            default:;
            }
        });
        settingSubscribeID = view->getSettingEvent()->subscribe([]() {
            brls::View* setting = new PlayerSetting();
            brls::Application::pushActivity(new brls::Activity(setting));
        });
    }

    ~RemotePlayer() override {
        auto& mpv = MPVCore::instance();
        mpv.getEvent()->unsubscribe(eventSubscribeID);
        view->getPlayEvent()->unsubscribe(playSubscribeID);
        view->getSettingEvent()->unsubscribe(settingSubscribeID);
        mpv.command("write-watch-later-config");
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
        // the constructor may have seeded a single title (standalone playback);
        // a real list rebuilds it from scratch, otherwise titles/urls desync
        titles.clear();
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
            MPVCore::instance().setUrl(this->url, extra);
            view->setTitie(name);
            return true;
        });

        view->getPlayEvent()->fire(index);
    }

    void setUrl(const std::string& path) {
        playSubscribeID = view->getPlayEvent()->subscribe([](int index) { return VideoView::close(true); });
        MPVCore::instance().setUrl(path);
    }

    void loadList() {
        auto& mpv = MPVCore::instance();
        int64_t count = mpv.getInt("playlist-count");
        for (int64_t n = 0; n < count; n++) {
            auto key = fmt::format("playlist/{}/title", n);
            titles.push_back(mpv.getString(key));
        }
        if (titles.size() > 1) view->setList(titles, 0);
        view->setTitie(titles.front());

        playSubscribeID = view->getPlayEvent()->subscribe([this, &mpv](int index) {
            if (index < 0 || index >= (int)titles.size()) {
                return VideoView::close();
            }
            MPVCore::instance().reset();
            view->setTitie(titles.at(index));
            mpv.command("playlist-play-index", std::to_string(index).c_str());
            return true;
        });
    }

private:
    VideoView* view = new VideoView();
    std::string url;
    std::vector<std::string> titles;
    std::unordered_map<std::string, std::string> subtitles;
    MPVEvent::Subscription eventSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
    brls::VoidEvent::Subscription settingSubscribeID;
};

class FileCard : public RecyclingGridItem {
public:
    FileCard() {
        this->inflateFromXMLRes("xml/view/dir_entry.xml");
        // X / F4 = pin/unpin a folder to the Files screen (issue #24). Enabled
        // per row via setActionAvailable (see cellForRow); when disabled X
        // falls through to the tab's "Add a server" as before.
        auto options = [this](brls::View*) -> bool {
            if (!this->onOptions) return false;
            this->onOptions();
            return true;
        };
        this->registerAction("hints/option"_i18n, brls::BUTTON_X, options);
        this->registerAction(KeyBind::getSetting(), options);
    }

    /// Set per bind by FileDataSource::cellForRow — opens the pin context menu.
    std::function<void()> onOptions;

    void setCard(const remote::DirEntry& item) {
        if (item.type == remote::EntryType::UP) {
            this->icon->setImageFromSVGRes("icon/ico-folder-up.svg");
            this->name->setText("main/remote/up"_i18n);
            this->size->setText("");
            return;
        }
        if (item.type == remote::EntryType::BLURAY) {
            this->icon->setImageFromSVGRes("icon/ico-play.svg");
            this->name->setText("main/remote/bluray"_i18n);
            this->size->setText("");
            return;
        }
        this->name->setText(item.name);
        // pinned shortcut on the Files root (issue #24): a star sets it apart
        // from plain folders / the SD card
        if (item.pinned) {
            this->icon->setImageFromSVGRes("icon/ico-star.svg");
            this->size->setText("main/remote/folder"_i18n);
            return;
        }
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

static std::set<std::string> videoExt = {".mp4", ".mkv", ".avi", ".flv", ".mov", ".wmv", ".webm", ".rm",
    ".rmvb", ".mpg", ".m2ts", ".mts", ".ts"};
static std::set<std::string> audioExt = {".mp3", ".flac", ".wav", ".ogg", ".m4a", ".aac", ".wma", ".ape"};
static std::set<std::string> imageExt = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp"};
static std::set<std::string> playlistExt = {".m3u", ".m3u8"};
static std::set<std::string> subtitleExt = {".srt", ".ass", ".ssa", ".sub", ".smi"};

class FileDataSource : public RecyclingGridDataSource {
public:
    /// isRoot: the Files root screen (devices + pinned shortcuts). Its order is
    /// fixed (SD card first, then pins) and never re-sorted (issue #23).
    FileDataSource(const DirList& r, RemoteView::Client c, bool isRoot = false)
        : list(std::move(r)), client(c), isRoot(isRoot) {
        // pinning targets the local storage only (issue #24)
        this->isLocal = (bool)std::dynamic_pointer_cast<remote::Local>(c);
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
            } else if (subtitleExt.count(ext)) {
                it.type = remote::EntryType::SUBTITLE;
            } else if (playlistExt.count(ext)) {
                it.type = remote::EntryType::PLAYLIST;
            }
        }
        this->resort();
    }

    size_t getItemCount() override { return this->list.size(); }

    /// Re-apply the current sort choice to the listing (issue #23). No-op on
    /// the root screen, whose order is intentional.
    void resort() {
        if (this->isRoot) return;
        remote::sortEntries(this->list, RemoteFilter::key(), RemoteFilter::desc());
    }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        FileCard* cell = dynamic_cast<FileCard*>(recycler->dequeueReusableCell("Cell"));
        auto& item = this->list.at(index);
        cell->setCard(item);
        // pin/unpin is offered only on local folders; on the root screen only
        // pinned shortcuts are actionable (a device root can't be pinned)
        bool folder = item.type == remote::EntryType::DIR || item.type == remote::EntryType::DEVICE;
        bool actionable = this->isLocal && folder && (this->isRoot ? item.pinned : true);
        cell->onOptions = actionable ? std::function<void()>([this, index]() { this->onContextMenu(index); }) : nullptr;
        cell->setActionAvailable(brls::BUTTON_X, actionable);
        return cell;
    }

    void onItemSelected(brls::Box* recycler, size_t index) override {
        auto& item = this->list.at(index);
        if (item.type == remote::EntryType::UP) {
            recycler->getParent()->dismiss();
            return;
        }

        if (item.type == remote::EntryType::DIR || item.type == remote::EntryType::DEVICE) {
            // a pinned shortcut whose folder vanished (renamed/removed/unplugged):
            // offer to drop it instead of failing silently (issue #24)
            if (item.pinned && !folderExists(item.path)) {
                std::string path = item.path;
                Dialog::cancelable("main/remote/pin_missing"_i18n, [path]() {
                    AppConfig::instance().removePin(path);
                    brls::sync([]() { Ums::instance().getEvent()->fire(Ums::instance().getDevice()); });
                });
                return;
            }
            auto* view = dynamic_cast<RemoteView*>(recycler->getParent());
            if (view) view->push(item.path);
            return;
        }

        if (item.type == remote::EntryType::BLURAY) {
            // item.path is the BDMV/STREAM folder: gather its .m2ts streams and
            // hand them to the player as a title list in natural (disc) order,
            // auto-starting on the largest — on a movie backup the biggest
            // stream is the feature film (issue #18). Listing local storage is
            // synchronous, so no async round-trip is needed here.
            DirList streams;
            try {
                streams = client->list(item.path);
            } catch (const std::exception&) {
                return;
            }
            DirList urls;
            for (auto& s : streams) {
                if (s.type != remote::EntryType::FILE) continue;
                auto pos = s.name.find_last_of('.');
                if (pos == std::string::npos) continue;
                std::string ext = s.name.substr(pos);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".m2ts" && ext != ".mts") continue;
                s.type = remote::EntryType::VIDEO;
                urls.push_back(s);
            }
            if (urls.empty()) return;
            std::sort(urls.begin(), urls.end(),
                [](const remote::DirEntry& a, const remote::DirEntry& b) { return a.name < b.name; });
            size_t largest = 0;
            for (size_t i = 1; i < urls.size(); i++) {
                if (urls.at(i).fileSize > urls.at(largest).fileSize) largest = i;
            }
            // setList skips index 0 and treats the argument as a list index, so
            // prepend the UP sentinel and target the largest at largest + 1
            DirList list;
            list.push_back({remote::EntryType::UP});
            for (auto& u : urls) list.push_back(u);
            RemotePlayer* view = new RemotePlayer(urls.at(largest));
            view->setList(list, largest + 1, client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
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
            return;
        }

        if (item.type == remote::EntryType::PLAYLIST) {
            RemotePlayer* view = new RemotePlayer(item);
            MPVCore::instance().setUrl(item.url(), client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
        }
    }

    void clearData() override { this->list.clear(); }

    /// X / F4 on a folder: pin it to the Files root, or remove an existing
    /// pin (issue #24). Local storage only; devices roots can't be pinned.
    void onContextMenu(size_t index) {
        if (!this->isLocal || index >= this->list.size()) return;
        auto& item = this->list.at(index);
        bool isFolder = item.type == remote::EntryType::DIR || item.type == remote::EntryType::DEVICE;
        if (!isFolder) return;
        // on the root screen only pinned shortcuts are actionable (a device
        // root like "sdmc:/" is already there, nothing to pin)
        if (this->isRoot && !item.pinned) return;

        auto& conf = AppConfig::instance();
        std::string path = item.path;
        bool pinned = item.pinned || conf.isPinned(path);
        std::string label = pinned ? "main/remote/unpin"_i18n : "main/remote/pin"_i18n;

        auto* dropdown = new brls::Dropdown(item.name, {label}, [path, pinned](int selected) {
            auto& conf = AppConfig::instance();
            if (pinned) {
                conf.removePin(path);
                brls::Application::notify("main/remote/unpinned"_i18n);
            } else {
                conf.addPin(path);
                brls::Application::notify("main/remote/pinned"_i18n);
            }
            // rebuild the root shortcut list on the next frame (this data
            // source may be the one being replaced — don't self-destruct)
            brls::sync([]() { Ums::instance().getEvent()->fire(Ums::instance().getDevice()); });
        });
        brls::Application::pushActivity(new brls::Activity(dropdown));
    }

private:
    /// True if the browser path points at an existing directory (issue #24).
    static bool folderExists(const std::string& path) {
        std::string p = path.rfind("file://") == 0 ? path.substr(7) : path;
        std::error_code ec;
        return fs::is_directory(p, ec);
    }

    DirList list;
    RemoteView::Client client;
    bool isRoot = false;
    bool isLocal = false;
};

/// Short label for a pinned folder shown on the Files root: its last path
/// component (issue #24). "sdmc:/A_Media/" -> "A_Media".
static std::string pinDisplayName(const std::string& path) {
    std::string p = path;
    if (p.rfind("file://", 0) == 0) p = p.substr(7);
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    auto pos = p.find_last_of('/');
    std::string name = pos == std::string::npos ? p : p.substr(pos + 1);
    return name.empty() ? path : name;
}

UmsView::UmsView(std::function<void()> onAddServer) : RemoteView(std::make_shared<remote::Local>()) {
    RecyclingGrid* view = this->newRecycler();
    this->stack.push_back(view);
    this->setContent(view);

    // custom empty state: same metrics as RecyclingGrid::setEmpty
    // (icon 56, title 17, gray subtitle 14) + "Add a server" button
    // under the placeholder — the grid cannot host one
    this->emptyBox = new brls::Box(brls::Axis::COLUMN);
    this->emptyBox->setGrow(1.0f);
    // same inset as the recycler: floating tab bar (60)
    this->emptyBox->setPaddingTop(70);
    this->emptyBox->setJustifyContent(brls::JustifyContent::CENTER);
    this->emptyBox->setAlignItems(brls::AlignItems::CENTER);
    this->emptyBox->setVisibility(brls::Visibility::GONE);

    auto* hintIcon = new SVGImage();
    hintIcon->setDimensions(56, 56);
    hintIcon->setImageFromSVGRes("icon/ico-folder.svg");
    this->emptyBox->addView(hintIcon);

    auto* hintTitle = new brls::Label();
    hintTitle->setFontSize(17);
    hintTitle->setMarginTop(12);
    hintTitle->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    hintTitle->setText("main/remote/empty_title"_i18n);
    this->emptyBox->addView(hintTitle);

    auto* hintSub = new brls::Label();
    hintSub->setFontSize(14);
    hintSub->setMarginTop(6);
    hintSub->setTextColor(brls::Application::getTheme().getColor("font/grey"));
    hintSub->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    hintSub->setText("main/remote/empty_sub"_i18n);
    this->emptyBox->addView(hintSub);

    if (onAddServer) {
        auto* btn = new IconButton();
        btn->setIcon("icon/ico-plus.svg");
        btn->setText("main/remote/add_server"_i18n);
        btn->setButtonStyle("bordered");
        btn->setMarginTop(24);
        btn->registerClickAction([onAddServer](...) {
            onAddServer();
            return true;
        });
        this->addButton = btn;
        this->emptyBox->addView(btn);
    }
    this->addView(this->emptyBox);

    auto ev = Ums::instance().getEvent();
    deviceSubscribeID = ev->subscribe([this, view](const Ums::DeviceList& r) {
        DirList dirs;
        const auto& pins = AppConfig::instance().getPins();
        dirs.reserve(r.size() + pins.size());
        for (auto& it : r) {
            remote::DirEntry entry;
            entry.type = it.id < 0 ? remote::EntryType::DIR : remote::EntryType::DEVICE;
            entry.name = it.name;
            entry.path = it.mount + "/";
            dirs.push_back(entry);
        }
        // pinned local folders (issue #24): listed after the devices, flagged
        // so FileCard shows the star icon and the context menu offers "unpin"
        for (const auto& p : pins) {
            remote::DirEntry entry;
            entry.type = remote::EntryType::DIR;
            entry.pinned = true;
            entry.path = p;
            entry.name = pinDisplayName(p);
            dirs.push_back(entry);
        }
        // no device/volume AND no pin: explicit empty state rather than a
        // mute grid ("Files" placeholder)
        if (dirs.empty()) {
            view->setVisibility(brls::Visibility::GONE);
            this->emptyBox->setVisibility(brls::Visibility::VISIBLE);
        } else {
            this->emptyBox->setVisibility(brls::Visibility::GONE);
            view->setVisibility(brls::Visibility::VISIBLE);
            // isRoot: keep the device/pin order, never re-sorted (issue #23)
            view->setDataSource(new FileDataSource(dirs, this->client, true));
        }
    });
    ev->fire(Ums::instance().getDevice());
}

UmsView::~UmsView() { Ums::instance().getEvent()->unsubscribe(deviceSubscribeID); }

brls::View* UmsView::getDefaultFocus() {
    if (this->addButton && this->emptyBox->getVisibility() == brls::Visibility::VISIBLE)
        return this->addButton->getDefaultFocus();
    return RemoteView::getDefaultFocus();
}

RemoteView::RemoteView(Client c) : client(c) { brls::Logger::debug("RemoteView: create"); }

RemoteView::~RemoteView() {
    brls::Logger::debug("RemoteView: deleted");
    this->setDimensions(View::AUTO, View::AUTO);
    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    /// 通知 MusicView 已关闭
    MusicView::instance().setParent(nullptr);
}

brls::View* RemoteView::getDefaultFocus() { return this->recycler; }

void RemoteView::push(const std::string& path) {
    RecyclingGrid* view = this->newRecycler();
    this->stack.push_back(view);
    this->setContent(view);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, &path]() {
        try {
            auto r = client->list(path);
            brls::sync([ASYNC_TOKEN, r]() {
                ASYNC_RELEASE
                this->recycler->setDataSource(new FileDataSource(r, client));
                if (this->stack.size() > 1) brls::Application::giveFocus(this->recycler);
            });
        } catch (const std::exception& ex) {
            std::string error = ex.what();
            brls::sync([ASYNC_TOKEN, error]() {
                ASYNC_RELEASE
                this->recycler->setError(error);
            });
        }
    });
}

void RemoteView::dismiss(std::function<void(void)> cb) {
    if (this->stack.size() > 1) {
        brls::View* lastView = this->recycler;
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
    // RecyclingGrid seeds a 12-cell skeleton (poster grid: 4 cols x 240px)
    // in its constructor. In this single-column file browser it just flashes
    // a screenful of oversized placeholders on every folder change — and
    // because that skeleton overflows the viewport, reloadData scrolls to the
    // default focus cell, which drags the floating tab bar off the top. Local
    // listings are instant and even remote ones are quick, so drop the
    // placeholder: an empty grid for the frame or two before the data lands.
    view->setDataSource(nullptr);
    // inside the scroll: the list starts under the Downloads tab's
    // floating tab bar (60) and scrolls beneath it
    view->setPaddingTop(70);
    view->setDefaultCellFocus(1);
    view->registerCell("Cell", []() { return new FileCard(); });
    view->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });
    // Y = sort panel (Name/Date/Size). Client-side: re-apply to the current
    // listing on close, no re-fetch (issue #23). No-op on the root screen.
    view->registerAction("main/media/sort"_i18n, brls::BUTTON_Y, [this](...) {
        RemoteFilter* filter = new RemoteFilter();
        filter->getEvent()->subscribe([this]() {
            auto* ds = dynamic_cast<FileDataSource*>(this->recycler->getDataSource());
            if (ds) {
                ds->resort();
                this->recycler->reloadData();
            }
        });
        brls::Application::pushActivity(new brls::Activity(filter));
        return true;
    });
    return view;
}

void RemoteView::play(const std::string& path, const std::string& name, const std::string& method) {
    RemotePlayer* view = new RemotePlayer({remote::EntryType::VIDEO, name, path}, method);
    brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
    view->setUrl(path);
}
