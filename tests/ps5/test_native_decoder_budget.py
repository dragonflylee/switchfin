#!/usr/bin/env python3
"""Exercise real native configure/load bodies with checked MPV API fault stubs."""
from source_fixture import native_source
from pathlib import Path
import os,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
PREFIX=r'''
#include "utils/ps5_native_decoder_budget.hpp"
#include "utils/ps5_native_cache_budget.hpp"
#include <cassert>
namespace ps5_native_startup { namespace detail { template<class... Args> void line(const char*, Args...) {} } }
enum class Ps5HeapPhase { LoadBefore, LoadAfter };
void ps5_native_heap_checkpoint(Ps5HeapPhase) {}
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#define PS5_NATIVE_GPU 1
#define MPV_MAKE_VERSION(a,b) (((a)<<16)|(b))
enum {MPV_FORMAT_INT64, MPV_ERROR_UNINITIALIZED = -3};
static int fault, reads, writes, destroys, submissionResult;
static int64_t requested, actual;
static unsigned long api;
static uint64_t submitted;
static std::vector<std::string> command;
namespace brls {
struct Logger {template<class... T>static void debug(T...){ } template<class... T>static void info(T...){ }};
inline void fatal(const char*) { throw std::runtime_error("configuration rejected"); }
}
static int mpv_get_property(void*,const char* name,int,int64_t* value){
 assert(!strcmp(name,"options/vd-lavc-threads"));++reads;
 if((fault==1&&reads==1)||(fault==3&&reads==2))return -1;
 *value=reads==1?requested:(fault==4?6:actual);return 0;
}
static int mpv_set_property(void*,const char* name,int,int64_t* value){
 assert(!strcmp(name,"vd-lavc-threads"));++writes;if(fault==2)return -1;actual=*value;return 0;
}
static void mpv_terminate_destroy(void*){++destroys;}
static unsigned long mpv_client_api_version(){return api;}
static int mpv_command_async(void*,uint64_t value,const char** args){
 submitted=value;command.clear();for(;*args;args++)command.emplace_back(*args);return submissionResult;
}
struct MPVCore {
 std::mutex playlistCommandMutex;
 uint64_t playlistMutationGeneration=0;
 void *mpv=reinterpret_cast<void*>(1);
 int64_t nativeDecoderThreads=2;
 ps5_native_cache_budget::Limits nativeCacheLimits;
 void configureActual(){
@CONFIGURE@
 }
 int setUrl(const std::string&,const std::string&,const std::string&,uint64_t,int64_t=0,int64_t=0);
};
@LOAD@
int main(){
 for(int f=0;f<5;f++)for(int64_t n:{INT64_MIN,int64_t{-1},int64_t{0},int64_t{1},int64_t{2},int64_t{6},INT64_MAX}){
  fault=f;reads=writes=destroys=0;requested=n;actual=99;MPVCore core;bool failed=false;
  try{core.configureActual();}catch(const std::runtime_error&){failed=true;}
  assert(failed==(f!=0));
  if(f){assert(destroys==1&&core.mpv==nullptr);continue;}
  assert(destroys==0&&reads==2&&writes==1&&core.nativeDecoderThreads==(n==1?1:2));
  for(unsigned long version:{MPV_MAKE_VERSION(2,2),MPV_MAKE_VERSION(2,3)}){
   api=version;
   for(const std::string extra:{"","aid=2,sid=no,start=00:01:02","vd-lavc-threads=16,aid=0,demuxer-max-bytes=999999999,demuxer-max-back-bytes=999999999,demuxer-donate-buffer=yes"}){
    const auto original=extra;
    assert(core.setUrl("fixture",extra,"replace",73)==0);
    assert(core.playlistMutationGeneration>0);
    assert(extra==original&&submitted==73&&command[0]=="loadfile"&&command[1]=="fixture"&&command[2]=="replace");
    const size_t slot=version>=MPV_MAKE_VERSION(2,3)?4:3;
    assert(command.size()==slot+1);if(slot==4)assert(command[3]=="0");
    const auto expected=extra+(extra.empty()?"":",")+"vd-lavc-threads="+(n==1?"1":"2")+",demuxer-max-bytes=8388608,demuxer-max-back-bytes=2097152,demuxer-donate-buffer=no";
    assert(command[slot]==expected);
    core.setUrl("next-fixture",extra,"append-play",74);
    assert(submitted==74&&command[2]=="append-play"&&command[slot]==expected);
    for(int failure:{-3,-4,-12,-20}) {
      submissionResult=failure;
      assert(core.setUrl("rejected",extra,"append",75)==failure);
      assert(submitted==75&&command[2]=="append"&&command[slot]==expected);
    }
    submissionResult=0;
    // only sources above 1440p take six threads; explicit one thread wins.
    struct Geometry {int64_t w,h,threads;};
    for(const Geometry g:std::initializer_list<Geometry>{{0,0,2},{1920,1080,2},{2560,1440,2},{2561,1080,6},{1920,1441,6},
        {3840,2064,6},{3840,2160,6},{INT64_MAX,INT64_MAX,6},{INT64_MIN,-1,2}}){
     assert(core.setUrl("uhd",extra,"replace",76,g.w,g.h)==0);
     const auto uhd=extra+(extra.empty()?"":",")+"vd-lavc-threads="+std::to_string(n==1?1:g.threads)+
         ",demuxer-max-bytes=8388608,demuxer-max-back-bytes=2097152,demuxer-donate-buffer=no";
     assert(submitted==76&&command[slot]==uhd);
     assert(ps5_native_decoder_budget::selectForSource(core.nativeDecoderThreads,g.w,g.h)==(n==1?1:g.threads));
    }
   }
  }
 }
 MPVCore absent;absent.mpv=nullptr;submitted=99;
 assert(absent.setUrl("fixture","","replace",100)==MPV_ERROR_UNINITIALIZED && submitted==99);
}
'''
class DecoderBudget(unittest.TestCase):
 def test_real_configure_and_load_bodies(self):
  text=native_source(ROOT/'app/src/view/mpv_core.cpp')
  start=text.index('    // Configure and read back the native decoder budget')
  end=text.index('    // Bound the application-owned heap',start)
  configure=text[start:end]
  begin=text.index('int MPVCore::setUrl(')
  load=text[begin:text.index('void MPVCore::togglePlay()')]
  self.assertLess(text.index('mpv_initialize(mpv)'),start)
  self.compile(configure,load)
 def compile(self,configure,load):
  with tempfile.TemporaryDirectory(prefix='native-decoder-budget-') as d:
   p=Path(d);(p/'test.cpp').write_text(PREFIX.replace('@CONFIGURE@',configure).replace('@LOAD@',load))
   compiler=os.environ.get('CXX','clang++');flags=os.environ.get('PS5_TEST_SANITIZERS','-fsanitize=address,undefined').split()
   subprocess.run([compiler,'-std=c++17','-g','-O1','-Wall','-Wextra','-Werror',*flags,'-I'+str(ROOT/'app/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1')+':halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1')
   subprocess.run([str(p/'test')],check=True,env=env)
if __name__=='__main__':unittest.main()
