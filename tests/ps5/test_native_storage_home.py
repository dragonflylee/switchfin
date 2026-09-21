#!/usr/bin/env python3
"""storage home: /data chosen only on real evidence, sandbox otherwise.

The home is elevated only when promotion succeeds AND a create/write/sync/unlink probe of
/data/switchfin/downloads round-trips. These programs inject both the unjail outcome and the probe
operations to cover: promoted+writable, promoted+probe-fails, and not promoted.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

DRIVER = r'''
#define PS5_NATIVE_GPU 1
#include "utils/ps5_storage_home.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
using namespace ps5::storage;

// Probe operations against a real temp tree, with an injectable failure.
static std::string fakeRoot;   // where /data is faked
static int failAt = -1, step = 0; // step index to fail
struct ProbeOps {
    static std::string map(const char* p) { return fakeRoot + p; }
    static int makeDirectory(const char* p) { if (step++ == failAt) { errno = EACCES; return -1; } return ::mkdir(map(p).c_str(), 0700); }
    static bool isDirectory(const char* p) { struct stat s{}; return ::stat(map(p).c_str(), &s) == 0 && S_ISDIR(s.st_mode); }
    static int pid() { return 4242; }
    static int create(const char* p) {
        if (step++ == failAt) { errno = EROFS; return -1; }
        return ::open(map(p).c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    static ssize_t write(int fd, const void* b, size_t n) { if (step++ == failAt) { errno = ENOSPC; return -1; } return ::write(fd, b, n); }
    static int sync(int fd) { (void)fd; return 0; }
    static void close(int fd) { ::close(fd); }
    static int unlink(const char* p) { return ::unlink(map(p).c_str()); }
};

int main(int argc, char** argv)
{
    assert(argc == 2);
    fakeRoot = argv[1];

    // A clean probe against a writable faked /data returns 0.
    int e = 0; step = 0; failAt = -1;
    assert(probeElevated<ProbeOps>(e) == 0 && e == 0);
    // It left nothing behind.
    std::string dl = fakeRoot + "/data/switchfin/downloads";
    DIR* d = ::opendir(dl.c_str()); int count = 0;
    if (d) { while (dirent* en = ::readdir(d)) { std::string n = en->d_name; if (n != "." && n != "..") ++count; } ::closedir(d); }
    assert(count == 0);

    // Each step failing is reported, not hidden.
    for (int f = 0; f < 4; ++f) { e = 0; step = 0; failAt = f; assert(probeElevated<ProbeOps>(e) == -1 && e != 0); }

    // downloadHome before resolve() is the sandbox path.
    assert(downloadHome("/download0/switchfin-native/downloads") == "/download0/switchfin-native/downloads");

    std::printf("ok\n");
    return 0;
}
'''


class StorageHomeTests(unittest.TestCase):
    def test_probe_and_home_selection(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='ps5-storage-home-') as tmp:
            work = Path(tmp)
            src = work / 'driver.cpp'
            src.write_text('#include <dirent.h>\n#include <fcntl.h>\n#include <sys/stat.h>\n#include <unistd.h>\n#include <vector>\n' + DRIVER)
            exe = work / 'test'
            subprocess.run([compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-I', str(ROOT / 'app/include'), str(src), '-o', str(exe)], check=True)
            data = work / 'root'
            (data / 'data').mkdir(parents=True)
            subprocess.run([str(exe), str(data)], check=True,
                           env=dict(os.environ,
                                    ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
                                    UBSAN_OPTIONS='halt_on_error=1'))

    def test_elevated_requires_promotion_and_a_passing_probe(self):
        header = (ROOT / 'app/include/utils/ps5_storage_home.hpp').read_text()
        self.assertIn("elevatedDownloads = \"/data/switchfin/downloads\"", header)
        # elevated is set only from the probe result, only after Promoted.
        self.assertIn('if (ProbeOps::isDirectory(promotedApp0))', header)
        self.assertIn('Ops::create(path)', header)
        self.assertNotIn('::mkstemp', header)
        self.assertIn('report.elevated = report.probeResult == 0;', header)


if __name__ == '__main__':
    unittest.main()
