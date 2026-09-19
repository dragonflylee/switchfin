#!/usr/bin/env python3
"""Actual scheduler and flights: ownership, partial delivery, cancellation and faults."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
CPP=r'''#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>
#include "utils/ps5_native_artwork_scheduler.hpp"

static thread_local int allocationFailure = -1;
void* operator new(std::size_t n) {
    if (allocationFailure >= 0 && allocationFailure-- == 0) throw std::bad_alloc();
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
static const auto ui = std::this_thread::get_id();
static std::atomic<int> live{0}, peak{0};
struct Pixel {
    int value;
    explicit Pixel(int value) : value(value) {
        int now=++live, old=peak;
        while(now>old && !peak.compare_exchange_weak(old,now)) {}
    }
    ~Pixel() { --live; }
};
struct Job {
    const int id;
    std::atomic_bool cancelled{false};
    std::unique_ptr<Pixel> pixels;
    int deliveries=0, retirements=0;
    explicit Job(int id) : id(id) {}
};
using Ref=std::shared_ptr<Job>;
using Queue=ps5::artwork::Scheduler<Ref>;
using Task=std::function<void(int&)>;
static auto cancelled=[](const Ref& job) noexcept { return job->cancelled.load(); };
static auto retire=[](const Ref& job) noexcept {
    assert(std::this_thread::get_id()==ui && job->retirements++==0);
    job->cancelled=true;job->pixels.reset();
};
static auto work=[](const Ref& job,int&) {
    assert(std::this_thread::get_id()!=ui);
    if(job->cancelled) return;
    job->pixels=std::make_unique<Pixel>(job->id);
};
static auto deliver=[](const Ref& job) {
    assert(std::this_thread::get_id()==ui && job->pixels && job->pixels->value==job->id);
    assert(job->deliveries++==0);
};
struct Fixture {
    Queue queue;
    std::vector<Ref> jobs;
    std::vector<Task> tasks;
    int mode=0;
    explicit Fixture(int count) {
        assert(live==0);peak=0;
        for(int i=0;i<count;++i) { jobs.push_back(std::make_shared<Job>(i));assert(queue.enqueue(jobs.back())); }
    }
    void submit(Task task) {
        if(mode==1)throw std::bad_alloc();
        if(mode==2){tasks.push_back(std::move(task));throw 17;}
        if(mode==3){std::thread t([&]{int resource=0;task(resource);});t.join();throw 19;}
        tasks.push_back(std::move(task));
    }
    void pump(bool complete=true) { queue.pump(complete,[this](Task task){submit(std::move(task));},work,cancelled,deliver,retire); }
    void run() {
        auto pending=std::move(tasks);tasks.clear();
        std::vector<std::thread> threads;
        for(auto& task:pending)threads.emplace_back([task=std::move(task)]{int resource=0;task(resource);});
        for(auto& t:threads)t.join();
    }
    void close() { for(auto& job:jobs)job->cancelled=true;queue.close(retire); }
    ~Fixture() { close();assert(tasks.empty()); }
};
static void window() {
    Fixture f(128);f.pump(false);assert(f.tasks.size()==4 && f.queue.snapshot().pending==124);
    std::vector<int> order;
    for(int frame=0;frame<128;++frame) {
        f.run();assert(live<=4);auto before=f.queue.snapshot();
        f.pump(false);assert(f.queue.snapshot().completed==before.completed);
        f.queue.pump(true,[&](Task task){f.submit(std::move(task));},work,cancelled,
            [&](const Ref& job){deliver(job);order.push_back(job->id);},retire);
        assert(f.queue.snapshot().completed==std::size_t(frame+1) && f.queue.snapshot().admitted<=4);
        assert(live<=3);
    }
    assert(f.tasks.empty() && !f.queue.snapshot().pending && !f.queue.snapshot().admitted && live==0 && peak==4);
    for(int i=0;i<128;++i)assert(order[i]==i && f.jobs[i]->retirements==1 && f.jobs[i]->deliveries==1);
}
static void cancelStates() {
    { Fixture f(8);f.pump(false);f.jobs[0]->cancelled=true;f.pump(false);
      assert(f.tasks.size()==5 && f.jobs[0]->retirements==1);f.run();assert(live==4 && f.jobs[0]->deliveries==0);
      f.close();assert(live==0); }
    { Fixture f(8);f.pump(false);f.run();assert(live==4);
      for(int i=0;i<4;++i)f.jobs[i]->cancelled=true;
      f.pump(false);assert(live==0 && f.tasks.size()==4);f.run();assert(live==4);f.close();assert(live==0); }
    { Fixture f(130);for(int i=0;i<128;++i)f.jobs[i]->cancelled=true;
      f.pump(false);assert(f.tasks.empty() && f.queue.snapshot().pending==66);
      f.pump(false);assert(f.tasks.empty() && f.queue.snapshot().pending==2);
      f.pump(false);assert(f.tasks.size()==2);f.run();f.pump();f.pump();assert(live==0); }
}
static void submissionFailures() {
    for(int mode=1;mode<=3;++mode) {
        Fixture f(1);f.mode=mode;f.pump(false);
        assert(f.queue.snapshot().submissionFailures==1);
        if(mode==3){assert(f.queue.snapshot().admitted==1 && live==1);f.pump();assert(f.jobs[0]->deliveries==1);}
        else {assert(!f.queue.snapshot().admitted && f.jobs[0]->retirements==1);f.run();assert(!f.jobs[0]->deliveries);}
        assert(live==0);
    }
}
static void workerAndDeliveryFailures() {
    for(int kind=0;kind<4;++kind) {
        Fixture f(1);
        auto failure=[kind](const Ref& job,int& resource){work(job,resource);if(kind==0)throw std::bad_alloc();if(kind==1)throw 7;};
        auto apply=[kind](const Ref& job){assert(job->pixels);if(kind==2)throw std::runtime_error("fixture");if(kind==3)throw 8;};
        auto pump=[&](bool allow){f.queue.pump(allow,[&](Task task){f.submit(std::move(task));},failure,cancelled,apply,retire);};
        pump(false);f.run();assert(live==1);pump(true);
        auto s=f.queue.snapshot();assert(s.completed==1 && s.workerFailures==std::size_t(kind<2) && s.deliveryFailures==std::size_t(kind>=2));
        assert(live==0 && f.jobs[0]->retirements==1);
    }
}
static void reentrancy() {
    { Fixture f(4);f.pump(false);f.run();
      f.queue.pump(true,[&](Task task){f.submit(std::move(task));},work,cancelled,[&](const Ref& job){
        deliver(job);f.jobs.push_back(std::make_shared<Job>(4));assert(f.queue.enqueue(f.jobs.back()));
        f.pump(true);assert(f.queue.snapshot().completed==0 && job->pixels);
      },retire);
      assert(f.queue.snapshot().completed==1 && f.tasks.size()==1);f.run();f.close();assert(live==0); }
    { Fixture f(8);f.pump(false);f.run();
      f.queue.pump(true,[&](Task task){f.submit(std::move(task));},work,cancelled,[&](const Ref& job){
        deliver(job);f.close();assert(job->pixels && job->retirements==0 && live==1);
        assert(!f.queue.enqueue(job));f.pump();
      },retire);
      assert(live==0 && f.queue.isClosing() && f.queue.snapshot().completed==1);
      for(const auto& job:f.jobs)assert(job->retirements==1); }
}
static void closeQueued() {
    Fixture f(8);f.pump(false);f.close();assert(f.queue.isClosing());f.run();assert(live==0);
    for(const auto& job:f.jobs)assert(job->retirements==1 && !job->deliveries);
}
static void runningAndOwnerLifetime(bool close) {
    assert(live==0);peak=0;
    auto queue=std::make_unique<Queue>();std::vector<Ref> jobs;std::vector<Task> tasks;
    for(int i=0;i<5;++i){jobs.push_back(std::make_shared<Job>(i));queue->enqueue(jobs.back());}
    std::mutex mutex;std::condition_variable cv;int entered=0;bool released=false;
    auto blocked=[&](const Ref& job,int& resource){
        work(job,resource);
        std::unique_lock<std::mutex> lock(mutex);++entered;cv.notify_all();cv.wait(lock,[&]{return released;});
    };
    auto pump=[&]{queue->pump(false,[&](Task task){tasks.push_back(std::move(task));},blocked,cancelled,deliver,retire);};
    pump();assert(tasks.size()==4);
    std::vector<std::thread> workers;
    for(auto& task:tasks)workers.emplace_back([task=std::move(task)]{int resource=0;task(resource);});
    tasks.clear();
    {std::unique_lock<std::mutex> lock(mutex);assert(cv.wait_for(lock,std::chrono::seconds(5),[&]{return entered==4;}));}
    assert(live==4);for(auto& job:jobs)job->cancelled=true;
    pump();assert(tasks.empty() && queue->snapshot().admitted==4 && live==4);
    if(close){queue->close(retire);queue.reset();jobs.clear();}
    {std::lock_guard<std::mutex> lock(mutex);released=true;}cv.notify_all();
    for(auto& t:workers)t.join();
    if(!close){pump();assert(queue->snapshot().admitted==0 && queue->snapshot().pending==0);queue->close(retire);}
    assert(live==0 && peak==4);
}
static void allocationRollback() {
    for(int failure=0;failure<2;++failure) {
        Queue queue;auto job=std::make_shared<Job>(0);bool threw=false;
        allocationFailure=failure;
        try{queue.enqueue(job);}catch(const std::bad_alloc&){threw=true;}
        allocationFailure=-1;
        assert(threw && queue.snapshot().pending==0 && queue.snapshot().admitted==0 && job.use_count()==1);
        queue.close(retire);
    }
}

static void partialDelivery() {
    {
        Fixture f(8);f.pump(false);f.run();std::vector<int> order;
        for(int pass=0;pass<16;++pass) {
            f.queue.pump(true,[&](Task task){f.submit(std::move(task));},work,cancelled,
                [&](const Ref& job) { assert(job->pixels);order.push_back(job->id);return ++job->deliveries==2; },retire);
            f.run();assert(f.queue.snapshot().admitted<=4 && live<=4);
        }
        assert(f.queue.snapshot().completed==8 && f.queue.snapshot().deferred==8);
        for(int i=0;i<16;++i) assert(order[i]==(i<8?i%4:4+(i-8)%4));
        assert(live==0 && f.queue.snapshot().completed==8);
    }
    for(int mode=0;mode<4;++mode) {
        Fixture f(1);f.pump(false);f.run();
        auto submit=[&](Task task){f.submit(std::move(task));};
        f.queue.pump(true,submit,work,cancelled,[](const Ref&){return false;},retire);
        assert(live==1 && f.queue.snapshot().ready==1 && f.queue.snapshot().deferred==1 && f.jobs[0]->retirements==0);
        if(mode==0) { f.jobs[0]->cancelled=true;f.pump(false); }
        else if(mode==1) f.close();
        else f.queue.pump(true,submit,work,cancelled,[&](const Ref& job)->bool {
            if(mode==2) { f.close();assert(job->pixels && live==1);return false; }
            throw 21;
        },retire);
        assert(live==0 && f.jobs[0]->retirements==1);
    }
}
#include "utils/ps5_native_artwork_flights.hpp"
struct FlightView {
    int pins=0,deliveries=0;
    void ptrLock() noexcept {assert(std::this_thread::get_id()==ui);++pins;}
    void ptrUnlock() noexcept {assert(std::this_thread::get_id()==ui && pins==1);--pins;}
    ~FlightView() {assert(pins==0);}
};
using Flights=ps5::artwork::Flights<FlightView,int>;
static void partialRegistry() {
    {
        Flights f;std::array<FlightView,65> views;bool start=false;Flights::Ref job;
        for(auto& view:views) {auto next=f.begin(&view,"shared",&start);if(!job)job=next;else assert(next==job);}
        auto zero=f.completeSome(job,0,[](FlightView*,int&){assert(false);return true;});
        assert(!zero.finished && f.current(job));int total=0,passes=0;
        while(total<65) {
            auto part=f.completeSome(job,7,[](FlightView* view,int&){assert(view->pins==1);++view->deliveries;return true;});
            total+=part.attempted;++passes;assert(part.attempted<=7 && part.failures==0 && part.finished==(total==65));
            assert(!f.current(job));
        }
        assert(passes==10);for(auto& view:views)assert(view.pins==0 && view.deliveries==1);f.close();
    }
    for(int mode=0;mode<4;++mode) {
        Flights f;FlightView first,second,newer;bool start=false;
        auto job=f.begin(&first,"shared",&start);f.begin(&second,"shared",&start);
        auto defer=f.completeSome(job,16,[](FlightView*,int&){return false;});
        assert(!defer.finished && defer.attempted==0 && first.pins==1 && second.pins==1 && !f.current(job));
        auto next=f.begin(&newer,"shared",&start);assert(start && next!=job && f.current(next));
        auto part=f.completeSome(job,1,[&](FlightView* view,int&)->bool {
            assert(view==&first);
            if(mode==0) throw std::bad_alloc();
            if(mode==1) throw 8;
            if(mode==2) { f.cancel(&second);f.begin(&first,"shared",&start);assert(!start); }
            if(mode==3) { f.close();return false; }
            return true;
        });
        assert(part.attempted==std::size_t(mode!=3) && part.failures==std::size_t(mode<2));
        if(mode<2) {assert(first.pins==0 && second.pins==1 && !part.finished);f.finish(job);}
        if(mode==2) {assert(first.pins==1 && second.pins==0 && part.finished);f.finish(job);assert(first.pins==1);}
        if(mode!=3) assert(f.current(next) && newer.pins==1);
        else assert(part.finished && newer.pins==0);
        f.close();
    }
    {
        Flights f;FlightView view;bool start=false;auto job=f.begin(&view,"shared",&start);
        f.completeSome(job,1,[](FlightView*,int&){return false;});
        allocationFailure=0;
        auto result=f.completeSome(job,1,[](FlightView* v,int&){++v->deliveries;return true;});
        allocationFailure=-1;assert(result.finished && result.attempted==1 && view.pins==0);f.close();
    }
}
int main() {
    window();cancelStates();submissionFailures();workerAndDeliveryFailures();reentrancy();closeQueued();
    runningAndOwnerLifetime(false);runningAndOwnerLifetime(true);allocationRollback();
    partialDelivery();partialRegistry();
    assert(live==0);puts("PASS:29 scheduler/registry ownership,partial-delivery,fault,budget scenarios,atomic completion and teardown");
}
'''
class NativeArtworkSchedulerTests(unittest.TestCase):
    def test_actual_scheduler_and_partial_registry(self):
        compiler=os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        sanitizer=os.environ.get("PS5_ARTWORK_SANITIZER","address,undefined")
        self.assertIn(sanitizer,("address,undefined","thread"))
        with tempfile.TemporaryDirectory(prefix="switchfin-artwork-scheduler-") as work:
            p=Path(work);cpp=p/"fixture.cpp";cpp.write_text(CPP);binary=p/"fixture"
            command=[compiler,"-std=c++17","-g","-O1","-UNDEBUG","-Wall","-Wextra","-Werror","-pthread","-fsanitize="+sanitizer,"-fno-omit-frame-pointer","-I"+str(ROOT/"app/include"),str(cpp),"-o",str(binary)]
            subprocess.run(command,check=True,timeout=120)
            env=dict(os.environ,ASAN_OPTIONS="detect_leaks="+("0" if sys.platform=="darwin" else "1")+":halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1",TSAN_OPTIONS="halt_on_error=1")
            subprocess.run([str(binary)],check=True,timeout=60,env=env)
if __name__=="__main__":unittest.main()
