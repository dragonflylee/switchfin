#pragma once

#include <view/recycling_grid.hpp>
#include <api/plex/types.hpp>

class VideoDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Item>;

    explicit VideoDataSource(const MediaList& r);
    explicit VideoDataSource(const MediaList& r, const std::string& parentId);

    size_t getItemCount() override;

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;

    void onItemSelected(brls::Box* recycler, size_t index) override;

    void onContextMenu(brls::Box* recycler, size_t index);

    void clearData() override;

    void appendData(const MediaList& data);

protected:
    MediaList list;
    std::string parentId;
};
