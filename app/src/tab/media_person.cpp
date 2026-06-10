#include "tab/media_person.hpp"
#include "view/recyling_video.hpp"
#include "api/plex.hpp"
#include "utils/image.hpp"

using namespace brls::literals;  // for _i18n

MediaPerson::MediaPerson(const plex::Role& role) : personId(role.id) {
    brls::Logger::debug("Tab MediaPerson: create");
    this->inflateFromXMLRes("xml/view/people.xml");

    this->labelName->setText(role.tag);
    // le personnage incarné n'est connu qu'en venant d'une fiche (Role.role)
    if (!role.role.empty()) {
        this->labelRole->setText(fmt::format(fmt::runtime("main/person/as"_i18n), role.role));
        this->labelRole->setVisibility(brls::Visibility::VISIBLE);
    }
    if (!role.thumb.empty()) {
        // le portrait d'un Role est une URL absolue (provider.plex.tv) ou un
        // chemin relatif au serveur
        if (role.thumb.rfind("http", 0) == 0) {
            Image::with(this->imagePhoto, role.thumb);
        } else {
            Image::load(this->imagePhoto, role.thumb, 440);
        }
    }
    this->doMedia();
}

MediaPerson::~MediaPerson() {
    brls::Logger::debug("Tab MediaPerson: delete");
    Image::cancel(this->imagePhoto);
}

void MediaPerson::doMedia() {
    std::string query = HTTP::encode_form({{"count", "60"}});

    ASYNC_RETAIN
    plex::getJSON<plex::Container<plex::Item>>(
        AppConfig::instance().getUrl(), AppConfig::instance().getToken(),
        [ASYNC_TOKEN](const plex::Container<plex::Item>& r) {
            ASYNC_RELEASE
            std::vector<plex::Item> movies, shows;
            for (auto& it : r.Items) {
                if (it.type == plex::mediaTypeMovie) movies.push_back(it);
                if (it.type == plex::mediaTypeShow) shows.push_back(it);
            }
            // pills « 12 · Films » : nombre + libellé pluriel, neutre dans
            // toutes les langues (pas d'accord à gérer)
            if (!movies.empty()) {
                this->labelMovies->setText(fmt::format("{} · {}", movies.size(), "main/person/movies"_i18n));
                this->pillMovies->setVisibility(brls::Visibility::VISIBLE);
            }
            if (!shows.empty()) {
                this->labelShows->setText(fmt::format("{} · {}", shows.size(), "main/person/shows"_i18n));
                this->pillShows->setVisibility(brls::Visibility::VISIBLE);
            }
            this->rowMovies->setItems(movies);
            this->rowSeries->setItems(shows);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->rowMovies->setItems({});
            this->rowSeries->setItems({});
            brls::Application::notify(ex);
        },
        plex::apiPersonMedia, this->personId, query);
}
