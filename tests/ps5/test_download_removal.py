#!/usr/bin/env python3
"""Actual PS5 directory retirement and non-active removal with re-add and faults."""
from source_fixture import native_source
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from source_fixture import extract_function
ROOT=Path(__file__).resolve().parents[2]
HARNESS=r'''
// Compile directory retirement independently of the application logger.
#define PS5_DOWNLOAD_NOTE(...) ((void)0)
#include "utils/ps5_download_removal.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
namespace fs=std::filesystem;
using namespace ps5::downloads;
static std::string root,mode;
namespace ps5::storage {static std::string sandboxRoot(){return {};} static bool promoted(){return false;}}
static std::vector<std::function<void()>> pending,ui;
static int creates=0,moves=0,notices=0,errors=0;
static bool worker=false;
static std::string read(const fs::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
static void put(const fs::path& p,const char* value){std::ofstream f(p);f<<value;assert(f.good());}
struct Ops:RemovalOps {
 static int createDirectory(const char* p){++creates;if(mode=="create"){errno=EACCES;return -1;}if(mode=="collisions"){errno=EEXIST;return -1;}return RemovalOps::createDirectory(p);}
 static int move(const char* s,const char* d){++moves;if(mode=="move"||mode=="cleanup"){errno=EIO;return -1;}if(mode=="false-missing"){errno=ENOENT;return -1;}return RemovalOps::move(s,d);}
 static int removeEmpty(const char* p){if(mode=="cleanup"){errno=EIO;return -1;}return RemovalOps::removeEmpty(p);}
};
namespace brls {
 struct Application {static void notify(const std::string&){++notices;}};
 struct Logger {template<class...T>static void info(const char*,T&&...){} template<class...T>static void error(const char*,T&&...){++errors;}};
 static void sync(std::function<void()> f){ui.push_back(std::move(f));}
 static void async(std::function<void()> f){assert(!worker);if(mode=="schedule")throw std::runtime_error("schedule failed");pending.push_back(std::move(f));}
}
enum class DownloadStatus{Queued,Downloading,Completed,Failed};
struct Item {std::string itemId="id",errorMessage,storageRoot;DownloadStatus status=DownloadStatus::Completed;};
struct Event {void fire(const std::string&,DownloadStatus){assert(!worker);}};
class DownloadManager {
public:
 std::mutex mutex;std::vector<Item> items{Item{}};std::shared_ptr<std::atomic_bool> currentCancel;Event statusEvent;int saves=0;
 std::string downloadDir()const{return root;}
 std::string indexDir()const{return root;}
 void saveIndex(){++saves;}
 bool retirePS5Download(const std::string&);
 void removeDownload(const std::string&);
};
@METHODS@
static void unit(const fs::path& base){
 for(const char* test:{"success","missing","create","move","false-missing","cleanup","collisions"}) {
  mode=test;creates=moves=0;auto dir=base/test;fs::create_directory(dir);
  if(mode!="missing"){fs::create_directory(dir/"id");put(dir/"id"/"keep","old");}
  std::string retired="unchanged";auto result=retireDirectory<Ops>(dir.string(),"id",retired);
  if(mode=="success") {
   assert(result==Retirement::Moved&&!fs::exists(dir/"id")&&read(fs::path(retired)/"keep")=="old");
   fs::create_directory(dir/"id");put(dir/"id"/"keep","new");fs::remove_all(retired);assert(read(dir/"id"/"keep")=="new");
  }else if(mode=="missing")assert(result==Retirement::Missing&&retired.empty());
  else {assert(result==Retirement::Failed&&retired.empty()&&read(dir/"id"/"keep")=="old");}
  if(mode=="collisions")assert(creates==128&&moves==0);
  size_t leftovers=0;for(const auto& entry:fs::directory_iterator(dir))if(isRetiredDirectory(entry.path().filename().string()))++leftovers;
  assert(leftovers==(mode=="cleanup"?1:0));
 }
 // A pre-existing reserved directory, even empty, is never replaced.
 auto dir=base/"occupied";fs::create_directories(dir/"id");put(dir/"id"/"keep","old");
 fs::create_directory(dir/(std::string(removalPrefix)+"0"));put(dir/(std::string(removalPrefix)+"0")/"keep","unrelated");
 std::string retired;assert(retireDirectory(dir.string(),"id",retired)==Retirement::Moved);
 assert(read(dir/(std::string(removalPrefix)+"0")/"keep")=="unrelated");
 creates=0;
 for(const auto& name:std::vector<std::string>{"",".","..","a/b","a\\b",std::string("a\0b",3),std::string(removalPrefix)+"7"})
  assert(retireDirectory<Ops>(dir.string(),name,retired)==Retirement::Failed);
 assert(creates==0);
}
static void manager(const fs::path& base){
 // Invalid item names must be rejected before retirement or its in-place fallback.
 root=(base/"guard"/"downloads").string();fs::create_directories(root);
 put(base/"guard"/"keep","outside");put(fs::path(root)/"keep","inside");
 for(const auto& name:std::vector<std::string>{"",".","..","../keep","a/b","a\\b",std::string("a\0b",3),std::string(removalPrefix)+"7"}) {
  DownloadManager dm;errno=0;
  assert(!dm.retirePS5Download(name)&&errno==EINVAL&&pending.empty());
  assert(read(base/"guard"/"keep")=="outside"&&read(fs::path(root)/"keep")=="inside");
 }
 for(const char* test:{"complete","queued","missing","file-at-item-path","active","schedule"}) {
  mode=test;root=(base/test).string();fs::create_directory(root);DownloadManager dm;notices=errors=0;
  if(mode=="file-at-item-path")put(fs::path(root)/"id","item-file");
  else if(mode!="missing"){fs::create_directory(fs::path(root)/"id");put(fs::path(root)/"id"/"keep","old");}
  if(mode=="queued")dm.items[0].status=DownloadStatus::Queued;
  if(mode=="active"){dm.items[0].status=DownloadStatus::Downloading;dm.currentCancel=std::make_shared<std::atomic_bool>(false);}
  dm.removeDownload("id");
  if(mode=="active"){assert(dm.currentCancel->load()&&dm.items[0].errorMessage=="removed"&&pending.empty());assert(read(fs::path(root)/"id"/"keep")=="old");continue;}
  // The filesystem fallback removes a non-directory at this item path synchronously.
  if(mode=="file-at-item-path"){assert(dm.items.empty()&&notices==0&&pending.empty()&&!fs::exists(fs::path(root)/"id"));continue;}
  assert(dm.items.empty()&&!fs::exists(fs::path(root)/"id"));
  if(mode=="schedule")assert(pending.empty()&&errors==1);
  // Re-add after retirement but before old deletion runs.
  fs::create_directory(fs::path(root)/"id");put(fs::path(root)/"id"/"keep","new");dm.items.push_back(Item{});
  for(auto& fn:ui)fn();ui.clear();for(auto& fn:pending){worker=true;fn();worker=false;}pending.clear();
  assert(read(fs::path(root)/"id"/"keep")=="new");
  size_t retired=0;for(const auto& entry:fs::directory_iterator(root))if(isRetiredDirectory(entry.path().filename().string()))++retired;
  assert(retired==(mode=="schedule"?1:0));
 }
}
int main(int argc,char**argv){assert(argc==3);if(std::string(argv[1])=="unit")unit(argv[2]);else manager(argv[2]);}
'''
class RemovalTests(unittest.TestCase):
 def test_retirement_and_actual_remove(self):
  compiler=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  source=native_source(ROOT/'app/src/utils/download.cpp')
  methods='\n'.join(extract_function(source,n) for n in ('bool DownloadManager::retirePS5Download(', 'void DownloadManager::removeDownload('))
  with tempfile.TemporaryDirectory(prefix='ps5-retirement-') as tmp:
   work=Path(tmp);cpp=work/'test.cpp';exe=work/'test';cpp.write_text(HARNESS.replace('@METHODS@',methods))
   subprocess.run([compiler,'-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-D__PS5__','-DPS5_NATIVE_GPU','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'app/include'),str(cpp),'-o',str(exe)],check=True)
   env=dict(os.environ,ASAN_OPTIONS='detect_leaks='+('0' if sys.platform=='darwin' else '1'),UBSAN_OPTIONS='halt_on_error=1')
   for mode in ('unit','manager'):
    data=work/mode;data.mkdir();subprocess.run([str(exe),mode,str(data)],check=True,env=env)
if __name__=='__main__':unittest.main()
