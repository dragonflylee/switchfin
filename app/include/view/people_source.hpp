#pragma once

#include <view/recycling_grid.hpp>
#include <api/jellyfin/media.hpp>

class PeopleDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::MediaPeople>;

    explicit PeopleDataSource(const MediaList& r);

    size_t getItemCount() override;

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;

    void onItemSelected(brls::Box* recycler, size_t index) override;

    void clearData() override;

protected:
    MediaList list;
};
