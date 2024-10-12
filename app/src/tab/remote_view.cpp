#include "tab/remote_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
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
        }

        auto& mpv = MPVCore::instance();
        eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
            auto& mpv = MPVCore::instance();
            switch (event) {
            case MpvEventEnum::MPV_LOADED: {
                if (titles.empty()) this->loadList();
                view->getProfile()->init();
                const char* flag = MPVCore::SUBS_FALLBACK ? "auto" : "select";
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
    }

    void setList(const DirList& list, size_t index, RemoteView::Client c) {
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

        playSubscribeID = view->getPlayEvent()->subscribe([this, list, urls, c](int index) {
            if (index < 0 || index >= (int)urls.size()) {
                return VideoView::close();
            }
            auto& it = urls.at(index);

            std::string name = it.name;
            auto pos = name.find_last_of(".");
            if (pos != std::string::npos) {
                name = name.substr(0, pos);
            }

            this->subtitles.clear();
            for (auto& it : list) {
                if (it.type == remote::EntryType::SUBTITLE) {
                    if (!it.name.rfind(name, 0)) {
                        this->subtitles.insert(std::make_pair(it.name.substr(pos), it.url.empty() ? it.path : it.url));
                    }
                }
            }
            MPVCore::instance().reset();
            MPVCore::instance().setUrl(it.path, c->extraOption());
            view->setTitie(name);
            return true;
        });

        view->getPlayEvent()->fire(index);
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
            view->setTitie(titles.at(index));
            mpv.command("playlist-play-index", std::to_string(index).c_str());
            return true;
        });
    }

private:
    VideoView* view = new VideoView();
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
        this->name->setText(item.name);
        if (item.type == remote::EntryType::DIR) {
            this->icon->setImageFromSVGRes("icon/ico-folder.svg");
            this->size->setText("main/remote/folder"_i18n);
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

static std::set<std::string> videoExt = {".mp4", ".mkv", ".avi", ".flv", ".mov", ".wmv", ".webm"};
static std::set<std::string> audioExt = {".mp3", ".flac", ".wav", ".ogg", ".m4a"};
static std::set<std::string> imageExt = {".jpg", ".jpeg", ".png", ".bmp", ".gif"};
static std::set<std::string> subtitleExt = {".srt", ".ass", ".ssa", ".sub", ".smi"};

class FileDataSource : public RecyclingGridDataSource {
public:
    FileDataSource(const DirList& r, RemoteView::Client c) : list(std::move(r)), client(c) {
        std::sort(this->list.begin(), this->list.end(), [](auto i, auto j) { return i.name < j.name; });

        for (auto& it : this->list) {
            if (it.type == remote::EntryType::DIR) continue;

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
            } else if (ext == ".m3u") {
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
        if (index == 0) {
            recycler->getParent()->dismiss();
            return;
        }

        if (item.type == remote::EntryType::DIR) {
            auto* view = dynamic_cast<RemoteView*>(recycler->getParent());
            if (view) view->push(item.path);
            return;
        }

        if (item.type == remote::EntryType::VIDEO) {
            RemotePlayer* view = new RemotePlayer(item);
            view->setList(this->list, index, client);
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
            return;
        }

        if (item.type == remote::EntryType::IMAGE) {
            return;
        }

        if (item.type == remote::EntryType::PLAYLIST) {
            RemotePlayer* view = new RemotePlayer(item);
            MPVCore::instance().setUrl(item.url.empty() ? item.path : item.url, client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
        }
    }

    void clearData() override { this->list.clear(); }

private:
    DirList list;
    RemoteView::Client client;
};

RemoteView::RemoteView(Client c) : client(c) {
    this->inflateFromXMLRes("xml/tabs/remote_view.xml");
    brls::Logger::debug("RemoteView: create");

    this->recycler->registerCell("Cell", []() { return new FileCard(); });
}

RemoteView::~RemoteView() {
    brls::Logger::debug("RemoteView: deleted");
    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;
}

brls::View* RemoteView::getDefaultFocus() { return this->recycler; }

void RemoteView::onCreate() {
    this->recycler->registerAction("hints/back"_i18n, brls::BUTTON_B, [this](...) {
        this->dismiss();
        return true;
    });

    this->registerAction("hints/refresh"_i18n, brls::BUTTON_X, [this](...) {
        this->load();
        return true;
    });
}

void RemoteView::push(const std::string& path) {
    this->stack.push_back(path);
    this->load();
}

void RemoteView::dismiss(std::function<void(void)> cb) {
    if (this->stack.size() > 1) {
        this->stack.pop_back();
        this->load();
    } else if (brls::Application::getInputType() == brls::InputType::TOUCH) {
        brls::View::dismiss();
    } else {
        AutoTabFrame::focus2Sidebar(this);
    }
}

void RemoteView::load() {
    this->recycler->showSkeleton();
    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN]() {
        try {
            auto r = client->list(this->stack.back());
            brls::sync([ASYNC_TOKEN, r]() {
                ASYNC_RELEASE
                this->recycler->setDataSource(new FileDataSource(r, client));
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