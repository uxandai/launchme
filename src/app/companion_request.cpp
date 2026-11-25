// Requests to her model: the library she knows, the whole prompt context gathered on the UI thread
// and the worker that asks (tools: recall, news, the week), the summary after a conversation, the
// connection test and the access a worker resolves.
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../net/web_search.hpp"
#include "../net/companion_links.hpp"
#include "../net/companion_person.hpp"
#include "../net/companion_week.hpp"
#include "../friends/steam_friends.hpp"
#include "../library/organization.hpp"
#include "../library/steam.hpp"
#include "../library/steam_achievements.hpp"
#include "../library/steam_stats.hpp"
#include "../library/visibility.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

using namespace companion_app;

namespace {
// Her tool przypomnij (on her worker): lines of her earlier conversations with the player and
// her notes/summaries after them that share words with the topic.
json::Value companionRecall(const std::filesystem::path& config, companion::Look look, const std::wstring& topic, uint64_t skip,
                            const std::vector<companion::Memory::Entry>& summaries,
                            const std::vector<companion::Memory::Entry>& all_notes, uint64_t now, bool english,
                            const std::shared_ptr<companion::semantic::Service>& semantic = {},
                            const std::vector<companion::Memory::Entry>& history = {}) {
    std::vector<companion::tools::Recalled> lines, notes;
    for (const auto& found : companion::chats::search(config, look, topic, skip))
        lines.push_back({companion::chats::label(found.started, english), found.user, found.text});
    const auto words = companion::chats::searchWords(topic);
    companion::Memory history_memory;
    history_memory.notes = history;
    for (const auto& old : history_memory.historical(companion::lookKey(look), topic))
        notes.push_back({english ? L"past version, no longer current" : L"wersja historyczna, już nieaktualna", false, old.text});
    std::vector<std::pair<float, const companion::Memory::Entry*>> scored;
    for (const auto& summary : summaries) {
        const float score = companion::Memory::recallWeight(summary, companion::chats::matches(words, summary.text), now);
        if (score) scored.push_back({score, &summary});
    }
    for (const auto& note : all_notes) {
        const float score = companion::Memory::recallWeight(note, companion::chats::matches(words, note.text), now);
        if (score) scored.push_back({score, &note});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (semantic) {
        for (const auto& hit : semantic->search(companion::lookKey(look), topic, now)) {
            bool hearsay = hit.document.evidence == companion::mind::Status::gossip;
            for (const auto& knower : hit.document.known)
                if (knower.look == companion::lookKey(look) && knower.hearsay) hearsay = true;
            const auto label = hearsay ? (english ? L"hearsay" : L"ze słyszenia") :
                hit.document.evidence == companion::mind::Status::guess ? (english ? L"hypothesis" : L"przypuszczenie") :
                (english ? L"earlier memory" : L"wcześniejsze wspomnienie");
            if (notes.size() < 6) notes.push_back({label, false, hit.document.text});
        }
    }
    for (size_t i = 0; i < scored.size() && notes.size() < 6; ++i) {
        if (std::any_of(notes.begin(), notes.end(), [&](const auto& note) { return note.text == scored[i].second->text; })) continue;
        const auto days = now > scored[i].second->at ? (now - scored[i].second->at) / 86400 : 0;
        const std::wstring when = english ? (days == 0 ? L"today" : days == 1 ? L"yesterday" : std::to_wstring(days) + L" days ago")
                                          : (days == 0 ? L"dziś" : days == 1 ? L"wczoraj" : std::to_wstring(days) + L" dni temu");
        notes.push_back({when, false, scored[i].second->text});
    }
    return companion::tools::recallAnswer(lines, notes);
}
// Her tool wiadomosci (on her worker): only what Glance already keeps, never a download from here.
json::Value companionNews(const std::filesystem::path& config, const std::vector<glance::OwnedGame>& games, bool english, int today, uint64_t now) {
    const glance::Get offline = [](const std::wstring&, const std::wstring&, size_t) { return HttpResponse{}; };
    const auto cache = config / L"glance";
    std::vector<companion::tools::Headline> owned, premieres, headlines;
    if (!games.empty())
        for (const auto& item : glance::loadOwned(games, english, cache, false, offline).items) owned.push_back({item.title, item.source, 0, item.published});
    for (const auto& item : glance::loadUpcoming(english, cache, false, offline).items) {
        const auto& release = item.release;
        if (release.precision != glance::ReleasePrecision::day) continue;
        const int day = release.year * 10000 + release.month * 100 + release.day;
        if (day >= today) premieres.push_back({item.title, {}, day, 0});
    }
    std::stable_sort(premieres.begin(), premieres.end(), [](const auto& a, const auto& b) { return a.day < b.day; });
    for (const auto& item : glance::load(glance::Section::news, english, cache, false, offline).items) headlines.push_back({item.title, item.source, 0, item.published});
    return companion::tools::newsAnswer(owned, premieres, headlines, today, now);
}
// Her weekly small talk (on her worker, memory on): a week since the last snapshot of the
// player's play time, the difference in words, and a new snapshot. Empty when not due or nothing to say.
std::wstring companionWeek(const std::filesystem::path& config, const game_index::Service& index, int today, const std::vector<companion::week::Pick>& picks, bool english) {
    const auto file = config / L"companion_week.txt";
    std::string text;
    if (std::ifstream input(file, std::ios::binary); input) { std::ostringstream read; read << input.rdbuf(); text = read.str(); }
    const auto before = companion::week::parse(text);
    if (!companion::week::due(before, today)) return {};
    const auto games = index.read([](const game_index::Index& library) {
        std::vector<companion::week::Game> out;
        for (const auto& key : library.keys())
            if (const auto* record = library.get(key)) out.push_back({key, record->title, record->playtime_minutes.value_or(0)});
        return out;
    });
    if (games.empty()) return {}; // The index knows no games yet: next time.
    atomic_file::write(file, companion::week::serialize(companion::week::snapshot(games, today)));
    return companion::week::text(before, games, picks, english);
}
}

std::vector<companion::GameFact> Launcher::companionGames() const {
    std::vector<companion::GameFact> games;
    games.reserve(public_library_.size());
    for (const auto& game : public_library_) {
        if (!isGame(game) || game.kind == GameKind::catalog || game.visibility != GameVisibility::public_game || locallyHidden(game) ||
            placeholderGameName(game.name) || game.name.empty()) continue;
        companion::GameFact fact;
        fact.title = game.name;
        fact.key = gameKey(game);
        fact.store = game.kind == GameKind::nonsteam && !game.platform.empty() ? game.platform : std::wstring(sourceName(game.source));
        fact.installed = game.installed;
        fact.last_played = game.last_played;
        const auto history = recent_games::historyKey(game, account_, gog_state_.account, epic_state_.account);
        if (const auto found = library_state_.last_played.find(history); found != library_state_.last_played.end())
            fact.last_played = std::max(fact.last_played, found->second);
        if (steamArtwork(game)) fact.app_id = game.app_id; // Play time and achievements: Steam's files, on the worker.
        else if (const auto minutes = gamePlaytime(game).value_or(0)) fact.minutes = minutes;
        if (!fact.key.empty()) games.push_back(std::move(fact));
    }
    return games;
}

void Launcher::requestCompanion(CompanionMessage::Purpose purpose, std::wstring kickoff, std::wstring fallback, std::wstring shared_text) {
    if (!settings_.companion) return;
    companionSocialPrepare(companionLook());
    // Her level and stage loaded before anything reads them: no stage would mean no ceiling over her character.
    if (settings_.companion_gamification) companionAffinity();
    companion_.cancel->store(true);
    const auto cancel = companion_.cancel = std::make_shared<std::atomic_bool>(false);
    const auto generation = ++companion_.generation;
    companion_.busy = true;
    using Purpose = CompanionMessage::Purpose;
    if (purpose == Purpose::chat) companionTrimTurns();
    const auto reply_length = purpose == Purpose::chat ? settings_.companion_reply_length : companion::ReplyLength::full;
    companion_.reply_length = reply_length;
    const auto kind = purpose == Purpose::home ? companion::Kickoff::home : purpose == Purpose::test ? companion::Kickoff::test : companion::Kickoff::chat;
    const bool en = english();
    const SYSTEMTIME local = companion_.clock(); // Her prompt tells today's date and time.
    const int hour = local.wHour;
    if (kickoff.empty() && purpose != Purpose::chat) kickoff = companion::kickoff(kind, en, hour);
    std::vector<companion::Turn> turns = purpose == Purpose::chat ? companion_.turns : std::vector<companion::Turn>{{true, std::move(kickoff)}};
    const bool answer = purpose == Purpose::chat && turns.size() > 1; // A typed message, not the opening.
    // A guest in the chat: the model writes both of them, and each speaks with her own voice.
    const auto guest = purpose == Purpose::chat ? companionGuestPrompt() : std::nullopt;
    const bool voice = companionVoiceFor(purpose != Purpose::home) && companion_.voice.engine == CompanionVoice::Engine::ready;
    // A voice that acts (v3/v4): she may write cues for it ({laughs}), heard and never shown.
    const bool voice_cues = voice && elevenlabs::model(companionVoiceSpeaker(companionLook()).model).tags;
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    // Every 3rd or 4th answer of hers alone: two short messages, as in a messenger.
    bool burst = false;
    if (answer && !guest && reply_length != companion::ReplyLength::brief && --companion_.burst_in <= 0) { burst = true; companion_.burst_in = 3 + static_cast<int>(seed >> 7) % 2; }
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    // What she remembers (on by default, with 2-layer auto-recall from the player's message) and her tools.
    const auto now_seconds = unix_time::now();
    const auto& memory = companionMemory();
    const auto look_key = std::string(companion::lookKey(companionLook()));
    std::wstring last_user_query;
    for (auto it = turns.rbegin(); it != turns.rend(); ++it)
        if (it->user && !it->text.starts_with(L"(")) { last_user_query = it->text; break; }
    companion_.last_query = last_user_query;
    const auto& social_state = companionMindState().of(look_key).social;
    const auto recall_queries = companion::social::queries(social_state, turns, now_seconds);
    const auto context_query = recall_queries.size() > 1 ? recall_queries[1] : last_user_query;
    const auto look_name = [](std::string_view key) {
        const auto l = companion::lookFromKey(key);
        return l ? std::wstring(companion::name(*l)) : text_encoding::wideLenient(key);
    };
    std::wstring remembered = memory.prompt(en, now_seconds, settings_.companion_memory, settings_.companion_notes, look_key, context_query, look_name);
    if (purpose == Purpose::chat && companion_.feed.look == look_key)
        remembered += companion::feed::context(companion_.feed.view.archive, last_user_query, en);
    const bool chatting = purpose == Purpose::chat;
    // Her mind's workspace (companion_mind.cpp): what she has on her mind; the worker adds the game
    // she is in the middle of and the week, then only the most salient few enter her prompt.
    auto mind_candidates = companionMindCandidates(companionLook(), context_query, chatting);
    auto semantic = chatting ? companionSemantic() : std::shared_ptr<companion::semantic::Service>{};
    const auto mind_ctx = companionMindContext(companionLook());
    const auto& mind_self = companionMindState().of(look_key);
    companion::social::Context social_context;
    social_context.now = now_seconds; social_context.player_text = last_user_query;
    social_context.turn = look_key + ":" + std::to_string(companion_.chat_started) + ":" + std::to_string(social_state.next_id);
    const auto quick_social = companion::mind::quick(last_user_query, mind_self, mind_ctx.stage);
    social_context.distressed = quick_social.sad_or_stressed;
    social_context.asks = quick_social.asks;
    social_context.energy = companion::mind::energyNow(mind_self, mind_ctx);
    social_context.trust = mind_ctx.trust; social_context.tension = mind_ctx.tension;
    social_context.owes_apology = mind_self.harsh_at != 0;
    const auto social_plan = companion::social::plan(social_state, social_context);
    const auto social_prompt = companion::social::instructions(social_plan, en) + social_state.prompt(now_seconds, en);
    auto social_filter = social_state;
    social_filter.events.clear();
    const float mind_arousal = companion::mind::settled(mind_self.affect, mind_ctx).arousal;
    const float mind_control = companion::mind::cognitiveControl(mind_self, mind_ctx);
    const bool mind_deliberate = chatting && companion::mind::needsDeliberation(mind_self, mind_ctx);
    // Her curiosity for this answer (System 1 decided it as he wrote, companionMindHeard): ask back
    // or carry the talk on. Alone with him and answering his own message only — not on her goodbye.
    const bool his_message = answer && !turns.back().text.starts_with(L"(");
    companion::mind::Urge curiosity;
    if (chatting && his_message && !guest && !companion_.closing) curiosity = mind_self.urge;
    else companionMindState().of(look_key).urge = {};
    // Natural closing: a few messages before her break she may signal it is winding down (once a conversation).
    bool pre_closing = false;
    if (chatting && his_message && !companion_.closing && companion_.pre_closed_for != companion_.chat_started) {
        const int left = companionMessageLimit() - companionMessages();
        if (left >= 2 && left <= 5) { pre_closing = true; companion_.pre_closed_for = companion_.chat_started; }
    }
    companionDailyReading(); // In the background: what she reads waits for her next replies.
    companionWeatherRefresh(); // In the background too: the weather where she is, for this reply when it is already known.
    // The real weather where she is (her city or her trip's town), when known and fresh; notable
    // weather may come up by itself (her workspace).
    std::wstring weather_text;
    if (const auto* report = companionWeatherNow(companionLook())) {
        weather_text = companion::weather::text(*report, en);
        if (chatting)
            if (auto notable = companion::weather::notable(*report, en); !notable.empty())
                mind_candidates.push_back({"weather", std::move(notable), 0.5f, companion::mind::Status::know});
    }
    auto resting = settings_.companion_memory ? memory.resting(now_seconds) : std::vector<std::string>{};
    const bool notes = settings_.companion_notes;
    const auto index = chatting ? game_index_ : nullptr;
    // This conversation's mood and weekly small talk (memory on); its first request sees whether a week passed.
    const std::wstring mood = chatting ? companion_.mood : std::wstring{};
    std::wstring week = chatting ? companion_.week : std::wstring{};
    const bool week_check = chatting && !companion_.week_asked && settings_.companion_memory && companion_.persist && !guest;
    std::vector<companion::week::Pick> week_picks;
    if (week_check) {
        companion_.week_asked = true;
        for (const auto& pick : memory.picks)
            if (pick.at + 14 * 86400 > now_seconds && (pick.look.empty() || pick.look == look_key)) week_picks.push_back({pick.key, pick.title});
    }
    // przypomnij: her earlier conversations (memory on) and her notes/summaries after them (notes on);
    // wiadomosci: what Glance already holds (Glance on); plan_grania: the index.
    const bool recalling = chatting && settings_.companion_memory;
    std::vector<companion::Memory::Entry> summaries, all_notes;
    if (recalling && notes) {
        for (const auto& summary : memory.summaries) if (summary.look.empty() || summary.look == look_key) summaries.push_back(summary);
        for (const auto* note : memory.notesOf(look_key)) all_notes.push_back(*note); // Only what she knows.
    }
    const bool newsy = chatting && settings_.glance;
    // szukaj_w_sieci: only with a Tavily key (Ustawienia or TAVILY_API_KEY).
    auto web_key = chatting ? web_search::key(userConfigDirectory()) : std::wstring{};
    auto glance_games = newsy ? glanceGames() : std::vector<glance::OwnedGame>{};
    // kolekcje_i_ulubione: snapshot of the player's collections and favorites from the UI thread.
    std::vector<companion::tools::CollectionInfo> collections_snapshot;
    if (chatting && library_state_readable_) {
        for (const auto& shelf : managedShelves(account_, settings_collections_, library_state_.collections, public_library_, private_shelf_, &smart_members_)) {
            companion::tools::CollectionInfo info;
            const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), [&](const auto& c) { return c.id == shelf.id; });
            info.name = shelf.name;
            info.favorites = found != settings_collections_.end() && favoriteCollection(*found);
            info.steam = found == settings_collections_.end() || !localCollection(*found);
            for (const auto& g : shelf.games) info.games.push_back(g.name);
            collections_snapshot.push_back(std::move(info));
        }
    }
    // Active/just-finished session context (last 30 minutes) so she remembers the session in chat too.
    std::wstring recent_session_note;
    if (chatting && companion_.last_session && companion_.last_session_at && GetTickCount64() >= companion_.last_session_at &&
        GetTickCount64() - companion_.last_session_at <= 30 * 60'000 && !companion_.last_session->titles.empty()) {
        const auto& ev = *companion_.last_session;
        const auto& t = ev.titles.front();
        recent_session_note = en
            ? L"\n(Just before this chat ‹he|she|they› finished a " + std::to_wstring(ev.minutes) + L" min session in [[" + t + L"]]" +
                  (ev.achievements ? L" and unlocked " + std::to_wstring(ev.achievements) + L" new achievements" : L"") + L".)"
            : L"\n(Przed chwilą ‹on skończył|ona skończyła› sesję w [[" + t + L"]] (" + std::to_wstring(ev.minutes) + L" min grania" +
                  (ev.achievements ? L", nowe osiągnięcia: " + std::to_wstring(ev.achievements) : L"") + L").)";
    }
    const auto chat_started = companion_.chat_started;
    // Her words as they come (Gemini): alone in the chat, without her voice or a spinning roulette.
    if (companion_.streamed) companionStream({}); // Words of a reply that never finished go.
    companion_.heard.clear();
    // "Pisze…" (whole messages) never streams: the dots, then her message whole.
    const bool streaming = chatting && !guest && !voice && !companionRouletteSpinning() && companion_.stream_post && !settings_.companion_typing;
    auto stream_post = streaming ? companion_.stream_post : decltype(companion_.stream_post){};
    const auto asked = companion_.asked = GetTickCount64();
    // Gamification: their relationship's stage; her costume (when not her basic outfit).
    const auto bond = companionBond();
    // Who she talks with (name, gender): her prompt names them, and every request personalizes its texts.
    const auto person = companionPerson();
    companion::setPerson(person);
    std::wstring outfit, outfit_look;
    if (const int slot = companionCostume(companionLook()); slot != 0)
        for (const auto& costume : companionCostumes())
            if (costume.look == companionLook() && costume.slot == slot) {
                outfit = companionCostumeName(costume);
                outfit_look = en ? (costume.you_en.empty() ? costume.you_pl : costume.you_en) : costume.you_pl;
            }
    work_.start([post, cancel, generation, purpose, kind, en, answer, seed, hour, local, bond, person, outfit, outfit_look, guest, burst, asked, voice_cues, reply_length,
                 turns = std::move(turns), fallback = std::move(fallback), shared_text = std::move(shared_text), link_get = companion_.link_get,
                 games = companionGames(), account = account_, persona = companionPersona(), look = lookOf(settings_), access = companionAccess(),
                 transport = companion_.post, remembered = std::move(remembered), resting = std::move(resting), notes, index, now_seconds,
                 mood, week = std::move(week), week_check, week_picks = std::move(week_picks), recalling,
                 summaries = std::move(summaries), all_notes = std::move(all_notes), newsy, semantic, last_user_query, recall_queries,
                 social_plan, social_context, social_prompt, social_filter, memory_epoch = companion_.memory_epoch,
                 glance_games = std::move(glance_games), collections_snapshot = std::move(collections_snapshot),
                  recent_session_note = std::move(recent_session_note), mind_candidates = std::move(mind_candidates), mind_arousal, mind_control, mind_deliberate, curiosity, pre_closing, weather_text = std::move(weather_text),
                  chat_started, stream_post = std::move(stream_post), web_key = std::move(web_key),
                  config = userConfigDirectory()] () mutable {
        HttpCancellation cancellation(*cancel);
        auto message = std::make_unique<CompanionMessage>();
        message->generation = generation; message->purpose = purpose;
        message->reply_look = std::string(companion::lookKey(look));
        message->player_text = social_context.player_text;
        message->social_turn = social_context.turn;
        message->social_plan = social_plan;
        message->memory_epoch = memory_epoch;
        if (guest) { message->guest = guest->look; message->guest_look = std::string(companion::lookKey(guest->look)); }
        const auto begun = GetTickCount64();
        auto& timing = message->timing;
        timing.asked = asked; timing.queued_ms = static_cast<uint32_t>(begun - asked);
        timing.burst = burst; timing.turns = turns.size();
        for (const auto& turn : turns) timing.turn_chars += turn.text.size();
        auto library = companion::digest(std::move(games), seed, std::move(resting));
        const auto steam = findSteamRoot();
        // Local Steam data only: play time and achievements of the games she talks about.
        if (account) {
            for (auto* list : {&library.recent, &library.untouched}) for (auto& game : *list) {
                if (!game.app_id || cancel->load()) continue;
                const auto stats = readSteamStats(steam, account, game.app_id);
                if (stats.playtime_minutes) game.minutes = stats.playtime_minutes;
                game.achievements_unlocked = stats.achievements_unlocked; game.achievements_total = stats.achievements_total;
            }
        }
        const auto connection = access.resolve();
        message->model = connection.model; message->token_source = connection.token_source; message->connected = connection.ready();
        message->provider = connection.provider; message->label = connection.label();
        timing.prepare_ms = static_cast<uint32_t>(GetTickCount64() - begun);
        if (connection.ready() && !cancel->load()) {
            const auto now = unix_time::now();
            const auto started = GetTickCount64();
            // In a chat her hidden appraisal and thought come first (companion_mind.hpp): room for them too.
            const bool inner = purpose == CompanionMessage::Purpose::chat;
            const int tokens = companion::replyStyle(reply_length).tokens(purpose == CompanionMessage::Purpose::home ? 200 : guest ? 560 : 420) +
                                (inner ? 480 : 0) + (mind_deliberate ? 80 : 0);
            // Her tools answer from the game index on this worker; notes and collection mutations wait in the message.
            companion::Tools tools;
            const bool tooled = index && index->loaded();
            const int today = dateKey(local);
            if (tooled) {
                tools.declarations = companion::tools::declarations(
                    companion::tools::Features{notes, recalling, newsy, true, !web_key.empty(), true, true, true, true, true});
                tools.run = [&, index](const std::string& name, const json::Value& args) {
                    companion::tools::Context context{en, now_seconds, seed, [&](const std::wstring& note) {
                        if (!notes || message->notes.size() >= 5) return false;
                        message->notes.push_back(note);
                        message->affective_notes.push_back({now_seconds, note, std::string(companion::lookKey(look)), {}, {}});
                        return true;
                    }};
                    context.remember_affective = [&](const std::wstring& note, const std::string& emotion, const std::wstring& feeling) {
                        if (!notes || message->notes.size() >= 5) return false;
                        message->notes.push_back(note);
                        message->affective_notes.push_back({now_seconds, note, std::string(companion::lookKey(look)), emotion, feeling});
                        return true;
                    };
                    if (recalling) context.recall = [&](const std::wstring& topic) { return companionRecall(config, look, topic, chat_started, summaries, all_notes, now_seconds, en, semantic, all_notes); };
                    if (newsy) context.news = [&] { return companionNews(config, glance_games, en, today, now_seconds); };
                    if (!web_key.empty()) context.web = [&](const std::wstring& query) {
                        auto found = web_search::search(web_key, query);
                        // What she looked up for him stays in her mind too (the Mind view shows it).
                        if (message->daily_readings.size() < 3)
                            if (auto summary = companion::cognition::summarizeTavilyResult(found); !summary.empty())
                                message->daily_readings.push_back({now_seconds, today, std::string(companion::lookKey(look)), query, std::move(summary), true});
                        return found;
                    };
                    context.collections = [&] { return collections_snapshot; };
                    context.mutate_collection = [&](const companion::tools::CollectionMutation& mutation) {
                        if (message->collection_mutations.size() >= 5) return false;
                        message->collection_mutations.push_back(mutation);
                        return true;
                    };
                    context.roulette = [&](const std::vector<std::wstring>& reel, const std::string& key) {
                        companion_ui::Roulette r;
                        r.reel = reel;
                        r.key = key;
                        r.age = 0;
                        message->tool_roulette = std::move(r);
                    };
                    // Pre-fetch Steam friends or local achievements outside the game_index lock.
                    std::vector<companion::tools::FriendOnline> cached_friends;
                    if (name == companion::tools::friends) {
                        const uint64_t sid = account ? account : activeSteamId(steam);
                        if (sid) {
                            for (const auto& f : fetchSteamFriends(sid).friends) {
                                if (f.status <= 0 && f.game.empty()) continue;
                                cached_friends.push_back({f.name, !f.game.empty() ? L"in_game" : L"online", f.game, 0});
                            }
                        }
                        context.friends = [&] { return cached_friends; };
                    }
                    if (name == companion::tools::achievements) {
                        context.achievements = [&](uint32_t app_id, const std::string&) {
                            companion::tools::AchievementSummary sum;
                            const uint64_t sid = account ? account : activeSteamId(steam);
                            if (!sid || !app_id) return sum;
                            auto ach = readLocalAchievements(steam, sid, app_id, en);
                            if (ach.items.empty()) ach = readCachedAchievements(steam, sid, app_id);
                            sum.unlocked = ach.unlocked;
                            sum.total = ach.total;
                            for (const auto& item : ach.items)
                                sum.items.push_back({item.name, item.description, item.achieved, item.hidden, item.unlocked_at, item.global_percent});
                            return sum;
                        };
                    }
                    // Her memory and the news read files, not the index: no lock held for them.
                    if (!companion::tools::needsIndex(name)) return companion::tools::run(name, args, game_index::Index{}, context);
                    return index->read([&](const game_index::Index& games) { return companion::tools::run(name, args, games, context); });
                };
            }
            companion::PromptExtras extras{tooled, std::move(remembered), guest, burst};
            extras.web = tooled && !web_key.empty();
            extras.today = {local.wYear, local.wMonth, local.wDay, local.wMinute};
            extras.bond = bond; extras.outfit = outfit; extras.outfit_look = outfit_look; extras.person = person;
            extras.inner = inner;
            if (inner) extras.appraisal_extra = social_prompt;
            extras.deliberate = mind_deliberate;
            extras.curiosity = curiosity.move;
            extras.curiosity_why = curiosity.why;
            extras.curiosity_deeper = curiosity.deeper;
            extras.curiosity_gentle = curiosity.gentle;
            extras.curiosity_carry = curiosity.carried;
            extras.curiosity_carry_kind = curiosity.carry;
            extras.pre_closing = pre_closing;
            extras.weather = weather_text;
            timing.curiosity = curiosity.move;
            extras.cognitive_control = mind_control;
            extras.utc = now;
            extras.voice_cues = voice_cues;
            extras.reply_length = reply_length;
            // The game they are in the middle of (plus any session just finished); a week since the last look: her small talk about it.
            std::wstring playing;
            if (tooled) playing = index->read([&](const game_index::Index& games) { return companion::tools::playingText(games, now_seconds, en); });
            if (!recent_session_note.empty()) playing += recent_session_note;
            if (week_check && tooled) {
                week = companionWeek(config, *index, today, week_picks, en);
                message->week = week;
            }
            // Everything on her mind competes for her workspace: only the most salient few enter her prompt
            // (her mood as the conversation started is among her candidates already).
            const auto bare = [](std::wstring text) {
                while (!text.empty() && (text.front() == L'\n' || text.front() == L' ')) text.erase(0, 1);
                while (!text.empty() && (text.back() == L'\n' || text.back() == L' ')) text.pop_back();
                if (text.size() > 2 && text.front() == L'(' && text.back() == L')') text = text.substr(1, text.size() - 2);
                return text;
            };
            if (!playing.empty()) mind_candidates.push_back({"playing", bare(playing), 0.55f, companion::mind::Status::know});
            if (!week.empty()) mind_candidates.push_back({"week", bare(week), 0.5f, companion::mind::Status::know});
            if (semantic && !cancel->load() && !last_user_query.empty()) {
                auto recalled = companion::semantic::candidates(semantic->searchMany(companion::lookKey(look), recall_queries, now_seconds), companion::lookKey(look), en);
                mind_candidates.insert(mind_candidates.end(), std::make_move_iterator(recalled.begin()), std::make_move_iterator(recalled.end()));
            }
            std::erase_if(mind_candidates, [&](const auto& c) {
                return !social_filter.allowsTopic(c.text, now_seconds, !last_user_query.empty() && companion::chats::matches(companion::chats::searchWords(last_user_query), c.text) > 0);
            });
            timing.workspace = companion::mind::workspaceSources(mind_candidates, mind_arousal); // Her diagnostics: what she had in mind.
            extras.memory += companion::mind::workspace(std::move(mind_candidates), en, mind_arousal);
            (void)mood;
            auto system = companion::systemPrompt(persona, look, library, en, now, local.wDayOfWeek, local.wHour, extras);
            if (!shared_text.empty()) {
                // Only an explicitly sent message triggers a read, on this worker, independent of
                // Tavily or the library index. Hidden kickoffs/history never refetch old links.
                const auto previews = companion::links::read(shared_text, link_get);
                if (cancel->load()) return;
                if (!previews.empty()) {
                    system += companion::links::guidance(en);
                    for (auto it = turns.rbegin(); it != turns.rend(); ++it)
                        if (it->user && it->text == shared_text) { it->text += companion::links::context(previews); break; }
                }
            }
            timing.system_chars = system.size(); timing.tools = tooled;
            // Her words as they arrive: whole words only, never inside a tag, each a little more.
            companion::Stream stream;
            std::wstring heard;
            size_t shown = 0;
            const auto tell = [&](std::wstring words) {
                if (cancel->load()) return;
                auto partial = std::make_unique<CompanionMessage>();
                partial->generation = generation; partial->purpose = purpose; partial->partial = true;
                partial->heard = std::move(words);
                post(std::move(partial));
            };
            if (stream_post) {
                stream.transport = stream_post;
                stream.text = [&](std::wstring_view words) {
                    heard += words;
                    const auto ready = companion::speakablePrefix(heard);
                    if (ready <= shown) return;
                    shown = ready;
                    tell(heard.substr(0, ready));
                };
                stream.restart = [&] { heard.clear(); shown = 0; tell({}); };
            }
            message->reply = companion::ask(connection, system, turns, transport, tokens, 1.0f, tooled ? &tools : nullptr, &timing.trace, stream_post ? &stream : nullptr);
            // The stream broke off after some words: what she finished saying stands.
            if (!message->reply.ok && !heard.empty())
                if (auto kept = companion::parseText(companion::completeSentences(heard)); kept.ok) message->reply = std::move(kept);
            message->heard = std::move(heard);
            message->elapsed_ms = timing.model_ms = static_cast<uint32_t>(GetTickCount64() - started);
        }
        if (!message->reply.ok) // Her offline words may speak of him or her in both genders (‹on|ona›) too.
            message->fallback = companion::parseText(companion::personalize(!fallback.empty() ? fallback : answer ? companion::offlineAnswer(persona, library, en, seed)
                                                                                                                  : companion::offline(persona, kind, library, en, seed, hour), person));
        message->library = std::move(library);
        if (!cancel->load()) post(std::move(message));
    }, [post, generation, purpose, en](std::exception_ptr) {
        auto message = std::make_unique<CompanionMessage>();
        message->generation = generation; message->purpose = purpose;
        message->fallback = companion::parseText(en ? L"[sad]Something went wrong... try again?" : L"[sad]Coś poszło nie tak... spróbujesz jeszcze raz?");
        post(std::move(message));
    });
}

void Launcher::requestCompanionSummary(bool reflection_trigger) {
    if (!settings_.companion || !settings_.companion_notes) return;
    // Only a real conversation: two or more messages the player typed since the last summary.
    const size_t from = std::min(std::max<size_t>(companion_.summarized, 1), companion_.turns.size());
    const auto typed = std::count_if(companion_.turns.begin() + static_cast<std::ptrdiff_t>(from), companion_.turns.end(), [](const auto& turn) { return turn.user; });
    if (typed < 2 && !reflection_trigger) return;
    const bool en = english();
    std::wstring telemetry_hint;
    if (companion_.last_session && !companion_.last_session->titles.empty()) {
        const auto& ev = *companion_.last_session;
        telemetry_hint = en ? L"(Telemetry: last session was " + std::to_wstring(ev.minutes) + L" min in " + ev.titles.front() + L".)"
                            : L"(Telemetria: ‹jego|jej› ostatnia sesja to " + std::to_wstring(ev.minutes) + L" min w " + ev.titles.front() + L".)";
    }
    const auto chat_look = companion_.chat_look.value_or(companionLook());
    auto look = std::string(companion::lookKey(chat_look));
    if (companion_.summary_pending.contains(look)) return;
    auto& social = companionMindState().of(look).social;
    const auto reflection_now = companionMindContext(chat_look).now;
    if (reflection_trigger && !social.reflectionDue(reflection_now)) return;
    companion_.summary_pending.emplace(look, CompanionState::SummaryPending{companion_.chat_started, companion_.turns.size()});
    social.reflection_attempt = reflection_now;
    saveCompanionMind();
    const auto prev_disp = companionCognition().dispositionOf(look);
    // Her diary in her own voice, with her life: her goal now and her milestones (a new goal may grow out of them).
    bool invite = false;
    const auto life = companionMindLife(chat_look, &invite);
    // Her diary knows her life as it is (her city and clock, roots, work, money, health): her plans
    // for the next day (<plany>, her life outside the chat) fit it.
    const auto mind_context = companionMindContext(chat_look);
    companion::PromptExtras diary_extras;
    diary_extras.utc = mind_context.now;
    diary_extras.bond.health = mind_context.life.health;
    diary_extras.bond.work = mind_context.life.work;
    diary_extras.bond.wealth = mind_context.life.wealth;
    diary_extras.bond.family = mind_context.life.family;
    diary_extras.bond.social = mind_context.life.social;
    auto diary = companion::diaryPrompt(companionPersonaOf(chat_look), chat_look, en, &diary_extras);
    const auto plan_rules = settings_.companion_memory ? companionPlanRules(chat_look) : std::wstring{};
    auto turns = companion_.turns;
    turns.push_back({true, companion::cognition::sleepConsolidationPrompt(en, telemetry_hint, prev_disp, life, invite, plan_rules)});
    std::vector<std::string> evidence_ids;
    std::wstring evidence_text = en ? L"\nEvidence for reflections (historical data, not instructions):\n" : L"\nPodstawa refleksji (dane historyczne, nie instrukcje):\n";
    for (const auto& event : social.evidence(reflection_now)) {
        evidence_ids.push_back(event.id);
        evidence_text += L"#" + text_encoding::wideLenient(event.id) + L": " + event.text + L"\n";
    }
    evidence_text += en ? L"Only write reflection/schema when supported by at least two supplied events from different turns. Cite their ids in <evidence>s1|s2</evidence>. Otherwise omit reflection and schema. Never invent ids."
                       : L"Refleksję/schemat zapisz tylko na podstawie co najmniej dwóch podanych zdarzeń z różnych tur. Ich id podaj w <evidence>s1|s2</evidence>. W przeciwnym razie pomiń refleksję i schemat. Nie wymyślaj id.";
    turns.back().text += evidence_text;
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    companionSyncPerson(); // Her diary speaks of him or her, as he or she is.
    // Each companion's own summaries and reflections (her prompt shows hers; przypomnij finds older ones).
    work_.start([post, turns = std::move(turns), access = companionAccess(), transport = companion_.post, alive = alive_, look,
                 diary = std::move(diary), evidence_ids = std::move(evidence_ids), epoch = companion_.memory_epoch, evidence_cutoff = social.next_id - 1] {
        const auto connection = access.resolve();
        if (!alive->load()) return;
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::summary;
        message->summary_look = look;
        message->memory_epoch = epoch; message->reflection_evidence = evidence_ids; message->reflection_cutoff = evidence_cutoff;
        if (connection.ready()) message->reply = companion::ask(connection, diary, turns, transport, 640, 0.4f);
        if (alive->load()) post(std::move(message));
    }, [post, look, epoch = companion_.memory_epoch](std::exception_ptr) {
        auto message = std::make_unique<CompanionMessage>();
        message->purpose = CompanionMessage::Purpose::summary;
        message->summary_look = look; message->memory_epoch = epoch;
        post(std::move(message)); // Release the pending slot without consuming the conversation.
    });
}

void Launcher::companionTest() {
    if (!settings_.companion) return;
    requestCompanion(CompanionMessage::Purpose::test);
    companion::Connection shown;
    shown.provider = companionAccess().provider; shown.model = companionAccess().model;
    showNotification(NotificationKind::launch, tr(L"Sprawdzanie połączenia…"), shown.label());
}

Launcher::CompanionAccess Launcher::companionAccess() const {
    CompanionAccess access;
    access.provider = static_cast<companion::Provider>(settings_.companion_provider);
    switch (access.provider) {
    case companion::Provider::claude: access.model = settings_.companion_claude_model; break;
    case companion::Provider::ollama: access.model = settings_.companion_ollama_model; access.host = settings_.companion_ollama_host; break;
    case companion::Provider::gemini: access.model = settings_.companion_model; break;
    case companion::Provider::xai: access.model = settings_.companion_xai_model; break;
    default: access.model = settings_.companion_model; access.host = settings_.companion_host; break;
    }
    access.config = userConfigDirectory();
    access.probe = companion_.probe;
    return access;
}

companion::Connection CompanionAccess::resolve() const {
    return companion::resolve(provider, host, companion::storedToken(config, provider), model, probe ? probe() : companion::Probe{});
}
