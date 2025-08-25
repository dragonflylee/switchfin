//
// Copyright 2023 dragonflylee
//

#pragma once

#include <borealis.hpp>
#include <borealis/core/singleton.hpp>
#include <api/jellyfin/media.hpp>
#include <client/client.hpp>
#include <utils/event.hpp>

class VideoProgressSlider;
class SVGImage;

class MusicView : public brls::Box, public brls::Singleton<MusicView> {
    enum RepeatMode { RepeatNone, RepeatOne, RepeatAll };

public:
    MusicView();
    ~MusicView() override;

    bool isTranslucent() override { return true; }

    void registerViewAction(brls::View* view);

    const std::string& currentId();

    void image(brls::Image *image);

    void load(const std::vector<jellyfin::Track>& items, size_t index);

    void load(const std::vector<remote::DirEntry>& items, size_t index, const std::string& extra);

public:
    struct Track {
        std::string Id;
        std::string Title;
        std::string Album;
        std::string ImageId;
        std::string ImageTag;

        Track(jellyfin::Track* item) : Id(item->Id), Title(item->Name) {
            this->Album = item->Album;
            this->ImageId = item->AlbumId;
            this->ImageTag = item->AlbumPrimaryImageTag;
        }
    };

private:
    BRLS_BIND(brls::Box, btnPrev, "music/prev");
    BRLS_BIND(brls::Box, btnNext, "music/next");
    BRLS_BIND(brls::Box, btnToggle, "music/toggle");
    BRLS_BIND(brls::Box, btnSuffle, "music/shuffle");
    BRLS_BIND(brls::Box, btnRepeat, "music/repeat");
    BRLS_BIND(SVGImage, btnRepeatIcon, "music/repeat/icon");
    BRLS_BIND(SVGImage, btnToggleIcon, "music/toggle/icon");
    BRLS_BIND(VideoProgressSlider, osdSlider, "music/progress");
    BRLS_BIND(brls::Label, leftStatusLabel, "music/left/status");
    BRLS_BIND(brls::Label, rightStatusLabel, "music/right/status");
    BRLS_BIND(brls::Label, playTitle, "music/play/title");

    bool toggleShuffle();

    bool toggleLoop();

    void registerMpvEvent();

    void unregisterMpvEvent();

    void reset();

    MPVEvent::Subscription eventSubscribeID;
    MPVCommandReply::Subscription replySubscribeID;

    using MusicList = std::unordered_map<int64_t, Track>;
    int64_t playSession = 0;
    std::string itemId;
    MusicList playList;

    RepeatMode repeat = RepeatNone;
};