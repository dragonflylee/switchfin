#include "view/search_list.hpp"
#include "view/h_recycling.hpp"
#include "view/video_source.hpp"
#include "view/video_card.hpp"
#include "api/jellyfin.hpp"

SearchList::SearchList() {
    brls::Logger::debug("View SearchList: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/view/recycler_list.xml");

    this->registerStringXMLAttribute("title", [this](std::string value) { this->title->setTitle(value); });

    this->registerStringXMLAttribute("itemType", [this](std::string value) { this->itemType = value; });

    this->registerFloatXMLAttribute("pageSize", [this](float value) { this->pageSize = value; });

    this->recycler->registerCell("Cell", VideoCardCell::create);
}

#ifdef PS5_NATIVE_GPU
SearchList::~SearchList() {
    if (requestCancelled) requestCancelled->store(true);
    brls::Logger::debug("View SearchList: delete");
}
#else
SearchList::~SearchList() { brls::Logger::debug("View SearchList: delete"); }
#endif

void SearchList::doRequest(const std::string& searchTerm) {
#ifdef PS5_NATIVE_GPU
    if (requestCancelled) requestCancelled->store(true);
    const auto generation = ++requestGeneration;
    requestCancelled = std::make_shared<std::atomic_bool>(false);
    auto context = jellyfin::RequestContext::capture();
    context.ownerCancel = requestCancelled;
#endif
    std::string query = HTTP::encode_form({
        {"searchTerm", searchTerm},
        {"includeItemTypes", this->itemType},
        {"recursive", "true"},
        {"includeMedia", "true"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"limit", std::to_string(this->pageSize)},
    });
#ifdef PS5_NATIVE_GPU
    const auto path = fmt::format(fmt::runtime(jellyfin::apiUserLibrary), context.user, query);
#endif

    ASYNC_RETAIN
#ifdef PS5_NATIVE_GPU
    jellyfin::request<jellyfin::Result<jellyfin::Episode>>(context,
        [path](const jellyfin::RequestContext& request) {
            return jellyfin::fetchJSON<jellyfin::Result<jellyfin::Episode>>(request, path);
        },
        [ASYNC_TOKEN, generation, context](const jellyfin::Result<jellyfin::Episode>& r) {
#else
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration || context.cancelled()) return;
            this->title->setSubtitle(std::to_string(r.TotalRecordCount));
#endif
            if (r.Items.empty()) {
                this->setVisibility(brls::Visibility::GONE);
                this->recycler->clearData();
            } else {
#ifdef PS5_NATIVE_GPU
                this->setVisibility(brls::Visibility::VISIBLE);
#else
                this->title->setSubtitle(std::to_string(r.TotalRecordCount));
#endif
                this->recycler->setDataSource(new VideoDataSource(r.Items));
            }
        },
#ifdef PS5_NATIVE_GPU
        [ASYNC_TOKEN, generation, context](const std::string& ex) {
#else
        [ASYNC_TOKEN](const std::string& ex) {
#endif
            ASYNC_RELEASE
#ifdef PS5_NATIVE_GPU
            if (generation != requestGeneration || context.cancelled()) return;
            this->setVisibility(brls::Visibility::VISIBLE);
#endif
            this->title->setSubtitle(ex);
            brls::Application::notify(ex);
#ifdef PS5_NATIVE_GPU
        });
#else
        },
        jellyfin::apiUserLibrary, AppConfig::instance().getUserId(), query);
#endif
}

#ifdef PS5_NATIVE_GPU
brls::View* SearchList::create() { return new SearchList(); }
#else
brls::View* SearchList::create() { return new SearchList(); }
#endif
