"""Compile the production report queue with queued/failing transport dispatch."""
import unittest

from source_fixture import SERVICES
from source_fixture import compile_run


def services():
    result = dict(SERVICES)
    result["borealis/core/thread.hpp"] = result["borealis/core/thread.hpp"].replace(
        "namespace brls {", "namespace brls {\ninline int asyncFailures=0, syncFailures=0, timerFailures=0;\ninline bool inlineAsync=false, inlineSync=false;"
    ).replace(
        "timers.push_back(f);", 'if(timerFailures) { --timerFailures; throw std::runtime_error("timer submission"); } timers.push_back(f);'
    ).replace(
        "workers.push_back(std::move(f));", 'if(asyncFailures) { --asyncFailures; throw std::runtime_error("worker submission"); } if(inlineAsync) f(); else workers.push_back(std::move(f));'
    ).replace(
        "ui.push_back(std::move(f));", 'if(syncFailures) { --syncFailures; throw std::runtime_error("UI submission"); } if(inlineSync) f(); else ui.push_back(std::move(f));'
    )
    result["api/http.hpp"] = result["api/http.hpp"].replace(
        "inline static int calls = 0;", "struct Sent { std::string url, body; Header headers; };\n    inline static std::vector<Sent> posts;\n    inline static std::function<void()> duringPost;\n    inline static int calls = 0;"
    ).replace("sentBody = body; return get", "posts.push_back({url,body,h}); if(duringPost) duringPost(); sentBody = body; return get")
    return result


PRELUDE = r'''
#include <api/jellyfin_report_queue.hpp>
#include <cassert>
#include <set>
using Queue = jellyfin::ReportQueue;
Queue::Clock::time_point timeNow{};
auto queue() { return std::make_shared<Queue>([] { return timeNow; }); }
auto context(int account=0) {
    auto result=jellyfin::RequestContext::capture();
    result.server="https://synthetic-"+std::to_string(account)+".invalid";
    result.user="user-"+std::to_string(account);
    result.headers={"Authorization: synthetic-"+std::to_string(account)};
    return result;
}
auto data(int session,int ticks=0,size_t padding=0) {
    return nlohmann::json{{"ItemId","item-"+std::to_string(session)},
        {"PlaySessionId","session-"+std::to_string(session)}, {"PositionTicks",ticks},
        {"Padding",std::string(padding,'x')}};
}
void post(const std::shared_ptr<Queue>& q,std::string_view kind,int session,int ticks=0,size_t padding=0,int account=0) {
    q->post(context(account),std::string(kind),data(session,ticks,padding));
}
void step() { if(!brls::workers.empty()) brls::run(brls::workers); if(!brls::ui.empty()) brls::run(brls::ui); }
void drain() { while(!brls::workers.empty()||!brls::ui.empty()) step(); }
void bounded(const std::shared_ptr<Queue>& q) {
    const auto s=q->stats();
    assert(s.sessions<=Queue::MAX_SESSIONS && s.highSessions<=Queue::MAX_SESSIONS);
    assert(s.reports<=Queue::MAX_SESSIONS*3 && s.highReports<=Queue::MAX_SESSIONS*3);
    assert(s.bytes<=Queue::MAX_RETAINED_BYTES && s.highBytes<=Queue::MAX_RETAINED_BYTES);
    assert(brls::workers.size()<=1);
}
'''


SUBMISSION_SCENARIO = r'''
#include <api/jellyfin_report_queue.hpp>
#include <cassert>
int main() {
    auto q=std::make_shared<jellyfin::ReportQueue>();
    auto c=jellyfin::RequestContext::capture();
    brls::asyncFailures=1;
    try { q->post(c,std::string(jellyfin::apiPlayStart),{{"ItemId","first"},{"PlaySessionId","first"}}); }
    catch(const std::exception&) {}
    q->post(c,std::string(jellyfin::apiPlayStart),{{"ItemId","second"},{"PlaySessionId","second"}});
    assert(brls::workers.size()==1); // the failed submission must not leave busy set
    brls::run(brls::workers);brls::run(brls::ui);
    assert(nlohmann::json::parse(HTTP::posts.back().body)["ItemId"]=="second");
    brls::timers.clear();
}
'''


class ReportQueueBounds(unittest.TestCase):
    def test_worker_submission_exception_unblocks_later_session(self):
        compile_run(SUBMISSION_SCENARIO, services())

    def test_outage_capacity_serialized_bytes_and_account_order(self):
        compile_run(PRELUDE + r'''
int main() {
    auto q=queue();
    // A stalled first attempt must not permit parallel transport under pressure.
    for(int i=0;i<100;++i) {
        post(q,jellyfin::apiPlayStart,i,0,7000,i);
        post(q,jellyfin::apiPlaying,i,1,7000,i);
        post(q,jellyfin::apiPlaying,i,2,7000,i);
        post(q,jellyfin::apiPlayStop,i,3,7000,i);
        bounded(q);
    }
    assert(HTTP::posts.empty() && brls::workers.size()==1);
    assert(q->stats().evictedSessions>0 && q->stats().dropped>0);
    const auto retained=q->stats();
    // Session 1's Start was evicted. Neither a later progress nor Stop revives it.
    post(q,jellyfin::apiPlaying,1,4,0,1);post(q,jellyfin::apiPlayStop,1,5,0,1);
    assert(q->stats().reports==retained.reports && q->stats().bytes==retained.bytes);
    HTTP::failure="ambiguous response loss";drain();HTTP::failure.clear();
    std::set<std::string> starts;
    for(const auto& sent:HTTP::posts) {
        auto body=nlohmann::json::parse(sent.body);
        auto id=body["PlaySessionId"].get<std::string>();
        const auto account=id.substr(std::string("session-").size());
        assert(sent.url.find("https://synthetic-"+account+".invalid")==0);
        assert(sent.headers.front()=="Authorization: synthetic-"+account);
        if(sent.url.find(std::string(jellyfin::apiPlayStart))==sent.url.size()-std::string(jellyfin::apiPlayStart).size())
            assert(starts.insert(id).second);
        else assert(starts.count(id)==1); // attempted Start precedes every retained sample/Stop
    }
    assert(q->stats().sessions==0 && q->stats().reports==0 && q->stats().bytes==0);
    assert(q->stats().responseFailures==HTTP::posts.size()); // no automatic retries
    bounded(q);
    // Tiny reports reach the independent session cap before the byte ceiling.
    for(int i=200;i<300;++i) {
        post(q,jellyfin::apiPlayStart,i);post(q,jellyfin::apiPlayStop,i);
        bounded(q);
    }
    assert(q->stats().sessions==Queue::MAX_SESSIONS);
    assert(q->stats().highSessions==Queue::MAX_SESSIONS);
    drain();assert(q->stats().sessions==0);
    // Caller-supplied metadata/header shape cannot bypass string/envelope limits.
    const auto calls=HTTP::posts.size();
    post(q,jellyfin::apiPlayStart,101,0,Queue::MAX_REPORT_BYTES);
    auto c=context();c.headers.assign(Queue::MAX_HEADERS+1,"");q->post(c,std::string(jellyfin::apiPlayStart),data(102));
    c=context();c.headers={std::string(Queue::MAX_IDENTITY_BYTES,'x')};q->post(c,std::string(jellyfin::apiPlayStart),data(103));
    assert(q->stats().reports==0 && HTTP::posts.size()==calls);
    brls::timers.clear();
}
''', services())

    def test_unsent_expiry_and_healthy_long_lived_session(self):
        compile_run(PRELUDE + r'''
int main() {
    auto q=queue();
    post(q,jellyfin::apiPlayStart,0);step();
    assert(q->stats().sessions==1 && q->stats().reports==0);
    // A healthy session and even a long pause outlive the unsent-report deadline.
    timeNow+=std::chrono::hours(3);brls::run(brls::timers);
    post(q,jellyfin::apiPlaying,0,10);step();
    assert(HTTP::posts.size()==2 && q->stats().expired==0);
    // Stall one progress, expire only the next unsent sample, retain the live marker.
    post(q,jellyfin::apiPlaying,0,11);post(q,jellyfin::apiPlaying,0,12);
    timeNow+=Queue::MAX_UNSENT_AGE;brls::run(brls::timers);
    assert(q->stats().reports==1 && q->stats().sessions==1 && q->stats().expired==1);
    assert(brls::workers.size()==1);step();
    post(q,jellyfin::apiPlayStop,0,13);drain();
    assert(nlohmann::json::parse(HTTP::posts.back().body)["PositionTicks"]==13);
    assert(q->stats().sessions==0);
    // Queued Start expiry drops its whole session; it cannot leave an orphan Stop.
    post(q,jellyfin::apiPlayStart,1);post(q,jellyfin::apiPlayStop,1);
    post(q,jellyfin::apiPlayStart,2);post(q,jellyfin::apiPlayStop,2);
    const auto before=HTTP::posts.size();timeNow+=Queue::MAX_UNSENT_AGE;brls::run(brls::timers);
    assert(q->stats().reports==1 && q->stats().sessions==1); // only the accepted worker remains
    post(q,jellyfin::apiPlaying,2);post(q,jellyfin::apiPlayStop,2);drain();
    assert(HTTP::posts.size()==before && q->stats().sessions==0);
    // A replay may reuse server session/item IDs while its previous Stop is queued.
    post(q,jellyfin::apiPlayStart,6);post(q,jellyfin::apiPlayStop,6,1);
    post(q,jellyfin::apiPlayStart,6,2);post(q,jellyfin::apiPlaying,6,3);post(q,jellyfin::apiPlayStop,6,4);
    const auto replayStart=HTTP::posts.size();drain();
    assert(HTTP::posts.size()==replayStart+5);
    for(int i=0;i<5;++i) assert(nlohmann::json::parse(HTTP::posts[replayStart+i].body)["PositionTicks"]==i);
    // Stop cleanup keeps its original credentials after cancellation/account switch.
    auto old=context(7);q->post(old,std::string(jellyfin::apiPlayStart),data(7));
    auto cleanup=old;cleanup.cancel.reset();cleanup.ownerCancel.reset();
    q->post(cleanup,std::string(jellyfin::apiPlayStop),data(7,99));old.cancel->store(true);
    drain();assert(HTTP::posts.back().headers.front()=="Authorization: synthetic-7");
    assert(HTTP::posts.back().url==old.server+std::string(jellyfin::apiPlayStop));
    bounded(q);brls::timers.clear();
}
''', services())

    def test_submission_ui_dispatch_duplicates_and_synchronous_completion(self):
        compile_run(PRELUDE + r'''
int main() {
    auto q=queue();brls::asyncFailures=1;
    post(q,jellyfin::apiPlayStart,0);assert(q->stats().submissionFailures==1);
    assert(q->stats().sessions==0 && q->stats().reports==0 && brls::workers.empty());
    post(q,jellyfin::apiPlayStop,0);assert(brls::workers.empty());
    // A failed submission in the middle of the drain must not strand later Stop.
    post(q,jellyfin::apiPlayStart,1);post(q,jellyfin::apiPlaying,1);post(q,jellyfin::apiPlayStop,1);
    brls::asyncFailures=1;step();assert(brls::workers.size()==1);drain();
    assert(q->stats().submissionFailures==2 && q->stats().reports==0);
    // Failed UI enqueue is recovered locally; it never reissues that POST.
    post(q,jellyfin::apiPlayStart,2);post(q,jellyfin::apiPlayStop,2);
    brls::syncFailures=1;brls::run(brls::workers);assert(brls::ui.empty());
    const auto calls=HTTP::posts.size();brls::run(brls::timers);
    assert(q->stats().completionDispatchFailures==1 && brls::workers.size()==1);
    drain();assert(HTTP::posts.size()==calls+1 && q->stats().sessions==0);
    // A delivered completion duplicated after the next attempt starts is harmless.
    post(q,jellyfin::apiPlayStart,3);post(q,jellyfin::apiPlayStop,3);
    brls::run(brls::workers);auto duplicate=brls::ui.front();brls::run(brls::ui);
    duplicate();duplicate();assert(brls::workers.size()==1);drain();
    // Timer polling may beat an already queued UI completion, also without overlap.
    post(q,jellyfin::apiPlayStart,4);post(q,jellyfin::apiPlayStop,4);
    brls::run(brls::workers);brls::run(brls::timers);
    assert(brls::workers.size()==1);brls::run(brls::ui);assert(brls::workers.size()==1);drain();
    // Synchronous adapters exercise iterative drain, not recursive completion chains.
    post(q,jellyfin::apiPlayStart,5);post(q,jellyfin::apiPlaying,5);post(q,jellyfin::apiPlayStop,5);
    brls::inlineAsync=true;brls::inlineSync=true;step();
    assert(q->stats().sessions==0 && q->stats().reports==0 && brls::workers.empty() && brls::ui.empty());
    bounded(q);brls::timers.clear();
}
''', services())

    def test_worker_wait_expiry_and_running_transport(self):
        compile_run(PRELUDE + r'''
int main() {
    auto q=queue();
    // Reviewer reproduction: async acceptance is not the start of HTTP.
    post(q,jellyfin::apiPlayStart,0);timeNow+=std::chrono::seconds(121);
    brls::run(brls::timers);assert(HTTP::posts.empty() && q->stats().reports==1);
    step();assert(HTTP::posts.empty() && q->stats().expired==1 && q->stats().sessions==0);
    post(q,jellyfin::apiPlaying,0);post(q,jellyfin::apiPlayStop,0);
    assert(brls::workers.empty());
    // Expiring a never-sent Start invalidates even newer, unexpired samples/Stop.
    post(q,jellyfin::apiPlayStart,1);timeNow+=std::chrono::seconds(119);
    post(q,jellyfin::apiPlaying,1);post(q,jellyfin::apiPlayStop,1);
    timeNow+=std::chrono::seconds(2);brls::run(brls::workers);
    assert(HTTP::posts.empty());auto duplicate=brls::ui.front();brls::run(brls::ui);
    assert(q->stats().sessions==0 && q->stats().reports==0 && q->stats().expired==4);
    post(q,jellyfin::apiPlayStart,2);duplicate();step();
    assert(HTTP::posts.size()==1 && q->stats().sessions==1);
    // Expired worker-queued Progress preserves the established Start's marker.
    post(q,jellyfin::apiPlaying,2);timeNow+=std::chrono::seconds(121);
    post(q,jellyfin::apiPlayStop,2,20);drain();
    assert(HTTP::posts.size()==2 && q->stats().sessions==0);
    assert(nlohmann::json::parse(HTTP::posts.back().body)["PositionTicks"]==20);
    // Expired worker-queued Stop is dropped and still retires its session.
    post(q,jellyfin::apiPlayStart,3);step();post(q,jellyfin::apiPlayStop,3);
    timeNow+=std::chrono::seconds(121);const auto before=HTTP::posts.size();step();
    assert(HTTP::posts.size()==before && q->stats().sessions==0);
    // An expiry result also survives failed UI submission through the mailbox.
    post(q,jellyfin::apiPlayStart,4);timeNow+=std::chrono::seconds(119);
    post(q,jellyfin::apiPlaying,4);post(q,jellyfin::apiPlayStop,4);
    timeNow+=std::chrono::seconds(2);brls::syncFailures=1;brls::run(brls::workers);
    assert(HTTP::posts.size()==before && brls::ui.empty());brls::run(brls::timers);
    assert(q->stats().sessions==0 && q->stats().reports==0 && q->stats().completionDispatchFailures==1);
    // Once HTTP has actually begun, age never abandons or repeats the transport.
    post(q,jellyfin::apiPlayStart,5);post(q,jellyfin::apiPlayStop,5);
    HTTP::duringPost=[&] {
        timeNow+=std::chrono::seconds(121);brls::run(brls::timers);
        assert(q->stats().reports==1 && brls::workers.empty());
        post(q,jellyfin::apiPlayStart,6);assert(brls::workers.empty());
    };
    brls::run(brls::workers);HTTP::duringPost={};
    assert(HTTP::posts.size()==before+1);brls::run(brls::ui);assert(brls::workers.size()==1);
    drain();post(q,jellyfin::apiPlayStop,6);drain();
    assert(HTTP::posts.size()==before+3 && q->stats().sessions==0);
    assert(q->stats().responseFailures==0); // expiry is not a server/HTTP failure
    bounded(q);brls::timers.clear();
}
''', services())

    def test_timer_failure_and_queue_lifetime(self):
        compile_run(PRELUDE + r'''
int main() {
    auto q=queue();brls::timerFailures=2;
    post(q,jellyfin::apiPlayStart,0);post(q,jellyfin::apiPlayStop,0);
    assert(q->stats().timerFailures==2 && q->stats().reports==1 && q->stats().expired==1);
    assert(brls::workers.size()==1);drain();assert(q->stats().sessions==0);
    // Weak expiry timers do not retain an otherwise idle queue/context.
    post(q,jellyfin::apiPlayStart,1);post(q,jellyfin::apiPlayStop,1);
    std::weak_ptr<Queue> weak=q;q.reset();assert(!weak.expired());drain();
    assert(weak.expired());while(!brls::timers.empty()) brls::run(brls::timers);
}
''', services())


if __name__ == "__main__":
    unittest.main()
