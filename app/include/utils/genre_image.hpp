#pragma once

#include <string>

/// Kometa genre posters (Default-Images) for the "Genres" tab cards:
/// Plex exposes NO thumb on /library/sections/{key}/genre
/// (verified on a real server 2026-06-10: Directory = fastKey/key/title/type only).
///
/// Source: https://github.com/Kometa-Team/Default-Images (master branch,
/// genre/ folder, 275 2:3 posters at 2000x3000) — Kometa's default image
/// set, doc: https://kometa.wiki/en/latest/defaults/both/genre/
/// Warning: the repo has NO declared license (api.github.com -> license: null);
/// images are loaded on the fly, nothing is redistributed in the app.
namespace GenreImage {

/// ABSOLUTE URL of the Kometa poster for a Plex genre title, or "" if no
/// poster matches (the card then keeps its placeholder, no request is
/// emitted — the set of known files is embedded, so never a 404).
///
/// The returned URL goes through the Plex server's photo transcoder
/// (/photo/:/transcode?url=<raw.githubusercontent.com/...>): it downloads
/// and resizes the 2000x3000 original (~600 KB, i.e. a 24 MB RGBA texture
/// — prohibitive on Switch) to the same 325 box as the other posters
/// (verified on a real server 2026-06-10: 200, JPEG 325x488 ~15 KB, including
/// names with space/&/+/apostrophe).
///
/// Matching is case-insensitive and covers the French/Italian labels of
/// Plex agents ("Comédie" -> Comedy, "Science-Fiction" -> Science Fiction,
/// "Dramma" -> Drama...).
std::string posterUrl(const std::string& title);

}  // namespace GenreImage
