// Her answers arriving on the UI thread: streamed words, the finished reply (messages, chips, games,
// collections she changed), summaries and background notes (weather, reading, her life).
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../library/custom_games.hpp"
#include "../library/organization.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <chrono>

using namespace companion_app;

void Launcher::companionFlush() {
    auto& chat = companion_.chat;
    for (auto& message : chat.queue) chat.log.push_back(std::move(message));
    chat.queue.clear(); chat.queue_clock = 0;
    if (!chat.log.empty() && !chat.log.back().user) chat.reveal = chat.log.back().text.size();
}

void Launcher::companionStream(const std::wstring& heard) {
    auto& chat = companion_.chat;
    // Her streamed messages: the end of the log (already appearing) and the queue (still to come).
    const size_t in_log = std::min(chat.log.size(), companion_.streamed > chat.queue.size() ? companion_.streamed - chat.queue.size() : 0);
    const auto settle = [&] {
        if (!chat.log.empty() && !chat.log.back().user) chat.reveal = std::min(chat.reveal, chat.log.back().text.size());
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (heard.empty()) { // Gone: words that only led up to her tool calls, or a reply that never finished.
        chat.log.erase(chat.log.end() - static_cast<std::ptrdiff_t>(in_log), chat.log.end());
        chat.queue.clear(); chat.queue_clock = 0;
        companion_.streamed = 0; companion_.heard.clear();
        if (!chat.log.empty() && !chat.log.back().user) chat.reveal = chat.log.back().text.size();
        settle();
        return;
    }
    companion_.heard = heard;
    auto said = companion::lines(heard, companionLook(), std::nullopt, companion_.reply_length == companion::ReplyLength::brief ? 1 : solo_messages, true);
    if (said.empty()) return;
    const bool first = companion_.streamed == 0;
    // Fewer messages than before (they merged): the extra ones go, from the end.
    for (size_t extra = companion_.streamed > said.size() ? companion_.streamed - said.size() : 0; extra; --extra) {
        if (!chat.queue.empty()) chat.queue.pop_back();
        else if (!chat.log.empty()) chat.log.pop_back();
    }
    const size_t kept = std::min(companion_.streamed, said.size());
    const size_t shown = std::min({in_log, chat.log.size(), kept > chat.queue.size() ? kept - chat.queue.size() : size_t{0}});
    for (size_t i = 0; i < said.size(); ++i) {
        companion_ui::Message entry{false, std::move(said[i].text), std::move(said[i].emotions), false};
        entry.speaker = said[i].speaker;
        entry.mentions = std::move(said[i].mentions);
        if (i < shown) chat.log[chat.log.size() - shown + i] = std::move(entry);
        else if (i - shown < chat.queue.size()) chat.queue[i - shown] = std::move(entry);
        else if (i == 0) { chat.log.push_back(std::move(entry)); chat.reveal = 0; chat.reveal_clock = 0; chat.scroll = 0; }
        else chat.queue.push_back(std::move(entry));
    }
    companion_.streamed = said.size();
    if (first && !chat.log.empty()) { // Her first words: her face follows them.
        const auto& opening = chat.log.back();
        companion_.rig.emote(opening.emotions.empty() ? companion::calm : opening.emotions.front().emotion);
    }
    settle();
}

void Launcher::companionChips(const std::vector<std::wstring>& titles, const std::vector<std::wstring>& prompts) {
    using Kind = companion_ui::Chip::Kind;
    auto& chips = companion_.chat.chips;
    chips.clear();
    const auto addGame = [&](const std::wstring& title, const std::string& key) {
        if (chips.size() >= 3 || std::any_of(chips.begin(), chips.end(), [&](const auto& chip) { return chip.game == key; })) return;
        if (std::none_of(public_library_.begin(), public_library_.end(), [&](const Game& game) { return gameKey(game) == key; })) return;
        chips.push_back({Kind::game, L"▶ " + title, {}, key});
    };
    // The roulette's pick first: she may not have written it in brackets.
    const auto& log = companion_.chat.log;
    const auto spun = std::find_if(log.rbegin(), log.rend(), [](const auto& message) { return message.roulette.has_value(); });
    if (spun != log.rend() && spun - log.rbegin() <= 1 && !spun->roulette->reel.empty()) addGame(spun->roulette->reel.back(), spun->roulette->key);
    for (const auto& title : titles) {
        const auto key = companionGameKey(title);
        if (key.empty()) continue;
        const auto game = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
        if (game != public_library_.end()) addGame(game->name, key);
    }
    const bool games = chips.size() >= 2;
    for (const auto& custom : prompts) {
        if (custom.empty() || chips.size() >= 6) break;
        if (std::any_of(chips.begin(), chips.end(), [&](const auto& chip) { return chip.label == custom; })) continue;
        chips.push_back({Kind::prompt, custom, custom});
    }
    chips.push_back({Kind::prompt, tr(L"W co mam zagrać?"), tr(L"W co mam zagrać?")});
    chips.push_back({Kind::roulette, spun != log.rend() && spun - log.rbegin() <= 1 ? tr(L"Losuj jeszcze raz") : tr(L"Wylosuj grę")});
    if (!games && prompts.empty())
        for (const auto* prompt : {tr(L"Zroastuj moją kupkę wstydu"), tr(L"Opowiedz żart"), tr(L"Co sądzisz o moich ostatnich grach?")})
            chips.push_back({Kind::prompt, prompt, prompt});
    if (companion_.chat.focus >= static_cast<int>(chips.size())) companion_.chat.focus = static_cast<int>(chips.size()) - 1;
}

void Launcher::companionApplyCollections(const std::vector<companion::tools::CollectionMutation>& mutations) {
    if (mutations.empty() || !library_state_readable_) return;
    using Action = companion::tools::CollectionMutation::Action;
    bool changed = false;
    std::wstring created_name;
    for (const auto& mut : mutations) {
        if (mut.action == Action::favorite || mut.action == Action::unfavorite) {
            const bool member = mut.action == Action::favorite;
            const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(), favoriteCollection);
            const SteamCollection fallback{tr(L"Ulubione"), {}, "user-collections.favorite"};
            const auto& col = found == settings_collections_.end() ? fallback : *found;
            for (const auto& key : mut.keys) {
                const auto g = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
                if (g == public_library_.end()) continue;
                if (!g->version_group.empty() && versions_.find(*g))
                    for (const auto& copy : *versions_.find(*g)) setCollectionMember(library_state_.collections, account_, col, copy, member);
                else setCollectionMember(library_state_.collections, account_, col, *g, member);
                changed = true;
            }
            continue;
        }
        const auto name = custom_games::trim(mut.collection);
        if (name.empty() || name.size() > 200) continue;
        const auto wanted = game_index::fold(name);
        auto existing = std::find_if(settings_collections_.begin(), settings_collections_.end(),
                                     [&](const SteamCollection& c) { return !favoriteCollection(c) && game_index::fold(c.name) == wanted; });
        SteamCollection col;
        if (existing != settings_collections_.end()) {
            col = *existing;
        } else if (mut.action == Action::create_or_add) {
            const bool use_steam = mut.steam && account_ != 0;
            const auto random = custom_games::newId({});
            if (random.empty()) continue;
            const std::string id = (use_steam ? "user-collections.uc-launchme-" : "local:") + random;
            col = SteamCollection{name, {}, id};
            const auto scope = collectionKey(account_, col);
            if (use_steam) library_state_.collections.creates.insert(scope);
            library_state_.collections.names[scope] = name;
            settings_collections_.push_back(col);
            created_name = name;
            changed = true;
        } else continue;
        const bool member = mut.action != Action::remove;
        for (const auto& key : mut.keys) {
            const auto g = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == key; });
            if (g == public_library_.end()) continue;
            if (!g->version_group.empty() && versions_.find(*g))
                for (const auto& copy : *versions_.find(*g)) setCollectionMember(library_state_.collections, account_, col, copy, member);
            else setCollectionMember(library_state_.collections, account_, col, *g, member);
            changed = true;
        }
    }
    if (changed) {
        collectionChanged();
        saveLibraryPreferences();
        saveSettings();
        if (!created_name.empty()) showNotification(NotificationKind::launch, tr(L"Utworzono kolekcję"), created_name);
    }
}

std::string Launcher::companionGameKey(const std::wstring& title) const {
    if (const auto* fact = companion_.library.find(title)) return fact->key;
    if (!game_index_) return {};
    // Only the very title (or another name of it): a suggestion is never guessed from a part.
    const auto wanted = game_index::fold(title);
    return game_index_->read([&](const game_index::Index& index) -> std::string {
        for (const auto* record : index.lookup(title, 3)) {
            if (game_index::fold(record->title) == wanted) return record->key;
            for (const auto& alias : record->aliases) if (game_index::fold(alias) == wanted) return record->key;
        }
        return {};
    });
}

void Launcher::onCompanion(std::unique_ptr<CompanionMessage> message) {
    using Purpose = CompanionMessage::Purpose;
    if (message->purpose == Purpose::summary) { // A finished chat for her notes & "Sleep" reflection: nothing to show.
        if (message->memory_epoch != companion_.memory_epoch) return;
        const auto& raw = message->reply.raw.empty() ? message->reply.text : message->reply.raw;
        const auto pending = companion_.summary_pending.find(message->summary_look);
        if (pending != companion_.summary_pending.end()) {
            // Failed requests remain eligible for retry; trimming adjusts the pending watermark.
            if (message->reply.ok && settings_.companion_notes && raw.size() > 3 && raw != L"-" &&
                pending->second.chat_started == companion_.chat_started &&
                message->summary_look == companion::lookKey(companion_.chat_look.value_or(companionLook()))) {
                companion_.summarized = std::max(companion_.summarized, std::min(pending->second.through, companion_.turns.size()));
                companionSaveChat();
            }
            companion_.summary_pending.erase(pending);
        }
        if (message->reply.ok && settings_.companion_notes && raw.size() > 3 && raw != L"-") {
            const auto now = unix_time::now();
            const auto consolidated = companion::cognition::parseConsolidation(raw);
            auto& social = companionMindState().of(message->summary_look).social;
            const bool grounded = social.validEvidence(consolidated.evidence_ids, message->reflection_evidence);
            const bool has_key_mem = !consolidated.key_memory.empty() && consolidated.key_memory != L"-";
            if (!consolidated.summary.empty() && consolidated.summary != L"-") {
                companionMemory().summarize(consolidated.summary, now, message->summary_look, has_key_mem);
                saveCompanionMemory();
            }
            if (has_key_mem) {
                auto& self = companionMindState().of(message->summary_look);
                if (companion::mind::addKeyMemory(self, consolidated.key_memory, now)) saveCompanionMind();
            }
            if (grounded && !consolidated.reflection.empty() && consolidated.reflection != L"-") {
                companionCognition().reflect(consolidated.reflection, now, message->summary_look, consolidated.evidence_ids);
            }
            if (!consolidated.disposition.empty() && consolidated.disposition != L"-") {
                companionCognition().setDisposition(message->summary_look, consolidated.disposition);
            }
            if (grounded && !consolidated.schema.empty() && consolidated.schema != L"-") {
                companionCognition().addSchema(consolidated.schema, now, message->summary_look, consolidated.evidence_ids);
            }
            if (grounded) { social.reflected_through = std::max(social.reflected_through, message->reflection_cutoff); saveCompanionMind(); }
            if ((!consolidated.reflection.empty() && consolidated.reflection != L"-") ||
                (!consolidated.disposition.empty() && consolidated.disposition != L"-") ||
                (!consolidated.schema.empty() && consolidated.schema != L"-")) {
                saveCompanionCognition();
            }
            if (!consolidated.inside_joke.empty() && consolidated.inside_joke != L"-") {
                auto& self = companionMindState().of(message->summary_look);
                if (companion::mind::addSharedIdiom(self, consolidated.inside_joke, now)) saveCompanionMind();
            }
            // A new goal of her own, written in her diary: it starts now when she has none.
            if (!consolidated.goal.empty())
                if (const auto look = companion::lookFromKey(message->summary_look)) {
                    auto& self = companionMindState().of(message->summary_look);
                    if (companion::mind::aspire(self, companionMindContext(*look), consolidated.goal)) saveCompanionMind();
                }
            // Her plans for the next day: her life outside the chat (they happen, or fall through).
            if (!consolidated.plans.empty() && settings_.companion_memory) companionMindEpisodes(message->summary_look, consolidated.plans, true, 0);
        }
        return;
    }
    if (message->purpose == Purpose::weather) { // The real weather where she is: for her next replies.
        companion_.weather_busy = false;
        if (message->weather) companion_.weather = std::move(message->weather);
        return;
    }
    if (message->purpose == Purpose::reading) { // Her own web reading, in the background: for her next replies.
        companion_.reading_busy = false;
        if (message->memory_epoch != companion_.memory_epoch) return;
        if (!message->daily_readings.empty()) {
            auto& cog = companionCognition();
            for (const auto& rd : message->daily_readings) cog.readOnline(rd.look, rd.day, rd.topic, rd.summary, rd.at, rd.asked);
            saveCompanionCognition();
        }
        return;
    }
    if (message->purpose == Purpose::life) { // Her diary filled in what she did between talks.
        companion_.life_busy = false;
        if (message->memory_epoch != companion_.memory_epoch) return;
        if (!message->summary_look.empty() && settings_.companion_memory) {
            if (message->reply.ok)
                companionMindEpisodes(message->summary_look, message->reply.raw.empty() ? message->reply.text : message->reply.raw, false, message->life_from);
            // Filled in either way: a quiet stretch stays quiet (no asking again for it).
            auto& self = companionMindState().of(message->summary_look);
            self.lived_until = std::max(self.lived_until, message->life_to);
            saveCompanionMind();
        }
        return;
    }
    if (message->generation != companion_.generation) return;
    if (message->partial) { // Her words so far, while the reply streams in.
        if (settings_.companion && message->purpose == Purpose::chat && !companion_.live_chat && companion_.chat.waiting) companionStream(message->heard);
        return;
    }
    companion_.busy = false;
    if (!message->week.empty()) companion_.week = message->week; // For the rest of this conversation.
    if (!settings_.companion) return;
    if (!message->noted) { // The diagnostics: when it arrived, not when a roulette let it show.
        message->noted = true;
        CompanionDiagnostic entry;
        entry.purpose = message->purpose;
        entry.timing = message->timing;
        entry.total_ms = static_cast<uint32_t>(GetTickCount64() - message->timing.asked);
        entry.model = message->connected ? message->label : L"";
        entry.ok = message->reply.ok;
        entry.status = message->reply.status;
        entry.arrived = GetTickCount64();
        auto& recent = companion_.diagnostics;
        recent.push_back(std::move(entry));
        if (recent.size() > 6) recent.erase(recent.begin());
    }
    companionRemember(*message);
    if (!message->collection_mutations.empty()) companionApplyCollections(message->collection_mutations);
    if (message->reply.ok) companionAffinitySuggested(message->reply.games); // Gamification: her suggestions pay when played.
    if (message->purpose == Purpose::chat && message->tool_roulette) {
        auto roulette = std::move(*message->tool_roulette);
        message->tool_roulette.reset();
        companionFlush();
        companion_.chat.log.push_back({false, {}, {}, false, std::move(roulette), false, companionLook()});
        companion_.rig.emote(companion::Emotion::excited);
    }
    // The roulette still spinning: her verdict waits for the reel to stop (companion_roulette.cpp).
    if (message->purpose == Purpose::chat && companionRouletteSpinning()) { companion_.held = std::move(message); return; }
    const bool answered = message->reply.ok;
    const auto& reply = answered ? message->reply : message->fallback;
    if (!message->library.all().empty() || message->library.total || companion_.library.all().empty()) companion_.library = std::move(message->library);
    companion_.offline = !answered;
    // The header names the model only when asked to (Settings); problems are always told.
    const std::wstring provider = companion::providerName(message->provider);
    const auto& model = message->label;
    if (!model.empty()) companion_.model_label = model;
    if (answered) companion_.status.clear();
    else if (!message->connected)
        companion_.status = message->provider == companion::Provider::foundry ? std::wstring(tr(L"Offline — brak tokenu Foundry (Ustawienia → System)")) :
                            std::wstring(tr(L"Offline — brak klucza API: ")) + provider + tr(L" (Ustawienia → System)");
    else if (message->reply.status == 401 || message->reply.status == 403) companion_.status = provider + tr(L": klucz lub token odrzucony — sprawdź go w Ustawieniach");
    else if (message->reply.status == 404) companion_.status = model + tr(L": model niedostępny");
    else if (message->reply.status == 429) companion_.status = provider + tr(L": limit zapytań — spróbuj za chwilę");
    else if (message->reply.status == 503) companion_.status = provider + tr(L": model przeciążony po stronie serwera (503) — spróbuj za chwilę");
    else if (message->reply.error == L"no model") companion_.status = provider + tr(L": brak modelu na serwerze");
    else if (message->reply.status && message->reply.status != 200) companion_.status = provider + L": HTTP " + std::to_wstring(message->reply.status);
    else if (message->reply.status == 200) companion_.status = provider + tr(L": pusta odpowiedź");
    else companion_.status = tr(L"Brak połączenia: ") + provider;
    const auto react = [&](bool chat) {
        const auto feeling = reply.emotions.empty() ? companion::calm : reply.emotions.front().emotion;
        companion_.rig.emote(feeling);
        if (companionVoiceFor(chat) && !reply.text.empty())
            companionSpeakLines({{companionLook(), reply.text, reply.emotions, reply.mentions, reply.cues}});
    };
    switch (message->purpose) {
    case Purpose::home: {
        const bool session_remark = std::exchange(companion_.bubble_session, false);
        companion_.bubble = {};
        companion_.bubble.text = reply.text; companion_.bubble.emotions = reply.emotions;
        companion_.bubble.mentions = reply.mentions;
        companion_.bubble.alpha = 1;
        react(false);
        // Her remark joins the conversation's context as something she said (it merges with her
        // last turn), so the chat does not contradict it; a session remark also appears in the chat
        // log with `free = true` so it does not count towards message_cap.
        if (session_remark && !reply.text.empty() && !companion_.live_chat && !companion_.chat.ended && !companion_.chat.waiting &&
            (!companion_.chat_look || *companion_.chat_look == companionLook())) {
            companion_ui::Message entry{false, reply.text, reply.emotions, false};
            entry.speaker = companionLook();
            entry.mentions = reply.mentions;
            entry.free = true;
            if (!companion_.chat.log.empty()) {
                companionFlush();
                companion_.chat.log.push_back(std::move(entry));
                companion_.chat.reveal = companion_.chat.log.back().text.size();
            }
        }
        if (answered && !reply.text.empty() && companion_.chat_look == companionLook() && !companion_.live_chat && !companion_.chat.ended &&
            !companion_.chat.log.empty() && !companion_.chat.waiting) {
            companion_.turns.push_back({false, reply.raw.empty() ? reply.text : reply.raw});
            companionSaveChat();
        }
        break;
    }
    case Purpose::test:
        if (answered) showNotification(NotificationKind::launch, tr(L"Połączono z modelem"), model + L" · " + std::to_wstring(message->elapsed_ms) + L" ms");
        else showNotification(NotificationKind::error, tr(L"Brak połączenia z modelem"), companion_.status +
                              (message->reply.error.empty() ? std::wstring{} : L"\n" + message->reply.error));
        react(true);
        if (!companionOpen()) {
            companion_.bubble = {};
            companion_.bubble.text = reply.text; companion_.bubble.emotions = reply.emotions; companion_.bubble.alpha = 1;
            companion_.bubble.mentions = reply.mentions;
        }
        break;
    default: {
        if (companion_.live_chat) companionCloseArchive(); // Her answer belongs to the live conversation.
        auto& chat = companion_.chat;
        // Her words already streaming in: the whole reply takes their place, and what is said stays said.
        const size_t streamed = companion_.streamed;
        const size_t in_log = std::min(chat.log.size(), streamed > chat.queue.size() ? streamed - chat.queue.size() : 0);
        companion_.streamed = 0; companion_.heard.clear();
        if (!streamed) companionFlush(); // Finish the previous line.
        chat.waiting = false;
        if (answered && !companion_.mood.empty()) companion_.mood_spent = true; // How she felt at the start: said once, in this reply.
        chat.hesitating = false;
        chat.hesitation_clock = 0;
        // Her reply as chat messages: two short ones now and then, both companions with a guest.
        auto& guest = companion_.guest;
        const auto host = companionLook();
        const auto with = message->guest && guest.look == message->guest ? message->guest : std::nullopt;
        // Alone, a question she ends with after something else is a message of its own (as in a messenger).
        auto said = companion::lines(reply.raw.empty() ? reply.text : reply.raw, host, with,
            with ? duo_messages : companion_.reply_length == companion::ReplyLength::brief ? 1 : solo_messages, !with);
        if (said.empty()) said.push_back({host, reply.text, reply.emotions, reply.mentions});
        const auto spoken_lines = said;
        const bool error = !answered && message->connected;
        const bool free_reply = std::exchange(companion_.next_reply_free, false);
        companion_.chat_quiet = 0;
        bool queued_first = false; // "Pisze…": even her first message waits behind the typing dots.
        if (streamed) {
            chat.queue.clear();
            const size_t keep = std::min(in_log, said.size());
            chat.log.erase(chat.log.end() - static_cast<std::ptrdiff_t>(in_log - keep), chat.log.end());
            for (size_t i = 0; i < said.size(); ++i) {
                companion_ui::Message entry{false, std::move(said[i].text), std::move(said[i].emotions), error};
                entry.speaker = said[i].speaker;
                entry.mentions = std::move(said[i].mentions);
                entry.free = free_reply;
                if (i < keep) chat.log[chat.log.size() - keep + i] = std::move(entry);
                else if (i == 0) { chat.log.push_back(std::move(entry)); chat.reveal = 0; chat.reveal_clock = 0; }
                else chat.queue.push_back(std::move(entry));
            }
            if (!chat.log.empty() && !chat.log.back().user) chat.reveal = std::min(chat.reveal, chat.log.back().text.size());
            chat.queue_clock = 0; chat.scroll = 0;
        } else {
            // "Pisze…": her whole reply waits behind the typing dots (the time the model took counts
            // as typing); with her voice speaking, the first message shows at once.
            const bool whole = settings_.companion_typing && !companionVoiceFor(true);
            for (auto& line : said) {
                companion_ui::Message entry{false, std::move(line.text), std::move(line.emotions), error};
                entry.speaker = line.speaker;
                entry.mentions = std::move(line.mentions);
                entry.free = free_reply;
                (!whole && &line == &said.front() ? chat.log : chat.queue).push_back(std::move(entry));
            }
            chat.reveal = 0; chat.reveal_clock = 0; chat.queue_clock = 0; chat.scroll = 0;
            if (whole && !chat.queue.empty()) {
                queued_first = true;
                const float waited = companion_.asked ? static_cast<float>(GetTickCount64() - companion_.asked) / 1000.0f : 0.0f;
                chat.queue_clock = std::clamp(waited, 0.0f, std::max(0.0f, wholeTypingSeconds(chat.queue.front().text) - 0.35f));
            }
        }
        // Her turn as she wrote it, hidden appraisal and thought too: she keeps her own format and her inner continuity.
        companion_.turns.push_back({false, !reply.full.empty() && !reply.appraisal.empty() ? reply.full : reply.raw.empty() ? reply.text : reply.raw});
        // Gamification: she set a boundary ([granica]); alone only — in a duo nobody knows whose it was.
        if (answered && reply.boundary && message->purpose == Purpose::chat && !with) companionBoundary();
        if (answered && message->purpose == Purpose::chat && !with) companionRelationshipShift(reply);
        if (answered && message->purpose == Purpose::chat && settings_.companion_notes &&
            companionMindState().of(message->reply_look).social.reflectionDue(companionMindContext(companionLook()).now)) requestCompanionSummary(true);
        companionChips(reply.games, reply.prompts);
        // A pad player lands on the first suggestion; a typist stays in the field.
        if (pad_input_ && chat.focus == companion_ui::focus_input && chat.input.empty() && !keyboard_.open && !chat.chips.empty()) chat.focus = 0;
        // A guest who dropped in steps in with her reply.
        if (with && !guest.leaving) guest.shown = true;
        // "Pisze…": her face changes when the message appears (the queue), not while she types.
        const auto& first = queued_first ? chat.queue.front() : chat.log.back();
        const auto feeling = first.emotions.empty() ? companion::calm : first.emotions.front().emotion;
        if (!queued_first) companionRig(first.speaker).emote(feeling);
        // Micro-expression: if her hidden subconscious feeling differs from what she shows out loud,
        // her face betrays it for a brief moment first.
        if (reply.subconscious_emotion && *reply.subconscious_emotion != feeling && !with)
            companionRig(first.speaker).flash(*reply.subconscious_emotion, 0.55f);
        const bool voiced = companionVoiceFor(true) && !spoken_lines.empty();
        if (voiced) companionSpeakLines(spoken_lines);
        if (!companion_.diagnostics.empty()) { // How long her typewriter (or "Pisze…" dots) takes, and whether her voice holds it.
            auto& noted = companion_.diagnostics.back();
            noted.messages = (queued_first ? 0 : 1) + chat.queue.size();
            noted.chars = queued_first ? 0 : first.text.size();
            noted.typing_seconds = queued_first ? 0 : typingSeconds(first.text);
            for (const auto& next : chat.queue) {
                noted.chars += next.text.size();
                noted.typing_seconds += settings_.companion_typing ? wholeTypingSeconds(next.text) : typingSeconds(next.text) + typingPause(next);
            }
            noted.voiced = voiced && companion_.voice.pending;
            noted.arrived = GetTickCount64();
        }
        // Her answer at the conversation's break threshold: she said goodbye; she rests now.
        if (companion_.closing) companionEnd(false);
        else companionSaveChat();
        break;
    }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
