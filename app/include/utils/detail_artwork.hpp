#pragma once

#include "utils/image.hpp"
#include "api/jellyfin/media.hpp"

// Movie/series detail callbacks otherwise discard the response. Retain only
// four bounded model identifiers, never generated URLs or request/view pins.
class DetailArtwork {
public:
    enum class Slot { Poster, Logo, Backdrop };
    static constexpr size_t maxIdentityBytes = 8 * 1024;

    template<class Detail>
    void assign(const Detail& detail) noexcept {
        clear();
        const auto primary = detail.ImageTags.find(jellyfin::imageTypePrimary);
        const auto logo = detail.ImageTags.find(jellyfin::imageTypeLogo);
        const std::string empty;
        const auto& p = primary == detail.ImageTags.end() ? empty : primary->second;
        const auto& l = logo == detail.ImageTags.end() ? empty : logo->second;
        const auto& b = detail.BackdropImageTags.empty() ? empty : detail.BackdropImageTags.front();
        size_t bytes = 0;
        for (const auto* part : {&detail.Id, &p, &l, &b}) {
            if (part->size() > maxIdentityBytes - bytes) return;
            bytes += part->size();
        }
        try {
            id = detail.Id; poster = p; logoTag = l; backdrop = b;
            account = AppConfig::instance().requestCancellation();
        } catch (...) { clear(); }
    }

    void retry(brls::Image* view, Slot slot) const {
        if (id.empty() || !account || account->load()) return;
        if (slot == Slot::Poster && !poster.empty())
            Image::load(view, jellyfin::apiPrimaryImage, id,
                HTTP::encode_form({{"tag", poster}, {"maxWidth", "325"}}));
        else if (slot == Slot::Logo && !logoTag.empty())
            Image::load(view, jellyfin::apiLogoImage, id,
                HTTP::encode_form({{"tag", logoTag}, {"maxWidth", "440"}}));
        else if (slot == Slot::Backdrop && !backdrop.empty())
            Image::load(view, jellyfin::apiBackdropImage, id, 0,
                HTTP::encode_form({{"tag", backdrop}, {"maxWidth", "1920"}}));
    }

private:
    void clear() noexcept {
        // Swap releases string storage too, including a partially copied model.
        std::string().swap(id); std::string().swap(poster);
        std::string().swap(logoTag); std::string().swap(backdrop);
        account.reset();
    }
    std::string id, poster, logoTag, backdrop;
    HTTP::Cancel account;
};
