"""Download destinations follow writable storage and retain unavailable preferences."""
import unittest
from source_fixture import compile_run


class DownloadLocations(unittest.TestCase):
    def test_modes_preferences_and_disconnected_drives(self):
        compile_run(r'''
#include <utils/ps5_storage_home.hpp>
#include <cassert>
#include <set>
using namespace ps5::storage;
struct Ops {
    inline static std::set<std::string> mounts, writablePaths, probed;
    static bool mounted(const std::string& root) { return mounts.count(root); }
    static bool writable(const std::string& path) {
        probed.insert(path);
        return writablePaths.count(path);
    }
};
int main() {
    const std::string sandbox = "/download0/switchfin-native/downloads";
    Ops::mounts = {"/mnt/usb0", "/mnt/ext1"};
    Ops::writablePaths = {sandbox, elevatedDownloads, "/mnt/usb0/switchfin/downloads",
                         "/mnt/ext1/switchfin/downloads"};
    state() = {};
    state().resolved = true;
    auto locations = discoverDownloadLocations<Ops>(sandbox);
    assert(locations.size() == 1 && locations[0].root == "sandbox");
    assert(locations[0].label == "Internal (sandbox)");
    assert(Ops::probed == std::set<std::string>{sandbox});
    assert(downloadLocationIndex(locations, "") == 0);
    assert(downloadLocationIndex(locations, "/mnt/usb0") == 0);

    state().sandboxRoot = sandboxPrefix;
    state().elevated = true;
    locations = discoverDownloadLocations<Ops>(sandbox);
    assert(locations.size() == 4);
    assert(downloadLocationIndex(locations, "sandbox") == 0);
    assert(downloadLocationIndex(locations, "") == 1); // legacy /data choice
    assert(downloadLocationIndex(locations, "/data") == 1);
    assert(locations[2].label == "External (usb0)");
    assert(locations[3].label == "External (ext1)");
    assert(downloadLocationIndex(locations, "/mnt/ext1") == 3);
    assert(downloadLocationIndex(locations, "/unknown") == 0);

    // A promoted sandbox may deny new directories despite retained settings FDs.
    Ops::writablePaths.erase(sandbox);
    locations = discoverDownloadLocations<Ops>(sandbox);
    assert(locations.size() == 3 && locations[0].root == "/data");
    assert(downloadLocationIndex(locations, "sandbox") == 0);

    // Mounted-but-read-only and disconnected drives are not offered or probed.
    Ops::writablePaths.erase("/mnt/ext1/switchfin/downloads");
    Ops::mounts.erase("/mnt/usb0");
    Ops::probed.clear();
    locations = discoverDownloadLocations<Ops>(sandbox);
    assert(locations.size() == 1 && locations[0].root == "/data");
    assert(!Ops::probed.count("/mnt/usb0/switchfin/downloads"));
    downloadSelection().locations = locations;
    setDownloadLocation("/mnt/usb0");
    assert(downloadHome(sandbox) == elevatedDownloads);
    assert(downloadLocationPreference() == "/mnt/usb0");
    assert(downloadLocationLabel() == "Internal (/data) (fallback)");
    setDownloadLocation("/data");
    assert(downloadLocationLabel() == "Internal (/data)");

    // External storage does not depend on /data passing its separate probe.
    state().elevated = false;
    Ops::writablePaths.insert("/mnt/ext1/switchfin/downloads");
    locations = discoverDownloadLocations<Ops>(sandbox);
    assert(locations.size() == 1 && locations[0].root == "/mnt/ext1");
    assert(downloadLocationIndex(locations, "sandbox") == -1);
    assert(downloadLocationIndex(locations, "/mnt/ext1") == 0);
    assert(downloadLocationIndex({}, "sandbox") == -1);
}
''')

    def test_directory_probe_preserves_existing_files(self):
        compile_run(r'''
#include <utils/ps5_storage_home.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
using namespace ps5::storage;
int main() {
    char root[] = "/tmp/switchfin-locations-XXXXXX";
    assert(mkdtemp(root));
    const std::string directory = std::string(root) + "/switchfin/downloads";
    assert(LocationSystem::writable(directory));
    assert(std::filesystem::is_empty(directory));
    const auto media = directory + "/existing.mkv";
    { std::ofstream stream(media); stream << "preserved"; }
    assert(LocationSystem::writable(directory));
    std::string contents;
    { std::ifstream stream(media); stream >> contents; }
    assert(contents == "preserved");
    assert(std::distance(std::filesystem::directory_iterator(directory),
                         std::filesystem::directory_iterator{}) == 1);
    // A file occupying the requested directory cannot pass the probe.
    assert(!LocationSystem::writable(media));
    // Missing roots cannot be manufactured by a probe for an unplugged device.
    assert(!LocationSystem::writable(std::string(root) + "/missing/switchfin/downloads"));
    std::filesystem::remove_all(root);
}
''')
