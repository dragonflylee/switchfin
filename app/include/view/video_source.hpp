#pragma once

#include <view/recycling_grid.hpp>
#include <api/jellyfin/media.hpp>

class VideoCardCell;

class VideoDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::Episode>;

    explicit VideoDataSource(const MediaList& r);
    explicit VideoDataSource(const MediaList& r, const std::string& parentId);

    size_t getItemCount() override;

#ifdef PS5_NATIVE_GPU
    std::string getItemKey(size_t index) override;

#endif
    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;
#ifdef PS5_NATIVE_GPU

    void retryArtwork(RecyclingGridItem* cell, size_t index) override;
#endif

    void onItemSelected(brls::Box* recycler, size_t index) override;

    void onContextMenu(VideoCardCell* cell, size_t index);

    void clearData() override;

    void appendData(const MediaList& data);

protected:
#ifdef PS5_NATIVE_GPU
    static void loadArtwork(VideoCardCell* cell, const jellyfin::Episode& item);
#endif
    MediaList list;
    std::string parentId;
};

class ProgramDataSource : public RecyclingGridDataSource {
public:
    using MediaList = std::vector<jellyfin::ProgramInfo>;

    explicit ProgramDataSource(const MediaList& r);

    size_t getItemCount() override;

    RecyclingGridItem* cellForRow(RecyclingView* recycler, size_t index) override;

    void onItemSelected(brls::Box* recycler, size_t index) override;

    void clearData() override;

    void appendData(const MediaList& data);

private:
    MediaList list;
};
