// VitaSDK's newlib is built without _WANT_IO_C99_FORMATS, so its printf
// family does not understand the C99 length modifiers z, t and j. Such a
// conversion is skipped without consuming its argument, which shifts every
// following argument: a later %s then dereferences an integer and crashes.
// mpv and FFmpeg use %zd/%zu in log messages (e.g. mpv's "Too many packets
// in the demuxer packet queues" warning), so this crashed the app.
//
// The string and stream formatters are wrapped at link time
// (-Wl,--wrap=..., see CMakeLists.txt) and the format is rewritten first:
// size_t and ptrdiff_t are 32-bit like int on the Vita, so z and t are
// dropped, and intmax_t is 64-bit, so j becomes ll.

#ifdef __PSV__

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool needsRewrite(const char* fmt) {
    for (const char* p = fmt; (p = std::strchr(p, '%')) != nullptr;) {
        ++p;
        if (*p == '%') {
            ++p;
            continue;
        }
        while (*p && std::strchr("-+ #0'123456789.*", *p)) ++p;
        while (*p && std::strchr("hlLqjzt", *p)) {
            if (*p == 'z' || *p == 't' || *p == 'j') return true;
            ++p;
        }
    }
    return false;
}

void rewrite(const char* fmt, char* out) {
    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            *out++ = *p++;
            continue;
        }
        *out++ = *p++;
        if (*p == '%') {
            *out++ = *p++;
            continue;
        }
        while (*p && std::strchr("-+ #0'123456789.*", *p)) *out++ = *p++;
        while (*p && std::strchr("hlLqjzt", *p)) {
            if (*p == 'j') {
                *out++ = 'l';
                *out++ = 'l';
            } else if (*p != 'z' && *p != 't') {
                *out++ = *p;
            }
            ++p;
        }
    }
    *out = '\0';
}

/// Holds the rewritten format; small formats stay on the stack.
class Format {
public:
    explicit Format(const char* fmt) : str(fmt) {
        if (!fmt || !needsRewrite(fmt)) return;
        // only j grows (to ll), so twice the length is always enough
        size_t size = std::strlen(fmt) * 2 + 1;
        char* out = size <= sizeof(stack) ? stack : heap = static_cast<char*>(std::malloc(size));
        if (!out) return;
        rewrite(fmt, out);
        str = out;
    }
    ~Format() { std::free(heap); }
    Format(const Format&) = delete;
    Format& operator=(const Format&) = delete;

    const char* str;

private:
    char stack[256];
    char* heap = nullptr;
};

}  // namespace

extern "C" {

int __real__svfprintf_r(struct _reent* r, FILE* fp, const char* fmt, va_list ap);
int __real__vfprintf_r(struct _reent* r, FILE* fp, const char* fmt, va_list ap);
int __real_vfprintf(FILE* fp, const char* fmt, va_list ap);

// sprintf, snprintf, vsnprintf, asprintf, vasprintf...
int __wrap__svfprintf_r(struct _reent* r, FILE* fp, const char* fmt, va_list ap) {
    Format f(fmt);
    return __real__svfprintf_r(r, fp, f.str, ap);
}

// printf, fprintf, vprintf
int __wrap__vfprintf_r(struct _reent* r, FILE* fp, const char* fmt, va_list ap) {
    Format f(fmt);
    return __real__vfprintf_r(r, fp, f.str, ap);
}

// vfprintf calls _vfprintf_r from its own object, which --wrap can't reach
int __wrap_vfprintf(FILE* fp, const char* fmt, va_list ap) {
    Format f(fmt);
    return __real_vfprintf(fp, f.str, ap);
}

}  // extern "C"

#endif  // __PSV__
