#include "api/websocket.hpp"
#include "utils/config.hpp"
#include "api/jellyfin.hpp"
#include <view/mpv_core.hpp>
#include <activity/player_view.hpp>
#include <libretro-common/retro_timers.h>
#include <curl/curl.h>

namespace {
const std::string msgKeepAlive = R"({"MessageType":"KeepAlive"})";
}  // namespace

websocket::websocket(const std::string& url) {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
    this->easy = curl_easy_init();
    if (this->easy == nullptr) {
        brls::Logger::error("ws init failed: {}", url);
        return;
    }
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());

    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(this->easy, CURLOPT_WRITEFUNCTION, onMsg);
    curl_easy_setopt(this->easy, CURLOPT_WRITEDATA, this);
    // 启用进度回调, 使 curl_easy_perform() 能被 onProgress 中断, 避免退出时 join() 卡死
    curl_easy_setopt(this->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(this->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFODATA, this);

    hb.setCallback([this]() {
        if (this->isStop.load()) return;
        std::lock_guard<std::mutex> lock(this->easyMutex);
        if (this->isStop.load() || this->easy == nullptr) return;
        size_t slen = msgKeepAlive.size();
        curl_ws_send(this->easy, msgKeepAlive.data(), slen, &slen, 0, CURLWS_TEXT);
    });

#ifdef BOREALIS_USE_STD_THREAD
    this->th = std::make_shared<std::thread>(wsRecv, this);
#else
    pthread_create(&this->th, nullptr, wsRecv, this);
#endif
#endif
}

websocket::~websocket() {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
    // 先置停止标志, 由 onProgress 中断 curl_easy_perform(), 再 join
    this->isStop.store(true);
    this->hb.stop();
#ifdef BOREALIS_USE_STD_THREAD
    if (this->th && this->th->joinable()) this->th->join();
#else
    if (this->th) pthread_join(this->th, nullptr);
#endif
    {
        std::lock_guard<std::mutex> lock(this->easyMutex);
        if (this->easy != nullptr) {
            curl_easy_cleanup(this->easy);
            this->easy = nullptr;
        }
    }
#endif
}

int websocket::onProgress(void* ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    websocket* p = reinterpret_cast<websocket*>(ptr);
    return (p == nullptr || p->isStop.load()) ? 1 : 0;
}

void* websocket::wsRecv(void* ptr) {
    websocket* p = reinterpret_cast<websocket*>(ptr);
    uint64_t delay = 1000;
    while (!p->isStop.load()) {
        CURL* easy = nullptr;
        {
            std::lock_guard<std::mutex> lock(p->easyMutex);
            easy = p->easy;
        }
        if (easy == nullptr) break;
        CURLcode res = curl_easy_perform(easy);
        if (p->isStop.load()) break;
        p->hb.stop();
        if (res != CURLE_OK) {
            brls::Logger::warning("ws perform failed: {}", curl_easy_strerror(res));
        } else {
            brls::Logger::info("ws connection closed by peer, reconnecting");
        }
        retro_sleep(delay);
        delay = std::min<uint64_t>(delay * 2, 30000);
    }
    brls::Logger::info("ws recv exit");
    return nullptr;
}

struct Message {
    std::string MessageType;
    std::string MessageId;
    nlohmann::json Data;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Message, MessageType, MessageId, Data);

struct MsgArguments {
    std::string Header;
    std::string Text;
    std::string TimeoutMs;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MsgArguments, Header, Text, TimeoutMs);

struct MsgPlay {
    std::vector<std::string> ItemIds;
    uint64_t StartPositionTicks = 0;
    std::string PlayCommand;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MsgPlay, ItemIds, StartPositionTicks, PlayCommand);

size_t websocket::onMsg(char* b, size_t size, size_t nitems, void* ptr) {
    try {
        if (nitems <= 2) return nitems;
        std::string resp(b, nitems);
        Message m = nlohmann::json::parse(resp);
        if (m.MessageType.empty()) return nitems;
        if (m.MessageType == "Playstate") {
            const std::string cmd = m.Data.value("Command", std::string{});
            if (cmd.empty()) return nitems;
            brls::sync([cmd, m]() {
                auto& mpv = MPVCore::instance();
                if (cmd == "PlayPause") {
                    mpv.togglePlay();
                } else if (cmd == "Stop") {
                    mpv.getCustomEvent()->fire(SYNC_STOP, nullptr);
                } else if (cmd == "Seek") {
                    int64_t seek = m.Data.value("SeekPositionTicks", int64_t{0});
                    mpv.seek(seek / jellyfin::PLAYTICKS, "absolute");
                } else {
                    mpv.getCustomEvent()->fire(cmd, nullptr);
                }
            });
        } else if (m.MessageType == "Play") {
            MsgPlay cmd = m.Data.get<MsgPlay>();
            if (cmd.PlayCommand == "PlayNow") {
                if (cmd.ItemIds.empty()) {
                    brls::Logger::warning("ws PlayNow without ItemIds: {}", resp);
                } else {
                    websocket::onPlayNow(cmd.ItemIds.front(), cmd.StartPositionTicks);
                }
            } else {
                brls::Logger::info("play command: {}", resp);
            }
        } else if (m.MessageType == "GeneralCommand") {
            auto it = m.Data.find("Arguments");
            if (it == m.Data.end() || !it->is_object()) return nitems;
            MsgArguments args = it->get<MsgArguments>();
            if (args.Text.empty()) return nitems;
            brls::Logger::info("ws display message: {}", args.Text);
            brls::sync([args]() { brls::Application::notify(args.Text); });
        } else if (m.MessageType == "ForceKeepAlive") {
            websocket* p = reinterpret_cast<websocket*>(ptr);
            if (p == nullptr || p->isStop.load()) return nitems;
            jellyfin::postJSON(
                {
                    {"PlayableMediaTypes", {"Video"}},
                    {"SupportedCommands", {"DisplayMessage"}},
                    {"SupportsPersistentIdentifier", false},
                    {"SupportsMediaControl", true},
                },
                [](...) {}, nullptr, jellyfin::apiCapabilities);
            if (!p->hb.isRunning()) p->hb.start(20000);
        } else if (m.MessageType != "KeepAlive" && m.MessageType != "Sessions") {
            brls::Logger::debug("ws recv: {}", resp);
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("parse ws {}", ex.what());
    }
    return nitems;
}

void websocket::onPlayNow(const std::string& itemId, uint64_t seekTicks) {
    jellyfin::getJSON<jellyfin::Episode>(
        [seekTicks](const jellyfin::Episode& item) {
            if (item.Type == "Audio") return;
            MPVCore::instance().getCustomEvent()->fire(SYNC_STOP, nullptr);
            PlayerView* view = new PlayerView(item, seekTicks);
            if (item.Type == jellyfin::mediaTypeEpisode) {
                view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
                if (item.SeriesId.is_string()) view->setSeries(item.SeriesId.get<std::string>());
            } else if (item.ProductionYear) {
                view->setTitie(fmt::format("{} ({})", item.Name, item.ProductionYear));
            } else {
                view->setTitie(item.Name);
            }
        },
        nullptr, jellyfin::apiUserItem, AppConfig::instance().getUserId(), itemId);
}