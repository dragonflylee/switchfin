#include "api/websocket.hpp"
#include "utils/config.hpp"
#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <libretro-common/retro_timers.h>
#include <curl/curl.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

websocket::websocket(const std::string& url) {
    this->easy = curl_easy_init();
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_CONNECT_ONLY, 2L); /* websocket style */

    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYHOST, 0L);

    this->isStopped = std::make_shared<std::atomic_bool>(false);
#ifdef BOREALIS_USE_STD_THREAD
    this->th = std::make_shared<std::thread>(ws_recv, this);
#else
    pthread_create(&this->th, nullptr, ws_recv, this);
#endif
#endif
}

websocket::~websocket() {
    this->isStopped->store(true);
    curl_easy_cleanup(this->easy);
}

void* websocket::ws_recv(void* ptr) {
#if LIBCURL_VERSION_NUM >= 0x080000 && !defined(__PS4__)
    websocket* p = reinterpret_cast<websocket*>(ptr);
    auto isStopped = p->isStopped;
    CURL* easy = p->easy;
    uint64_t timeout = 0;

    while (!isStopped->load()) {
        CURLcode res = curl_easy_perform(easy);
        if (res != CURLE_OK) {
            brls::Logger::warning("ws perform failed: {}", curl_easy_strerror(res));
            if (!timeout) break;
            retro_sleep(timeout *= 2);
            continue;
        }
        timeout = 500;

        int ws_sockfd;
        res = curl_easy_getinfo(easy, CURLINFO_ACTIVESOCKET, &ws_sockfd);
        if (res != CURLE_OK) {
            brls::Logger::warning("ws get socket: {}", curl_easy_strerror(res));
            break;
        }

        fd_set rfds;
        size_t rlen, slen;
        const struct curl_ws_frame* meta;
        struct timeval tv = {.tv_sec = 10};
        std::string buf(2048, '\0');
        const std::string resp = R"({"MessageType":"KeepAlive"})";

        for (;;) {
            FD_ZERO(&rfds);
            FD_SET(ws_sockfd, &rfds);
            int ret = select(1, &rfds, NULL, NULL, &tv);
            if (ret < 0 || isStopped->load()) break;
            if (ret == 0) {
                res = curl_ws_send(easy, resp.data(), resp.size(), &slen, 0, CURLWS_TEXT);
                if (res != CURLE_OK) break;
            } else {
                res = curl_ws_recv(easy, buf.data(), buf.size(), &rlen, &meta);
                if (res == CURLE_AGAIN) continue;
                if (res != CURLE_OK) break;

                if (meta->flags & CURLWS_CLOSE) break;
                if (meta->flags & CURLWS_TEXT) on_msg(buf.substr(0, rlen));
            }
        }

        if (res != CURLE_OK) {
            std::string msg = fmt::format("ws failed: {}", curl_easy_strerror(res));
            brls::sync([msg] { brls::Application::notify(msg); });
        }
    }

    brls::Logger::info("ws recv exit");
#endif
    return nullptr;
}

struct Message {
    std::string MessageType;
    std::string MessageId;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Message, MessageType, MessageId);

const std::string MsgKeepAlive = "KeepAlive";

void websocket::on_msg(const std::string& resp) {
    try {
        Message m = nlohmann::json::parse(resp);
        if (m.MessageType.compare(MsgKeepAlive)) {
            brls::Logger::debug("ws recv: {}", resp);
        }
    } catch (const std::exception& ex) {
        brls::Logger::warning("parse ws {}", ex.what());
    }
}