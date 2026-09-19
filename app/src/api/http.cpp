#include "api/http.hpp"
#ifdef PS5_NATIVE_GPU
#include "api/curl_tls.hpp"
#include "api/curl_dns_share.hpp"
#endif
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <curl/curl.h>
#ifdef PS5_NATIVE_GPU
#include "utils/ps5_native_http_diagnostics.hpp"
#include <limits>
#endif
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

const std::string HTTP::USER_AGENT = fmt::format(
    "Mozilla/5.0 {}/{} ({})", AppVersion::getPackageName(), AppVersion::getVersion(), AppVersion::getPlatform());

/// @brief curl context

HTTP::HTTP() : chunk(nullptr) {
    static struct Global {
        Global() {
#ifdef BOREALIS_USE_GXM
            mbedtls_platform_set_calloc_free(sce_calloc, sce_free);
#ifdef PS5_NATIVE_GPU
            CURLcode rc = curl_global_init_mem(CURL_GLOBAL_DEFAULT, sce_malloc, sce_free, sce_realloc, sce_strdup, sce_calloc);
#else
            curl_global_init_mem(CURL_GLOBAL_DEFAULT, sce_malloc, sce_free, sce_realloc, sce_strdup, sce_calloc);
#endif
#else
            CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
#ifdef PS5_NATIVE_GPU
            if (rc != CURLE_OK)
                throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::GlobalInit, rc);
#endif
            brls::Logger::debug("curl global init {}", std::to_string(rc));
#endif
#ifdef PS5_NATIVE_GPU
            if (rc != CURLE_OK) throw curl_error(rc);
            try {
                this->share = std::make_unique<CurlDnsShare>();
            } catch (...) {
                curl_global_cleanup();
                // This boundary may fail without returning any CURLcode.
                throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::DnsShare, -1);
            }
#else
            this->share = curl_share_init();
            curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
#endif
        }
        ~Global() {
#ifdef PS5_NATIVE_GPU
            this->share.reset();
#else
            curl_share_cleanup(this->share);
#endif
            curl_global_cleanup();
            brls::Logger::debug("curl cleanup");
        }
        // Avoids initalization order problems
        std::mutex init_lock;
#ifdef PS5_NATIVE_GPU
        std::unique_ptr<CurlDnsShare> share;
#else
        CURLSH* share;
#endif
    } global;

    global.init_lock.lock();
    this->easy = curl_easy_init();
    global.init_lock.unlock();
#ifdef PS5_NATIVE_GPU
    if (!this->easy)
        throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::EasyCreate, -1);
#endif

    curl_easy_setopt(this->easy, CURLOPT_USERAGENT, USER_AGENT.c_str());
    curl_easy_setopt(this->easy, CURLOPT_FOLLOWLOCATION, 1L);
#ifdef PS5_NATIVE_GPU
    CURLcode shared = curl_easy_setopt(this->easy, CURLOPT_SHARE, global.share->get());
    if (shared != CURLE_OK) {
        curl_easy_cleanup(this->easy);
        this->easy = nullptr;
        throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::ShareOption, shared);
    }
#else
    curl_easy_setopt(this->easy, CURLOPT_SHARE, global.share);
#endif
    // enable all supported built-in compressions
    curl_easy_setopt(this->easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(this->easy, CURLOPT_VERBOSE, 0L);
#ifdef PS5_NATIVE_GPU
    // the download loop is receive-bound on this console. The same
    // file streams at gigabit to a wired desktop, but the native client
    // stalls near ~40 MiB/s with the disk ~90% idle. libcurl's default
    // 16 KiB receive buffer forces a recv()+write-callback cycle every
    // 16 KiB (~2.5k/s at 40 MiB/s); the per-syscall overhead is the
    // ceiling. A 256 KiB buffer cuts that ~16x. CURLOPT_BUFFERSIZE is an
    // option enum, so this adds no native import.
    curl_easy_setopt(this->easy, CURLOPT_BUFFERSIZE, 262144L);
    CURLcode tls = configureCurlTls(this->easy);
    if (tls != CURLE_OK) {
        curl_easy_cleanup(this->easy);
        this->easy = nullptr;
        throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::TlsOptions, tls);
    }
#else
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(this->easy, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
    // Every request runs from a background thread (ThreadPool / brls::async), so
    // libcurl must never reach for SIGALRM to time out a name resolve — signals
    // only work on the main thread and are unsafe multi-threaded (curl docs:
    // "libcurl cannot function properly multi-threaded unless CURLOPT_NOSIGNAL
    // is set"). With NOSIGNAL, DNS timeouts rely solely on the async (threaded)
    // resolver, which is why the Vita curl build must enable it.
    curl_easy_setopt(this->easy, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x071900 && !defined(__PS4__)
    curl_easy_setopt(this->easy, CURLOPT_TCP_KEEPALIVE, 1L);
#endif
#ifdef PS5_NATIVE_GPU
    CURLcode socketOptions=curl_easy_setopt(this->easy,CURLOPT_SOCKOPTDATA,this);
    if(socketOptions==CURLE_OK) {
        socketOptions=curl_easy_setopt(this->easy,CURLOPT_SOCKOPTFUNCTION,
            +[](void* owner,curl_socket_t socket,curlsocktype purpose) noexcept -> int {
                if(purpose!=CURLSOCKTYPE_IPCXN)return CURL_SOCKOPT_OK;
                auto* session=static_cast<HTTP*>(owner);
                const auto mode=ps5_native_socket_mode::establish(socket);
                // widen the socket receive buffer best-effort. The
                // stack may clamp it; failure is never a reason to reject
                // the socket (curl would abort the whole transfer).
                { const int rcvbuf = 4 * 1024 * 1024;
                  ::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)); }
                // Bound the connect at the stack, which is where the 75 s lives.
                // Its outcome is recorded and never acted on: rejecting the
                // socket would abort the transfer this is meant to protect.
                (void)ps5_native_socket_mode::bindConnect(socket, session->connect_deadline_ms);
                return mode.accepted?CURL_SOCKOPT_OK:CURL_SOCKOPT_ERROR;
            });
    }
    if(socketOptions!=CURLE_OK) {
        curl_easy_cleanup(this->easy);
        this->easy=nullptr;
        throw ps5_native_http::initializationFailure(ps5_native_http::RequestStage::SocketOptions,socketOptions);
    }
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
#ifdef PS5_NATIVE_GPU
        curl_slist* next = curl_slist_append(chunk, h.c_str());
        if (!next) {
            curl_slist_free_all(chunk);
            throw std::bad_alloc();
        }
        chunk = next;
    }
    // Empty headers must clear a reused handle too. Commit only after libcurl
    // accepts the replacement, so failure preserves the old list and pointer.
    const auto result = curl_easy_setopt(this->easy, CURLOPT_HTTPHEADER, chunk);
    if (result != CURLE_OK) {
        curl_slist_free_all(chunk);
        throw std::runtime_error("Could not configure HTTP headers");
#else
        chunk = curl_slist_append(chunk, h.c_str());
#endif
    }
    curl_slist_free_all(this->chunk);
    this->chunk = chunk;
#ifdef PS5_NATIVE_GPU
#else
    if (chunk != nullptr) curl_easy_setopt(this->easy, CURLOPT_HTTPHEADER, chunk);
#endif
}

void HTTP::set_option(const Range& r) {
    const std::string range_str = std::to_string(r.start) + "-" + std::to_string(r.end);
    curl_easy_setopt(this->easy, CURLOPT_RANGE, range_str.c_str());
}

void HTTP::set_option(const Timeout& t) {
#ifdef PS5_NATIVE_GPU
    this->connect_deadline_ms=t.timeout;
#endif
    curl_easy_setopt(this->easy, CURLOPT_TIMEOUT_MS, t.timeout);
    curl_easy_setopt(this->easy, CURLOPT_CONNECTTIMEOUT_MS, t.timeout);
}

int HTTP::easy_progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    HTTP* ctx = reinterpret_cast<HTTP*>(clientp);
    ctx->event.fire(dltotal, dlnow);
#ifdef PS5_NATIVE_GPU
    // 0, not CURL_PROGRESSFUNC_CONTINUE: the latter means "carry on *and* keep
    // libcurl's own progress meter running", which draws a \r-updating bar on
    // stderr. Under websrv stderr is a pipe back to the launching HTTP client
    // and crashlog.c leaves it unbuffered, so every update is a blocking
    // write(2) from whichever thread is transferring.
    return (ctx->is_cancel && ctx->is_cancel->load()) ? 1 : 0;
}

void HTTP::reset() {
    curl_easy_setopt(this->easy, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFOFUNCTION, nullptr);
    curl_easy_setopt(this->easy, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(this->easy, CURLOPT_RANGE, nullptr);
    this->is_cancel.reset();
    this->event.clear();
#else
    return ctx->is_cancel->load() ? 1 : CURL_PROGRESSFUNC_CONTINUE;
#endif
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
#ifdef PS5_NATIVE_GPU
    // Empty callbacks are valid; do not require or dereference either pointer.
    if (size == 0 || nmemb == 0) return 0;
    if (!ptr || !userdata || nmemb > std::numeric_limits<size_t>::max() / size)
        return CURL_WRITEFUNC_ERROR;
    const size_t count = size * nmemb;
    if (count > static_cast<size_t>(std::numeric_limits<std::streamsize>::max()))
        return CURL_WRITEFUNC_ERROR;
    try {
        auto* ctx = static_cast<std::ostream*>(userdata);
        ctx->write(ptr, static_cast<std::streamsize>(count));
        return ctx->good() ? count : CURL_WRITEFUNC_ERROR;
    } catch (...) {
        // ostream may throw on a short write or from its streambuf. Never let
        // an exception cross libcurl's C callback boundary.
        return CURL_WRITEFUNC_ERROR;
    }
#else
    std::ostream* ctx = reinterpret_cast<std::ostream*>(userdata);
    size_t count = size * nmemb;
    ctx->write(ptr, count);
    return count;
#endif
}

int HTTP::perform(std::ostream* body) {
    curl_easy_setopt(this->easy, CURLOPT_WRITEFUNCTION, easy_write_cb);
    curl_easy_setopt(this->easy, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(this->easy, CURLOPT_PROXY, PROXY_STATUS ? PROXY.c_str() : nullptr);

#ifdef PS5_NATIVE_GPU
    this->native_error_buffer.begin(this->easy);
    ps5_native_http::ResolverScope resolver_scope;
#endif
    CURLcode res = curl_easy_perform(this->easy);
#ifdef PS5_NATIVE_GPU
    // Mark the return before getinfo/error-buffer processing, so an active
    // sample cannot mislabel a later observation wait as curl_easy_perform.
    auto snapshot = ps5_native_http::capture(this->easy, res);
    snapshot.resolver = resolver_scope.observation;
    this->native_error_buffer.finish(&snapshot);
    if (res != CURLE_OK) {
        throw ps5_native_http::Failure(snapshot);
    }
#else
    if (res != CURLE_OK) throw curl_error(res);
#endif

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

#ifdef PS5_NATIVE_GPU
long HTTP::last_status() const {
    long status_code = 0;
    curl_easy_getinfo(this->easy, CURLINFO_RESPONSE_CODE, &status_code);
    return status_code;
}

#endif
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
