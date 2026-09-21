#!/usr/bin/env python3
"""Fault-inject actual native URL-flight registration and completion ownership."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeArtworkFlightsTests(unittest.TestCase):
    def test_allocation_rollback_reentrancy_and_delivery_faults(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "C++ host compiler required")
        with tempfile.TemporaryDirectory(prefix="native-artwork-flights-") as work:
            binary = Path(work) / "fixture"
            command = [compiler, "-std=c++17", "-g", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       "-I", str(ROOT / "app/include"),
                       str(ROOT / "tests/ps5/native_artwork_flights_faults.cpp"), "-o", str(binary)]
            if sys.platform.startswith("linux"):
                command += ["-D_GLIBCXX_DEBUG", "-D_GLIBCXX_ASSERTIONS"]
            subprocess.run(command, check=True, timeout=120)
            environment = dict(os.environ,
                               ASAN_OPTIONS="detect_leaks=" + ("0" if sys.platform == "darwin" else "1") + ":halt_on_error=1",
                               UBSAN_OPTIONS="halt_on_error=1")
            subprocess.run([str(binary)], check=True, timeout=30, env=environment)


if __name__ == "__main__":
    unittest.main()
