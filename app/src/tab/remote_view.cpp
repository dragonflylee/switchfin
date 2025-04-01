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

const int RESUME_THRESHOLD = 60;

class RemoteResume {
public:
    RemoteResume(const std::string& name) {
        this->path = fmt::format("{}/resume_{}.json", AppConfig::instance().configDir(), name);

        std::ifstream readFile(this->path);
        if (readFile.is_open()) {
            try {
                this->list = nlohmann::json::parse(readFile);
            } catch (const std::exception& e) {
                brls::Logger::error("load resume history: {}", e.what());
            }
        }
    }

    void end(const std::string url) {
        if (this->list.erase(url)) this->save();
    }

    int64_t get(const std::string url) {
        auto it = this->list.find(url);
        if (it == this->list.end()) return 0;
        return it->second;
    }

    void update(const std::string url, int64_t progress) {
        this->list.insert_or_assign(url, progress);
        this->save();
    }

private:
    std::unordered_map<std::string, int64_t> list;
    std::string path;  // 播放历史

    void save() {
        std::ofstream writeFile(this->path);
        if (writeFile.is_open()) {
            nlohmann::json j(this->list);
            writeFile << j.dump(2);
            writeFile.close();
        }
    }
};

class RemotePlayer : public brls::Box {
public:
    RemotePlayer(const remote::DirEntry& item, RemoteResume* rr) {
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
        this->resume = rr;

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
                const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
                for (auto& it : this->subtitles) {
                    mpv.command("sub-add", it.second.c_str(), flag, it.first.c_str());
                }
                break;
            }
            case MpvEventEnum::END_OF_FILE:
                if (this->resume) this->resume->end(this->url);
                break;
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
        if (this->resume) {
            if (mpv.video_progress > RESUME_THRESHOLD && mpv.duration - mpv.video_progress > RESUME_THRESHOLD) {
                this->resume->update(this->url, mpv.video_progress);
            } else {
                this->resume->end(this->url);
            }
        }
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
            std::string extra = c->extraOption();
            int64_t seek = this->resume->get(this->url);
            if (seek > 0) {
                extra += (extra.empty() ? "start=" : ",start=") + misc::sec2Time(seek);
            }
            MPVCore::instance().setUrl(this->url, extra);
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
            MPVCore::instance().reset();
            view->setTitie(titles.at(index));
            mpv.command("playlist-play-index", std::to_string(index).c_str());
            return true;
        });
    }

private:
    VideoView* view = new VideoView();
    RemoteResume* resume;
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
static std::set<std::string> playlistExt = {".m3u", ".m3u8"};
static std::set<std::string> subtitleExt = {".srt", ".ass", ".ssa", ".sub", ".smi"};

class FileDataSource : public RecyclingGridDataSource {
public:
    FileDataSource(const DirList& r, RemoteView::Client c, RemoteResume* rr)
        : list(std::move(r)), resume(rr), client(c) {
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

        if (item.type == remote::EntryType::DIR) {
            auto* view = dynamic_cast<RemoteView*>(recycler->getParent());
            if (view) view->push(item.path);
            return;
        }

        if (item.type == remote::EntryType::VIDEO) {
            RemotePlayer* view = new RemotePlayer(item, this->resume);
            view->setList(this->list, index, client);
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
            return;
        }

        if (item.type == remote::EntryType::IMAGE) {
            return;
        }

        if (item.type == remote::EntryType::PLAYLIST) {
            RemotePlayer* view = new RemotePlayer(item, nullptr);
            MPVCore::instance().setUrl(item.url(), client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
        }
    }

    void clearData() override { this->list.clear(); }

private:
    DirList list;
    RemoteResume* resume;
    RemoteView::Client client;
};

RemoteView::RemoteView(Client c, const std::string& name) : client(c) {
    this->inflateFromXMLRes("xml/tabs/remote_view.xml");
    brls::Logger::debug("RemoteView: create");

    this->resume = std::make_unique<RemoteResume>(name);
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
                this->recycler->setDataSource(new FileDataSource(r, client, this->resume.get()));
            });
        } catch (const std::exception& ex) {
            std::string error = ex.what();
            brls::sync([ASYNC_TOKEN, error]() {
                ASYNC_RELEASE
                this->recycler->setError(error);

                auto dialog = new brls::Dialog(error);
                dialog->addButton("hints/retry"_i18n, [this]() { brls::sync([this]() { this->load(); }); });
                dialog->addButton("hints/cancel"_i18n, []() {});
                dialog->open();
            });
        }
    });
}