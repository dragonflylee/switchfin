#!/usr/bin/env python3
"""Native encoded-body admission with real HTTP callbacks and valid 4K pixels."""
from source_fixture import native_source
import gzip
import http.server
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import zlib

from source_fixture import extract_function

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <ostream>
#include <thread>
#include <vector>
#include "utils/ps5_native_artwork_body.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include <borealis/extern/nanovg/stb_image.h>
class HTTP { public: static size_t easy_write_cb(char*,size_t,size_t,void*); };
@CALLBACK@
using namespace ps5::artwork;
static EncodedBudget* observed = nullptr;
static size_t live = 0, peak = 0, calls = 0, failCall = 0;
static std::map<void*,size_t> allocations;
static std::mutex allocationMutex;
struct Heap {
    static void* allocate(size_t bytes) noexcept {
        std::lock_guard<std::mutex> lock(allocationMutex);
        assert(observed && observed->bytes() >= live + bytes);
        if (++calls == failCall) return nullptr;
        void* result = std::malloc(bytes); assert(result);
        allocations[result] = bytes; live += bytes; peak = std::max(peak,live);
        return result;
    }
    static void deallocate(void* value) noexcept {
        if (!value) return;
        std::lock_guard<std::mutex> lock(allocationMutex);
        assert(observed && observed->bytes() >= live && allocations.count(value));
        live -= allocations.at(value); allocations.erase(value); std::free(value);
    }
};
using Body = EncodedBody<Heap>;
static void unit() {
    EncodedBudget budget(48*1024); observed = &budget;
    const std::string block(16*1024,'x');
    for (int fail = 0; fail <= 2; ++fail) {
        calls=0; failCall=fail; peak=0;
        {
            Body body(budget,32*1024); std::ostream stream(&body);
            stream.write(block.data(),block.size());
            if (fail==1) {
                assert(!stream.good() && body.empty() && budget.bytes()==0);
                assert(body.failure()==Body::Failure::Allocation);
            } else {
                assert(stream.good() && budget.bytes()==16*1024);
                stream.write(block.data(),block.size());
                if (fail==2) {
                    assert(!stream.good() && body.size()==16*1024 && budget.bytes()==16*1024);
                    assert(body.failure()==Body::Failure::Allocation);
                } else {
                    assert(stream.good() && body.size()==32*1024 && peak==48*1024);
                    assert(budget.bytes()==32*1024);
                    stream.put('!'); assert(!stream.good());
                    assert(body.failure()==Body::Failure::ResponseLimit);
                }
                assert(std::all_of(body.data(),body.data()+body.size(),[](char c){return c=='x';}));
            }
            const auto before=body.size(); stream.clear(); stream.put('?');
            assert(!stream.good() && body.size()==before); // failure stays terminal
        }
        assert(live==0 && allocations.empty() && budget.bytes()==0);
    }
    failCall=0;
    {
        Body first(budget), second(budget); std::ostream a(&first),b(&second);
        a.write(block.data(),block.size()); b.write(block.data(),block.size());
        a.put('y'); assert(!a.good() && first.failure()==Body::Failure::ResidentLimit);
        assert(first.size()==block.size() && budget.bytes()==32*1024);
    }
    assert(budget.bytes()==0);
    {
        Body empty(budget,0); std::ostream stream(&empty);
        stream.write(nullptr,0); assert(stream.good() && budget.bytes()==0);
        stream.put('x'); assert(!stream.good() && budget.bytes()==0);
    }
    {
        Body odd(budget,31); std::ostream stream(&odd);
        for(int i=0;i<31;++i) stream.put(static_cast<char>(i));
        assert(stream.good() && odd.size()==31 && budget.bytes()==31);
        stream << std::flush; assert(stream.good());
        for(int i=0;i<31;++i) assert(odd.data()[i]==i);
        const char one='a'; assert(odd.sputn(&one,std::numeric_limits<std::streamsize>::max())==0);
        assert(odd.size()==31 && odd.failure()==Body::Failure::ResponseLimit);
    }
    assert(budget.bytes()==0);
    // All workers retain a first allocation before any attempts to grow.
    EncodedBudget concurrent(8*16384); observed=&concurrent; peak=0;
    std::atomic<int> ready{0}; std::atomic<bool> release{false};
    std::vector<std::thread> workers;
    for(int i=0;i<8;++i) workers.emplace_back([&] {
        Body body(concurrent); std::ostream stream(&body); stream.write(block.data(),block.size());
        assert(stream.good()); ++ready;
        while(!release.load()) std::this_thread::yield();
        stream.put('x'); // admission may fail or succeed as other bodies retire
        assert(body.size()==16384 || body.size()==16385);
    });
    while(ready.load()!=8) std::this_thread::yield();
    assert(concurrent.bytes()==8*16384); release=true;
    for(auto& worker:workers) worker.join();
    assert(concurrent.bytes()==0 && live==0 && peak<=8*16384);
}
static void transfer(const char* base) {
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    auto* easy=curl_easy_init(); assert(easy);
    curl_easy_setopt(easy,CURLOPT_NOPROXY,"*");
    curl_easy_setopt(easy,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(easy,CURLOPT_TIMEOUT_MS,3000L);
    curl_easy_setopt(easy,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(easy,CURLOPT_WRITEFUNCTION,HTTP::easy_write_cb);
    EncodedBudget budget(48*1024); observed=&budget;
    for(const char* path:{"exact","oversize","chunked","gzip","truncated","exact"}) {
        {
            Body body(budget,32*1024); std::ostream stream(&body);
            // Exercise the production catch boundary when ostream throws too.
            stream.exceptions(std::ios::badbit|std::ios::failbit);
            const auto url=std::string(base)+path;
            curl_easy_setopt(easy,CURLOPT_URL,url.c_str());
            curl_easy_setopt(easy,CURLOPT_WRITEDATA,&stream);
            const auto result=curl_easy_perform(easy);
            const bool gzipSupported=(curl_version_info(CURLVERSION_NOW)->features&CURL_VERSION_LIBZ)!=0;
            std::printf("HTTP case=%s result=%d bytes=%zu gzip=%d\n",path,result,body.size(),gzipSupported);
            if(std::string(path)=="exact") {
                assert(result==CURLE_OK && stream.good() && body.size()==32768);
                assert(std::all_of(body.data(),body.data()+body.size(),[](char c){return c=='a';}));
            } else if(std::string(path)=="truncated") {
                assert(result==CURLE_PARTIAL_FILE && body.size()==16384);
            } else if(std::string(path)=="gzip" && !gzipSupported) {
                // The WS-enabled curl 8.18 host build has no content decoder:
                // it passes unsolicited gzip bytes through unchanged. The image
                // decoder must reject those bytes. Gzip expansion/admission is
                // exercised separately with the zlib-enabled host libraries.
                assert(result==CURLE_OK && stream.good() && body.size()>2 && body.size()<256);
                assert(static_cast<unsigned char>(body.data()[0])==0x1f && static_cast<unsigned char>(body.data()[1])==0x8b);
                int w=0,h=0,c=0;
                assert(!stbi_load_from_memory(reinterpret_cast<const unsigned char*>(body.data()),static_cast<int>(body.size()),&w,&h,&c,4));
                assert(body.failure()==Body::Failure::None);
            } else {
                assert(result==CURLE_WRITE_ERROR && !stream.good());
                assert(body.size()<=32768 && body.failure()==Body::Failure::ResponseLimit);
            }
        }
        assert(budget.bytes()==0 && live==0);
    }
    curl_easy_cleanup(easy); curl_global_cleanup();
}
static void image(const char* filename) {
    EncodedBudget budget; observed=&budget;
    {
        Body body(budget); std::ostream stream(&body); std::ifstream file(filename,std::ios::binary);
        assert(file.good()); std::array<char,16384> chunk;
        while(file) {file.read(chunk.data(),chunk.size());stream.write(chunk.data(),file.gcount());assert(stream.good());}
        assert(body.size()>31*1024*1024 && body.size()<=encodedResponseLimit);
        int w=0,h=0,c=0;
        auto* pixels=stbi_load_from_memory(reinterpret_cast<const unsigned char*>(body.data()),static_cast<int>(body.size()),&w,&h,&c,4);
        assert(pixels && w==3840 && h==2160 && c==4);
        for(size_t i=0;i<static_cast<size_t>(w)*h;++i) {
            assert(pixels[4*i]==30 && pixels[4*i+1]==80 && pixels[4*i+2]==170 && pixels[4*i+3]==255);
        }
        stbi_image_free(pixels);
        assert(budget.bytes()==32*1024*1024 && peak==48*1024*1024);
    }
    assert(budget.bytes()==0 && live==0);
}
int main(int argc,char** argv) {
    assert(argc==2 || argc==3);
    if(std::string(argv[1])=="unit") unit();
    else if(std::string(argv[1])=="http") transfer(argv[2]);
    else image(argv[2]);
    assert(allocations.empty());
}
'''


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *_): pass
    def do_GET(self):
        data = b'a' * (32768 if self.path in ('/exact', '/truncated') else 32769)
        if self.path == '/gzip': data = gzip.compress(data)
        self.send_response(200)
        self.send_header('Connection', 'close')
        self.send_header('Transfer-Encoding' if self.path == '/chunked' else 'Content-Length',
                         'chunked' if self.path == '/chunked' else str(len(data)))
        if self.path == '/gzip': self.send_header('Content-Encoding', 'gzip')
        self.end_headers()
        try:
            if self.path == '/chunked':
                for start in range(0, len(data), 8192):
                    chunk = data[start:start + 8192]
                    self.wfile.write(f'{len(chunk):x}\r\n'.encode() + chunk + b'\r\n')
                self.wfile.write(b'0\r\n\r\n')
            else:
                self.wfile.write(data[:16384] if self.path == '/truncated' else data)
        except (BrokenPipeError, ConnectionResetError): pass
        self.close_connection = True


def png(path):
    # Ordinary uncompressed RGBA PNG: full 4K geometry just below the body cap.
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    row = b'\0' + bytes((30, 80, 170, 255)) * 3840
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 3840, 2160, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(row * 2160, 0)) + chunk(b'IEND', b''))


class NativeArtworkBodyTests(unittest.TestCase):
    def test_admission_transport_and_pixels(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        flags = os.environ.get('CURL_TEST_CFLAGS')
        libs = os.environ.get('CURL_TEST_LIBS')
        if flags is None: flags = subprocess.check_output(['curl-config', '--cflags'], text=True)
        if libs is None: libs = subprocess.check_output(['curl-config', '--libs'], text=True)
        source = native_source(ROOT / 'app/src/api/http.cpp')
        with tempfile.TemporaryDirectory(prefix='native-artwork-body-') as tmp:
            work = Path(tmp)
            cpp = work / 'test.cpp'
            cpp.write_text(HARNESS.replace('@CALLBACK@', extract_function(source, 'size_t HTTP::easy_write_cb(')))
            binary = work / 'test'
            subprocess.run([compiler, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-pthread',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-D__PS5__','-DPS5_NATIVE_GPU',
                            '-I', str(ROOT / 'app/include'), '-I', str(ROOT / 'library/borealis/library/include'),
                            *shlex.split(flags), str(cpp), *shlex.split(libs), '-o', str(binary)], check=True)
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=' + ('0' if sys.platform == 'darwin' else '1'),
                       UBSAN_OPTIONS='halt_on_error=1')
            subprocess.run([str(binary), 'unit'], check=True, env=env)
            fixture = work / '4k.png'; png(fixture)
            subprocess.run([str(binary), 'image', str(fixture)], check=True, env=env)
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            try:
                subprocess.run([str(binary), 'http', f'http://127.0.0.1:{server.server_port}/'], check=True, env=env)
            finally:
                server.shutdown(); server.server_close(); thread.join()


if __name__ == '__main__': unittest.main()
