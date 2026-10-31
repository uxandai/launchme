// Towarzyszka (Settings → Asystent): an animated anime companion. On the home she stands in a tile
// (where each layout puts it: src/ui/home_widgets.hpp) and greets the player once per start; C, the
// left trigger or a click opens her chat, where she jokes, teases the player about the backlog and
// suggests games from the library (▶ chips start them). Answers come from the chosen AI provider
// (Gemini, Claude, Grok, Ollama; src/net/companion*.cpp) on a worker; without a connection she
// speaks canned lines. Her reactions to the launcher: companion_events.cpp; the game roulette:
// companion_roulette.cpp; her figure: src/ui/avatar.hpp and src/ui/companion_renderer.cpp.
// This file: her place on the home screen, identity, memory/cognition/mood stores, forgetting and
// remembering, the tick and the view. Requests: companion_request.cpp; answers:
// companion_answer.cpp; the chat: companion_chat.cpp, companion_input.cpp; her mind's screen:
// companion_mind_view.cpp; shared helpers: companion_app_internal.hpp.
#include "launcher.hpp"
#include "companion_app_internal.hpp"
#include "../net/companion_person.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unix_time.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>

static_assert(AppSettings::companion_provider_keys.size() == companion::provider_count, "AppSettings::CompanionProvider mirrors companion::Provider");

using namespace companion_app;

bool Launcher::homeOverlayAvailable(bool allow_closing) const {
    return screen_ == ScreenMode::shelves && topModal() == Modal::none && !keyboard_.open &&
        !closing_ && !screensaverActive() && !launchAnimationActive() && !pending_launch_ &&
        !quick_open_ && (allow_closing || quick_progress_ <= 0.003f) && !friends_open_ && (allow_closing || friends_progress_ <= 0.003f) &&
        !category_menu_open_ && (allow_closing || category_menu_progress_ <= 0.003f) && !menu_open_ && (allow_closing || menu_progress_ <= 0.003f);
}

home_widgets::Placement Launcher::homeWidgets() const {
    using home_widgets::Placement;
    // The desktop window: Glance is a top bar shortcut and she has her card in the sidebar, so no
    // free-standing tile (her bubble still speaks, beside that card).
    if (desktop_mode_) return Placement::tiles;
    switch (settings_.home_layout) {
    case AppSettings::HomeLayout::xmb:
    case AppSettings::HomeLayout::metro: return Placement::tiles;
    case AppSettings::HomeLayout::psfive: return Placement::corner;
    case AppSettings::HomeLayout::flix: return Placement::pills;
    case AppSettings::HomeLayout::snitch: return Placement::pills;
    default: return Placement::column;
    }
}

home_widgets::Spots Launcher::homeWidgetSpots() const {
    const auto view = renderer_.viewport();
    auto spots = home_widgets::spots(homeWidgets(), view.width, view.height, settings_.glance, settings_.companion);
    if (snitchActive()) { // Small shortcuts beside the profile; the clock owns the right corner.
        float left = 164;
        if (spots.companion) { spots.companion = settings_ui::Rect{left, 60, left + home_widgets::companion_pill_width, 100}; left += home_widgets::companion_pill_width + 10; }
        if (spots.glance) spots.glance = settings_ui::Rect{left, 60, left + home_widgets::glance_pill_width, 100};
    }
    // Flix with the menu's icons at the top: Glance and her avatar as two more small icons in that row.
    if (flixIconsShown()) home_widgets::icons::place(spots, quick_menu::count(static_cast<int>(plugin_menu_.size())), view.width);
    return spots;
}

// Flix (Opcje układu → Ikony menu w prawym górnym rogu): the quick bar's icons small at the top.
bool Launcher::flixIconsShown() const {
    return settings_.flix_top_icons && settings_.home_layout == AppSettings::HomeLayout::flix && !desktop_mode_;
}
int Launcher::flixIconCount() const {
    return quick_menu::count(static_cast<int>(plugin_menu_.size())) + (settings_.glance ? 1 : 0) + (settings_.companion ? 1 : 0);
}
// A click on one of the quick icons (Glance and her avatar answer through their spots).
bool Launcher::flixIconClick(float x, float y, bool double_click) {
    if (!flixIconsShown() || !homeOverlayAvailable()) return false;
    const auto view = renderer_.viewport();
    const int item = home_widgets::icons::hit(x, y, flixIconCount(), view.width);
    if (item < 0 || item >= quick_menu::count(static_cast<int>(plugin_menu_.size()))) return false;
    if (!double_click) { quick_open_ = true; quick_selected_ = item; quickAccept(); }
    return true;
}

bool Launcher::companionAvailable(bool allow_closing) const { return settings_.companion && homeOverlayAvailable(allow_closing); }

companion::Look Launcher::companionLook() const { return lookOf(settings_); }

companion::Persona Launcher::companionPersona() const { return companionPersonaOf(companionLook()); }

companion::Persona Launcher::companionPersonaOf(companion::Look look) const {
    // Fixed per companion: her character.txt's persona= (no setting changes who she is).
    const auto* character = companion::character(look);
    return character ? character->persona : companion::Persona::genki;
}

avatar::Rig& Launcher::companionRig(companion::Look speaker) {
    const auto& guest = companion_.guest;
    return guest.look && *guest.look == speaker && speaker != companionLook() ? companion_.guest.rig : companion_.rig;
}

companion::Memory& Launcher::companionMemory() {
    if (!companion_.memory) companion_.memory = companion::loadMemory(userConfigDirectory());
    return *companion_.memory;
}

void Launcher::saveCompanionMemory() {
    if (!companion_.memory) return;
    companionSemantic(false);
    // Writes run in order of their generation: an older save never lands after a newer one or a clearing.
    const auto generation = ++companion_.memory_io->generation;
    work_.start([memory = *companion_.memory, config = userConfigDirectory(), io = companion_.memory_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) companion::saveMemory(config, memory);
    });
}

companion::cognition::State& Launcher::companionCognition() {
    if (!companion_.cognition) companion_.cognition = companion::cognition::loadState(userConfigDirectory());
    return *companion_.cognition;
}

void Launcher::saveCompanionCognition() {
    companionSemantic(false);
    if (!companion_.cognition || !companion_.persist) return;
    const auto generation = ++companion_.cognition_io->generation;
    work_.start([state = *companion_.cognition, config = userConfigDirectory(), io = companion_.cognition_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) companion::cognition::saveState(config, state);
    });
}

companion::mood::State& Launcher::companionMoodState() {
    if (!companion_.mood_state) {
        std::ifstream input(userConfigDirectory() / L"companion_mood.txt", std::ios::binary);
        std::ostringstream text;
        if (input) text << input.rdbuf();
        companion_.mood_state = companion::mood::parse(text.str());
    }
    return *companion_.mood_state;
}

void Launcher::saveCompanionMood() {
    if (!companion_.mood_state || !companion_.persist) return;
    // An older save never lands after a newer one.
    const auto generation = ++companion_.mood_io->generation;
    work_.start([text = companion::mood::serialize(*companion_.mood_state), file = userConfigDirectory() / L"companion_mood.txt",
                 io = companion_.mood_io, generation] {
        std::lock_guard lock(io->mutex);
        if (io->generation.load() == generation) atomic_file::write(file, text);
    });
}

void Launcher::companionMoodPlayed(const std::string& key, uint64_t played_ms) {
    if (!settings_.companion || !settings_.companion_memory || !companion_.persist || key.empty()) return;
    companionMindPlayed(key, played_ms); // Her expectations about the game, settled by telemetry.
    const auto now = companionNow();
    const auto unix_now = unix_time::now();
    const auto host = std::string(companion::lookKey(companionLook()));
    auto& state = companionMoodState();
    bool changed = false;
    // A game she suggested lately, played for a while: she is pleased (whoever suggested it).
    if (played_ms >= 15 * 60'000)
        for (const auto& pick : companionMemory().picks)
            if (pick.key == key && pick.at + 14 * 86400 > unix_now && !pick.title.empty()) {
                state.note(pick.look.empty() ? host : pick.look, companion::mood::Kind::pleased, now, pick.title);
                changed = true;
            }
    // A long session ending in the small hours: she worries a little.
    if (const auto clock = companion_.clock(); played_ms >= 30 * 60'000 && clock.wHour >= 1 && clock.wHour < 5) {
        state.note(host, companion::mood::Kind::late, now);
        changed = true;
    }
    if (changed) saveCompanionMood();
}

void Launcher::companionForget() {
    ++companion_.memory_epoch;
    companion_.summary_pending.clear();
    // Replies already in flight belong to the forgotten context, including streamed/held replies.
    companion_.cancel->store(true);
    ++companion_.generation;
    companion_.busy = false;
    companion_.chat.waiting = false;
    companion_.chat.hesitating = false;
    companion_.held.reset();
    if (companion_.streamed) companionStream({});
    companion_.heard.clear();
    if (companion_.semantic) companion_.semantic->clear();
    companion_.semantic.reset();
    {
        std::error_code ec;
        std::filesystem::remove(userConfigDirectory() / L"companion_vectors.dat", ec);
    }
    companion_.memory = companion::Memory{};
    ++companion_.memory_io->generation; // A save still waiting is dropped.
    {
        std::lock_guard lock(companion_.memory_io->mutex);
        companion::clearMemory(userConfigDirectory());
    }
    companion_.cognition = companion::cognition::State{};
    ++companion_.cognition_io->generation;
    {
        std::lock_guard lock(companion_.cognition_io->mutex);
        companion::cognition::clearState(userConfigDirectory());
    }
    companion_.mind = companion::mind::State{}; // Her mind too: goals, expectations, what she said about herself.
    ++companion_.mind_io->generation;
    {
        std::lock_guard lock(companion_.mind_io->mutex);
        companion::mind::clearState(userConfigDirectory());
    }
    // Her moods and the weekly snapshot go with it.
    companion_.mood_state = companion::mood::State{};
    companion_.mood.clear(); companion_.week.clear();
    ++companion_.mood_io->generation;
    {
        std::lock_guard lock(companion_.mood_io->mutex);
        std::error_code ec;
        std::filesystem::remove(userConfigDirectory() / L"companion_mood.txt", ec);
        std::filesystem::remove(userConfigDirectory() / L"companion_week.txt", ec);
    }
    showNotification(NotificationKind::launch, tr(L"Towarzyszka AI"), tr(L"Pamięć wyczyszczona"));
}

void Launcher::companionRemember(const CompanionMessage& message) {
    if (!message.reply.ok || (!settings_.companion_memory && !settings_.companion_notes)) return;
    const auto now = unix_time::now();
    auto& memory = companionMemory();
    const auto look = std::string(companion::lookKey(companionLook())); // Who suggested it: she is pleased when it is played.
    if (settings_.companion_memory) {
        memory.said(message.reply.text, now);
        for (const auto& title : message.reply.games)
            if (auto key = companionGameKey(title); !key.empty()) memory.suggested(std::move(key), title, now, look);
    }
    const auto reply_feeling = message.reply.subconscious_emotion.value_or(
        message.reply.emotions.empty() ? companion::calm : message.reply.emotions.front().emotion);
    const std::string reply_emotion_key = companion::emotionKey(reply_feeling);
    if (settings_.companion_notes) {
        // What she heard is hers (and a guest's in the chat too): another companion learns it only by gossip.
        const auto learn = [&](const std::wstring& text, const std::string& emotion, const std::wstring& feeling) {
            companion::Memory::Facts facts;
            facts.look = message.reply_look.empty() ? look : message.reply_look;
            facts.guest = message.guest_look;
            facts.emotion = emotion;
            facts.feeling = feeling;
            facts.confidence = 0;
            memory.note(text, now, facts);
        };
        if (!message.affective_notes.empty()) {
            for (const auto& entry : message.affective_notes)
                learn(entry.text, entry.emotion.empty() ? reply_emotion_key : entry.emotion, !entry.feeling.empty() ? entry.feeling : message.reply.subconscious);
        } else {
            for (const auto& note : message.notes) learn(note, reply_emotion_key, message.reply.subconscious);
        }
    }
    saveCompanionMemory();
    companionMindReplied(message); // Her appraisal: her mood, what she learned, expects, promised and set herself.

    // Update cognitive graph: subconscious thoughts, daily Tavily readings, and Hebbian synapses.
    auto& cog = companionCognition();
    bool cog_changed = false;
    if (!message.reply.subconscious.empty()) {
        const auto feeling = message.reply.subconscious_emotion.value_or(
            message.reply.emotions.empty() ? companion::calm : message.reply.emotions.front().emotion);
        cog.think(message.reply.subconscious, feeling, now, look);
        cog_changed = true;
    }
    for (const auto& rd : message.daily_readings) {
        cog.readOnline(rd.look.empty() ? look : rd.look, rd.day, rd.topic, rd.summary, now, rd.asked);
        cog_changed = true;
    }
    if (!companion_.last_query.empty()) {
        cog.learnHebbian(companion_.last_query, now);
        cog_changed = true;
    }
    for (const auto& note : message.notes) {
        cog.learnHebbian(note, now);
        cog_changed = true;
    }
    if (cog_changed) saveCompanionCognition();
}

bool Launcher::tickCompanion(float dt) {
    if (!settings_.companion) { companion_.tile_progress = 0; return false; }
    pollCompanionFeed();
    if (settings_.companion_gamification && !companion_.affinity) companionAffinity(); // Her level, before it is drawn.
    const bool pulsing = companion_.affinity_pulse > 0 || companion_.affinity_card_pulse > 0;
    if (companion_.affinity_pulse > 0) companion_.affinity_pulse = std::max(0.0f, companion_.affinity_pulse - dt / 1.6f); // A new level: the badge glows.
    if (companion_.affinity_card_pulse > 0) companion_.affinity_card_pulse = std::max(0.0f, companion_.affinity_card_pulse - dt / 1.6f); // Her card: it flashes.
    // Her voice: check the ElevenLabs key right away, so even her greeting can be spoken.
    if (companion_.age == 0 && settings_.companion_voice != AppSettings::CompanionVoice::off) requestCompanionVoiceStatus();
    companion_.age += dt;
    const bool home = companionAvailable(true);
    // Her greeting and remarks need her bubble: every home but the Flix pills (they wait for another layout).
    const bool speaks = home && home_widgets::speaks(homeWidgets());
    const bool chat_open = companionOpen();
    const float before = companion_.tile_progress;
    if (!home) companion_.tile_progress = 0;
    else if (!settings_.screen_transitions) companion_.tile_progress = 1;
    else companion_.tile_progress = std::min(1.0f, companion_.tile_progress + dt / 0.30f);
    const float hover = companionAvailable() && companion_.hover ? 1.0f : 0.0f;
    if (settings_.screen_transitions) companion_.hover_motion.step(hover, dt, spring::glide.omega, spring::glide.zeta);
    else companion_.hover_motion.snap(hover);
    if (companion_.hover_motion.settled(hover, 0.002f)) companion_.hover_motion.snap(hover);
    bool moving = companion_.tile_progress != before || companion_.hover_motion.value != hover || pulsing;
    // Once per start she jumps up out of her tile as it appears, landing with a bounce.
    if (home && companion_.entrance < 1) {
        if (!settings_.screen_transitions) companion_.entrance = 1;
        else if (companion_.tile_progress >= 1) {
            companion_.entrance = std::min(1.0f, companion_.entrance + dt / 0.9f);
            if (companion_.entrance >= 1) companion_.rig.emote(companion_.rig.emotion()); // The landing hop.
        }
        moving = true;
    }
    // Settled once the home has its games (or after a few seconds): her remarks may start. She no
    // longer greets in a bubble of her own — the chat keeps the conversation, and a greeting there
    // would not know it. Asleep by her clock, she waits for her morning.
    const bool asleep = home && companionAsleep(companionLook());
    if (speaks && !companion_.greeted && !asleep && companion_.age > 1.2f && (!scanning_ || companion_.age > 6.0f)) {
        companion_.greeted = true;
        companion_.rig.emote(companion::Emotion::smile);
        requestCompanionPremieres();
    }
    // A long quiet on the home makes her doze off (and so does her rest after a conversation); the
    // next input wakes her with a start. Her night keeps her asleep whatever the player does.
    const bool quiet = std::chrono::steady_clock::now() - last_input_ > std::chrono::seconds(45) || (home && (asleep || companionResting()));
    if (home && quiet && !companion_.dozing && !companion_.busy && companion_.bubble.alpha <= 0) {
        companion_.dozing = true;
        companion_.rig.emote(companion::Emotion::sleepy, false);
    } else if (companion_.dozing && (!quiet || chat_open)) {
        companion_.dozing = false;
        if (companion_.rig.emotion() == companion::Emotion::sleepy) companion_.rig.flash(companion::Emotion::shock, 1.4f);
    }
    // Speech: characters appear at her pace, each one moving her mouth (the guest's for her lines).
    const auto speak = [&](avatar::Rig& rig, const std::wstring& text, const std::vector<companion::Segment>& emotions, size_t& shown, float& clock) {
        if (shown >= text.size()) return false;
        if (companionVoiceHolding()) return true; // Her text starts with her voice.
        const bool voiced = companion_.voice.pending || audio_.speaking(); // The audio moves her mouth instead.
        clock += dt * speech_rate;
        while (shown < text.size()) {
            const float cost = shown ? pause(text[shown - 1]) : 1.0f;
            if (clock < cost) break;
            clock -= cost;
            for (const auto& segment : emotions)
                if (segment.at == shown && segment.at && segment.emotion != rig.emotion()) rig.emote(segment.emotion);
            if (!voiced) rig.speak(text[shown]);
            ++shown;
        }
        if (shown >= text.size() && !voiced) rig.silence();
        return true;
    };
    auto& chat = companion_.chat;
    bool speaking = false, guest_speaking = false;
    if (chat_open) {
        if (tickCompanionFeed(dt)) moving = true;
        chat.age += dt; chat.pose_age += dt;
        // Her enlarged photo fades and grows in, and out again.
        if (const float target = chat.viewer ? 1.0f : 0.0f; chat.viewer_motion != target) {
            chat.viewer_motion = !settings_.screen_transitions ? target : target > chat.viewer_motion ? std::min(target, chat.viewer_motion + dt / 0.18f)
                                                                                                    : std::max(target, chat.viewer_motion - dt / 0.14f);
            moving = true;
        }
        // Her 3D Mind & Memory screen fades in and keeps the holographic sphere gently rotating.
        if (const float target = chat.mind ? 1.0f : 0.0f; chat.mind_motion != target) {
            chat.mind_motion = !settings_.screen_transitions ? target : target > chat.mind_motion ? std::min(target, chat.mind_motion + dt / 0.22f)
                                                                                                  : std::max(target, chat.mind_motion - dt / 0.16f);
            moving = true;
        }
        if (chat.mind || chat.mind_motion > 0.003f) {
            if (chat.mind) chat.mind_age = settings_.screen_transitions ? chat.mind_age + dt : companion_ui::Layout::mind_build_seconds;
            const float prev_yaw = chat.mind_yaw;
            if (!chat.mind_dragging) chat.mind_yaw += dt * 0.18f;
            // Every few seconds in the mind view she strikes a new expression/pose hop in her portrait.
            if (static_cast<int>(prev_yaw * 1.3f) != static_cast<int>(chat.mind_yaw * 1.3f)) {
                static constexpr std::array<companion::Emotion, 8> lively_emotions{
                    companion::Emotion::playful, companion::Emotion::smug, companion::Emotion::smile, companion::Emotion::thinking,
                    companion::Emotion::excited, companion::Emotion::gaming, companion::Emotion::drink, companion::Emotion::wave};
                const size_t pick = static_cast<size_t>(std::abs(static_cast<int>(chat.mind_yaw * 10.0f))) % lively_emotions.size();
                companion_.rig.emote(lively_emotions[pick]);
            }
            if (settings_.screen_transitions) {
                chat.mind_yaw_spring.step(chat.mind_yaw, dt, spring::glide.omega, spring::glide.zeta);
                chat.mind_pitch_spring.step(chat.mind_pitch, dt, spring::glide.omega, spring::glide.zeta);
                chat.mind_zoom_spring.step(chat.mind_zoom, dt, 18.0f, 1.0f); // A softer glide for the zoom.
            } else {
                chat.mind_yaw_spring.snap(chat.mind_yaw);
                chat.mind_pitch_spring.snap(chat.mind_pitch);
                chat.mind_zoom_spring.snap(chat.mind_zoom);
            }
            moving = true;
        }
        if (tickCompanionRoulette(dt)) moving = true;
        if (tickCompanionPhoto(dt)) moving = true;
        if (chat.waiting && chat.hesitating) {
            const float prev = chat.hesitation_clock;
            chat.hesitation_clock += dt;
            if (prev < 0.9f && chat.hesitation_clock >= 0.9f)
                companion_.rig.flash(companion::Emotion::shy, 0.55f, companion::Emotion::thinking);
            moving = true;
        }
        // "Pisze…" (whole messages): whatever she says appears at once, never letter by letter.
        if (settings_.companion_typing && chat.revealing()) chat.reveal = chat.log.back().text.size();
        if (!chat.log.empty() && !chat.log.back().user) {
            auto& message = chat.log.back();
            auto& rig = companionRig(message.speaker);
            (&rig == &companion_.rig ? speaking : guest_speaking) = speak(rig, message.text, message.emotions, chat.reveal, chat.reveal_clock);
        }
        // The next message of her reply, after a moment of typing (with "Pisze…", as long as typing it takes).
        if (!chat.revealing() && !chat.queue.empty() && !companionVoiceHolding()) {
            chat.queue_clock += dt;
            if (chat.queue_clock >= (settings_.companion_typing ? wholeTypingSeconds(chat.queue.front().text) : typingPause(chat.queue.front()))) {
                chat.log.push_back(std::move(chat.queue.front()));
                chat.queue.erase(chat.queue.begin());
                chat.reveal = settings_.companion_typing ? chat.log.back().text.size() : 0;
                chat.reveal_clock = 0; chat.queue_clock = 0; chat.scroll = 0;
                const auto& next = chat.log.back();
                companionRig(next.speaker).emote(next.emotions.empty() ? companion::calm : next.emotions.front().emotion);
            }
            moving = true;
        }
        if (tickCompanionGuest(dt)) moving = true;
        if (tickCompanionInitiative(dt)) moving = true;
        chat.scroll = std::clamp(chat.scroll, 0.0f, std::max(0.0f, renderer_.companionScrollLimit()));
        if (settings_.screen_transitions) chat.scroll_motion.step(chat.scroll, dt, spring::glide.omega, spring::glide.zeta);
        else chat.scroll_motion.snap(chat.scroll);
        if (!chat.scroll_motion.settled(chat.scroll, 0.2f)) moving = true; else chat.scroll_motion.snap(chat.scroll);
        // Her entrance: the full-figure pose, then her close-up (companion_ui::poseAlpha).
        moving = moving || chat.age < 0.5f || chat.pose_age < companion_ui::pose_hold + companion_ui::pose_out + 0.05f || chat.waiting;
    } else if (speaks) {
        auto& bubble = companion_.bubble;
        speaking = speak(companion_.rig, bubble.text, bubble.emotions, bubble.reveal, bubble.reveal_clock);
        if (!speaking && !bubble.text.empty()) bubble.shown += dt;
        // XMB and Metro: her bubble lies over tiles of the layout; moving through them sends it away
        // (a remark is not worth covering what the player is looking for).
        if ((xmbActive() || metroActive()) && !bubble.waiting && !bubble.text.empty() && bubble.alpha > 0 &&
            GetTickCount64() - nav_direction_at_ < 400) bubble.shown = std::max(bubble.shown, 14.0f);
        const float target = bubble.waiting || (!bubble.text.empty() && bubble.shown < 14.0f) ? 1.0f : 0.0f;
        const float previous = bubble.alpha;
        bubble.alpha = target > bubble.alpha ? std::min(1.0f, bubble.alpha + dt / 0.2f) : std::max(0.0f, bubble.alpha - dt / 0.6f);
        if (bubble.alpha != previous) moving = true;
        if (bubble.waiting && !companion_.busy && bubble.text.empty()) bubble.waiting = false;
        pumpCompanionEvents();
    }
    // Only the selected preview moves; the other cards keep a neutral, still pose.
    const bool previews = settings_.companion && companion::lookCount() > 0 && (companion_.picker.open ||
        (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::companion)));
    if (previews) {
        while (companion_.previews.size() < static_cast<size_t>(companion::lookCount()))
            companion_.previews.emplace_back(23 + 6 * static_cast<int>(companion_.previews.size()));
        const auto& picker = companion_.picker;
        const int selected = static_cast<int>(picker.open && !picker.choices.empty() ? picker.choices[static_cast<size_t>(picker.focus)] : companionLook());
        if (companion_.preview_look != selected) {
            companion_.preview_look = selected;
            companion_.preview_clock = 0;
            companion_.previews[static_cast<size_t>(selected)] = avatar::Rig{23 + 6u * static_cast<uint32_t>(selected)};
        }
        companion_.preview_clock += dt;
        if (companion_.preview_clock > 2.6f) {
            companion_.preview_clock = 0;
            companion_.preview_emotion = (companion_.preview_emotion + 1) % companion::emotion_count;
            companion_.previews[static_cast<size_t>(selected)].emote(static_cast<companion::Emotion>(companion_.preview_emotion));
        }
        companion_.previews[static_cast<size_t>(selected)].step(dt);
    }
    if (tickCompanionVoice(dt)) speaking = true;
    const bool visible = (home && companion_.tile_progress > 0) || chat_open;
    if (visible) companion_.rig.step(dt, !speaking);
    if (chat_open && (companion_.guest.look || companion_.guest.motion > 0)) companion_.guest.rig.step(dt, !guest_speaking);
    speaking = speaking || guest_speaking;
    // Idle animation only while someone is watching and recently active; speech always plays.
    const bool lively = speaking || chat_open || companion_.bubble.alpha > 0 || std::chrono::steady_clock::now() - last_input_ < std::chrono::seconds(60);
    return moving || speaking || (windowWatched() && ((visible && lively) || previews));
}

std::vector<std::wstring> Launcher::companionDiagnosticsLines() const {
    const bool en = english();
    const auto seconds = [en](uint64_t ms) {
        if (ms < 1000) return std::to_wstring(ms) + L" ms";
        const auto tenths = (ms + 50) / 100;
        return std::to_wstring(tenths / 10) + (en ? L"." : L",") + std::to_wstring(tenths % 10) + L" s";
    };
    std::vector<std::wstring> out;
    out.push_back(tr(L"Diagnostyka"));
    const auto now = GetTickCount64();
    if (companion_.busy && companion_.asked) out.push_back(std::wstring(tr(L"Czekam na model: ")) + seconds(now - companion_.asked) + L"…");
    const auto& recent = companion_.diagnostics;
    if (recent.empty()) {
        if (!companion_.busy) out.push_back(tr(L"Jeszcze nic nie zmierzono."));
        return out;
    }
    const auto& last = recent.back();
    const auto& timing = last.timing;
    out.push_back(std::wstring(tr(L"Ostatnia odpowiedź: ")) + seconds(last.total_ms) + (last.model.empty() ? std::wstring(L" · ") + tr(L"bez modelu (offline)") : L" · " + last.model));
    out.push_back(std::wstring(tr(L"kolejka ")) + seconds(timing.queued_ms) + tr(L" · przygotowanie ") + seconds(timing.prepare_ms) + tr(L" · model ") + seconds(timing.model_ms));
    for (size_t i = 0; i < timing.trace.calls.size(); ++i) {
        const auto& call = timing.trace.calls[i];
        std::wstring line = std::wstring(tr(L"zapytanie ")) + std::to_wstring(i + 1) + L": " + seconds(call.ms);
        if (call.status != 200) line += L" · HTTP " + std::to_wstring(call.status);
        if (call.prompt_tokens >= 0) line += L" · " + std::to_wstring(call.prompt_tokens) + L" → " + std::to_wstring(std::max(call.output_tokens, 0)) + tr(L" tokenów");
        if (call.thought_tokens > 0) line += tr(L" · myśli ") + std::to_wstring(call.thought_tokens);
        if (call.copies) line += tr(L" · kopie: ") + std::to_wstring(call.copies) + (call.answered_by ? tr(L", odpowiedziała kopia ") + std::to_wstring(call.answered_by)
                                                                                                   : std::wstring(tr(L", odpowiedział oryginał")));
        out.push_back(std::move(line));
    }
    if (!timing.trace.tools.empty()) {
        std::wstring line = tr(L"narzędzia: ");
        for (size_t i = 0; i < timing.trace.tools.size(); ++i)
            line += (i ? L", " : L"") + text_encoding::wideLenient(timing.trace.tools[i].name) + L" " + seconds(timing.trace.tools[i].ms);
        out.push_back(std::move(line));
    }
    if (last.voiced) out.push_back(last.voice_ms ? std::wstring(tr(L"głos: pierwsze zdanie po ")) + seconds(*last.voice_ms) :
                                   std::wstring(tr(L"głos: czeka… ")) + seconds(now - last.arrived));
    if (last.chars)
        out.push_back(std::wstring(tr(L"pisanie: ok. ")) + seconds(static_cast<uint64_t>(last.typing_seconds * 1000)) + L" · " + std::to_wstring(last.chars) + tr(L" znaków") +
                      (last.messages > 1 ? L" · " + std::to_wstring(last.messages) + tr(L" wiadomości") : L""));
    std::wstring prompt = std::wstring(tr(L"prompt: ")) + std::to_wstring(timing.system_chars + timing.turn_chars) + tr(L" znaków") + L" · " + std::to_wstring(timing.turns) + tr(L" tur");
    if (timing.tools) prompt += tr(L" · narzędzia");
    if (last.timing.burst) prompt += tr(L" · krótkie wiadomości");
    out.push_back(std::move(prompt));
    if (recent.size() > 1) {
        std::wstring earlier = tr(L"wcześniej: ");
        for (size_t i = recent.size() - 1; i-- > 0;) earlier += seconds(recent[i].total_ms) + (i ? L" · " : L"");
        out.push_back(std::move(earlier));
    }
    // This conversation, measured: how often she asks, her length against his, repeated openings and
    // stories, her curiosity's three paths and what was on her mind most.
    {
        int replies = 0, questions = 0, repeats = 0, retold = 0, her = 0, his = 0, clarify = 0, lead = 0, reflect = 0;
        std::map<std::string, int> sources;
        for (const auto& m : companion_.turn_metrics) {
            if (m.conversation != companion_.chat_started) continue;
            ++replies; questions += m.question; repeats += m.repeated_opening; retold += m.retold;
            her += m.her_words; his += m.his_words;
            clarify += m.curiosity == companion::CuriousMove::clarify;
            lead += m.curiosity == companion::CuriousMove::lead;
            reflect += m.curiosity == companion::CuriousMove::reflect;
            for (const auto& source : m.workspace) ++sources[source];
        }
        if (replies) {
            const int ratio_tenths = his ? (her * 10 + his / 2) / his : 0;
            out.push_back(std::wstring(tr(L"rozmowa: ")) + std::to_wstring(replies) + tr(L" odpowiedzi · pytania ") + std::to_wstring(questions * 100 / replies) + L"%" +
                          (his ? std::wstring(tr(L" · jej/jego długość ")) + std::to_wstring(ratio_tenths / 10) + (en ? L"." : L",") + std::to_wstring(ratio_tenths % 10) + L"×" : std::wstring{}));
            out.push_back(std::wstring(tr(L"ciekawość: dopytania ")) + std::to_wstring(clarify) + tr(L" · pociągnięcia ") + std::to_wstring(lead) + tr(L" · odbicia ") +
                          std::to_wstring(reflect) + tr(L" · powtórzone początki ") + std::to_wstring(repeats) + tr(L" · powtórzone historie ") + std::to_wstring(retold));
            std::vector<std::pair<std::string, int>> ranked(sources.begin(), sources.end());
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            if (!ranked.empty()) {
                std::wstring line = tr(L"w głowie: ");
                for (size_t i = 0; i < ranked.size() && i < 5; ++i)
                    line += (i ? L", " : L"") + text_encoding::wideLenient(ranked[i].first) + L" ×" + std::to_wstring(ranked[i].second);
                out.push_back(std::move(line));
            }
        }
    }
    return out;
}

void Launcher::companionView(CompanionView& view) const {
    view.feed = &companion_.feed.view;
    view.look = lookOf(settings_);
    view.name = companion::name(view.look);
    view.persona = companion::personaName(companionPersona());
    const auto device = hintDevice();
    view.key = device == HintDevice::keyboard ? L"C" : device == HintDevice::playstation ? L"L2" : device == HintDevice::nintendo ? L"ZL" : L"LT";
    // The model only when asked for (Settings → "Pokaż model w czacie"); problems always.
    if (!companion_.status.empty()) view.status = companion_.status;
    else if (settings_.companion_model_info) {
        companion::Connection shown;
        shown.provider = companionAccess().provider; shown.model = companionAccess().model;
        view.status = companion_.model_label.empty() ? shown.label() : companion_.model_label;
    } else view.status.clear();
    // Her voice is credited to ElevenLabs (their free plan asks for it), with the voice she speaks with;
    // when it fails, why.
    if (companionVoiceFor(true) && companion_.voice.engine == CompanionVoice::Engine::ready) {
        const auto name = companionVoiceName(companionLook());
        view.status += (view.status.empty() ? L"" : L" · ") + std::wstring(L"ElevenLabs") + (name.empty() ? L"" : L": " + name);
        if (const auto problem = companionVoiceProblem(); !problem.empty()) view.status += L" (" + problem + L")";
    } else if (companionVoiceFor(true) && companion_.voice.engine == CompanionVoice::Engine::failed)
        view.status += (view.status.empty() ? L"" : L" · ") + std::wstring(tr(L"Głos: ")) + companionVoiceProblem();
    view.pose = companion_.rig.pose();
    const auto spots = homeWidgetSpots();
    const bool shown = settings_.companion && companionAvailable(true) && companion_.tile_progress > 0 && spots.companion;
    view.tile = shown && home_widgets::floating(spots.placement);
    view.pill = shown && spots.placement == home_widgets::Placement::pills;
    if (spots.companion) view.spot = *spots.companion;
    view.tile_alpha = settings_.screen_transitions ? glance_ui::ease(companion_.tile_progress) : 1;
    view.tile_hover = companion_.hover_motion.value;
    view.entrance = companion_.entrance;
    const auto& bubble = companion_.bubble;
    view.bubble = bubble.text.substr(0, std::min(bubble.reveal, bubble.text.size()));
    view.bubble_mentions = bubble.mentions;
    view.bubble_alpha = bubble.alpha;
    view.bubble_waiting = bubble.waiting;
    view.chat = companionOpen() ? &companion_.chat : nullptr;
    view.picker = companion_.picker.open ? &companion_.picker : nullptr;
    view.link_pointer = topModal() == Modal::none && !companion_.chat.menu && !companion_.chat.confirm && !companion_.chat.viewer && !companion_.chat.mind && !companion_.chat.feed &&
        !friends_open_ && !desktop_menu_ && !desktop_quick_.open && !desktop_meta_.open && !desktop_downloads_open_ &&
        !keyboard_.open ? companion_.link_pointer : std::nullopt;
    view.photo = companion_.photo;
    view.chat_note = companionChatNote();
    view.can_restart = companion_.chat.ended && !companionResting();
    if (companion_.chat.archive) // A history page: when it was, in the header.
        view.status = std::wstring(tr(L"Historia")) + L" · " + companion::chats::label(companion_.archive_started, english());
    view.diagnostics = settings_.companion_debug && companionOpen() ? companionDiagnosticsLines() : std::vector<std::wstring>{};
    if (companionOpen() && (companion_.chat.mind || companion_.chat.mind_motion > 0.003f)) {
        view.mind_nodes = companionMindNodes();
        view.mind_telemetry = companionMindTelemetry();
        // Her mind's own words speak of you in both genders (‹on|ona›): as you are.
        const auto gender = companionPerson().gender;
        for (auto& node : view.mind_nodes)
            for (auto* text : {&node.label, &node.category, &node.detail, &node.when, &node.feeling_label})
                if (text->find(companion::person_open) != std::wstring::npos) *text = companion::personalize(*text, gender);
        auto& tel = view.mind_telemetry;
        for (auto* text : {&tel.bio, &tel.live_thought, &tel.live_thought_kind, &tel.mood})
            if (text->find(companion::person_open) != std::wstring::npos) *text = companion::personalize(*text, gender);
    } else {
        view.mind_nodes.clear();
        view.mind_telemetry = {};
    }
    // Gamification: the host's level and sympathy (loaded by tickCompanion).
    view.affinity = settings_.companion_gamification && companion_.affinity.has_value();
    view.affinity_pulse = companion_.affinity_pulse;
    view.affinity_card_pulse = companion_.affinity_card_pulse;
    view.streak = 0; view.affinity_card = 0;
    const auto count = static_cast<size_t>(companion::lookCount());
    view.preview_levels.assign(count, 0);
    view.preview_costumes.assign(count, 0);
    if (view.affinity) {
        const auto standing = companion::affinity::standing(companion::affinity::pointsOf(companion_.affinity->points, companion::lookKey(view.look)));
        view.affinity_level = standing.level; view.affinity_progress = standing.progress;
        const int today = dateKey(companion_.clock());
        view.streak = companion_.affinity->currentStreak(today);
        view.affinity_card = static_cast<int>(companion_.affinity->card(companion::lookKey(view.look), today));
        for (size_t i = 0; i < count; ++i) view.preview_levels[i] = companionLevel(static_cast<companion::Look>(i));
    }
    view.costume = companionCostume(view.look);
    for (size_t i = 0; i < count; ++i) view.preview_costumes[i] = companionCostume(static_cast<companion::Look>(i));
    // A guest in the chat: beside her, smaller, coming in or running off.
    view.guest.reset(); view.duo = 0; view.guest_leaving = false; view.guest_costume = 0;
    if (const auto& guest = companion_.guest; guest.look && *guest.look != view.look) {
        view.guest = guest.look;
        view.guest_costume = companionCostume(*guest.look);
        view.guest_name = companion::name(*guest.look);
        view.guest_persona = companion::personaName(companionPersonaOf(*guest.look));
        view.guest_pose = guest.rig.pose();
        view.duo = guest.motion;
        view.guest_leaving = !guest.shown;
    }
    view.previews.assign(count, avatar::Pose{});
    const auto& picker = companion_.picker;
    const auto preview = static_cast<size_t>(picker.open && !picker.choices.empty() ? picker.choices[static_cast<size_t>(picker.focus)] : companionLook());
    if (preview < count && preview < companion_.previews.size()) view.previews[preview] = companion_.previews[preview].pose();
}
