#include <borealis.hpp>
#include "tab/offline_collection.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "utils/offline_library.hpp"

OfflineCollection::OfflineCollection(const std::string& sectionKey) {
    this->setGrow(1.f);
    this->registerCell("Cell", VideoCardCell::create);
    // same 2:3 poster grid as the online library (media_collection.cpp)
    this->spanCount = brls::getStyle().getMetric("app/grid/6");
    this->itemImageRatio = 1.5f;
    this->itemExtraHeight = 55;
    float side = brls::getStyle()["main/content_padding_sides"];
    this->setPadding(70, side, brls::getStyle()["main/content_padding_top_bottom"], side);

    auto& lib = OfflineLibrary::instance();
    std::vector<plex::Item> items;
    if (sectionKey.empty()) {
        for (auto& s : lib.sections()) {
            auto part = lib.sectionItems(s.key);
            items.insert(items.end(), part.begin(), part.end());
        }
    } else {
        items = lib.sectionItems(sectionKey);
    }

    if (items.empty()) {
        this->setEmpty(brls::getStr("main/download/offline_title"), brls::getStr("main/download/offline_sub"),
            "icon/ico-cloud.svg");
        return;
    }
    auto* ds = new VideoDataSource(items);
    ds->setLocalContext(true);
    this->setDataSource(ds);
}
