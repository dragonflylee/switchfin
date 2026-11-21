#include "api/http.hpp"
#include "utils/config.hpp"
#include <borealis/core/logger.hpp>
#include <curl/curl.h>
#include <mutex>
#ifndef BOREALIS_USE_STD_THREAD
#include <pthread.h>
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

namespace {

// 每线程一个常驻 multi，线程内复用 easy handle
class Pool {
private:
    Pool() {
        this->curlm = curl_multi_init();
        if (this->curlm) curl_multi_setopt(this->curlm, CURLMOPT_MAXCONNECTS, 6L);
    }

public:
    static constexpr size_t MAX_IDLE = 4;

    static Pool* instance() { return get(true); }

    static void release_thread() { get(false); }

    CURLM* multi() const { return this->curlm; }

    CURL* acquire() {
        std::lock_guard<std::mutex> lock(this->poolMutex);
        if (this->curlm == nullptr) return nullptr;
        if (this->idleHandles.empty()) return curl_easy_init();
        CURL* easy = this->idleHandles.back();
        this->idleHandles.pop_back();
        return easy;
    }

    void release(CURL* easy) {
        if (easy == nullptr) return;
        std::lock_guard<std::mutex> lock(this->poolMutex);
        // 复位选项，同时断开对调用方即将释放的串表等数据的引用；连接缓存会保留
        curl_easy_reset(easy);
        if (this->curlm != nullptr && this->idleHandles.size() < MAX_IDLE) {
            this->idleHandles.push_back(easy);
            return;
        }
        curl_easy_cleanup(easy);
    }

private:
    // keep=false 时释放并返回 nullptr。PS4 libc 未导出 __cxa_thread_atexit_impl，
    // Switch 的 TLS 也不可靠，所以自己管理池的创建与销毁。
    static Pool* get(bool keep) {
#ifdef BOREALIS_USE_STD_THREAD
        static thread_local Pool* pool = keep ? new Pool() : nullptr;
        return pool;
#else
        static pthread_key_t key = create_key();

        Pool* pool = static_cast<Pool*>(pthread_getspecific(key));
        if (keep && pool == nullptr) {
            pool = new Pool();
            pthread_setspecific(key, pool);
        } else if (!keep && pool != nullptr) {
            pthread_setspecific(key, nullptr);
            delete pool;
        }
        return pool;
#endif
    }

#ifndef BOREALIS_USE_STD_THREAD
    static pthread_key_t create_key() {
        static pthread_key_t key;
        static std::once_flag once;
        std::call_once(once, []() { pthread_key_create(&key, nullptr); });
        return key;
    }
#endif

    ~Pool() {
        for (CURL* easy : this->idleHandles) {
            curl_easy_cleanup(easy);
        }
        if (this->curlm != nullptr) curl_multi_cleanup(this->curlm);
    }

    CURLM* curlm = nullptr;
    std::vector<CURL*> idleHandles;
    std::mutex poolMutex;
};

}  // namespace

HTTP::HTTP() {
    static struct Global {
        Global() {
#ifdef BOREALIS_USE_GXM
            mbedtls_platform_set_calloc_free(sce_calloc, sce_free);
            CURLcode rc = curl_global_init_mem(CURL_GLOBAL_DEFAULT, sce_malloc, sce_free, sce_realloc, sce_strdup, sce_calloc);
#else
            CURLcode rc = curl_global_init(CURL_GLOBAL_ALL);
            brls::Logger::debug("curl global init {}", std::to_string(rc));
#endif
            if (rc != CURLE_OK) {
                brls::Logger::error("curl global init failed: {}", curl_easy_strerror(rc));
                return;
            }
        }
        ~Global() {
            curl_global_cleanup();
            brls::Logger::debug("curl cleanup");
        }
    } global;
    (void)global;

    CURL* easy = Pool::instance()->acquire();
    this->easy = easy;
    if (easy == nullptr) return;

    curl_easy_setopt(easy, CURLOPT_USERAGENT, USER_AGENT.c_str());
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    // enable all supported built-in compressions
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 0L);
    // 请求都在后台线程，禁止 libcurl 用信号做超时
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x071900 && !defined(__PS4__)
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
#endif
}

HTTP::~HTTP() {
    if (this->chunk != nullptr) curl_slist_free_all(this->chunk);
    if (this->easy != nullptr) Pool::instance()->release(static_cast<CURL*>(this->easy));
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
    // end == 0 means "from start to the end of the file" -> "start-".
    const std::string range_str = r.end > 0 ? std::to_string(r.start) + "-" + std::to_string(r.end)
                                            : std::to_string(r.start) + "-";
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
    CURL* easy = static_cast<CURL*>(this->easy);
    // 用当前线程的 multi 驱动：句柄可能是在别的线程借出的
    CURLM* multi = Pool::instance()->multi();
    if (easy == nullptr || multi == nullptr) throw curl_error("curl handle unavailable (out of memory)");

    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, easy_write_cb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(easy, CURLOPT_PROXY, PROXY_STATUS ? PROXY.c_str() : nullptr);

    // 每次传输都要 add/remove 一次，连接缓存挂在 multi 上不会丢
    CURLMcode mc = curl_multi_add_handle(multi, easy);
    if (mc != CURLM_OK) throw curl_error(fmt::format("curl multi: {}", curl_multi_strerror(mc)));

    // 任何抛出路径都要把句柄摘下来，否则下次复用会失败
    struct Detach {
        CURLM* multi;
        CURL* easy;
        ~Detach() { curl_multi_remove_handle(this->multi, this->easy); }
    } detach{multi, easy};

    CURLcode res = CURLE_OK;
    int running = 0;
    for (;;) {
        mc = curl_multi_perform(multi, &running);
        if (mc != CURLM_OK) throw curl_error(fmt::format("curl multi: {}", curl_multi_strerror(mc)));
        if (!running) break;
        mc = curl_multi_poll(multi, nullptr, 0, 100, nullptr);
        if (mc != CURLM_OK) throw curl_error(fmt::format("curl multi: {}", curl_multi_strerror(mc)));
    }

    bool done = false;
    int pending = 0;
    CURLMsg* msg = nullptr;
    while ((msg = curl_multi_info_read(multi, &pending)) != nullptr) {
        if (msg->msg == CURLMSG_DONE && msg->easy_handle == easy) {
            res = msg->data.result;
            done = true;
        }
    }
    if (!done) throw curl_error("curl multi: transfer result missing");
    if (res != CURLE_OK) throw curl_error(res);

    long status_code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status_code);
    this->lastCode = static_cast<int>(status_code);
    return this->lastCode;
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

void HTTP::cleanup_thread() { Pool::release_thread(); }

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
