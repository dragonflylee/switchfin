#!/usr/bin/env python3
"""Exercise actual native cache configuration, API failures and budget overrides."""
from source_fixture import native_source
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include "utils/ps5_native_cache_budget.hpp"
#include <cassert>
namespace ps5_native_startup { namespace detail { template<class... Args> void line(const char*, Args...) {} } }
enum class Ps5HeapPhase { LoadBefore, LoadAfter };
void ps5_native_heap_checkpoint(Ps5HeapPhase) {}
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
enum { MPV_FORMAT_INT64, MPV_FORMAT_FLAG };
namespace brls { inline void fatal(const char*) { throw std::runtime_error("rejected"); } }
static int calls, failureCall, corruptCall, destroys;
static int64_t values[3];
static int index(const char* name, bool reading) {
    if (reading) { assert(!strncmp(name,"options/",8)); name+=8; }
    if (!strcmp(name,"demuxer-max-bytes")) return 0;
    if (!strcmp(name,"demuxer-max-back-bytes")) return 1;
    assert(!strcmp(name,"demuxer-donate-buffer")); return 2;
}
static int mpv_get_property(void*,const char* name,int format,void* pointer) {
    int i=index(name,true); assert(format==(i==2?MPV_FORMAT_FLAG:MPV_FORMAT_INT64));
    if (++calls==failureCall) return -1;
    int64_t value=values[i]+(calls==corruptCall?1:0);
    if (i==2) *static_cast<int*>(pointer)=value; else *static_cast<int64_t*>(pointer)=value;
    return 0;
}
static int mpv_set_property(void*,const char* name,int format,void* pointer) {
    int i=index(name,false); assert(format==(i==2?MPV_FORMAT_FLAG:MPV_FORMAT_INT64));
    if (++calls==failureCall) return -1;
    values[i]=i==2?*static_cast<int*>(pointer):*static_cast<int64_t*>(pointer);
    return 0;
}
static void mpv_terminate_destroy(void*) { ++destroys; }
struct MPVCore {
    void* mpv=reinterpret_cast<void*>(1);
    ps5_native_cache_budget::Limits nativeCacheLimits{17,19};
    void configureActual() {
@CONFIGURE@
    }
};
int main() {
    using namespace ps5_native_cache_budget;
    for (int64_t forward : {INT64_MIN,int64_t{-1},int64_t{0},int64_t{1},maximumForward,INT64_MAX})
    for (int64_t backward : {INT64_MIN,int64_t{0},int64_t{1},maximumBackward,INT64_MAX})
    for (int donation : {0,1})
    for (int fail=0;fail<=9;++fail) {
        values[0]=forward;values[1]=backward;values[2]=donation;
        calls=destroys=corruptCall=0;failureCall=fail; MPVCore core;bool rejected=false;
        try { core.configureActual(); } catch(const std::runtime_error&) { rejected=true; }
        assert(rejected==(fail!=0));
        if (fail) {
            assert(destroys==1&&core.mpv==nullptr&&calls==fail);
            assert(core.nativeCacheLimits.forward==17&&core.nativeCacheLimits.backward==19);
            continue;
        }
        assert(destroys==0&&calls==9);
        assert(core.nativeCacheLimits.forward==std::clamp(forward,int64_t{0},maximumForward));
        assert(core.nativeCacheLimits.backward==std::clamp(backward,int64_t{0},maximumBackward));
        assert(values[2]==0);
    }
    for(int corrupt : {3,6,9}) {
        values[0]=maximumForward;values[1]=maximumBackward;values[2]=1;
        calls=destroys=failureCall=0;corruptCall=corrupt;MPVCore core;bool rejected=false;
        try { core.configureActual(); } catch(const std::runtime_error&) { rejected=true; }
        assert(rejected&&destroys==1&&core.mpv==nullptr&&calls==corrupt);
    }
    const std::string extra="aid=2,demuxer-max-bytes=999999999,demuxer-max-back-bytes=999999999,demuxer-donate-buffer=yes";
    const auto original=extra;
    assert(loadOptions(extra,{INT64_MAX,INT64_MAX})==extra+",demuxer-max-bytes=8388608,demuxer-max-back-bytes=2097152,demuxer-donate-buffer=no");
    assert(extra==original);
    assert(loadOptions("",{INT64_MIN,0})=="demuxer-max-bytes=0,demuxer-max-back-bytes=0,demuxer-donate-buffer=no");
    assert(loadOptions("start=12",{1024,512})=="start=12,demuxer-max-bytes=1024,demuxer-max-back-bytes=512,demuxer-donate-buffer=no");
}
'''


class CacheBudget(unittest.TestCase):
    def test_real_configuration_and_load_budget(self):
        source = native_source(ROOT / "app/src/view/mpv_core.cpp")
        begin = source.index("    // Bound the application-owned heap")
        end = source.index('    brls::Logger::info("ps5 native: vd-lavc-dr=', begin)
        self.assertGreater(begin, source.index("mpv_initialize(mpv)"))
        with tempfile.TemporaryDirectory(prefix="native-cache-budget-") as directory:
            path = Path(directory)
            (path / "test.cpp").write_text(HARNESS.replace("@CONFIGURE@", source[begin:end]))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17", "-g", "-O1",
                            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-I" + str(ROOT / "app/include"), str(path / "test.cpp"),
                            "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], check=True, env=dict(os.environ,
                ASAN_OPTIONS="detect_leaks=" + ("0" if sys.platform == "darwin" else "1") + ":halt_on_error=1",
                UBSAN_OPTIONS="halt_on_error=1"))


if __name__ == "__main__":
    unittest.main()
