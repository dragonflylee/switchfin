#include "activity/player_view.hpp"
#include "tab/media_movie.hpp"
#include "view/h_recycling.hpp"
#include "view/video_card.hpp"
#include "view/text_box.hpp"
#include "view/people_source.hpp"
#include "view/video_source.hpp"
#include "view/mpv_core.hpp"
#include "view/icon_button.hpp"
#include "api/jellyfin.hpp"
#include "utils/misc.hpp"
#include "utils/dialog.hpp"
#include <fmt/ranges.h>
#include <ctime>

using namespace brls::literals;  // for _i18n

namespace {
std::string joinNames(const std::vector<std::string>& list) {
    return fmt::format("{}", fmt::join(list, ", "));
}
}  // namespace

MediaMovie::MediaMovie(const jellyfin::Item& item) : runTimeTicks(item.RunTimeTicks), itemId(item.Id) {
    brls::Logger::debug("Tab MediaMovie: create");
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/movie.xml");

    this->labelTitle->setText(item.Name);
    this->people->registerCell("Cell", MediaCardCell::create);
    this->crew->registerCell("Cell", MediaCardCell::create);
    this->similar->registerCell("Cell", VideoCardCell::create);

    if (brls::Application::getThemeVariant() == brls::ThemeVariant::LIGHT) {
        this->imageFade->setImageFromRes("img/fade-bottom-light.png");
    }
    // the buttons and the cast row have no geometric overlap: D-pad nav
    // cannot find it. Explicit route — the row materializes its first cell
    // if needed (HRecyclerFrame::getDefaultFocus) and the "centered" scroll
    // follows the focus
    this->btnPlay->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");
    this->btnDownload->setCustomNavigationRoute(brls::FocusDirection::DOWN, "movie/people");

    this->btnPlay->registerClickAction([this, item](...) {
        PlayerView* view = new PlayerView(item, this->playTicks, this->sourceId);
        view->setTitie(item.ProductionYear ? fmt::format("{} ({})", item.Name, item.ProductionYear) : item.Name);
        return true;
    });

    auto& dm = DownloadManager::instance();
    this->updateDownloadButton();
    // live progress on the button (events emitted on the UI thread)
    this->progressSub =
        dm.getProgressEvent()->subscribe([this](const std::string& id, int64_t downloaded, int64_t total) {
            if (id != this->itemId || total <= 0) return;
            // "Downloading... (42%)" — a bare percentage does not say what
            // the button does; completion goes back through updateDownloadButton
            this->btnDownload->setText(
                fmt::format("{} ({:.0f}%)", "main/download/downloading"_i18n, downloaded * 100.0 / total));
        });
    this->statusSub = dm.getStatusEvent()->subscribe([this](const std::string& id, DownloadStatus status) {
        if (id == this->itemId) this->updateDownloadButton();
    });
    this->btnDownload->registerClickAction([this](...) {
        auto& dm = DownloadManager::instance();
        switch (dm.findItem(this->itemId)) {
        case DownloadStatus::Queued:
        case DownloadStatus::Downloading:
            Dialog::cancelable("main/download/confirm_cancel"_i18n, [this]() {
                DownloadManager::instance().cancelDownload(this->itemId);
                this->updateDownloadButton();
            });
            break;
        case DownloadStatus::Completed:
            brls::Logger::info("MediaMovie download completed: {}", this->itemId);
            brls::Application::notify("main/download/completed"_i18n);
            break;
        default:
            int qi = AppConfig::instance().getValueIndex(AppConfig::DOWNLOAD_QUALITY);
            dm.addDownload(this->itemId, static_cast<DownloadQuality>(qi));
            this->updateDownloadButton();
        }
        return true;
    });

    this->updateFavoriteButton(item.UserData.IsFavorite);
    this->btnFavorite->registerClickAction([this](...) {
        if (this->isFavorite)
            this->unFavorite();
        else
            this->doFavorite();
        return true;
    });

    this->doMovie();
    this->doSimilar();
}

MediaMovie::~MediaMovie() {
    this->playbackTimingTicker.stop();
    brls::Logger::debug("Tab MediaMovie: delete");
    auto& dm = DownloadManager::instance();
    dm.getProgressEvent()->unsubscribe(this->progressSub);
    dm.getStatusEvent()->unsubscribe(this->statusSub);
    Image::cancel(this->imageLogo);
    Image::cancel(this->imagePoster);
    Image::cancel(this->imageBackdrop);
}

void MediaMovie::updateDownloadButton() {
    auto& dm = DownloadManager::instance();
    switch (dm.findItem(this->itemId)) {
    case DownloadStatus::Completed:
        this->btnDownload->setText("main/download/completed"_i18n);
        break;
    case DownloadStatus::Queued:
    case DownloadStatus::Downloading:
        this->btnDownload->setText("main/download/downloading"_i18n);
        break;
    default:
        this->btnDownload->setText("main/download/start"_i18n);
    }
}

void MediaMovie::updatePlaybackTiming() {
    if (this->runTimeTicks == 0) return;

    std::string text = misc::formatMediaDuration(this->runTimeTicks / jellyfin::PLAYTICKS);

    const uint64_t position = this->playTicks > 0 ? static_cast<uint64_t>(this->playTicks) : 0;
    const uint64_t remaining = position < this->runTimeTicks ? this->runTimeTicks - position : this->runTimeTicks;
    const auto now = std::time(nullptr);
    if (now != std::time_t(-1)) {
        const std::time_t finish = now + static_cast<int64_t>(remaining / jellyfin::PLAYTICKS);
        const std::tm* local = std::localtime(&finish);
        char time[6];
        if (local && std::strftime(time, sizeof(time), "%H:%M", local)) {
            text += " · " + fmt::format(fmt::runtime("main/media/ends_at"_i18n), time);
        }
    }
    this->labelPlaybackTiming->setText(text);
}

void MediaMovie::doRequest() {
    int64_t ticks = MPVCore::instance().playback_time;
    this->playTicks = ticks * jellyfin::PLAYTICKS;
    this->btnPlay->setText(ticks > 0 ? misc::sec2Time(ticks) : "main/media/play"_i18n);
    this->updatePlaybackTiming();
}

void MediaMovie::updateCredits(const jellyfin::Detail& r) {
    std::vector<jellyfin::MediaPeople> cast, crew;
    std::vector<std::string> directors, writers;
    for (const auto& it : r.People) {
        if (it.Type == "Actor" || it.Type.empty()) {
            cast.push_back(it);
        } else {
            crew.push_back(it);
            if (it.Type == "Director") directors.push_back(it.Name);
            if (it.Type == "Writer") writers.push_back(it.Name);
        }
    }

    auto setRow = [](brls::View* row, brls::Label* label, const std::string& text) {
        row->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        if (!text.empty()) label->setText(text);
    };

    // 标签行本身就是 Label，自身兼作容器，无需额外包装
    auto setLabel = [](brls::Label* label, const std::string& text) {
        label->setVisibility(text.empty() ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
        if (!text.empty()) label->setText(text);
    };

    if (cast.empty()) {
        this->people->setVisibility(brls::Visibility::GONE);
        this->labelPeople->setVisibility(brls::Visibility::GONE);
    } else {
        this->people->setDataSource(new PeopleDataSource(cast));
        this->people->setVisibility(brls::Visibility::VISIBLE);
        this->labelPeople->setVisibility(brls::Visibility::VISIBLE);
    }

    setRow(this->boxDirector, this->labelDirector, joinNames(directors));
    setRow(this->boxWriter, this->labelWriter, joinNames(writers));

    if (crew.empty()) {
        this->crew->setVisibility(brls::Visibility::GONE);
        this->labelCrew->setVisibility(brls::Visibility::GONE);
    } else {
        this->crew->setDataSource(new PeopleDataSource(crew));
        this->crew->setVisibility(brls::Visibility::VISIBLE);
        this->labelCrew->setVisibility(brls::Visibility::VISIBLE);
    }

    setLabel(this->labelTags,
        r.Tags.empty() ? std::string()
                       : fmt::format("{}: {}", "main/media/tags"_i18n, fmt::format("{}", fmt::join(r.Tags, ", "))));

    std::vector<std::string> studios;
    for (const auto& it : r.Studios) studios.push_back(it.Name);
    setRow(this->boxStudios, this->labelStudios, joinNames(studios));

    setRow(this->boxRelease, this->labelRelease,
        r.PremiereDate.empty() ? std::string() : r.PremiereDate.substr(0, r.PremiereDate.find('T')));

    std::vector<std::string> providers;
    for (const auto& [key, value] : r.ProviderIds) providers.push_back(key);
    std::sort(providers.begin(), providers.end());
    setRow(this->boxProviders, this->labelProviders, joinNames(providers));

    brls::View* rows[] = {this->labelTags, this->boxProviders, this->boxStudios, this->boxRelease,
        this->boxDirector, this->boxWriter};
    brls::View* last = nullptr;
    for (auto* row : rows) {
        if (row->getVisibility() == brls::Visibility::VISIBLE) last = row;
    }
    for (auto* row : rows) row->setMarginBottom(row == last ? 0 : 12);
    this->infoBox->setVisibility(last ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void MediaMovie::doMovie() {
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

            if (r.RunTimeTicks > 0) {
                this->labelDuration->setText(misc::formatMediaDuration(r.RunTimeTicks / jellyfin::PLAYTICKS));
                this->pillDuration->setVisibility(brls::Visibility::VISIBLE);
            } else {
                this->pillDuration->setVisibility(brls::Visibility::GONE);
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


            std::map<std::string, std::string> streamText;
            auto parseStream = [](const jellyfin::Source& source) {
                std::string video, audio;
                for (const auto& stream : source.MediaStreams) {
                    if (stream.Type == jellyfin::streamTypeVideo && video.empty()) {
                        std::string resolution = !stream.HeightRange.empty() ? stream.HeightRange
                            : (stream.Height > 0 ? fmt::format("{}p", stream.Height) : std::string());
                        std::string codec = stream.Codec;
                        std::transform(codec.begin(), codec.end(), codec.begin(),
                            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                        std::vector<std::string> parts;
                        if (!resolution.empty()) parts.push_back(resolution);
                        if (!codec.empty()) parts.push_back(codec);
                        if (!stream.VideoRange.empty()) parts.push_back(stream.VideoRange);
                        video = fmt::format("{}", fmt::join(parts, " "));
                    } else if (stream.Type == jellyfin::streamTypeAudio && audio.empty() &&
                               (stream.IsDefault || source.DefaultAudioStreamIndex == stream.Index)) {
                        std::vector<std::string> parts;
                        if (!stream.Language.empty()) parts.push_back(stream.Language);
                        if (stream.Channels > 0) parts.push_back(fmt::format("{} ch", stream.Channels));
                        audio = fmt::format("{}", fmt::join(parts, " · "));
                    }
                }
                std::vector<std::string> parts;
                if (!video.empty()) parts.push_back(video);
                if (!audio.empty()) parts.push_back(audio);
                return fmt::format("{}", fmt::join(parts, " · "));
            };
            auto showStream = [this](const std::string& text) {
                if (text.empty()) {
                    this->labelMedia->setVisibility(brls::Visibility::GONE);
                    return;
                }
                this->labelMedia->setText(fmt::format("{} {}", "main/media/media_info"_i18n, text));
                this->labelMedia->setVisibility(brls::Visibility::VISIBLE);
            };

            for (const auto& source : r.MediaSources) {
                streamText[source.Id] = parseStream(source);
            }
            if (!r.MediaSources.empty()) showStream(streamText[r.MediaSources.front().Id]);

            if (r.MediaSources.size() > 1) {
                std::vector<std::string> names, ids;
                for (auto& it : r.MediaSources) {
                    names.push_back(it.Name);
                    ids.push_back(it.Id);
                }
                this->btnSource->init(
                    "main/setting/version"_i18n, names, 0, [this, ids, streamText, showStream](int index) {
                        this->sourceId = ids[index];
                        auto it = streamText.find(this->sourceId);
                        if (it != streamText.end()) showStream(it->second);
                    });
                this->btnSource->setVisibility(brls::Visibility::VISIBLE);
            }

            this->playTicks = r.UserData.PlaybackPositionTicks;
            this->btnPlay->setText(
                this->playTicks > 0 ? misc::sec2Time(this->playTicks / jellyfin::PLAYTICKS) : "main/media/play"_i18n);

            if (this->runTimeTicks > 0) {
                this->updatePlaybackTiming();
                this->labelGenres->setMarginBottom(6);
                this->labelPlaybackTiming->setVisibility(brls::Visibility::VISIBLE);
                this->playbackTimingTicker.setCallback([this]() { this->updatePlaybackTiming(); });
                this->playbackTimingTicker.start(60000);
            }
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->people->setVisibility(brls::Visibility::GONE);
        },
        jellyfin::apiUserItem, AppConfig::instance().getUserId(), this->itemId);
}

void MediaMovie::doSimilar() {
    std::string query = HTTP::encode_form({
        {"userId", AppConfig::instance().getUserId()},
        {"limit", "12"},
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
            brls::Logger::warning("MediaMovie doSimilar: {}", ex);
            brls::Application::notify(fmt::format("similar: {}", ex));
        },
        jellyfin::apiSimilar, this->itemId, query);
}

bool MediaMovie::doFavorite() {
    ASYNC_RETAIN
    jellyfin::postJSON(
        {
            {"itemId", this->itemId},
        },
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaMovie doFavorite: {}", ex);
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                [ex]() { brls::Application::notify(fmt::format("favorite: {}", ex)); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

bool MediaMovie::unFavorite() {
    ASYNC_RETAIN
    jellyfin::deleteJSON<jellyfin::UserDataResult>(
        [ASYNC_TOKEN](const jellyfin::UserDataResult& r) {
            ASYNC_RELEASE
            this->updateFavoriteButton(r.IsFavorite);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            brls::Logger::warning("MediaMovie unFavorite: {}", ex);
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                [ex]() { brls::Application::notify(fmt::format("unfavorite: {}", ex)); });
        },
        jellyfin::apiFavoriteItems, AppConfig::instance().getUserId(), this->itemId);

    return true;
}

void MediaMovie::updateFavoriteButton(bool favorite) {
    this->isFavorite = favorite;
    if (favorite) {
        this->btnFavorite->setIcon("icon/ico-heart.svg");
        this->btnFavorite->setText("main/media/del_favorite"_i18n);
    } else {
        this->btnFavorite->setIcon("icon/ico-heart-gray.svg");
        this->btnFavorite->setText("main/media/add_favorite"_i18n);
    }
}