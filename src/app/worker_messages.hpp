#pragma once
#include "../artwork/identified_artwork.hpp"
#include "../artwork/download_art.hpp"
#include "../artwork/bulk_covers.hpp"
#include "../artwork/steamgriddb.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/collections.hpp"
#include "../library/drm_free.hpp"
#include "../library/rawg.hpp"
#include "../library/screenscraper.hpp"
#include "../library/shortcuts.hpp"
#include "../library/steam_achievements.hpp"
#include "../library/store_adapter.hpp"
#include "../model/devices.hpp"
#include "../platform/audio.hpp"
#include "../platform/game_activity.hpp"
#include "../platform/sensors.hpp"
#include "../platform/steam_library_sync.hpp"
#include "../store/ea_account.hpp"
#include "../plugins/jobs.hpp"
#include "../store/web_import.hpp"
#include "../ui/screensaver.hpp"
#include "../ui/image_decoder.hpp"
#include "../net/glance.hpp"
#include "../net/companion_cognition.hpp"
#include "../net/companion_weather.hpp"
#include "../net/companion_backup.hpp"
#include "../net/companion_events.hpp"
#include "../net/companion_tools.hpp"
#include "../net/elevenlabs.hpp"
#include "../ui/companion_view.hpp"
#include <Windows.h>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#define LAUNCHME_MESSAGE(id, offset, type, handler) inline constexpr UINT id = WM_APP + offset;
#include "worker_messages.def"
#undef LAUNCHME_MESSAGE

// List-view descriptions: fetched after focus settles, with language-specific disk caching.
struct PreviewMessage {
    uint32_t generation = 0;
    uint32_t app_id = 0;
    GameDetails details;
};
struct RatingsMessage {
    uint32_t generation = 0;
    ratings::Map ratings;
    rawg::Map metadata;
    std::unordered_map<std::string, GameDetails> emulation; // By gameKey: ES-DE gamelist.xml / ScreenScraper details.
    std::unordered_map<std::string, std::wstring> names;    // By gameKey: titles matched by ROM file (AppSettings::emulation_names).
    std::unordered_set<std::string> manual;                  // By gameKey: names the player picked in Zidentyfikuj grę (always shown).
    // Games ScreenScraper just identified, with the artwork slots still empty on disk: retried now.
    std::vector<std::pair<Game, std::vector<ArtworkKind>>> artwork_refresh;
    std::wstring screenscraper_error; // Shown once: rejected credentials, quota, service closed.
};
// Settings → ScreenScraper → "Sprawdź połączenie": ssuserInfos.php answered (or not).
struct ScreenScraperTestMessage {
    bool ok = false;
    std::wstring text;
};
// The artwork cache re-encoded once at the sizes the launcher draws (artwork/image_compact.hpp).
struct CacheCompactedMessage {
    uint64_t saved = 0;
};
struct DownloadArtMessage {
    std::string key;
    std::vector<uint8_t> bytes;
};
struct BulkCoversMessage { uint64_t generation = 0; bulk_covers::Progress progress; };
struct SteamSyncMessage { steam_sync::Result result; };
struct GlanceMessage {
    enum class Kind { feed, image, done } kind = Kind::feed;
    uint32_t generation = 0;
    int section = 0;
    size_t index = 0;
    glance::Feed feed;
    std::vector<uint8_t> bytes;
    bool failed = false;
};
// Towarzyszka (src/app/companion_request.cpp, received by companion_answer.cpp): one answer, from
// the model or offline.
struct CompanionMessage {
    // summary: a finished chat in a sentence or two, for her memory (no reply shown); reading: her
    // background web reading (daily_readings, no reply); life: her diary filling in what she did
    // between talks (reply.raw: <epizody>, summary_look and life_from/life_to; no reply shown).
    // weather: the real weather where she is (companion_weather.hpp), fetched in the background; no reply.
    enum class Purpose { home, chat, test, summary, reading, life, weather } purpose = Purpose::chat;
    std::optional<companion::weather::Report> weather;
    uint64_t life_from = 0, life_to = 0;
    std::vector<std::wstring> notes; // Facts about the player she chose to remember (her tool).
    std::vector<companion::Memory::Entry> affective_notes; // Notes with her emotional reaction & feeling attached.
    uint32_t generation = 0;
    companion::Reply reply;    // The model's answer; ok == false: it failed (status, error).
    companion::Reply fallback; // Her offline line when no model answered.
    companion::Digest library;
    bool connected = false;    // Host and token were found: the model was asked.
    std::string model;
    companion::Provider provider = companion::Provider::foundry;
    std::wstring label;        // "Gemini 3.8 Flash · Foundry", "llama3.1 · Ollama".
    companion::Source token_source = companion::Source::none;
    uint32_t elapsed_ms = 0;
    std::optional<companion::Look> guest; // The guest the reply was written with (her lines are signed).
    // partial: her words so far while the reply streams in (heard, raw, tags and all); only
    // generation and purpose are set besides. The whole reply follows as usual.
    bool partial = false;
    std::wstring heard;
    std::wstring week;        // This conversation's weekly small talk (companion_week.hpp), when it was due.
    std::string summary_look; // summary: whose conversation it was.
    std::wstring player_text;
    std::string social_turn;
    companion::social::Plan social_plan;
    uint64_t memory_epoch = 0;
    std::vector<std::string> reflection_evidence;
    uint64_t reflection_cutoff = 0;
    std::vector<companion::tools::CollectionMutation> collection_mutations; // kolekcje_i_ulubione: applied on the UI thread.
    std::optional<companion_ui::Roulette> tool_roulette;                    // zakrec_ruletka: spun in the chat.
    std::vector<companion::cognition::Reading> daily_readings;              // Autonomous daily Tavily readings.
    std::string reply_look;   // chat: whose reply it is (her mind takes it in), the guest's too.
    std::string guest_look;
    // Where the time went (the chat's diagnostics: "…" → Pokaż diagnostykę).
    struct Timing {
        uint64_t asked = 0;      // GetTickCount64 when the request went to the worker.
        uint32_t queued_ms = 0;  // Until the worker started on it.
        uint32_t prepare_ms = 0; // The library digest and local Steam stats.
        uint32_t model_ms = 0;   // Every call to the model and her tools.
        size_t system_chars = 0, turns = 0, turn_chars = 0;
        bool tools = false, burst = false;
        companion::Trace trace;
        std::vector<std::string> workspace; // The parts of her mind that entered her prompt (mind::workspaceSources).
        companion::CuriousMove curiosity = companion::CuriousMove::none; // This reply's curiosity urge.
    } timing;
    bool noted = false; // In the diagnostics already (a roulette verdict comes back once the reel stops).
};
// Her voice (src/app/companion_voice.cpp): the ElevenLabs key's check (and the account's voices),
// or one synthesized chunk of her line `generation` (`last`: the line is complete).
struct CompanionVoiceMessage {
    enum class Kind { status, audio } kind = Kind::status;
    bool ok = false;                       // status: the key works.
    std::vector<elevenlabs::Voice> voices; // status: the account's voices (may stay empty).
    elevenlabs::Problem problem = elevenlabs::Problem::none; // Both: what went wrong.
    uint32_t status = 0;                   // Both: its HTTP status.
    uint32_t generation = 0;               // audio
    AudioClip clip;                        // audio: empty when synthesis failed.
    int speaker = -1;                      // audio: companion::Look index of the speaker (-1: host).
    bool last = false;                     // audio
    std::string voice;                     // audio: the voice that spoke.
    std::vector<std::string> refused;      // audio: voices her plan refused on the way.
};
// Her photo (src/app/companion_photos.cpp): the picture for this chat; empty when none was picked.
struct CompanionFeedMessage {
    enum class Kind { archive, image, social, private_message } kind = Kind::archive;
    uint32_t generation = 0;
    std::string look, image_id;
    std::optional<companion::feed::State> archive;
    std::optional<companion::feed::Social> social;
    std::wstring submitted;
    std::wstring status;
    std::vector<uint8_t> bytes;
    bool complete = true;
    uint32_t width = 0, height = 0;
};
struct CompanionBackupMessage { companion::backup::Result result; std::wstring name; };
struct CompanionPhotoMessage {
    uint32_t chat = 0;          // The chat it was picked for (companion_.photo_chat).
    std::vector<uint8_t> bytes;
    std::wstring description;   // What it shows (<stem>.txt beside it), else empty.
    std::wstring file;          // Relative to the photo folder (kept in the chat's history).
};
// The game index changed (src/app/game_index.cpp); posted at most once until handled.
struct GameIndexMessage {
    uint64_t revision = 0;
};
// Towarzyszka's reactions (src/app/companion_events.cpp): a Steam game's progress when its session
// starts (baseline) or ends (session), or today's premieres from Glance's calendar.
struct CompanionEventMessage {
    enum class Kind { baseline, session, premieres } kind = Kind::baseline;
    std::string key;                  // baseline, session: the game.
    std::wstring title;               // session
    uint64_t played_ms = 0;           // session
    companion::Progress progress;     // baseline: before playing; session: after it.
    std::vector<std::wstring> titles; // premieres
};
// Downloaded videos moved to a new folder (Biblioteka → Folder filmów).
struct VideoMoveMessage {
    size_t moved = 0, kept = 0, failed = 0;
};
// The video of the game focused for 3 s (src/app/game_video.cpp); empty file: none.
struct VideoMessage {
    uint32_t generation = 0;
    std::string key;
    std::filesystem::path file;
    bool silent = false; // Steam trailers: muted, the launcher's music goes on.
};
struct IdentifyMessage {
    uint32_t generation = 0;
    bool identified = false;
    rawg::Search search;
    std::vector<screenscraper::GameInfo> found;    // ScreenScraper search: the game of each row.
    std::optional<screenscraper::GameInfo> picked; // ScreenScraper identification, saved.
    std::optional<rawg::Metadata> metadata;
    IdentifiedArtworkResult artwork;
    std::wstring error;
};
struct AchievementsMessage {
    uint32_t generation = 0;
    bool icon = false;
    bool final = true; // List: no fuller list follows.
    SteamAchievements list;
    size_t index = 0; // Icon: its row.
    std::wstring url; // Icon: the row's URL when it was asked for (a replaced list may reorder rows).
    std::vector<uint8_t> bytes;
};
struct ScreensaverMessage {
    uint32_t generation = 0;
    bool scan = false;
    std::vector<std::pair<Game, screensaver::Artwork>> games;
    uint32_t sequence = 0;
    std::vector<uint8_t> bytes; // Empty when unreadable.
};
struct DashboardMessage {
    uint32_t generation = 0;
    sensors::Reading sensors;
    bool discord_running = false;
};
// DRM-free folder scan (src/app/drm_free.cpp): progress, then the scanned catalog, then
// RAWG identifications as they complete. Catalogs are merged into the current one on arrival.
struct DrmFreeMessage {
    uint32_t generation = 0;
    enum class Kind { progress, scanned, identified, finished } kind = Kind::progress;
    drm_free::Progress progress;
    drm_free::Catalog catalog;
    std::vector<std::string> artwork_reset; // Game keys whose automatic artwork the worker removed.
    rawg::Map metadata;                     // Newly identified games: description, developer, rating.
};
struct CustomGamesMessage {
    uint32_t generation = 0;
    std::unordered_map<std::string, bool> installed;
};
struct GameActivityMessage {
    uint32_t generation = 0;
    uint64_t observed_at = 0;
    std::vector<std::pair<std::string, GameActivity>> activities;
    uint64_t focus_request = 0;
    std::vector<GameProcess> focus_processes; // Worker-found identities for a pending launch handoff.
};
// Library games found running (findRunningGames), however they were started.
struct RunningGamesMessage {
    uint32_t version = 0; // Candidate list the scan used; an older one is ignored.
    std::vector<Game> games;
};
struct CropMessage {
    uint32_t generation = 0;
    std::filesystem::path image;
    std::vector<uint8_t> bytes;
    CropFocus focus, automatic;
    int width = 0, height = 0;
    std::wstring error;
};
struct PcLibraryRequest {
    std::vector<Game> gog_owned, epic_owned, previous_battlenet;
    std::string gog_account, epic_account;
    std::vector<std::filesystem::path> multimc; // Launchers the user pointed at (Ustawienia → MultiMC / Prism).
};
struct PcLibraryMessage {
    uint32_t generation = 0;
    bool failed = false;
    StoreLibrary library;
    std::optional<web_import::Snapshot> ea_import, battlenet_import;
};
struct HeadphonesMessage {
    bool image = false, connected = false;
    std::vector<uint8_t> bytes;
    std::optional<int> battery;
};
struct ControllersMessage {
    std::vector<devices::Controller> controllers;
    devices::Picture image = devices::Picture::none; // Non-none: image-only update.
    std::vector<uint8_t> bytes;
};
struct StoreImportMessage {
    uint32_t generation = 0;
    GameSource source = GameSource::ea;
    bool cleared = false;
    bool invalid = false;
    bool save_failed = false;
    ea::Failure failure = ea::Failure::none;
    web_import::Snapshot snapshot;
};
struct ArtworkBrowserMessage {
    // local: an image file from disk; preview: a video wallpaper's moving preview, downloaded.
    enum class Type { list, thumbnail, apply, search, local, preview } type = Type::list;
    ArtworkKind kind = ArtworkKind::cover;
    uint32_t generation = 0;
    GridPage page;
    GridSearch search;
    uint32_t asset_id = 0;
    uint32_t applied_id = 0;
    std::vector<uint8_t> bytes;
    CropFocus focus;
    std::wstring error;
    // The wallpaper picker: the stored wallpaper (apply, local) or preview (preview), and whether it is a video.
    bool wallpaper = false, video = false;
    std::filesystem::path file;
};
struct StoreMessage {
    uint32_t generation = 0;
    uint64_t account = 0;
    bool details = false;
    bool info_ready = false;
    bool installation_checked = true;
    plugins::CatalogResult search;
    GameDetails info;
    std::optional<Game> installed;
};
using PluginsMessage = plugins::Result;
struct LibraryWorkerErrorMessage {
    bool artwork = false;
};
struct StoreChangeMessage {
    bool pc = true; // PC store installations (uninstall registry, store manifests).
    std::wstring uninstalled; // A game Launchme uninstalled itself (Legendary): its name.
    bool failed = false;
};
// Phone access (companion_remote.cpp): work its server thread hands to the UI thread.
class Launcher;
struct RemoteMessage {
    std::function<void(Launcher&)> run;
};
struct LibraryMessage {
    bool installation_update = false;
    bool refresh = false; // Full snapshot of the unchanged account, asked for by the UI (steam_rescan_).
    uint64_t account = 0;
    std::vector<Game> games;
    std::vector<Game> private_games;
    std::vector<SteamCollection> collections;
    std::unordered_set<uint32_t> excluded;
    std::vector<ShortcutGame> shortcuts;
    std::unordered_map<uint32_t, std::wstring> cached_names;
};
struct CoverMessage {
    std::string key;
    std::wstring badge;
    uint32_t generation = 0;
    HeroTone hero_tone = HeroTone::full;
    bool prefetched = false;
    bool cache_miss = false; // No bytes: the disk lane found no poster and handed the job to the downloaders.
    uint32_t revision = 0;
    // Posters/landscapes are kept in RAM only when a Flix card asked for them.
    enum class Kind { cover, logo, hero, badge, poster, landscape, disc } kind = Kind::cover;
    std::vector<uint8_t> bytes;
    CropFocus focus;
};
struct FriendsMessage {
    uint32_t generation = 0;
    FriendsResult result;
};
struct AvatarMessage {
    uint64_t id = 0;
    uint32_t generation = 0;
    std::vector<uint8_t> bytes;
};
struct AudioMessage {
    SoundCue cue = SoundCue::navigation;
    AudioClip clip;
    uint32_t generation = 0;
    bool preview = false;
};
struct MetadataMessage {
    uint64_t account = 0;
    uint32_t generation = 0;
    bool finished = true;
    std::unordered_map<uint32_t, std::wstring> names;
};
