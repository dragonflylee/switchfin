#include "tab/remote_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/mpv_core.hpp"
#include "view/music_view.hpp"
#include "view/player_setting.hpp"
#include "client/local.hpp"
#include "utils/misc.hpp"
#include "utils/config.hpp"

using namespace brls::literals;

class RemotePlayer : public brls::Box {
public:
    RemotePlayer(const remote::DirEntry& item) {
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
static std::set<std::string> playlistExt = {".m3u", ".m3u8"};
static std::set<std::string> subtitleExt = {".srt", ".ass", ".ssa", ".sub", ".smi"};

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
            return;
        }

        if (item.type == remote::EntryType::PLAYLIST) {
            RemotePlayer* view = new RemotePlayer(item);
            MPVCore::instance().setUrl(item.url(), client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
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
    view->setDefaultCellFocus(1);
    view->registerCell("Cell", []() { return new FileCard(); });
    view->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });
    return view;
}

void RemoteView::play(const std::string& path, const std::string& name) {
    RemotePlayer* view = new RemotePlayer({remote::EntryType::VIDEO, name, path});
    brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
    view->setUrl(path);
}