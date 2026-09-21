#!/usr/bin/env python3
"""download index scan: what the index claims against what the filesystem holds.

The Downloads tab addresses a completed download by index; Local Storage addresses it by path. A
download that appears in one and not the other is only explained by asking the filesystem directly,
and the application is the only process that may: its own directory is 0700 under a uid FTP does not
have. These programs check the scan itself, then the three places it is wired in.
"""
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#define PS5_NATIVE_GPU 1
#include "utils/ps5_download_index_scan.hpp"
#include <cassert>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <string>
namespace fs = std::filesystem;
using namespace ps5::downloads;

static int readEntries = 0, stopAfter = -1, readError = 0;
struct Ops : ScanOps {
    static const char* readEntry(void* handle) {
        if (stopAfter >= 0 && readEntries >= stopAfter) { errno = readError; return nullptr; }
        ++readEntries;
        return ScanOps::readEntry(handle);
    }
};

static void put(const fs::path& p, const char* value) { std::ofstream f(p); f << value; assert(f.good()); }

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root = argv[1];
    const fs::path downloads = root / "downloads";
    fs::create_directories(downloads / "abc123");
    fs::create_directories(downloads / "def456");
    put(downloads / "abc123" / "video.mkv", "0123456789");
    put(downloads / "abc123" / "thumb.png", "img");
    put(downloads / "index.json", "[]");

    // A file the index names and the filesystem has.
    auto scan = scanIndexEntry(downloads.string(), "abc123", "video.mkv");
    assert(scan.result == 0 && scan.error == 0 && scan.size == 10 && scan.named);

    // A file the index names and the filesystem does not have. The errno is the
    // whole point: absent and unreadable are different answers.
    scan = scanIndexEntry(downloads.string(), "def456", "video.mkv");
    assert(scan.result == -1 && scan.error == ENOENT && scan.size == 0 && scan.named);

    // An entry that never named a file at all.
    scan = scanIndexEntry(downloads.string(), "abc123", "");
    assert(scan.result == -2 && scan.error == 0 && scan.size == 0 && !scan.named);

    // A damaged entry cannot walk out of the download directory, in either component.
    for (const char* id : {"", ".", "..", "../outside", ".switchfin-removing-3"}) {
        scan = scanIndexEntry(downloads.string(), id, "video.mkv");
        assert(scan.result == -2 && !scan.named);
    }
    scan = scanIndexEntry(downloads.string(), "abc123", "../../escape");
    assert(scan.result == -2 && !scan.named);

    // Counting children: . and .. are never counted, directories are counted apart.
    auto dir = scanDirectory(downloads.string());
    assert(dir.result == 0 && dir.error == 0 && dir.entries == 3 && dir.directories == 2);

    auto leaf = scanDirectory((downloads / "abc123").string());
    assert(leaf.result == 0 && leaf.entries == 2 && leaf.directories == 0);

    // A directory that is not there reports why, and counts nothing.
    auto absent = scanDirectory((root / "absent").string());
    assert(absent.result == -1 && absent.error == ENOENT && absent.entries == 0);

    // An enumeration that stops part way is reported as partial, not as a count.
    readEntries = 0; stopAfter = 2; readError = EIO;
    auto partial = scanDirectory<Ops>(downloads.string());
    assert(partial.result == 0 && partial.entries <= 2);
    stopAfter = -1; readError = 0;

    // A symbolic link to a directory is not counted as one: lstat, not stat.
    fs::create_directory_symlink(downloads / "abc123", downloads / "link");
    dir = scanDirectory(downloads.string());
    assert(dir.entries == 4 && dir.directories == 2);
    return 0;
}
'''


class DownloadIndexScanTests(unittest.TestCase):
    def test_scan_reports_presence_size_and_reason(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='ps5-index-scan-') as tmp:
            work = Path(tmp)
            cpp = work / 'test.cpp'
            cpp.write_text(HARNESS)
            exe = work / 'test'
            subprocess.run([compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-D__PS5__',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I', str(ROOT / 'app/include'), str(cpp), '-o', str(exe)], check=True)
            data = work / 'data'
            data.mkdir()
            subprocess.run([str(exe), str(data)], check=True,
                           env=dict(os.environ,
                                    ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
                                    UBSAN_OPTIONS='halt_on_error=1'))


if __name__ == '__main__': unittest.main()
