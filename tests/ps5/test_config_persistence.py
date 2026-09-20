"""Sandbox settings remain accessible after promotion changes pathname access."""
import unittest
from source_fixture import ROOT, compile_run, extract_function, native_source


class ConfigPersistence(unittest.TestCase):
    def test_retained_files_and_io_failures(self):
        compile_run(r'''
#include <utils/ps5_config_file.hpp>
#include <nlohmann/json.hpp>
#include <cassert>
#include <filesystem>
#include <thread>
#include <sys/stat.h>
using namespace ps5::configuration;
struct Ops : FileOps {
    inline static bool promoted = false, interruptRead = false, interruptWrite = false;
    inline static int writeError = 0, syncError = 0, truncateError = 0, closes = 0;
    inline static bool zeroWrite = false;
    static int open(const char* path) {
        if (promoted) { errno = EACCES; return -1; }
        return FileOps::open(path);
    }
    static ssize_t read(int fd, void* data, size_t size) {
        if (interruptRead) { interruptRead = false; errno = EINTR; return -1; }
        return FileOps::read(fd, data, std::min(size, size_t(7)));
    }
    static ssize_t write(int fd, const void* data, size_t size) {
        if (interruptWrite) { interruptWrite = false; errno = EINTR; return -1; }
        if (writeError) { errno = writeError; return -1; }
        if (zeroWrite) return 0;
        return FileOps::write(fd, data, std::min(size, size_t(3)));
    }
    static int truncate(int fd, off_t size) {
        if (truncateError) { errno = truncateError; return -1; }
        return FileOps::truncate(fd, size);
    }
    static int sync(int fd) {
        if (syncError) { errno = syncError; return -1; }
        return FileOps::sync(fd);
    }
    static void close(int fd) { ++closes; FileOps::close(fd); }
};
int main() {
    char root[] = "/tmp/switchfin-settings-XXXXXX";
    assert(mkdtemp(root));
    const std::string dir = root, config = dir + "/config.json", index = dir + "/index.json";
    const std::string original = R"({"login":"saved-account","setting":"long-value"})";
    const std::string updated = R"({"login":"saved-account","setting":2})";
    std::string contents;
    {
        File<Ops> file, downloads;
        assert(!file.read(contents) && errno == EBADF);
        assert(!file.save(original) && errno == EBADF);
        assert(!file.prepare(dir + "/missing/config.json"));
        assert(file.prepare(config) && downloads.prepare(index));
        assert(file.read(contents) && contents.empty());
        struct stat state{};
        assert(stat(config.c_str(), &state) == 0 && (state.st_mode & 0777) == 0600);
        Ops::promoted = true;
        // No second open, even when the promoted pathname is unavailable.
        assert(file.prepare("/unreachable/config.json"));
        Ops::interruptWrite = true;
        assert(file.save(original));
        assert(downloads.save("[]"));
        Ops::interruptRead = true;
        assert(file.read(contents) && contents == original);
        assert(file.save(updated));
        assert(file.read(contents) && contents == updated);
        assert(stat(config.c_str(), &state) == 0 && size_t(state.st_size) == updated.size());
        Ops::writeError = ENOSPC;
        assert(!file.save(original) && errno == ENOSPC);
        Ops::writeError = 0; Ops::zeroWrite = true;
        assert(!file.save(original) && errno == EIO);
        Ops::zeroWrite = false;
        assert(file.read(contents) && contents == updated);
        Ops::truncateError = EIO;
        assert(!file.save(updated) && errno == EIO);
        Ops::truncateError = 0; Ops::syncError = EIO;
        assert(!file.save(updated) && errno == EIO);
        for (int error : {EINVAL, ENOSYS, EOPNOTSUPP}) {
            Ops::syncError = error; assert(file.save(updated));
        }
        Ops::syncError = 0;
        // Saving from separate callers cannot interleave descriptor offsets.
        std::thread a([&] { for (int i=0;i<20;++i) assert(file.save(original)); });
        std::thread b([&] { for (int i=0;i<20;++i) assert(file.save(updated)); });
        a.join(); b.join();
        assert(file.save(updated));
    }
    assert(Ops::closes == 2);
    // A new sandbox launch loads the same inode without truncating it.
    Ops::promoted = false;
    {
        File<Ops> file;
        assert(file.prepare(config));
        assert(file.read(contents) && contents == updated);
        assert(nlohmann::json::parse(contents).at("login") == "saved-account");
        Ops::promoted = true;
        assert(file.save(original));
    }
    Ops::promoted = false;
    {
        File<Ops> file;
        assert(file.prepare(config));
        assert(file.read(contents) && contents == original);
    }
    std::filesystem::remove_all(dir);
}
''')

    def test_production_save_reports_failures(self):
        save = extract_function(native_source(ROOT / 'app/src/utils/config.cpp'), 'void AppConfig::save()')
        compile_run(r'''
#include <utils/ps5_config_file.hpp>
#include <nlohmann/json.hpp>
#include <cassert>
#include <filesystem>
namespace ps5_native_startup::detail { template<class... T> void line(T...) {} }
namespace brls {
struct Logger { template<class... T> static void warning(T...) {} };
struct Application {
    inline static int failures = 0;
    static void notify(const char*) { ++failures; }
};
}
struct AppConfig { std::string login = "account"; int setting = 1; void save(); };
void to_json(nlohmann::json& json, const AppConfig& config) {
    json = {{"login", config.login}, {"setting", config.setting}};
}
''' + save + r'''
int main() {
    AppConfig config;
    config.save(); // A missing descriptor must not be reported as a successful save.
    assert(brls::Application::failures == 1);
    char root[] = "/tmp/switchfin-config-save-XXXXXX";
    assert(mkdtemp(root));
    assert(ps5::configuration::settingsFile().prepare(std::string(root) + "/config.json"));
    config.save();
    std::string contents;
    assert(ps5::configuration::settingsFile().read(contents));
    assert(nlohmann::json::parse(contents).at("login") == "account");
    config.setting = 2; config.save();
    assert(ps5::configuration::settingsFile().read(contents));
    assert(nlohmann::json::parse(contents).at("setting") == 2);
    assert(brls::Application::failures == 1);
    std::filesystem::remove_all(root);
}
''')

    def test_core_files_open_before_promotion(self):
        main = native_source(ROOT / 'app/src/main.cpp')
        promotion = main.index('ps5::storage::resolve();')
        for operation in ('settingsFile().prepare(', 'downloadIndexFile().prepare(',
                          'std::fopen(appLogPath.c_str(), "a")',
                          'open(driverLogPath.c_str(), O_WRONLY',
                          'std::fopen(subfont.c_str(), "wb")'):
            self.assertLess(main.index(operation), promotion, operation)
        config = native_source(ROOT / 'app/src/utils/config.cpp')
        paths = extract_function(config, 'std::string AppConfig::configDir()')
        self.assertIn('"/download0/switchfin-native"', paths)
        self.assertIn('ps5::storage::sandboxRoot() + "/download0/switchfin-native"', paths)
        downloads = native_source(ROOT / 'app/src/utils/download.cpp')
        self.assertIn('downloadIndexFile()', extract_function(downloads, 'void DownloadManager::loadIndex()'))
        self.assertIn('downloadIndexFile()', extract_function(downloads, 'void DownloadManager::saveIndex()'))


if __name__ == '__main__':
    unittest.main()
