#pragma once

#include <view/recycling_grid.hpp>
#include <api/plex/types.hpp>

class PeopleDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<plex::Role>;

    explicit PeopleDataSource(const MediaList& r);

    size_t getItemCount() override;

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;

    void onItemSelected(brls::Box* recycler, size_t index) override;

    void clearData() override;

protected:
    MediaList list;
};
