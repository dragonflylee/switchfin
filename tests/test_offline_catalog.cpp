// Standalone logic test — offline catalog tree building (T2, SPEC AC2/AC10/AC20).
//
//   c++ -std=gnu++17 -arch x86_64 \
//       -Iapp/include -Ilibrary/borealis/library/include/borealis/extern \
//       tests/test_offline_catalog.cpp -o /tmp/t && /tmp/t
//
// Exercises the PURE catalog logic (offline_catalog.hpp) that turns a flat set
// of plex::Item nodes into a navigable libraries -> shows -> seasons -> episodes
// tree, including synthesis of missing ancestors for legacy downloads.

#include <cstdio>
#include <utils/offline_catalog.hpp>

static int failures = 0;
#define CHECK(cond)                                          \
    do {                                                     \
        if (!(cond)) {                                       \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                      \
        }                                                    \
    } while (0)

static plex::Item movie(const std::string& key, const std::string& title, const std::string& sec) {
    plex::Item it;
    it.ratingKey = key;
    it.type = plex::mediaTypeMovie;
    it.title = title;
    it.librarySectionID = sec;
    it.librarySectionTitle = "Films";
    return it;
}

// legacy episode: no real ancestor ratingKeys, only grandparentTitle + indexes
static plex::Item legacyEpisode(const std::string& key, const std::string& show, int season, int ep) {
    plex::Item it;
    it.ratingKey = key;
    it.type = plex::mediaTypeEpisode;
    it.title = "Ep " + std::to_string(ep);
    it.grandparentTitle = show;
    it.parentIndex = season;
    it.index = ep;
    return it;
}

int main() {
    std::vector<plex::Item> nodes;
    nodes.push_back(movie("m1", "Zodiac", "1"));
    nodes.push_back(movie("m2", "Amelie", "1"));
    // two legacy episodes of the same show/season, added out of order
    nodes.push_back(legacyEpisode("e2", "Scrubs", 1, 2));
    nodes.push_back(legacyEpisode("e1", "Scrubs", 1, 1));

    auto full = offline::synthesizeAncestors(nodes);

    // a show node and a season node must have been synthesized
    int shows = 0, seasons = 0, episodes = 0, movies = 0;
    for (auto& it : full) {
        if (it.type == plex::mediaTypeShow) ++shows;
        else if (it.type == plex::mediaTypeSeason) ++seasons;
        else if (it.type == plex::mediaTypeEpisode) ++episodes;
        else if (it.type == plex::mediaTypeMovie) ++movies;
    }
    CHECK(movies == 2);
    CHECK(shows == 1);
    CHECK(seasons == 1);
    CHECK(episodes == 2);

    // sections: one Films (movies), one for the show
    auto sections = offline::buildSections(full);
    CHECK(sections.size() == 2);

    // the movie section holds both films, sorted by title (Amelie < Zodiac)
    auto films = offline::sectionItems(full, "1");
    CHECK(films.size() == 2);
    CHECK(films.size() == 2 && films[0].title == "Amelie" && films[1].title == "Zodiac");

    // find the synthesized show and walk down to episodes
    std::string showKey;
    for (auto& it : full)
        if (it.type == plex::mediaTypeShow) showKey = it.ratingKey;
    CHECK(!showKey.empty());

    auto seasonsOf = offline::childrenOf(full, showKey);
    CHECK(seasonsOf.size() == 1);
    CHECK(seasonsOf.size() == 1 && seasonsOf[0].type == plex::mediaTypeSeason);

    std::string seasonKey = seasonsOf.empty() ? "" : seasonsOf[0].ratingKey;
    auto eps = offline::childrenOf(full, seasonKey);
    CHECK(eps.size() == 2);
    // episodes sorted by index (e1 before e2)
    CHECK(eps.size() == 2 && eps[0].ratingKey == "e1" && eps[1].ratingKey == "e2");

    // idempotence: real ancestor nodes already present must not be duplicated
    plex::Item show;
    show.ratingKey = "S9";
    show.type = plex::mediaTypeShow;
    show.title = "Real Show";
    plex::Item season;
    season.ratingKey = "S9-1";
    season.type = plex::mediaTypeSeason;
    season.parentRatingKey = "S9";
    season.index = 1;
    plex::Item epReal;
    epReal.ratingKey = "E100";
    epReal.type = plex::mediaTypeEpisode;
    epReal.parentRatingKey = "S9-1";
    epReal.grandparentRatingKey = "S9";
    epReal.index = 1;
    std::vector<plex::Item> real = {show, season, epReal};
    auto real2 = offline::synthesizeAncestors(real);
    CHECK(real2.size() == 3);  // nothing synthesized
    CHECK(offline::childrenOf(real2, "S9").size() == 1);
    CHECK(offline::childrenOf(real2, "S9-1").size() == 1);

    // asset paths to cache for a fiche (T3): non-empty images only
    plex::Item art;
    art.thumb = "/t";
    art.art = "/a";
    art.clearLogo = "/l";
    plex::Role withFace;
    withFace.thumb = "/r1";
    plex::Role noFace;  // empty thumb must be skipped
    art.roles = {withFace, noFace};
    auto assets = offline::assetPaths(art);
    CHECK(assets.size() == 4);

    if (failures == 0) {
        printf("test_offline_catalog: OK\n");
        return 0;
    }
    printf("test_offline_catalog: %d FAILURE(S)\n", failures);
    return 1;
}
