// Standalone logic test — plex::Item JSON round-trip (T1, SPEC AC1/AC20).
//
// The project has no unit-test framework (BUILD_TESTING OFF); these are tiny
// self-contained programs compiled on demand. See tests/run.sh.
//
//   c++ -std=gnu++17 -arch x86_64 \
//       -Iapp/include -Ilibrary/borealis/library/include/borealis/extern \
//       tests/test_plex_json.cpp -o /tmp/t && /tmp/t
//
// RED before to_json exists: `json j = ep;` fails to compile.
// GREEN once plex::to_json(Item) round-trips through from_json.

#include <cstdio>
#include <api/plex/types.hpp>

using nlohmann::json;

static int failures = 0;
#define CHECK(cond)                                        \
    do {                                                   \
        if (!(cond)) {                                     \
            printf("FAIL: %s (line %d)\n", #cond, __LINE__); \
            ++failures;                                    \
        }                                                  \
    } while (0)

int main() {
    // an episode with full ancestry, genres, cast, media/part/stream, logo, section
    plex::Item ep;
    ep.ratingKey = "1234";
    ep.key = "/library/metadata/1234";
    ep.guid = "plex://episode/abc";
    ep.type = plex::mediaTypeEpisode;
    ep.title = "Pilot";
    ep.summary = "The one that starts it all — accents: é à ü.";
    ep.year = 2004;
    ep.thumb = "/library/metadata/1234/thumb/1";
    ep.art = "/library/metadata/9/art/1";
    ep.clearLogo = "https://img/logo.png";
    ep.duration = 1500000;
    ep.viewOffset = 42000;
    ep.viewCount = 1;
    ep.index = 1;
    ep.parentIndex = 2;
    ep.parentRatingKey = "99";
    ep.parentTitle = "Season 2";
    ep.grandparentRatingKey = "9";
    ep.grandparentTitle = "Scrubs";
    ep.grandparentThumb = "/library/metadata/9/thumb/1";
    ep.librarySectionID = "3";
    ep.librarySectionTitle = "TV Shows";
    ep.contentRating = "TV-14";
    ep.rating = 8.4;
    ep.audienceRating = 9.1;
    ep.genres = {"Comedy", "Drama"};

    plex::Role role;
    role.id = "7";
    role.tag = "Zach Braff";
    role.role = "J.D.";
    role.thumb = "/t/7";
    ep.roles = {role};

    plex::Stream sub;
    sub.id = 1;
    sub.streamType = plex::streamTypeSubtitle;
    sub.language = "English";
    sub.isDefault = true;
    sub.forced = false;
    plex::Part part;
    part.id = 10;
    part.key = "/library/parts/10/file.mkv";
    part.container = "mkv";
    part.size = 1234567;
    part.streams = {sub};
    plex::Media media;
    media.id = 5;
    media.videoResolution = "1080";
    media.videoCodec = "h264";
    media.bitrate = 8000;
    media.parts = {part};
    ep.media = {media};

    json j = ep;                            // to_json
    plex::Item back = j.get<plex::Item>();  // from_json

    CHECK(back.ratingKey == ep.ratingKey);
    CHECK(back.type == ep.type);
    CHECK(back.title == ep.title);
    CHECK(back.summary == ep.summary);
    CHECK(back.year == ep.year);
    CHECK(back.thumb == ep.thumb);
    CHECK(back.art == ep.art);
    CHECK(back.clearLogo == ep.clearLogo);
    CHECK(back.duration == ep.duration);
    CHECK(back.viewOffset == ep.viewOffset);
    CHECK(back.index == ep.index);
    CHECK(back.parentIndex == ep.parentIndex);
    CHECK(back.parentRatingKey == ep.parentRatingKey);
    CHECK(back.parentTitle == ep.parentTitle);
    CHECK(back.grandparentRatingKey == ep.grandparentRatingKey);
    CHECK(back.grandparentTitle == ep.grandparentTitle);
    CHECK(back.grandparentThumb == ep.grandparentThumb);
    CHECK(back.librarySectionID == ep.librarySectionID);
    CHECK(back.librarySectionTitle == ep.librarySectionTitle);
    CHECK(back.contentRating == ep.contentRating);
    CHECK(back.rating == ep.rating);
    CHECK(back.audienceRating == ep.audienceRating);
    CHECK(back.genres.size() == 2);
    CHECK(back.genres.size() == 2 && back.genres[0] == "Comedy" && back.genres[1] == "Drama");
    CHECK(back.roles.size() == 1);
    CHECK(back.roles.size() == 1 && back.roles[0].tag == "Zach Braff" && back.roles[0].role == "J.D." &&
        back.roles[0].thumb == "/t/7");
    CHECK(back.media.size() == 1);
    CHECK(back.media.size() == 1 && back.media[0].videoResolution == "1080" &&
        back.media[0].videoCodec == "h264" && back.media[0].bitrate == 8000);
    CHECK(back.media.size() == 1 && back.media[0].parts.size() == 1 &&
        back.media[0].parts[0].key == part.key && back.media[0].parts[0].container == "mkv");
    CHECK(back.media.size() == 1 && back.media[0].parts.size() == 1 &&
        back.media[0].parts[0].streams.size() == 1 &&
        back.media[0].parts[0].streams[0].streamType == plex::streamTypeSubtitle &&
        back.media[0].parts[0].streams[0].isDefault == true &&
        back.media[0].parts[0].streams[0].language == "English");

    if (failures == 0) {
        printf("test_plex_json: OK\n");
        return 0;
    }
    printf("test_plex_json: %d FAILURE(S)\n", failures);
    return 1;
}
