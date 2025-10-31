#include "api/http.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <curl/curl.h>
#if defined(BOREALIS_USE_GXM)
#include <mbedtls/platform.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
static SceUID mempool_id = 0;
static void* mempool_addr = nullptr;
static size_t mempool_size = 20 * 1024 * 1024;
static void* s_mspace = nullptr;
int __attribute__((optimize("no-optimize-sibling-calls"))) malloc_finalize() {
    if (s_mspace) sceClibMspaceDestroy(s_mspace);
    if (mempool_addr) sceGxmUnmapMemory(mempool_addr);
    if (mempool_id) sceKernelFreeMemBlock(mempool_id);
    return 0;
}

int malloc_init() {
    int res;
    if (s_mspace) return 0;
    mempool_id = sceKernelAllocMemBlock("curl", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, mempool_size, nullptr);
    sceKernelGetMemBlockBase(mempool_id, &mempool_addr);
    if (!mempool_addr) goto error;
    res = sceGxmMapMemory(mempool_addr, mempool_size, SCE_GXM_MEMORY_ATTRIB_RW);
    if (res != SCE_OK) goto error;
    s_mspace = sceClibMspaceCreate(mempool_addr, mempool_size);
    if (!s_mspace) goto error;

    return 0;
error:
    malloc_finalize();
    return 1;
}

void __attribute__((optimize("no-optimize-sibling-calls"))) * sce_malloc(size_t size) {
    if (!s_mspace) malloc_init();
    return sceClibMspaceMalloc(s_mspace, size);
}

void __attribute__((optimize("no-optimize-sibling-calls"))) sce_free(void* ptr) {
    if (!ptr || !s_mspace) return;
    sceClibMspaceFree(s_mspace, ptr);
}

void __attribute__((optimize("no-optimize-sibling-calls"))) * sce_calloc(size_t nelem, size_t size) {
    if (!s_mspace) malloc_init();
    return sceClibMspaceCalloc(s_mspace, nelem, size);
}

void __attribute__((optimize("no-optimize-sibling-calls"))) * sce_realloc(void* ptr, size_t size) {
    if (!s_mspace) malloc_init();
    return sceClibMspaceRealloc(s_mspace, ptr, size);
}

char __attribute__((optimize("no-optimize-sibling-calls"))) * sce_strdup(const char* str) {
    size_t len;
    char* newstr;
    if (!str) return (char*)nullptr;
    len = strlen(str) + 1;
    newstr = (char*)sce_malloc(len);
    if (!newstr) return (char*)nullptr;
    sceClibMemcpy(newstr, str, len);
    return newstr;
}
#endif

#ifndef CURL_PROGRESSFUNC_CONTINUE
#define CURL_PROGRESSFUNC_CONTINUE 0x10000001
#endif

class curl_error : public std::exception {
public:
    explicit curl_error(CURLcode code) : m(curl_easy_strerror(code)) {}
    explicit curl_error(const std::string& arg) : m(arg) {}
    const char* what() const noexcept override { return m.c_str(); }

private:
    std::string m;
};

static std::string user_agent =
    fmt::format("{}/{} ({})", AppVersion::getPackageName(), AppVersion::getVersion(), AppVersion::getPlatform());

/// @brief curl context

HTTP::HTTP() : chunk(nullptr) {
    static struct Global {
        Global() {
#ifdef BOREALIS_USE_GXM
            mbedtls_platform_set_calloc_free(sce_calloc, sce_free);
            curl_global_init_mem(CURL_GLOBAL_DEFAULT, sce_malloc, sce_free, sce_realloc, sce_strdup, sce_calloc);
#else
            CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
            brls::Logger::debug("curl global init {}", std::to_string(rc));
#endif
            this->share = curl_share_init();
            curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        }
        ~Global() {
            curl_share_cleanup(this->share);
            curl_global_cleanup();
            brls::Logger::debug("curl cleanup");
        }
        // Avoids initalization order problems
        std::mutex init_lock;
        CURLSH* share;
    } global;

    global.init_lock.lock();
    this->easy = curl_easy_init();
    global.init_lock.unlock();

    curl_easy_setopt(this->easy, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(this->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(this->easy, CURLOPT_SHARE, global.share);
    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYHOST, 0L);
#if LIBCURL_VERSION_NUM >= 0x071900 && !defined(__PS4__)
    curl_easy_setopt(this->easy, CURLOPT_TCP_KEEPALIVE, 1L);
#endif
}

HTTP::~HTTP() {
    if (this->chunk != nullptr) curl_slist_free_all(this->chunk);
    if (this->easy != nullptr) curl_easy_cleanup(this->easy);
}

void HTTP::set_user_agent(const std::string& agent) { curl_easy_setopt(this->easy, CURLOPT_USERAGENT, agent.c_str()); }

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

void HTTP::set_basic_auth(const std::string& user, const std::string& passwd) {
    curl_easy_setopt(this->easy, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(this->easy, CURLOPT_USERNAME, user.c_str());
    curl_easy_setopt(this->easy, CURLOPT_PASSWORD, passwd.c_str());
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
    curl_easy_setopt(this->easy, CURLOPT_PROXY, PROXY_STATUS ? PROXY.c_str() : nullptr);

    CURLcode res = curl_easy_perform(this->easy);
    if (res != CURLE_OK) throw curl_error(res);

    long status_code = 0;
    curl_easy_getinfo(this->easy, CURLINFO_RESPONSE_CODE, &status_code);
    return status_code;
}

std::string HTTP::encode_form(const Form& form) {
    std::ostringstream ss;
    char* escaped;
    for (auto it = form.begin(); it != form.end(); ++it) {
        if (it->second.empty()) continue;
        if (it != form.begin()) ss << '&';
        escaped = curl_escape(it->second.c_str(), it->second.size());
        ss << it->first << '=' << escaped;
        curl_free(escaped);
    }
    return ss.str();
}

void HTTP::_get(const std::string& url, std::ostream* out) {
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_HTTPGET, 1L);
    int code = this->perform(out);
    if (code >= 400) throw curl_error(fmt::format("http status {}", code));
}

bool HTTP::getinfo(char** arg) { return curl_easy_getinfo(this->easy, CURLINFO_CONTENT_TYPE, arg) == CURLE_OK; }

int HTTP::propfind(const std::string& url, std::ostream* out) {
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_CUSTOMREQUEST, "PROPFIND");
    return this->perform(out);
}

std::string HTTP::_post(const std::string& url, const std::string& data) {
    std::ostringstream body;
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(this->easy, CURLOPT_POSTFIELDSIZE, data.size());
    int code = this->perform(&body);
    if (code >= 400) throw curl_error(fmt::format("http status {}", code));
    return body.str();
}

void HTTP::_delete(const std::string& url, std::ostream* out) {
    curl_easy_setopt(this->easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(this->easy, CURLOPT_CUSTOMREQUEST, "DELETE");
    int code = this->perform(out);
    if (code >= 400) throw curl_error(fmt::format("http status {}", code));
}
