#!/usr/bin/env python3
"""Exercise native getcwd allocation and sandbox-root matching."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeWorkingDirectory(unittest.TestCase):
    def test_matching_and_error_results(self):
        with tempfile.TemporaryDirectory(prefix='native-getcwd-') as directory:
            work = Path(directory)
            (work / 'sys').mkdir()
            (work / 'unistd.h').write_text('#include <stddef.h>\nchar *getcwd(char *, size_t);\n')
            (work / 'sys/stat.h').write_text('struct stat { unsigned long st_dev, st_ino; };\nint stat(const char *, struct stat *);\n')
            source = work / 'test.c'
            source.write_text(r'''
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "sys/stat.h"
static const char *selected = "/app0";
static int stat_failure;
int stat(const char *path, struct stat *result) {
    if (stat_failure) { errno = EACCES; return -1; }
    result->st_dev = 7;
    result->st_ino = (!strcmp(path, ".") || !strcmp(path, selected)) ? 12 : 19;
    return 0;
}
char *ps5_getcwd(char *, size_t);
int main(void) {
    char buffer[64];
    for (size_t size = 1; size <= strlen(selected); ++size) {
        errno = 0;
        assert(ps5_getcwd(buffer, size) == NULL && errno == ERANGE);
        errno = 0;
        assert(ps5_getcwd(NULL, size) == NULL && errno == ERANGE);
    }
    assert(ps5_getcwd(buffer, sizeof buffer) == buffer && !strcmp(buffer, selected));
    char *allocated = ps5_getcwd(NULL, 0);
    assert(allocated && !strcmp(allocated, selected)); free(allocated);
    errno = 0;
    assert(ps5_getcwd(buffer, 0) == NULL && errno == EINVAL);
    for (int failure = 0; failure <= 1; ++failure) {
        selected = "/unrecognized"; stat_failure = failure;
        assert(ps5_getcwd(buffer, sizeof buffer) == buffer && !strcmp(buffer, "/"));
    }
}
''')
            executable = work / 'test'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-DPS5_NATIVE_GPU=1',
                '-Dgetcwd=ps5_getcwd', '-Dstat=fixture_stat', '-I', str(work), str(source),
                str(ROOT / 'scripts/ps5/native-player/posix/native_getcwd.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
