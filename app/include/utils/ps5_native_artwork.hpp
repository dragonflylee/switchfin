#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace ps5::artwork {

// The registry is confined to the UI thread. A worker holds its own Request,
// writes payload before enqueueing completion, and never touches the view.
// Queued completions retain only weak references: cancelling a finished worker
// also frees its decoded pixels even if the UI queue will never run again.
template <class View, class Payload>
class Requests {
public:
    struct Request {
        View* const view;
        const std::string url;
        const std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        Payload payload;

        Request(View* view, std::string url) : view(view), url(std::move(url)) {}
    };
    using Ref = std::shared_ptr<Request>;

    Ref begin(View* view, const std::string& url) {
        cancel(view);
        if (closing) return {};
        auto request = std::make_shared<Request>(view, url);
        active.emplace(view, request);
        view->ptrLock();
        return request;
    }

    bool current(const Ref& request) const {
        if (!request || closing || request->cancelled->load()) return false;
        const auto found = active.find(request->view);
        return found != active.end() && found->second == request;
    }

    void finish(const Ref& request) {
        if (!request) return;
        const auto found = active.find(request->view);
        if (found == active.end() || found->second != request) return;
        release(found);
    }

    void cancel(View* view) {
        const auto found = active.find(view);
        if (found != active.end()) release(found);
    }

    void close() {
        if (closing) return;
        closing = true;
        while (!active.empty()) release(active.begin());
    }

    bool isClosing() const { return closing; }

private:
    using Map = std::unordered_map<View*, Ref>;
    Map active;
    bool closing = false;

    void release(typename Map::iterator found) {
        auto request = std::move(found->second);
        active.erase(found);
        request->cancelled->store(true);
        request->view->ptrUnlock();
    }
};

} // namespace ps5::artwork
