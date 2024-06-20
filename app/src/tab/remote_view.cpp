#include "tab/remote_view.hpp"
#include "view/recycling_grid.hpp"
#include "view/svg_image.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include "view/mpv_core.hpp"
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
        this->setDimensions(width, height);
        this->addView(view);

        auto& mpv = MPVCore::instance();
        eventSubscribeID = mpv.getEvent()->subscribe([this, item](MpvEventEnum event) {
            switch (event) {
            case MpvEventEnum::MPV_LOADED:
                view->getProfile()->init(item.name);
                break;
            default:;
            }
        });

        playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });
    }

    ~RemotePlayer() override {
        auto& mpv = MPVCore::instance();
        mpv.getEvent()->unsubscribe(eventSubscribeID);
        view->getPlayEvent()->unsubscribe(playSubscribeID);
    }

    bool playIndex(int index) { return VideoView::dismiss(); }

private:
    VideoView* view = new VideoView();
    MPVEvent::Subscription eventSubscribeID;
    brls::Event<int>::Subscription playSubscribeID;
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

class FileDataSource : public RecyclingGridDataSource {
public:
    FileDataSource(const DirList& r, RemoteView::Client c) : list(std::move(r)), client(c) {
        for (auto& it : this->list) {
            if (it.type == remote::EntryType::DIR) continue;

            auto pos = it.name.find_last_of('.');
            if (pos == std::string::npos) continue;
            std::string ext = it.name.substr(pos);
            if (videoExt.count(ext)) {
                it.type = remote::EntryType::VIDEO;
            } else if (audioExt.count(ext)) {
                it.type = remote::EntryType::AUDIO;
            } else if (imageExt.count(ext)) {
                it.type = remote::EntryType::IMAGE;
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
        if (item.type == remote::EntryType::DIR) {
            auto* view = dynamic_cast<RemoteView*>(recycler->getParent());
            if (view) view->push(item.path);
            return;
        }

        if (item.type == remote::EntryType::VIDEO) {
            RemotePlayer* view = new RemotePlayer(item);
            MPVCore::instance().setUrl(item.path, client->extraOption());
            brls::Application::pushActivity(new brls::Activity(view), brls::TransitionAnimation::NONE);
            return;
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

RemoteView::~RemoteView() { brls::Logger::debug("RemoteView: deleted"); }

brls::View* RemoteView::getDefaultFocus() { return this->recycler; }

void RemoteView::onCreate() {
    this->recycler->registerAction("hints/cancel"_i18n, brls::BUTTON_B, [this](...) {
        if (this->stack.size() > 1) {
            this->stack.pop_back();
            this->load();
        } else if (brls::Application::getInputType() == brls::InputType::TOUCH) {
            this->dismiss();
        } else {
            AutoTabFrame::focus2Sidebar(this);
        }
        return true;
    });
}

void RemoteView::push(const std::string& path) {
    this->stack.push_back(path);
    this->load();
}

void RemoteView::load() {
    ASYNC_RETAIN
    this->recycler->showSkeleton();
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