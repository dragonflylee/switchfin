/*
    Copyright 2023 dragonflylee
*/

#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "tab/media_series.hpp"
#include "view/h_recycling.hpp"
#include "view/icon_button.hpp"
#include "view/text_box.hpp"
#include "view/video_card.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include <fmt/ranges.h>

using namespace brls::literals;  // for _i18n

namespace {
std::string yearOf(const std::string& date) {
    return date.size() < 4 ? std::string() : date.substr(0, 4);
}

bool isPast(const std::string& date) {
    if (date.size() < 10) return false;
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return false;
    char today[11];
    if (!std::strftime(today, sizeof(today), "%Y-%m-%d", local)) return false;
    return date.substr(0, 10) < today;
}
}  // namespace

MediaSeries::MediaSeries(const jellyfin::Episode& item) {
    brls::Logger::debug("Tab MediaSeries: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/series.xml");

    if (item.Type == jellyfin::mediaTypeSeries) {
        this->seriesId = item.Id;
    } else if (item.SeriesId.is_string()) {
        this->seriesId = item.SeriesId.get<std::string>();
    }

    this->labelTitle->setText(item.Name);
    this->seasons->registerCell("Cell", VideoCardCell::create);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);
    this->special->registerCell("Cell", VideoCardCell::create);

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    }
    // 按钮与季列表无几何重叠，需显式指定 D-pad 路由
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");
    this->btnFavorite->setCustomNavigationRoute(brls::FocusDirection::DOWN, "series/seasons");

    this->btnPlay->registerClickAction([this](...) {
        this->doPlay();
        return true;
    });
    this->btnDownload->registerClickAction([this](...) {
        this->doDownloadSeries();
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    this->statusSub = dm.getStatusEvent()->subscribe(
        [this](const std::string& id, DownloadStatus status) { this->updateDownloadButton(); });

    this->updateFavoriteButton(item.UserData.IsFavorite);
    this->btnFavorite->registerClickAction([this](...) {
        if (this->isFavorite)
            this->unFavorite();
        else
            this->doFavorite();
        return true;
    });

    this->doSeason();
    this->doSeries();
    this->doSimilar();
    this->doSpecial();
}

MediaSeries::~MediaSeries() {
    brls::Logger::debug("Tab MediaSeries: delete");
    auto& dm = DownloadManager::instance();
    dm.getStatusEvent()->unsubscribe(this->statusSub);
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaSeries::doRequest() {
    // after playback: next episode (Play button) + watched states of the
    // season cards; the episodes are refreshed by MediaSeason itself
    this->doSeason();
}

void MediaSeries::updateCredits(const jellyfin::Detail& r) {
    auto setRow = [](brls::View* row, brls::Label* label, const std::string& text) {
        row->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        if (!text.empty()) label->setText(text);
    };

    auto setLabel = [](brls::Label* label, const std::string& text) {
        label->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        if (!text.empty()) label->setText(text);
    };

    setLabel(this->labelTags,
        r.Tags.empty() ? std::string()
                       : fmt::format("{}: {}", "main/media/tags"_i18n, fmt::format("{}", fmt::join(r.Tags, ", "))));

    this->boxProviders->setVisibility(r.ProviderIds.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    for (const auto& [key, id] : r.ProviderIds) {
        this->boxProviders->addView(misc::providerLink(key, id, true));
    }

    std::vector<std::string> studios;
    for (const auto& it : r.Studios) studios.push_back(it.Name);
    setRow(this->boxStudios, this->labelStudios, misc::joinNames(studios));

    std::string span = yearOf(r.PremiereDate);
    if (!span.empty()) {
        const bool ongoing = r.Status == "Continuing" && (r.EndDate.empty() || !isPast(r.EndDate));
        const std::string end = ongoing ? "main/media/present"_i18n : yearOf(r.EndDate);
        if (!end.empty()) span = fmt::format("{} - {}", span, end);
    }
    setRow(this->boxRelease, this->labelRelease, span);

    brls::View* rows[] = {this->labelTags, this->boxProviders, this->boxStudios, this->boxRelease};
    brls::View* last = nullptr;
    for (auto* row : rows) {
        if (row->getVisibility() == brls::Visibility::VISIBLE) last = row;
    }
    for (auto* row : rows) row->setMarginBottom(row == last ? 0 : 12);
    this->infoBox->setVisibility(last ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void MediaSeries::doSeries() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Detail>(
        [ASYNC_TOKEN](const jellyfin::Detail& r) {
            ASYNC_RELEASE
            this->labelTitle->setText(r.Name);
            this->labelYear->setText(std::to_string(r.ProductionYear));

            if (r.OriginalTitle.empty() || r.OriginalTitle == r.Name) {
                this->labelOriginalTitle->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelOriginalTitle->setText(
                    fmt::format("{}: {}", "main/media/original_title"_i18n, r.OriginalTitle));
                this->labelOriginalTitle->setVisibility(brls::Visibility::VISIBLE);
            }

            if (r.RecursiveItemCount > 0) {
                this->labelEpisodes->setText(
                    fmt::format(fmt::runtime("main/media/episodes"_i18n), r.RecursiveItemCount));
                this->pillEpisodes->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->pillEpisodes->setVisibility(brls::Visibility::GONE);
            }

            if (r.OfficialRating.empty()) {
                this->pillParental->setVisibility(brls::Visibility::GONE);
            } else {
                this->parentalRating->setText(r.OfficialRating);
                this->pillParental->setVisibility(brls::Visibility::VISIBLE);
            }
            if (r.CommunityRating == 0.f) {
                this->pillRating->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelRating->setText(fmt::format("{:.1f}", r.CommunityRating));
                this->pillRating->setVisibility(brls::Visibility::VISIBLE);
            }

            if (r.Taglines.empty()) {
                this->labelTagline->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelTagline->setText(r.Taglines.front());
                this->labelTagline->setVisibility(brls::Visibility::VISIBLE);
            }

            this->labelOverview->setText(r.Overview);

            if (r.Genres.empty()) {
                this->labelGenres->setVisibility(brls::Visibility::GONE);
            } else {
                this->labelGenres->setText(fmt::format("{}", fmt::join(r.Genres, ", ")));
                this->labelGenres->setVisibility(brls::Visibility::VISIBLE);
            }

            this->updateCredits(r);

            if (r.People.size() > 0) {
                this->people->setDataSource(new PeopleDataSource(r.People));
            } else {
                this->people->setVisibility(brls::Visibility::GONE);
            }
            this->updateFavoriteButton(r.UserData.IsFavorite);

            if (r.UserData.Played) {
                this->btnPlay->setIcon("icon/ico-checkmark.svg");
                this->btnPlay->setText("main/media/played"_i18n);
                this->btnPlay->setButtonStyle("disabled");
            }

            auto poster = r.ImageTags.find(jellyfin::imageTypePrimary);
            if (poster != r.ImageTags.end()) {
                Image::load(this->imagePoster, 300, jellyfin::apiPrimaryImage, r.Id,
                    HTTP::encode_form({
                        {"tag", poster->second},
                    }));
            }

            auto logo = r.ImageTags.find(jellyfin::imageTypeLogo);
            if (logo != r.ImageTags.end()) {
                Image::load(this->imageLogo, 440, jellyfin::apiLogoImage, r.Id,
                    HTTP::encode_form({
                        {"tag", logo->second},
                    }));
            }

            if (r.BackdropImageTags.size() > 0) {
                Image::load(this->imageBackdrop, 1280, jellyfin::apiBackdropImage, r.Id, 0,
                    HTTP::encode_form({{ "tag", r.BackdropImageTags.at(0) }}));
            } else {
                this->bannerBox->setVisibility(brls::Visibility::GONE);
                this->contentRow->setMarginTop(0);
                this->contentInfo->setMarginTop(0);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->seriesId);
}

void MediaSeries::doSeason() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.empty()) {
                this->labelSeasons->setVisibility(brls::Visibility::GONE);
                this->seasons->setVisibility(brls::Visibility::GONE);
                return;
            }
            this->seasons->setDataSource(new VideoDataSource(r.Items, this->seriesId));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("doSeason {}", ex);
        },
        jellyfin::apiShowSeanon, this->seriesId, query);
}

void MediaSeries::doSimilar() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
        {"enableImageTypes", "Primary"},
        {"fields", "ItemCounts"},
    });

    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                this->similar->setDataSource(new VideoDataSource(r.Items));
                this->similar->setVisibility(brls::Visibility::VISIBLE);
                this->labelSimilar->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->similar->setVisibility(brls::Visibility::GONE);
                this->labelSimilar->setVisibility(brls::Visibility::GONE);
                this->similar->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->similar->setVisibility(brls::Visibility::GONE);
            this->labelSimilar->setSubtitle(ex);
            brls::Logger::warning("MediaSeries doSimilar: {}", ex);
            brls::Application::notify(fmt::format("similar: {}", ex));
        },
        jellyfin::apiSimilar, this->seriesId, query);
}

void MediaSeries::doSpecial() {
    ASYNC_RETAIN
    jellyfin::getJSON<std::vector<jellyfin::Episode>>(
        [ASYNC_TOKEN](const std::vector<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.size() > 0) {
                this->special->setDataSource(new VideoDataSource(r));
                this->special->setVisibility(brls::Visibility::VISIBLE);
                this->labelSpecial->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->special->setVisibility(brls::Visibility::GONE);
                this->labelSpecial->setVisibility(brls::Visibility::GONE);
                this->special->clearData();
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->special->setVisibility(brls::Visibility::GONE);
            this->labelSpecial->setSubtitle(ex);
            brls::Logger::warning("MediaSeries doSpecial: {}", ex);
            brls::Application::notify(fmt::format("special: {}", ex));
        },
        jellyfin::apiItemSpecial, AppConfig::instance().getUserId(), this->seriesId);
}

void MediaSeries::doPlay() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"fields", "MediaSourceCount"},
        {"seriesId", this->seriesId},
    });
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            if (r.Items.size() > 0) {
                PlayerView* view = new PlayerView(r.Items[0]);
                view->setTitie(
                    fmt::format("S{}E{} - {}", r.Items[0].ParentIndexNumber, r.Items[0].IndexNumber, r.Items[0].Name));
                brls::sync([view]() { brls::Application::giveFocus(view); });
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaSeries doPlay: {}", ex);
            brls::Application::notify(fmt::format("play: {}", ex));
        },
        jellyfin::apiShowNextUp, query);
}

void MediaSeries::doDownloadSeries() {
    ASYNC_RETAIN
    jellyfin::getJSON<jellyfin::Result<jellyfin::Episode>>(
        [ASYNC_TOKEN](const jellyfin::Result<jellyfin::Episode>& r) {
            ASYNC_RELEASE
            auto& dm = DownloadManager::instance();
            std::vector<std::string> wanted;
            for (auto& item : r.Items) {
                // Only queue episodes that are neither present nor in progress.
                DownloadStatus status = dm.findItem(item.Id);
                if (status == DownloadStatus::NotFound || status == DownloadStatus::Failed ||
                    status == DownloadStatus::Cancelled) {
                    wanted.push_back(item.Id);
                }
            }
            if (wanted.empty()) {
                brls::Logger::info("MediaSeries download all completed: {}", this->seriesId);
                brls::Application::notify("main/download/completed"_i18n);
                return;
            }
            Dialog::cancelable(
                fmt::format(fmt::runtime("main/download/confirm_season"_i18n), wanted.size()), [wanted]() {
                    auto& dm = DownloadManager::instance();
                    int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
                    for (auto& key : wanted) dm.addDownload(key, static_cast<DownloadQuality>(qi));
                    brls::Logger::info("MediaSeries download queued: {}", wanted.size());
                    brls::Application::notify("main/download/queued"_i18n);
                });
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaSeries doDownloadSeries: {}", ex);
            brls::Application::notify(fmt::format("download: {}", ex));
        },
        jellyfin::apiShowEpisodes, this->seriesId, "");
}

void MediaSeries::updateDownloadButton() {
    auto& dm = DownloadManager::instance();
    auto it = dm.findSeries(this->seriesId);
    if (it.first == 0) {
        this->btnDownload->setText("main/download/start"_i18n);
    } else if (it.first == it.second) {
        this->btnDownload->setText("main/download/completed"_i18n);
    } else {
        this->btnDownload->setText(fmt::format("{} {}/{}", "main/download/downloading"_i18n, it.second, it.first));
    }
}

bool MediaSeries::doFavorite() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->seriesId},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaSeries doFavorite: {}", ex);
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                [ex]() { brls::Application::notify(fmt::format("favorite: {}", ex)); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->seriesId);

    return true;
}

bool MediaSeries::unFavorite() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaSeries unFavorite: {}", ex);
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                [ex]() { brls::Application::notify(fmt::format("unfavorite: {}", ex)); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->seriesId);

    return true;
}

void MediaSeries::updateFavoriteButton(bool favorite) {
    this->isFavorite = favorite;
    if (favorite) {
        this->btnFavorite->setIcon("icon/ico-heart.svg");
        this->btnFavorite->setText("main/media/del_favorite"_i18n);
    } else {
        this->btnFavorite->setIcon("icon/ico-heart-gray.svg");
        this->btnFavorite->setText("main/media/add_favorite"_i18n);
    }
}
