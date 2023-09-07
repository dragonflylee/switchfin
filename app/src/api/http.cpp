#include "api/http.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <sstream>
#include <fstream>

#ifndef CURL_PROGRESSFUNC_CONTINUE
#define CURL_PROGRESSFUNC_CONTINUE 0x10000001
#endif

class curl_error : public std::exception {
public:
    explicit curl_error(CURLcode c) : code(c) {}
    const char* what() const noexcept override { return curl_easy_strerror(this->code); }

private:
    CURLcode code;
};

class curl_status_code : public std::exception {
public:
    explicit curl_status_code(int s) { msg = fmt::format("http status {}", s); }
    const char* what() const noexcept override { return msg.c_str(); }

private:
    std::string msg;
};

static std::string user_agent = fmt::format("{}/{}", AppVersion::getPackageName(), AppVersion::getVersion());

/// @brief curl context

HTTP::HTTP() : chunk(nullptr) {
    static struct Global {
        Global() {
            CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
            brls::Logger::debug("curl init {}", std::to_string(rc));
        }
        ~Global() {
            curl_global_cleanup();
            brls::Logger::debug("curl cleanup");
        }
    } global;

    this->easy = curl_easy_init();

    curl_easy_setopt(this->easy, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(this->easy, CURLOPT_FOLLOWLOCATION, 1L);
    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
}

HTTP::~HTTP() {
    if (this->chunk != nullptr) curl_slist_free_all(this->chunk);
    if (this->easy != nullptr) curl_easy_cleanup(this->easy);
}

void HTTP::add_header(const std::string& header) { this->chunk = curl_slist_append(this->chunk, header.c_str()); }

void HTTP::set_option(const Header& hs) {
    curl_slist* chunk = nullptr;
    for (auto& h : hs) {
        chunk = curl_slist_append(chunk, h.c_str());
    }
    curl_slist_free_all(this->chunk);
    this->chunk = chunk;
    if (chunk != nullptr) curl_easy_setopt(this->easy, CURLOPT_HTTPHEADER, chunk);
}

void HTTP::set_option(const Range& r) {
    const std::string range_str = std::to_string(r.start) + "-" + std::to_string(r.end);
    curl_easy_setopt(this->easy, CURLOPT_RANGE, range_str.c_str());
}

void HTTP::set_option(const Timeout& t) {
    curl_easy_setopt(this->easy, CURLOPT_TIMEOUT_MS, t.timeout);
    curl_easy_setopt(this->easy, CURLOPT_CONNECTTIMEOUT_MS, t.timeout);
}

int HTTP::easy_progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    HTTP* ctx = reinterpret_cast<HTTP*>(clientp);
    ctx->event.fire(dltotal, dlnow);
    return ctx->is_cancel->load() ? 1 : CURL_PROGRESSFUNC_CONTINUE;
}

void HTTP::set_option(const Cancel& c) {
    this->is_cancel = std::move(c);
    curl_easy_setopt(this->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFOFUNCTION, easy_progress_cb);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFODATA, this);
}

void HTTP::set_option(Progress::Callback p) {
    this->event.subscribe(p);
    curl_easy_setopt(this->easy, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFOFUNCTION, easy_progress_cb);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFODATA, this);
}

void HTTP::set_option(const Cookies& cookies) {
    std::stringstream ss;
    char* escaped;
    for (auto& c : cookies) {
        ss << c.name << "=";
        escaped = curl_easy_escape(this->easy, c.value.c_str(), c.value.size());
        if (escaped) {
            ss << escaped;
            curl_free(escaped);
        }
        ss << "; ";
    }
    curl_easy_setopt(this->easy, CURLOPT_COOKIE, ss.str().c_str());
}

size_t HTTP::easy_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::ostream* ctx = reinterpret_cast<std::ostream*>(userdata);
    size_t count = size * nmemb;
    ctx->write(ptr, count);
    return count;
}

int HTTP::perform(std::ostream* body) {
    curl_easy_setopt(this->easy, CURLOPT_WRITEFUNCTION, easy_write_cb);
    curl_easy_setopt(this->easy, CURLOPT_WRITEDATA, body);

    CURLcode res = curl_easy_perform(this->easy);
    if (res != CURLE_OK) throw curl_error(res);

    int status_code = 0;
    curl_easy_getinfo(this->easy, CURLINFO_RESPONSE_CODE, &status_code);
    return status_code;
}

std::string HTTP::encode_form(const Form& form) {
    std::ostringstream ss;
    char* escaped;
    for (auto it = form.begin(); it != form.end(); ++it) {
        if (it != form.begin()) ss << '&';
        escaped = curl_escape(it->second.c_str(), it->second.size());
        ss << it->first << '=' << escaped;
        curl_free(escaped);
    }
    return ss.str();
}

std::string HTTP::get(const std::string& url) {
    std::ostringstream body;
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_HTTPGET, 1L);
    int status_code = this->perform(&body);
    if (status_code >= 400) throw curl_status_code(status_code);
    return body.str();
}

void HTTP::download(const std::string& url, const std::string& path) {
    std::ofstream of(path);
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_HTTPGET, 1L);
    int status_code = this->perform(&of);
    if (status_code >= 400) throw curl_status_code(status_code);
}

std::string HTTP::post(const std::string& url, const std::string& data) {
    std::ostringstream body;
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(this->easy, CURLOPT_POSTFIELDSIZE, data.size());
    int status_code = this->perform(&body);
    if (status_code >= 400) throw curl_status_code(status_code);
    return body.str();
}