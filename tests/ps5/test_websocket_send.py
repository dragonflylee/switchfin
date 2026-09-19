"""Run the actual bounded heartbeat send body with forced partial I/O."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WebSocketSend(unittest.TestCase):
    def test_partial_again_stop_timeout_and_error(self):
        header = (ROOT / 'app/include/api/websocket_transport.hpp').read_text()
        begin = header.index('    CURLcode sendText(')
        end = header.index('\n    CURLcode connectAndReceive(', begin)
        body = header[begin:end]
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            self.skipTest('host C++ compiler required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = r'''
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
// Minimal I/O double. Real curl types and ABI are covered by the loopback
// suite and the target build; this fixture needs no host curl development kit.
struct CURL;
using curl_off_t = long long;
enum CURLcode { CURLE_OK, CURLE_AGAIN, CURLE_SEND_ERROR,
                CURLE_ABORTED_BY_CALLBACK, CURLE_OPERATION_TIMEDOUT };
constexpr unsigned CURLWS_TEXT = 1;
int mode, calls, elapsed;
extern "C" CURLcode curl_ws_send(CURL*, const void* data, size_t length, size_t* sent,
                                 curl_off_t frame_size, unsigned flags) {
    ++calls;
    assert(frame_size == 0 && flags == CURLWS_TEXT);
    *sent = 0;
    if (mode == 0) {
        if (calls == 1) {
            assert(length == 6 && !std::memcmp(data, "ABCDEF", 6));
            *sent = 2;
            return CURLE_OK;
        }
        assert(length == 4 && !std::memcmp(data, "CDEF", 4));
        if (calls == 2) return CURLE_AGAIN;
        *sent = 4;
        return CURLE_OK;
    }
    return mode == 3 ? CURLE_SEND_ERROR : CURLE_AGAIN;
}
struct TestClock {
    static auto now() {
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(elapsed));
    }
};
struct Sender {
    using Clock = TestClock;
    bool stopped = false;
    unsigned waits = 0;
    bool isStopped() const { return stopped; }
    bool wait(unsigned milliseconds) {
        assert(milliseconds == 20);
        ++waits;
        elapsed += milliseconds;
        if (mode == 1) stopped = true;
        return stopped;
    }
''' + body + r'''
};
int main() {
    for (mode = 0; mode < 4; ++mode) {
        Sender sender;
        calls = elapsed = 0;
        auto result = sender.sendText(nullptr, "ABCDEF");
        if (mode == 0) { assert(result == CURLE_OK && calls == 3 && sender.waits == 2); }
        if (mode == 1) { assert(result == CURLE_ABORTED_BY_CALLBACK && calls == 1); }
        if (mode == 2) { assert(result == CURLE_OPERATION_TIMEDOUT && calls == 100 && elapsed == 2000); }
        if (mode == 3) { assert(result == CURLE_SEND_ERROR && calls == 1 && sender.waits == 0); }
    }
}
'''
            (path / 'send.cpp').write_text(source)
            binary = path / 'send-test'
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            str(path / 'send.cpp'), '-o', str(binary)], check=True)
            subprocess.run([binary], check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
