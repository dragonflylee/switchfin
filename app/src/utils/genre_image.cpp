#include "utils/genre_image.hpp"
#include "utils/config.hpp"
#include "api/backend.hpp"
#include "api/http.hpp"

#include <fmt/format.h>
#include <cctype>
#include <unordered_map>

namespace {

/// File names (without .jpg) ACTUALLY present in
/// github.com/Kometa-Team/Default-Images/genre (listed via the GitHub API
/// on 2026-06-10, 275 files). Embedded so we NEVER emit a request to a
/// nonexistent poster: a genre outside this list keeps its placeholder.
/// The case is that of the files — it is authoritative in the raw URL.
const char* const kometaPosters[] = {
    "1001 Movies You Must See Before You Die", "Absurd Comedy", "Absurdism", "Action", "Action & Adventure",
    "Action Comedy", "Adult", "Adult Cartoon", "Adult Cartoons", "Adventure", "Alien", "Alternate History",
    "America", "Animal", "Animals", "Animated Short", "Animated Shorts", "Animation", "Anime", "Anthology",
    "Anti-Hero", "Apocalypse", "Arthouse", "Artificial Intelligence", "Assassin", "Astronaut", "Award Shows",
    "Backroads Horror", "Betrayal", "Biography", "Biopic", "Blaxploitation", "Body Horror", "Bottle", "Boxing",
    "Boys Love", "Buddy Comedy", "Bug", "Building", "Cannibal", "Caper", "Cars", "Chick Flick", "Children",
    "Children's Cartoon", "Children's Cartoons", "Christmas", "Colorado", "Comedy", "Comedy Horror",
    "Coming of Age", "Competition", "Con Artist", "Conspiracy", "Cop", "Costume Drama", "Courtroom",
    "Creature Feature", "Creature Horror", "Crime", "Crime Comedy", "Criterion Collection", "Cult Classics",
    "Cyber-Thriller", "Cyberpunk", "Dark Comedy", "Dark Fantasy", "Demons", "Detective", "Dinosaur", "Dinosaurs",
    "Disney", "Disney Pixar Dreamworks", "Documentary", "Dragon", "Drama", "Dramedy", "Dreamworks", "Dystopian",
    "Ecchi", "Elevated Horror", "Engineering", "Engineering Disaster", "Engineering Disasters", "Epic", "Erotica",
    "Espionage", "Experimental", "Exploitation", "Extreme", "Fairy Tale", "Family", "Fantasy", "Fantasy Horror",
    "Film Noir", "First Responder", "Folk Horror", "Food", "Football", "Foreign", "Foreign Giallo", "Foreign Noir",
    "Found Footage", "Found Footage Horror", "Fringe", "Fugitive", "Funny Sci-Fi", "Gaijinsploitation", "Game",
    "Game Show", "Gangster", "Ghost", "Girls Love", "Gothic", "Gourmet", "Guilty Pleasure", "Harem", "Heartbreak",
    "Heist", "Hentai", "Historical Event", "Historical Fiction", "History", "Home and Garden", "Horror",
    "Horror Parody", "Hostage", "Human Body", "Hustle", "Indie", "Inspirational", "Jungle Adventure", "Kids",
    "LGBTQ+", "Lost Treasure", "Louisiana", "Lovecraftian", "MMA", "Manufacturing", "Martial Arts",
    "May the Fourth", "Mecha", "Medical", "Medieval", "Melodrama", "Military", "Mind-Bend", "Mind-Fuck",
    "Mind-Fuck2", "Mini-Series", "Mockumentary", "Monster", "Movies That Defined Our Childhood", "Music",
    "Musical", "Mystery", "Mystery Box", "Mythology", "Natural Disaster", "Nature", "Naval", "Neo-Noir",
    "New Years Eve", "News", "News & Politics", "Ninja", "Occult", "Outdoor Adventure", "Outlaw", "Pandemic",
    "Paranormal", "Parody", "Period Drama", "Philosophy", "Pinku", "Pixar", "Plant", "Plants", "Police",
    "Politics", "Post-Apocalyptic", "Prehistoric", "President", "Prison", "Psychedelic", "Psychological",
    "Psychological Horror", "Reality", "Religion", "Remake", "Revenge", "Robot", "Romance", "Romantic Comedy",
    "Romantic Drama", "Samurai", "Satire", "School", "Sci-Fi & Fantasy", "Sci-Fi Horror", "Science Fiction",
    "Seductive", "Serial Killer", "Sexploitation", "Short", "Shoujo", "Shounen", "Silent", "Silent2",
    "Sinister Screens", "Slapstick", "Slasher", "Sleazy", "Slice of Life", "Soap", "Space", "Space Opera",
    "Spaghetti Western", "Splatter", "Sport", "Spy", "Stand-Up Comedy", "Steampunk", "Stephen King",
    "Stoner Comedy", "Stop-Motion", "Super Power", "Superhero", "Supernatural", "Surreal", "Surreal Comedy",
    "Surreal Horror", "Surrealism", "Survival", "Suspense", "Swashbuckler", "Sword & Sandal", "Sword & Sorcery",
    "TV Movie", "Talk Show", "Technology", "The Arts", "Thriller", "Time Loop", "Time Travel",
    "Top Grossing Films Annually", "Top Grossing Films of All-Time", "Trains", "Travel", "Treasure Hunt",
    "True Crime", "Ufo", "Ultimate Bass", "Unexpectedly Amazing", "Urban Fantasy", "Utopia", "Vampire",
    "Video Game", "Video Nasty", "Visually Insane", "War", "War & Politics", "Weather", "Wedding", "Werewolf",
    "Western", "Whodunit", "Witch", "Wizardry & Witchcraft", "World War", "Wuxia", "Zombie", "Zombie Comedy",
    "Zombie Horror", "other"};

/// Non-English labels (or variants) -> Kometa file name. Keys in ASCII
/// lowercase, UTF-8 accents kept as-is (lowerAscii does not touch
/// multi-byte bytes). Covers the French TMDB/Plex genres seen on a real
/// server (sections 1 and 2, 2026-06-10) + standard TMDB translations;
/// "Dramma" = Italian leftover observed on the server.
const std::unordered_map<std::string, const char*> aliases = {
    {"action/aventure", "Action & Adventure"},
    {"actualités", "News"},
    {"arts martiaux", "Martial Arts"},
    {"aventure", "Adventure"},
    {"biographie", "Biography"},
    {"comédie", "Comedy"},
    {"comédie dramatique", "Dramedy"},
    {"comédie musicale", "Musical"},
    {"comédie romantique", "Romantic Comedy"},
    {"court-métrage", "Short"},
    {"documentaire", "Documentary"},
    {"drame", "Drama"},
    {"dramma", "Drama"},
    {"enfants", "Children"},
    {"familial", "Family"},
    {"famille", "Family"},
    {"fantastique", "Fantasy"},
    {"feuilleton", "Soap"},
    {"film-noir", "Film Noir"},
    {"guerre", "War"},
    {"guerre & politique", "War & Politics"},
    {"histoire", "History"},
    {"horreur", "Horror"},
    {"mini-série", "Mini-Series"},
    {"mini-séries", "Mini-Series"},
    {"musique", "Music"},
    {"mystère", "Mystery"},
    {"policier", "Crime"},
    {"réalité", "Reality"},
    {"romantique", "Romance"},
    {"sci-fi", "Science Fiction"},
    {"science-fiction", "Science Fiction"},
    {"science-fiction & fantastique", "Sci-Fi & Fantasy"},
    {"talk", "Talk Show"},
    {"téléfilm", "TV Movie"},
};

/// ASCII lowercase only: bytes >= 0x80 (é, è...) are left intact —
/// the alias keys are written with the same convention
std::string lowerAscii(const std::string& s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return out;
}

/// %-encodes a path segment (space, &, +, apostrophe... of Kometa names).
/// curl_escape (HTTP::encode_form) will then re-encode these % as %25 in
/// the transcoder's url= parameter — the server decodes once and requests
/// GitHub with the expected %20/%26 (full chain verified on a real server
/// 2026-06-10 with "Sci-Fi & Fantasy", "LGBTQ+", "Children's Cartoon").
std::string encodeSegment(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

/// title (ASCII lowercase) -> Kometa file name, table built once:
/// the 275 files indexed by their own name, then the aliases
const std::unordered_map<std::string, const char*>& lookupTable() {
    static const std::unordered_map<std::string, const char*> table = [] {
        std::unordered_map<std::string, const char*> t;
        for (const char* stem : kometaPosters) t.emplace(lowerAscii(stem), stem);
        for (const auto& [key, stem] : aliases) t.emplace(key, stem);
        return t;
    }();
    return table;
}

}  // namespace

std::string GenreImage::posterUrl(const std::string& title) {
    if (title.empty()) return "";

    const auto& table = lookupTable();
    auto it = table.find(lowerAscii(title));
    if (it == table.end()) return "";

    std::string raw = fmt::format(
        "https://raw.githubusercontent.com/Kometa-Team/Default-Images/master/genre/{}.jpg",
        encodeSegment(it->second));

    // external resource proxied/resized by the backend (325 box, ratio preserved)
    return AppConfig::instance().backend().imageUrlExternal(raw, 325, 325);
}
