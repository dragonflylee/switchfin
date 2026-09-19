import os, re, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def extract_function(source, signature):
    if source.count(signature) != 1:
        raise AssertionError("Expected one production definition: " + signature)
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

def function(source, name):
    start = re.search(r"^(?:(?:inline )?static )?[A-Za-z_][^\n]*\b" + name + r"\(", source, re.M)
    if not start:
        raise ValueError(f"Missing patched driver function {name}")
    opening = source.index("{", start.start())
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{": depth += 1
        if source[end] == "}": depth -= 1
        end += 1
    return source[start.start():end]

def compile_run(source, services=None):
    with tempfile.TemporaryDirectory(prefix="switchfin-jellyfin-") as directory:
        root = Path(directory)
        for name, value in (services or {}).items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        (root / "test.cpp").write_text(source)
        subprocess.run([
            os.environ.get("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-DFMT_HEADER_ONLY", "-DPS5_NATIVE_GPU",
            "-I", str(root), "-I", str(ROOT / "app/include"),
            "-I", str(ROOT / "app/include/api"),
            "-I", str(ROOT / "library/borealis/library/include/borealis/extern"),
            "-I", str(ROOT / "library/borealis/library/lib/extern/fmt/include"),
            str(root / "test.cpp"), "-o", str(root / "test"),
        ], check=True)
        subprocess.run([str(root / "test")], check=True)

SERVICES = {
    "api/jellyfin.hpp": (ROOT / "app/include/api/jellyfin.hpp").read_text(),
    "borealis/core/logger.hpp": "#pragma once\n",
    "borealis/core/thread.hpp": r'''
#pragma once
#include <deque>
#include <functional>
namespace brls {
inline std::deque<std::function<void()>> workers, ui, timers;
inline size_t delay(long, const std::function<void()>& f) { timers.push_back(f); return timers.size(); }
inline void async(std::function<void()> f) { workers.push_back(std::move(f)); }
inline void sync(std::function<void()> f) { ui.push_back(std::move(f)); }
inline void run(std::deque<std::function<void()>>& queue) {
    auto f = std::move(queue.front()); queue.pop_front(); f();
}
}
''',
    "api/http.hpp": r'''
#pragma once
#include <atomic>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
struct HTTP {
    using Header = std::vector<std::string>;
    using Cancel = std::shared_ptr<std::atomic_bool>;
    struct Timeout {};
    inline static std::string response, failure, sentUrl, sentBody;
    inline static Header sentHeaders;
    inline static int calls = 0;
    static std::string get(const std::string& url, const Header& headers, Timeout, Cancel cancel) {
        ++calls; sentUrl = url; sentHeaders = headers;
        if (cancel && cancel->load()) throw std::runtime_error("cancelled");
        if (!failure.empty()) throw std::runtime_error(failure);
        return response;
    }
    static std::string post(const std::string& url, const std::string& body, const Header& h, Timeout t, Cancel c) {
        sentBody = body; return get(url, h, t, c);
    }
    static void set_option(HTTP&, const Header& h, Timeout, Cancel) { sentHeaders = h; }
    void _delete(const std::string& url, std::ostream* body) { sentUrl = url; *body << response; }
};
''',
    "utils/config.hpp": r'''
#pragma once
#include <fmt/format.h>
#include <atomic>
#include <memory>
struct AppConfig {
    std::string url = "https://first.invalid", user = "first-user", token = "first-token";
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    static AppConfig& instance() { static AppConfig c; return c; }
    const std::string& getUrl() const { return url; }
    const std::string& getUserId() const { return user; }
    const std::string& getToken() const { return token; }
    std::string getAuth(const std::string& t) const { return "Authorization: " + t; }
    auto requestCancellation() const { return cancelled; }
    void invalidateRequests() { cancelled->store(true); cancelled = std::make_shared<std::atomic_bool>(false); }
};
''',
}

def native_source(path):
    text = path.read_text()
    strings = []
    def mask(match):
        strings.append(match.group())
        return '"SOURCE_FIXTURE_RAW_' + str(len(strings) - 1) + '"'
    text = re.sub(r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"', mask, text, flags=re.S)
    result = subprocess.run(['unifdef', '-k', '-DPS5_NATIVE_GPU=1', '-D__PS5__=1'],
                            input=text + '\n', text=True, capture_output=True)
    if result.returncode not in (0, 1):
        raise RuntimeError(result.stderr)
    text = result.stdout
    for index, value in enumerate(strings):
        text = text.replace('"SOURCE_FIXTURE_RAW_' + str(index) + '"', value)
    return text
