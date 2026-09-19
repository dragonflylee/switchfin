#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include "utils/ps5_native_artwork_flights.hpp"
static thread_local int failAfter=-1;
void* operator new(std::size_t n) {
    if(failAfter==0) {failAfter=-1;throw std::bad_alloc();}
    if(failAfter>0)--failAfter;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void* operator new[](std::size_t n) {return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
#if defined(__cpp_sized_deallocation)
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
#endif
struct View {
    int pins=0,locks=0,unlocks=0;
    void ptrLock() {++pins;++locks;}
    void ptrUnlock() {assert(pins>0);--pins;++unlocks;}
    ~View() {assert(pins==0 && locks==unlocks);}
};
using Registry=ps5::artwork::Flights<View,std::shared_ptr<int>>;
int main() {
    const std::string key(200,'k');int rejectedFresh=0,rejectedFollower=0,succeeded=0;
    for(bool follower:{false,true}) for(int at=0;at<32;++at) {
        View first,second;Registry registry;Registry::Ref initial,added;bool start=false;
        if(follower) {initial=registry.begin(&first,key,&start);assert(initial && start && first.pins==1);}
        bool failed=false;failAfter=at;
        try {added=registry.begin(&second,key,&start);}
        catch(const std::bad_alloc&) {failed=true;}
        failAfter=-1;
        if(failed) {
            assert(!start && second.pins==0);
            (follower?rejectedFollower:rejectedFresh)++;
            added=registry.begin(&second,key,&start);assert(added && start!=follower);
        } else {assert(added && start!=follower);++succeeded;}
        if(follower) assert(initial==added && registry.current(initial) && first.pins==1);
        assert(second.pins==1);registry.close();registry.close();
        assert(first.pins==0 && second.pins==0 && added->cancelled->load());
        assert(!registry.begin(&second,key,&start) && !start);
        initial.reset();added.reset();
        assert(registry.snapshot().requests==0 && registry.snapshot().keyBytes==0 && registry.snapshot().consumers==0);
    }
    assert(rejectedFresh>0 && rejectedFollower>0 && succeeded>0);
    // Closing from delivery releases every subscriber once and prevents the
    // callback loop from dereferencing any remaining view after close.
    {
        View first,second;Registry registry;bool start=false;
        auto job=registry.begin(&first,key,&start);registry.begin(&second,key,&start);
        int calls=0;auto failures=registry.complete(job,[&](View*,auto&){++calls;registry.close();});
        assert(calls==1 && failures==0 && first.pins==0 && second.pins==0);
    }
    // Failure of one callback does not leave remaining consumers pinned or
    // prevent their delivery; no arbitrary exception strings are exposed.
    {
        View first,second;Registry registry;bool start=false;
        auto job=registry.begin(&first,key,&start);registry.begin(&second,key,&start);
        int calls=0;auto failures=registry.complete(job,[&](View*,auto&){if(++calls==1)throw 7;});
        assert(calls==2 && failures==1 && first.pins==0 && second.pins==0 && job->cancelled->load());
    }
    printf("PASS registry allocation positions=64 rejected_fresh=%d rejected_follower=%d successful=%d close_and_delivery_faults=2\n",rejectedFresh,rejectedFollower,succeeded);
}
