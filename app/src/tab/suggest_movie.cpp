#include "tab/suggest_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "api/jellyfin.hpp"

using namespace brls::literals;  // for _i18n

class RecommendCell : public RecyclingGridItem {
public:
    RecommendCell() {
        this->inflateFromXMLRes("xml/view/recycler_list.xml");
        this->setFocusable(false);
        this->recycler->registerCell("Cell", VideoCardCell::create);
    }

    void setCell(const jellyfin::Recommend& it) {
        if (it.RecommendationType == "SimilarToLikedItem") {
            this->setTitle(fmt::format(fmt::runtime("main/recommend/like"_i18n), it.BaselineItemName));
        } else if (it.RecommendationType == "HasDirectorFromRecentlyPlayed") {
            this->setTitle(fmt::format(fmt::runtime("main/recommend/directed"_i18n), it.BaselineItemName));
        } else if (it.RecommendationType == "HasActorFromRecentlyPlayed") {
            this->setTitle(fmt::format(fmt::runtime("main/recommend/starring"_i18n), it.BaselineItemName));
        } else {
            this->setTitle(fmt::format(fmt::runtime("main/recommend/watched"_i18n), it.BaselineItemName));
        }
        this->setDataSource(it.Items);
    }

    void setTitle(const std::string& text) { this->title->setTitle(text); }

    void setDataSource(const std::vector<jellyfin::Episode>& r) {
        this->recycler->setDataSource(new VideoDataSource(r));
    }

private:
    BRLS_BIND(brls::Header, title, "recycler/title");
    BRLS_BIND(HRecyclerFrame, recycler, "recycler/videos");
};

class RecommendDataSource : public RecyclingGridDataSource {
public:
    explicit RecommendDataSource(const jellyfin::Recommends& r) : list(std::move(r)) {}

    size_t getItemCount() override { return this->list.size() + 1; }

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override {
        if (index == 0) return recycler->dequeueReusableCell("Latest");
        auto cell = dynamic_cast<RecommendCell*>(recycler->dequeueReusableCell("Cell"));
        cell->setCell(this->list.at(index - 1));
        return cell;
    }

    void clearData() override { this->list.clear(); }

private:
    jellyfin::Recommends list;
};

SuggestMovie::SuggestMovie(const std::string id) : itemId(id) {
    this->latest = new RecommendCell();
    this->latest->setTitle("main/home/latest"_i18n);

    this->recycler = new RecyclingGrid();
    this->recycler->spanCount = 1;
    this->recycler->setGrow(1.0);
    this->addView(recycler);

    this->recycler->registerCell("Latest", [this]() { return this->latest; });
    this->recycler->registerCell("Cell", []() { return new RecommendCell(); });
}

void SuggestMovie::onCreate() {
    this->doLatest();
    this->doRecommend();
}

void SuggestMovie::doRecommend() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"categoryLimit", "6"},
        {"ItemLimit", "6"},
        {"fields", "PrimaryImageAspectRatio,BasicSyncInfo"},
        {"enableImageTypes", "Primary,Backdrop,Thumb"},
    });

    this->recycler->estimatedRowSpace = 20;
    this->recycler->estimatedRowHeight = 300;
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Recommends>(
        [ASYNC_TOKEN](const jellyfin::Recommends& r) {
            ASYNC_RELEASE
            this->recycler->estimatedRowSpace = 0;
            this->recycler->estimatedRowHeight = 325;
            this->recycler->setDataSource(new RecommendDataSource(r));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->recycler->setError(ex);
        },
        jellyfin::apiMovieRecommend, query);
}

void SuggestMovie::doLatest() {
    std::string query = HTTP::encode_form({
        {"enableImageTypes", "Primary"},
        {"parentId", this->itemId},
        {"includeItemTypes", jellyfin::mediaTypeMovie},
        {"fields", "BasicSyncInfo,Chapters"},
        {"limit", "20"},
    });
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Episode>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            this->latest->setDataSource(r);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Application::notify(ex);
        },
        jellyfin::apiUserLatest, AppConfig::instance().getUserId(), query);
}