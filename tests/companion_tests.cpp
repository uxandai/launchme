// Companion: reply parsing, prompts and characters, connection resolution, offline lines, her
// reactions to launcher events (queue, library watch, sessions), the roulette and the PNGtuber
// rig, all without a network or this machine's environment. `--live` asks the real Foundry model
// once (manual check, never in CTest).
#include "companion_fixture.hpp"
#include "../src/net/companion_affinity.hpp"
#include "../src/net/companion_characters.hpp"
#include "../src/net/companion_chats.hpp"
#include "../src/net/companion_cognition.hpp"
#include "../src/net/companion_costumes.hpp"
#include "../src/net/companion_events.hpp"
#include "../src/net/companion_memory.hpp"
#include "../src/net/companion_mood.hpp"
#include "../src/net/companion_person.hpp"
#include "../src/net/companion_photos.hpp"
#include "../src/net/companion_week.hpp"
#include "../src/net/companion_weather.hpp"
#include "../src/net/companion_providers.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/net/companion_tools.hpp"
#include "../src/net/elevenlabs.hpp"
#include "../src/platform/wav.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/net/json.hpp"
#include "../src/ui/avatar.hpp"
#include "../src/platform/text_encoding.hpp"

#include <Windows.h>

#include <cmath>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {
// The test's companions (companion_fixture.hpp, installed in main): never the built-in ones.
constexpr companion::Look hana_look = companion_fixture::hana;
constexpr companion::Look rin_look = companion_fixture::rin;
// A RIFF/WAVE file: 16-bit PCM, `frames` of a quiet tone.
std::vector<uint8_t> wavFixture(uint32_t rate, uint16_t channels, uint32_t frames) {
    std::vector<uint8_t> bytes;
    const auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    const auto put16 = [&](uint16_t v) { bytes.push_back(static_cast<uint8_t>(v)); bytes.push_back(static_cast<uint8_t>(v >> 8)); };
    const auto text = [&](const char* tag) { bytes.insert(bytes.end(), tag, tag + 4); };
    const uint32_t data = frames * channels * 2;
    text("RIFF"); put32(36 + 8 + data); text("WAVE");
    text("fmt "); put32(16); put16(1); put16(channels); put32(rate); put32(rate * channels * 2); put16(static_cast<uint16_t>(channels * 2)); put16(16);
    text("LIST"); put32(4); text("INFO"); // An extra chunk before the samples.
    text("data"); put32(data);
    for (uint32_t i = 0; i < frames * channels; ++i) put16(static_cast<uint16_t>(static_cast<int16_t>(4000 * std::sin(i * 0.05))));
    return bytes;
}

std::vector<companion::GameFact> fixtureGames() {
    std::vector<companion::GameFact> games;
    const auto add = [&](const wchar_t* title, bool installed, uint64_t played, std::optional<uint32_t> minutes, uint32_t unlocked = 0, uint32_t total = 0) {
        companion::GameFact game;
        game.title = title; game.key = "steam:" + std::to_string(games.size() + 10); game.store = L"Steam";
        game.installed = installed; game.last_played = played; game.minutes = minutes;
        game.achievements_unlocked = unlocked; game.achievements_total = total;
        games.push_back(std::move(game));
    };
    add(L"Elden Ring", true, 1'760'000'000, 7200, 23, 42);
    add(L"Portal 2", true, 1'759'000'000, 600, 51, 51);
    add(L"Hades", true, 0, std::nullopt);
    add(L"Disco Elysium", false, 0, std::nullopt);
    add(L"Cyberpunk 2077", false, 1'700'000'000, 30, 2, 57);
    add(L"   ", true, 0, std::nullopt); // A nameless entry never reaches the prompt.
    return games;
}

int units() {
    using namespace companion;
    // Tags: emotions (with synonyms), stray tags (old gesture tags too), suggestions and markdown.
    auto reply = parseText(L"[excited] Yahallo! [nod]Zagraj w [[Portal 2]] albo *[[Hades]]*. [blush]Albo... [foo]nie.");
    if (!reply.ok || reply.text != L"Yahallo! Zagraj w Portal 2 albo Hades. Albo... nie.") return 100;
    if (reply.emotions.size() != 2 || reply.emotions[0].at != 0 || reply.emotions[0].emotion != Emotion::excited ||
        reply.emotions[1].emotion != Emotion::shy || reply.text.substr(reply.emotions[1].at, 4) != L"Albo") return 101;
    if (parseText(L"[happy]a").emotions[0].emotion != Emotion::smile || parseText(L"[neutral]a").emotions[0].emotion != Emotion::cool ||
        parseText(L"[smug][nod]a").emotions[0].emotion != Emotion::smug || parseText(L"[crazy]a").emotions[0].emotion != Emotion::playful) return 102;
    if (reply.games != std::vector<std::wstring>{L"Portal 2", L"Hades"}) return 103;
    if (reply.mentions != std::vector<GameMention>{{18, 8}, {32, 5}}) return 1400;
    {
        const auto linked = parseText(L"  \U0001F3AE [smile] [[ Żółw  2 ]] , **[[Żółw 2]]**! [thinking][[Hades]] ");
        if (linked.text != L"\U0001F3AE Żółw 2, Żółw 2! Hades" || linked.mentions.size() != 3) return 1401;
        const std::vector<std::wstring> titles{L"Żółw 2", L"Żółw 2", L"Hades"};
        for (size_t i = 0; i < titles.size(); ++i)
            if (linked.text.substr(linked.mentions[i].at, linked.mentions[i].length) != titles[i]) return 1402;
        if (!parseText(L"[smile]Jeszcze [[Hades").mentions.empty() || !parseText(L"[[]] [[  ]] Zwykły tekst").mentions.empty()) return 1403;
    }
    reply = parseText(L"Bez tagu.");
    if (!reply.ok || reply.emotions.size() != 1 || reply.emotions[0].emotion != calm) return 104;
    if (parseText(L"[happy]").ok || parseText(L"  ").ok) return 105;
    if (parseText(L"[sad] Wynik [2] i [a b c d e f g h i j k l m n o p q r s t] zostaje.").text != L"Wynik [2] i [a b c d e f g h i j k l m n o p q r s t] zostaje.") return 106;
    // Keys round-trip; unknown keys are rejected.
    for (int i = 0; i < emotion_count; ++i) if (emotionFromKey(emotionKey(static_cast<Emotion>(i))) != static_cast<Emotion>(i)) return 107;
    if (emotionFromKey("wave") != Emotion::wave || emotionFromKey("") || emotionFromKey("bogus")) return 108;
    if (lookFromKey("rin") != rin_look || lookFromKey("hana") != hana_look || lookKey(rin_look) != "rin" || personaFromKey("yandere") != Persona::yandere || personaFromKey("x") || lookFromKey("")) return 109;
    if (std::wstring(name(hana_look)) != L"Hana" || std::wstring(name(rin_look)) != L"Rin" || modelLabel("gemini-3-8-flash") != L"Gemini 3.8 Flash") return 110;

    // Digest and prompt: counts, recent order, untouched and backlog, local data only.
    const auto library = digest(fixtureGames(), 5);
    if (library.total != 5 || library.installed != 3 || library.never_played != 2) return 120;
    if (library.recent.size() != 3 || library.recent[0].title != L"Elden Ring" || library.untouched.size() != 1 || library.backlog.size() != 1) return 121;
    if (!library.find(L"portal 2") || !library.find(L"PORTAL-2") || library.find(L"Doom")) return 122;
    const auto text = library.text(false, 1'760'100'000);
    if (text.find(L"Elden Ring [Steam] — wczoraj, 120 godz., osiągnięcia 23/42 (54%)") == std::wstring::npos ||
        text.find(L"Zainstalowane, ale nigdy nieuruchomione: Hades") == std::wstring::npos || text.find(L"Disco Elysium") == std::wstring::npos) return 123;
    if (library.text(true, 1'760'100'000).find(L"Elden Ring [Steam] — yesterday, 120 h") == std::wstring::npos) return 124;
    const auto prompt = systemPrompt(Persona::tsundere, rin_look, library, false, 1'760'100'000, 3, 21);
    if (prompt.find(L"Rin") == std::wstring::npos || prompt.find(L"tsundere") == std::wstring::npos || prompt.find(L"[[Tytuł]]") == std::wstring::npos ||
        prompt.find(L"środa, godz. 21:00") == std::wstring::npos || prompt.find(L"Cyberpunk 2077") == std::wstring::npos) return 125;
    // Every character: a key that round-trips, a name and its own personality in the prompt.
    for (int i = 0; i < persona_count; ++i) {
        const auto persona = static_cast<Persona>(i);
        if (personaFromKey(personaKey(persona)) != persona || std::wstring(personaName(persona)).empty()) return 126;
        const std::string key = personaKey(persona);
        const std::wstring word(key.begin(), key.end());
        if (systemPrompt(persona, hana_look, library, true, 0, 0, 9).find(word) == std::wstring::npos ||
            systemPrompt(persona, rin_look, library, false, 0, 0, 9).find(word) == std::wstring::npos) return 127;
    }
    if (std::wstring(personaName(Persona::himedere)) != L"Himedere" || personaFromKey("kuudere") != Persona::kuudere) return 128;
    // Today's date and time: "dd.mm.yyyy, hh:mm" (she must not take her training date for today).
    {
        PromptExtras dated;
        dated.today = {2026, 10, 15, 7};
        const auto pl = systemPrompt(Persona::genki, hana_look, library, false, 0, 4, 9, dated);
        const auto en = systemPrompt(Persona::genki, hana_look, library, true, 0, 4, 9, dated);
        if (pl.find(L"czwartek, 15.10.2026, godz. 09:07") == std::wstring::npos || pl.find(L"premiery") == std::wstring::npos) return 7801;
        if (en.find(L"Thursday, October 15, 2026, 09:07") == std::wstring::npos) return 7802;
    }
    // Gamification: points → level 1–50 (each level harder) and the bar towards the next one.
    {
        namespace affinity = companion::affinity;
        if (affinity::standing(0).level != 1 || affinity::standing(0).progress != 0) return 7803;
        if (affinity::standing(affinity::step(1)).level != 2 || affinity::standing(affinity::step(1) / 2).progress != 0.5f) return 7804;
        if (affinity::standing(affinity::max_points).level != affinity::max_level || affinity::standing(affinity::max_points * 3).level != affinity::max_level ||
            affinity::standing(affinity::max_points).progress != 1.0f || affinity::standing(-5).level != 1) return 7805;
        for (int level = 1; level < affinity::max_level; ++level) if (affinity::step(level + 1) <= affinity::step(level)) return 7807;
        // Six stages: strangers, a light flirt from 5, more flirt from 10, flirty friends from 20, a crush
        // from 30 and a long-distance relationship at the top (50); the top takes about three months.
        if (affinity::max_level != 50 || affinity::stage(1) != affinity::Stage::strangers || affinity::stage(4) != affinity::Stage::strangers ||
            affinity::stage(5) != affinity::Stage::light_flirt || affinity::stage(10) != affinity::Stage::flirt || affinity::stage(20) != affinity::Stage::friends ||
            affinity::stage(30) != affinity::Stage::crush || affinity::stage(49) != affinity::Stage::crush || affinity::stage(50) != affinity::Stage::long_distance) return 7808;
        if (affinity::max_points < 30000 || affinity::max_points > 42000 || affinity::threshold(5) < 800 || affinity::threshold(5) > 1500) return 7847;
        // The file: points, the streak and her suggestions.
        auto state = affinity::parse("hana=130\r\nrin=bogus\nblue=4\nday=20261014\nstreak=2\npick=20261010 steam:620\npick=bad\n");
        if (affinity::pointsOf(state.points, "hana") != 130 || affinity::pointsOf(state.points, "rin") != 0 || affinity::pointsOf(state.points, "blue") != 4 || state.day != 20261014 || state.streak != 2 || state.picks.size() != 1 ||
            state.picks[0].key != "steam:620" || affinity::pointsOf(affinity::parse("mei=7\nBad=3\n").points, "mei") != 7 || affinity::parse("Bad=3\n").points.size()) return 7806;
        // The first version's looks: white and red are Yuki's and Akane's points (the larger one wins).
        const auto legacy = affinity::parse("white=50\nyuki=20\nred=-3\n");
        if (affinity::pointsOf(legacy.points, "yuki") != 50 || affinity::pointsOf(legacy.points, "akane") != 0 || legacy.points.contains("white") || legacy.points.contains("red")) return 7830;
        const auto again = affinity::parse(affinity::serialize(state));
        if (again.points != state.points || again.day != state.day || again.streak != state.streak || again.picks.size() != 1 || again.picks[0].day != 20261010) return 7809;
        // Days in a row: across months and years; a missed day starts again.
        if (!affinity::dayAfter(20261031, 20261101) || !affinity::dayAfter(20261231, 20270101) || !affinity::dayAfter(20240228, 20240229) ||
            affinity::dayAfter(20261015, 20261017) || affinity::dayAfter(0, 20261015)) return 7810;
        if (state.visit(20261015) != affinity::dailyBonus(3) || state.streak != 3 || state.visit(20261015) != 0 || state.currentStreak(20261016) != 3) return 7811;
        if (state.currentStreak(20261017) != 0 || state.visit(20261017) != affinity::dailyBonus(1) || state.streak != 1) return 7812;
        // Her suggestion pays once, within two weeks.
        if (!state.takeSuggestion("steam:620", 20261015) || state.takeSuggestion("steam:620", 20261015)) return 7813;
        state.suggested("steam:10", 20260901);
        if (state.takeSuggestion("steam:10", 20261015)) return 7814;
        // Her cards: a yellow one first (nothing lost), a red one while it is live (a small share of
        // the level's step, never below the level's threshold); none from level 30 on.
        {
            affinity::State cards;
            const int64_t mid = affinity::threshold(4) + affinity::step(4) / 2; // Halfway through level 4.
            cards.points["hana"] = mid;
            auto booked = cards.book("hana", 20261015);
            if (booked.card != affinity::Card::yellow || booked.lost || cards.points["hana"] != mid || cards.card("hana", 20261015) != affinity::Card::yellow ||
                cards.card("hana", 20261016) != affinity::Card::yellow || cards.card("hana", 20261017) != affinity::Card::none ||
                cards.card("rin", 20261015) != affinity::Card::none) return 7831;
            booked = cards.book("hana", 20261016);
            const int64_t share = affinity::step(4) * affinity::penalty_percent / 100;
            if (booked.card != affinity::Card::red || booked.lost != share || cards.points["hana"] != mid - share || cards.card("hana", 20261016) != affinity::Card::red) return 7832;
            // Just levelled up: a red card takes only what is above the threshold, then nothing.
            cards.points["hana"] = affinity::threshold(4) + 3;
            if (cards.book("hana", 20261016).lost != 3 || cards.points["hana"] != affinity::threshold(4) || cards.book("hana", 20261016).lost != 0 ||
                affinity::level(cards.points["hana"]) != 4) return 7833;
            // Two clean days: yellow again.
            if (cards.book("hana", 20261019).card != affinity::Card::yellow) return 7834;
            // From level 30 on she no longer minds; at the top or at a threshold nothing is lost; at least a message's worth.
            cards.points["rin"] = affinity::threshold(affinity::boundary_level);
            if (cards.book("rin", 20261015).card != affinity::Card::none || cards.cards.contains("rin") || affinity::penalty(affinity::max_points) != 0 ||
                affinity::penalty(0) != 0 || affinity::penalty(affinity::step(1) - 1) != std::max<int64_t>(affinity::step(1) * affinity::penalty_percent / 100, affinity::per_message)) return 7835;
            // The file keeps them; bad lines go, and "card" is never a character's points.
            const auto kept = affinity::parse(affinity::serialize(cards) + "card=bad\ncard=20261015 green mei\ncard=20261015 red\n");
            if (kept.cards.size() != 1 || kept.card("hana", 20261019) != affinity::Card::yellow ||
                affinity::parse("card=20261015 red rin\n").card("rin", 20261016) != affinity::Card::red || kept.points.contains("card")) return 7836;
            // Her [granica] / [boundary] tag: never shown; flirting carries none.
            const auto set = parseText(L"[granica][angry]Hej, hamuj. Nie tak szybko.");
            if (!set.boundary || set.text != L"Hej, hamuj. Nie tak szybko." || set.emotions.front().emotion != Emotion::angry ||
                !parseText(L"[Boundary] [shy]Too far.").boundary || parseText(L"[shy]Flirciarz z ciebie~").boundary) return 7837;
        }
    }
    // Her prompt: the relationship's stage, the streak and a guest's rivalry, only with a level; her costume.
    {
        PromptExtras bonded;
        bonded.bond = {20, 3, 12};
        bonded.guest = Guest{rin_look, Persona::yandere};
        bonded.outfit = L"Kimono";
        const auto pl = companion::personalize(systemPrompt(Persona::tsundere, hana_look, library, false, 0, 4, 9, bonded), Gender::male);
        if (pl.find(L"poziom 20 na 50") == std::wstring::npos || pl.find(L"przyjaciółmi") == std::wstring::npos || pl.find(L"3 dni z rzędu") == std::wstring::npos ||
            pl.find(L"Poziom Rin u niego to 12") == std::wstring::npos || pl.find(L"masz na sobie: Kimono") == std::wstring::npos ||
            pl.find(L"gracz", pl.find(L"nigdy „gracz”") + 14) != std::wstring::npos) return 7815; // "gracz" only where she is told it is not a word for him.
        if (systemPrompt(Persona::tsundere, hana_look, library, true, 0, 4, 9, bonded).find(L"level 20 of 50") == std::wstring::npos) return 7816;
        if (systemPrompt(Persona::tsundere, hana_look, library, false, 0, 4, 9).find(L"Twoja relacja") != std::wstring::npos) return 7817;
        // Her boundaries: alone and below level 50 the rule (flirting is not crossing a line); never in
        // a duo or from level 50 on. A live red card cools her.
        {
            PromptExtras early;
            early.bond = {12, 0, 0, 2};
            const auto boundaries = systemPrompt(Persona::tsundere, hana_look, library, false, 0, 4, 9, early);
            if (boundaries.find(L"[granica]") == std::wstring::npos || boundaries.find(L"flirt") == std::wstring::npos ||
                boundaries.find(L"wyraźnie chłodniejsza") == std::wstring::npos ||
                systemPrompt(Persona::tsundere, hana_look, library, true, 0, 4, 9, early).find(L"[boundary]") == std::wstring::npos) return 7838;
            early.bond = {companion::affinity::boundary_level, 0, 0, 0};
            if (systemPrompt(Persona::tsundere, hana_look, library, false, 0, 4, 9, early).find(L"[granica]") != std::wstring::npos || pl.find(L"[granica]") != std::wstring::npos) return 7839;
        }
        // A level-up remark away from the chat.
        Event up{EventKind::level_up};
        up.count = 12; up.titles = {L"Kimono"};
        if (eventKickoff(up, false).find(L"poziom 12") == std::wstring::npos || eventLine(Persona::genki, up, false, 1).find(L"Kimono") == std::wstring::npos) return 7818;
    }
    // Characters: the manifest CMake builds from res/companion/<key>/character.txt and the costume
    // folders; any number of them, bad keys skipped (day, streak, pick and card too), a BOM ignored, an
    // unnamed one named by her key (never dropped: her index picks her pictures), costumes ordered.
    {
        namespace costumes = companion::costumes;
        const auto list = parseCharacters(
            "[hana]\n\xEF\xBB\xBFname=Hana\n# a comment\norder=1\ncolor=#FF8000\npersona=kuudere\ncard_pl=Różowe włosy\nyou_pl=Masz różowe włosy.\nher_pl=Ma różowe włosy.\n"
            "costume=1 10 costume_10_school-uniform\r\ncostume.name_pl=Mundurek szkolny\ncostume.you_pl=Masz na sobie mundurek.\n"
            "costume.face_smile=270.5 214 160\ncostume.face_cool= 280 230 150 \ncostume.face_nope=1 2 3\ncostume.face_sad=1 2\ncostume.face_shy=1 2 0\n"
            "costume=0 0 costume_00\ncostume=bad\ncostume=2 100 costume_x\n"
            "[Bad Key]\nname=Nope\n[day]\nname=Day\n[nameless]\ncolor=#000000\n[soon]\nname=???\nstatus=soon\ncostume=0 0 costume_00\n");
        if (list.size() != 3 || list[0].key != "hana" || list[0].name != L"Hana" || list[0].persona != Persona::kuudere || list[0].soon ||
            list[1].key != "nameless" || list[1].name != L"Nameless" || list[1].soon || !list[2].soon) return 7819;
        if (list[0].r != 1.0f || list[0].b != 0.0f || list[0].appearance(false, false) != L"Ma różowe włosy." || list[0].appearance(true, true) != L"Masz różowe włosy." ||
            list[0].card(true) != L"Różowe włosy") return 7825;
        const auto& worn = list[0].costumes;
        if (worn.size() != 2 || worn[0].slot != 0 || worn[1].level != 10 || list[2].costumes[0].look != static_cast<Look>(2)) return 7826;
        if (costumes::name(worn[1]) != L"Mundurek szkolny" || worn[1].you_pl != L"Masz na sobie mundurek." || !costumes::name(worn[0]).empty() ||
            costumes::name(costumes::Costume{Look{}, 3, 10, "costume_10_school-uniform"}) != L"School uniform" || costumes::find(worn, static_cast<Look>(0), "costume_10_school-uniform") != &worn[1] ||
            costumes::find(worn, static_cast<Look>(1), "costume_10_school-uniform")) return 7820;
        // faces.txt (tools/companion_faces.py): her face per feeling; a bad or empty line leaves it unknown.
        const auto& faces = worn[1].faces;
        const auto face = [&](Emotion e) { return faces[static_cast<size_t>(e)]; };
        if (face(Emotion::smile).x != 270.5f || face(Emotion::smile).y != 214 || face(Emotion::smile).size != 160 || face(Emotion::cool).size != 150 ||
            face(Emotion::sad).size != 0 || face(Emotion::shy).size != 0 || worn[0].faces[static_cast<size_t>(Emotion::smile)].size != 0) return 7828;
        if (!characterKey("hana") || !characterKey("mei-2") || characterKey("") || characterKey("Hana") || characterKey("day") || characterKey("streak") ||
            characterKey("pick") || characterKey("card") || characterKey(std::string(33, 'a'))) return 7827;
        // The test's own two (companion_fixture.hpp), each with her fixed character.
        if (lookCount() != 2 || character(hana_look)->persona != Persona::himedere || character(rin_look)->persona != Persona::yandere || availableLooks().size() != 2) return 7829;
    }
    // Photos by level: only from <folder>/<her key>/, its own files always, its <from-to>/ folders
    // with her level; nothing of anyone else's and nothing loose in the folder.
    {
        namespace photos = companion::photos;
        if (photos::rangeStart(L"10-19") != 10 || photos::rangeStart(L"00-09") != 0 || photos::rangeStart(L"abc") != -1 || photos::rangeStart(L"19-10") != -1) return 7821;
        const auto root = std::filesystem::temp_directory_path() / (L"launchme-photos-" + std::to_wstring(GetCurrentProcessId()));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        for (const auto* file : {L"hana/00-09/a.png", L"hana/10-19/b.png", L"hana/notes/c.png", L"hana/50-59/top.png", L"shared.png", L"60-69/e.png", L"rin/30-39/d.png"}) {
            std::filesystem::create_directories((root / file).parent_path(), ec);
            atomic_file::write(root / file, std::string_view("\x89PNG"));
        }
        const auto low = photos::pick(root, hana_look, 5, {}, 1);
        const auto high = photos::pick(root, hana_look, 15, {L"hana\\00-09\\a.png"}, 1);
        const auto top = photos::pick(root, hana_look, companion::affinity::max_level, {L"hana\\00-09\\a.png", L"hana\\10-19\\b.png"}, 1);
        const auto locked = photos::pick(root, rin_look, 20, {}, 1);
        const auto opened = photos::pick(root, rin_look, 30, {}, 1);
        const auto nobody = photos::pick(root, static_cast<Look>(7), 99, {}, 1);
        std::filesystem::remove_all(root, ec);
        if (!low || low->filename() != L"a.png" || !high || high->filename() != L"b.png" || !top || top->filename() != L"top.png") return 7822;
        if (locked || !opened || opened->filename() != L"d.png" || nobody) return 7823;
        if (photos::pick(root, hana_look, 99, {}, 1)) return 7824; // No folder at all: nothing.
    }
    // The home greeting knows the time of day.
    if (dayPart(1) != DayPart::night || dayPart(23) != DayPart::night || dayPart(5) != DayPart::morning || dayPart(12) != DayPart::afternoon ||
        dayPart(18) != DayPart::evening) return 129;
    if (kickoff(Kickoff::home, false, 1).find(L"późna noc") == std::wstring::npos || kickoff(Kickoff::home, true, 9).find(L"morning") == std::wstring::npos ||
        kickoff(Kickoff::home, false).find(L"Jest ") != std::wstring::npos) return 119;

    // Request body: valid JSON, alternating roles starting with the player, merged repeats.
    const auto body = requestBody(L"System \"quoted\"\nline", {{false, L"orphan"}, {true, L"Cześć"}, {true, L"jeszcze"}, {false, L"[happy]Hej"}}, 300, 1.0f);
    const auto parsed = json::parse(body);
    if (!parsed || !parsed->get("contents") || parsed->get("contents")->elements.size() != 2) return 130;
    const auto& first = parsed->get("contents")->elements[0];
    if (first.get("role")->stringOr() != "user" || first.get("parts")->elements[0].get("text")->stringOr() != "Cześć\njeszcze") return 131;
    if (parsed->get("generationConfig")->get("maxOutputTokens")->intOr() != 300 ||
        parsed->get("systemInstruction")->get("parts")->elements[0].get("text")->stringOr() != "System \"quoted\"\nline") return 132;

    // Responses: model text (thoughts skipped), HTTP errors and empty candidates.
    const std::string ok = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"x","thought":true},{"thoughtSignature":"abc","text":"[smug]Znowu [[Elden Ring]]?"}]},"finishReason":"STOP"}]})";
    reply = parseResponse({200, {ok.begin(), ok.end()}});
    if (!reply.ok || reply.text != L"Znowu Elden Ring?" || reply.emotions[0].emotion != Emotion::smug || reply.games.size() != 1) return 140;
    const std::string denied = R"({"error":{"code":401,"message":"Unauthorized"}})";
    reply = parseResponse({401, {denied.begin(), denied.end()}});
    if (reply.ok || reply.status != 401 || reply.error.find(L"Unauthorized") == std::wstring::npos) return 141;
    const std::string empty = R"({"candidates":[{"finishReason":"SAFETY"}]})";
    reply = parseResponse({200, {empty.begin(), empty.end()}});
    if (reply.ok || reply.error.find(L"SAFETY") == std::wstring::npos) return 142;

    // ask(): endpoint, Bearer header, thinking off for Flash, one retry after a transient error.
    Connection connection;
    connection.host = L"https://stack.example.palantirfoundry.test/"; connection.token = L"abcdefghijklmnopqrstuvwxyz.0123456789";
    int calls = 0;
    std::wstring seen_url, seen_headers; std::string seen_body;
    reply = ask(connection, L"sys", {{true, L"hej"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& request) {
        ++calls; seen_url = url; seen_headers = headers; seen_body = request;
        if (calls == 1) return HttpResponse{503, {}};
        return HttpResponse{200, {ok.begin(), ok.end()}};
    });
    if (!reply.ok || calls != 2 || seen_url != L"https://stack.example.palantirfoundry.test/api/v2/llm/proxy/google/v1/models/ri.language-model-service..language-model.gemini-3-8-flash:generateContent" ||
        seen_headers.find(L"Authorization: Bearer abcdefghijklmnopqrstuvwxyz.0123456789\r\n") != 0 || seen_body.find("\"thinkingBudget\":0") == std::string::npos ||
        !json::parse(seen_body)) return 150;
    connection.model = "gemini-3-1-pro"; calls = 1;
    ask(connection, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string& request) { seen_body = request; return HttpResponse{200, {ok.begin(), ok.end()}}; });
    if (seen_body.find("thinkingBudget") != std::string::npos || !json::parse(seen_body) || json::parse(seen_body)->get("generationConfig")->get("maxOutputTokens")->intOr() < 2048) return 151;
    calls = 0;
    reply = ask(Connection{}, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string&) { ++calls; return HttpResponse{}; });
    if (reply.ok || calls) return 152;

    // The token limit cut the reply (Flash's stray thoughts count against it): complete sentences
    // stay; with none complete she asks once more with a far larger limit.
    if (completeSentences(L"[smile]Hej! Zagraj w [[Hol") != L"[smile]Hej!" || completeSentences(L"Dobry wiecz") != L"" ||
        completeSentences(L"Ojej... no nic. [exc") != L"Ojej... no nic." || completeSentences(L"Zagraj w [[Portal 2.") != L"" ||
        completeSentences(L"Hej! Graj!<ja>やあ！遊") != L"Hej! Graj!" || completeSentences(L"Hej!<ja>やあ！</ja>") != L"Hej!<ja>やあ！</ja>" ||
        completeSentences(L"„Baka!” A potem") != L"„Baka!”") return 153;
    const std::string cut = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[excited]Dobry wieczór! Znowu [[Elden Ri"}]},"finishReason":"MAX_TOKENS"}]})";
    reply = parseResponse({200, {cut.begin(), cut.end()}});
    if (!reply.ok || !reply.truncated || reply.text != L"Dobry wieczór!" || !reply.games.empty()) return 154;
    const std::string stub = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[excited]Dobry wiecz"}]},"finishReason":"MAX_TOKENS"}],"usageMetadata":{"thoughtsTokenCount":190}})";
    connection.model = "gemini-3-8-flash"; calls = 0;
    std::vector<int> limits;
    reply = ask(connection, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string& request) {
        limits.push_back(json::parse(request)->get("generationConfig")->get("maxOutputTokens")->intOr());
        return ++calls == 1 ? HttpResponse{200, {stub.begin(), stub.end()}} : HttpResponse{200, {ok.begin(), ok.end()}};
    }, 200);
    if (!reply.ok || calls != 2 || limits.size() != 2 || limits[0] != 200 + thought_room || limits[1] <= limits[0]) return 155;

    // Resolution: Settings beat the environment, the environment beats opencode; opencode
    // supplies the plugin's literal token and a Foundry provider's host.
    std::map<std::wstring, std::wstring> env;
    std::map<std::wstring, std::string> files;
    Probe probe;
    probe.environment = [&](const wchar_t* key) { const auto found = env.find(key); return found == env.end() ? std::wstring{} : found->second; };
    probe.read_file = [&](const std::filesystem::path& file) -> std::optional<std::string> {
        const auto found = files.find(file.lexically_normal().wstring());
        if (found == files.end()) return std::nullopt;
        return found->second;
    };
    probe.opencode_dir = L"C:\\fake\\opencode";
    files[(probe.opencode_dir / L"plugins/connection.ts").lexically_normal().wstring()] =
        "// FOUNDRY_TOKEN is your password\nconst FOUNDRY_HOST = \"https://x/\"\nconst FOUNDRY_TOKEN = \"plugin.token.value.0123456789abcdef\"\n";
    files[(probe.opencode_dir / L"opencode.json").lexically_normal().wstring()] =
        "{\n // comment\n \"provider\": {\"ollama\": {\"options\": {\"baseURL\": \"http://localhost:11434/v1\"}},\n"
        " \"foundry-google\": {\"options\": {\"baseURL\": \"https://klima.example.palantirfoundry.co.uk/api/v2/llm/proxy/google/v1\", \"apiKey\": \"x\"},},\n}}";
    auto resolved = resolve(L"", L"", "", probe);
    if (!resolved.ready() || resolved.token != L"plugin.token.value.0123456789abcdef" || resolved.token_source != Source::opencode ||
        resolved.host != L"https://klima.example.palantirfoundry.co.uk/" || resolved.host_source != Source::opencode || resolved.model != default_model) return 160;
    env[L"FOUNDRY_TOKEN"] = L"environment.token.value.0123456789"; env[L"FOUNDRY_HOST"] = L"https://env.palantirfoundry.test/";
    resolved = resolve(L"", L"", "gemini-3-7-flash", probe);
    if (resolved.token_source != Source::environment || resolved.host != L"https://env.palantirfoundry.test/" || resolved.model != "gemini-3-7-flash") return 161;
    resolved = resolve(L"typed.palantirfoundry.test/api/x", L"settings.token.value.0123456789", "", probe);
    if (resolved.token_source != Source::settings || resolved.host != L"https://typed.palantirfoundry.test/" || resolved.host_source != Source::settings) return 162;
    env.clear(); files.clear();
    if (resolve(L"", L"", "", probe).ready()) return 163;
    if (!normalizeHost(L"http://insecure.test").empty() || normalizeHost(L" host.test ") != L"https://host.test/") return 164;

    // Offline lines: tagged like model replies, suggestions only from the library.
    for (int character = 0; character < persona_count; ++character)
        for (const auto kind : {Kickoff::chat, Kickoff::home})
            for (uint32_t seed = 0; seed < 6; ++seed) for (const bool english : {false, true}) {
                const auto persona = static_cast<Persona>(character);
                const auto line = parseText(offline(persona, kind, library, english, seed, seed % 2 ? 2 : 14));
                if (!line.ok || line.games.empty() || line.text.find(L'{') != std::wstring::npos) return 170;
                for (const auto& game : line.games) if (!library.find(game)) return 171;
                const auto answer = parseText(offlineAnswer(persona, library, english, seed));
                if (!answer.ok || answer.games.size() != 1 || !library.find(answer.games[0])) return 172;
            }
    const auto none = digest({}, 1);
    if (!parseText(offline(Persona::tsundere, Kickoff::home, none, false, 0)).games.empty() || !parseText(offlineAnswer(Persona::genki, none, true, 0)).ok) return 173;
    for (int character = 0; character < persona_count; ++character) // At night the greeting is a goodnight.
        if (parseText(offline(static_cast<Persona>(character), Kickoff::home, library, false, 0, 2)).emotions.front().emotion != Emotion::sleepy) return 174;

    // Her reactions: sessions (play time, achievements, completion, milestones), the remark queue,
    // the library watch, today's premieres and the roulette.
    if (milestone(590, 610) != 10 || milestone(0, 59) || milestone(5990, 6085) != 100 || milestone(55, 6100) != 100 || milestone(70, 80)) return 300;
    if (sessionEvent(L"Hades", 30'000, {}, {}) || sessionEvent(L"", 600'000, {}, {})) return 301;
    auto session = sessionEvent(L"Elden Ring", 95 * 60'000, {5990, 23, 42}, {6085, 25, 42});
    if (!session || session->kind != EventKind::session || session->minutes != 95 || session->achievements != 2 || session->unlocked != 25 ||
        session->total != 42 || session->completed || session->milestone != 100) return 302;
    session = sessionEvent(L"Portal 2", 20 * 60'000, {600, 50, 51}, {620, 51, 51});
    if (!session || !session->completed || eventKickoff(*session, false).find(L"100%") == std::wstring::npos) return 303;
    session = sessionEvent(L"Hades", 2 * 60'000, {std::nullopt, 0, 0}, {std::nullopt, 0, 0});
    if (!session || session->achievements || session->milestone || eventKickoff(*session, true).find(L"very short") == std::wstring::npos) return 304;
    const auto event = [](EventKind kind, std::vector<std::wstring> titles, uint32_t count = 1) {
        Event e; e.kind = kind; e.titles = std::move(titles); e.count = count; e.minutes = 42; e.hour = 1; e.minute = 7;
        return e;
    };
    for (const auto kind : {EventKind::launch, EventKind::session, EventKind::added, EventKind::installed, EventKind::uninstalled, EventKind::premieres, EventKind::late_night}) {
        const auto e = event(kind, kind == EventKind::late_night ? std::vector<std::wstring>{} : std::vector<std::wstring>{L"Hades", L"Portal 2"}, 2);
        for (const bool english : {false, true}) {
            const auto told = eventKickoff(e, english);
            if (told.front() != L'(' || told.back() != L')' || (!e.titles.empty() && told.find(L"Hades") == std::wstring::npos)) return 310;
            if (kind == EventKind::late_night && told.find(L"1:07") == std::wstring::npos) return 311;
            for (int character = 0; character < persona_count; ++character) {
                const auto line = parseText(eventLine(static_cast<Persona>(character), e, english, character));
                // Every game mention is linked, including uninstalled games and premieres.
                const bool linked = kind != EventKind::late_night;
                if (!line.ok || line.text.find(L'{') != std::wstring::npos || (linked && (line.games.empty() || line.games[0] != L"Hades")) || (!linked && !line.games.empty())) return 312;
            }
        }
    }
    if (parseText(eventLine(Persona::kuudere, event(EventKind::launch, {L"Hades"}), false, 0)).emotions.front().emotion != Emotion::cool) return 313;
    // Queue: the most important first, repeats merged, stale remarks dropped.
    EventQueue queue;
    queue.push(event(EventKind::added, {L"Hades"}), 0);
    queue.push(event(EventKind::launch, {L"Doom"}), 10);
    queue.push(event(EventKind::added, {L"Portal 2"}), 20);
    queue.push(event(EventKind::launch, {L"Quake"}), 30); // A newer launch replaces the queued one.
    auto next = queue.pop(40);
    if (queue.size() != 1 || !next || next->kind != EventKind::launch || next->titles != std::vector<std::wstring>{L"Quake"}) return 320;
    next = queue.pop(50);
    if (!next || next->kind != EventKind::added || next->count != 2 || next->titles != std::vector<std::wstring>{L"Hades", L"Portal 2"} || queue.pop(60)) return 321;
    queue.push(event(EventKind::late_night, {}), 0);
    queue.push(event(EventKind::launch, {L"Doom"}), 0);
    if (queue.pop(16 * 60'000) || queue.size()) return 322; // Both stale by then.
    // Library watch: the first look only remembers; then installs, uninstalls and new games.
    LibraryWatch watch;
    if (!watch.update({{"a", L"Alpha", true}, {"b", L"Beta", false}}, {}).empty() || !watch.ready() || !watch.knows("b")) return 330;
    auto changes = watch.update({{"a", L"Alpha", false}, {"b", L"Beta", true}, {"c", L"Gamma", true}, {"d", {}, true}}, {"b", "c", "d"});
    const auto of = [&](EventKind kind) { return std::find_if(changes.begin(), changes.end(), [&](const Event& e) { return e.kind == kind; }); };
    if (changes.size() != 3 || of(EventKind::installed)->titles != std::vector<std::wstring>{L"Beta"} ||
        of(EventKind::uninstalled)->titles != std::vector<std::wstring>{L"Alpha"} ||
        of(EventKind::added)->count != 2 || of(EventKind::added)->titles != std::vector<std::wstring>{L"Gamma"}) return 331; // "d" is still unnamed.
    if (!watch.update({{"a", L"Alpha", false}, {"b", L"Beta", true}}, {}).empty()) return 332;
    std::vector<RosterEntry> imported;
    std::vector<std::string> imported_keys;
    for (int i = 0; i < 41; ++i) { imported.push_back({"x" + std::to_string(i), L"X", true}); imported_keys.push_back("x" + std::to_string(i)); }
    if (!watch.update(imported, imported_keys).empty()) return 333; // An import is no news.
    // Premieres: only dated for today, most popular first.
    std::vector<glance::Item> calendar(4);
    calendar[0].title = L"Tomorrow"; calendar[0].release = {glance::ReleasePrecision::day, 2026, 10, 15};
    calendar[1].title = L"Big Game"; calendar[1].release = {glance::ReleasePrecision::day, 2026, 10, 14};
    calendar[2].title = L"Someday"; calendar[2].release = {glance::ReleasePrecision::month, 2026, 10, 0};
    calendar[3].title = L"Indie"; calendar[3].release = {glance::ReleasePrecision::day, 2026, 10, 14};
    if (premieresToday(calendar, 20261014) != std::vector<std::wstring>{L"Big Game", L"Indie"} || premieresToday(calendar, 20261014, 1).size() != 1) return 340;
    // Roulette: a reel of pool indices without repeats in a row, the same for the same seed.
    const auto reel = spinReel(5, 7);
    if (reel.size() != 28 || reel != spinReel(5, 7) || std::any_of(reel.begin(), reel.end(), [](size_t i) { return i >= 5; })) return 350;
    for (size_t i = 1; i < reel.size(); ++i) if (reel[i] == reel[i - 1]) return 351;
    if (spinReel(1, 3, 6) != std::vector<size_t>(6, 0) || !spinReel(0, 3).empty()) return 352;
    if (rouletteKickoff(L"Hades", false).find(L"[[Hades]]") == std::wstring::npos) return 353;
    for (int character = 0; character < persona_count; ++character)
        for (const bool english : {false, true})
            if (parseText(rouletteLine(static_cast<Persona>(character), L"Hades", english, character)).games != std::vector<std::wstring>{L"Hades"}) return 354;
    // ElevenLabs: models, settings, voices, her speaker chain, chunks with audio tags, the request
    // body, refused voices giving way, problems, the key's check and its storage.
    const auto wav = wavFixture(24000, 1, 2400);
    {
        using namespace elevenlabs;
        if (!model("eleven_v4_turbo").tags || model("eleven_v3").stitching || model("eleven_multilingual_v2").tags || model("x_custom").label || !model("x_custom").stitching) return 430;
        if (!modelId("eleven_v4") || modelId("Bad Model") || modelId("") || !voiceId("cgSgspJ2msm6clMCkdW9") || voiceId("abc/../x")) return 431;
        const auto set = parseSettings("stability=0.3, similarity=0.9 ,style=2, speed=1.1, speaker_boost=0, bogus=1, speed=x", Settings{});
        if (set.stability != 0.3f || set.similarity != 0.9f || set.style != 0.0f || set.speed != 1.1f || set.speaker_boost) return 432;
        const auto sleepy = tuned(Settings{0.5f, 0.75f, 0.2f, 1.0f}, Emotion::sleepy);
        if (sleepy.speed >= 1.0f || sleepy.stability <= 0.5f || tuned(Settings{0.05f, 0.75f, 0.95f, 1.19f}, Emotion::excited).speed > 1.2f ||
            tuned(Settings{0.05f, 0.75f, 0.95f, 1.19f}, Emotion::excited).style > 1.0f) return 433;
        if (!emotionTag(Emotion::laugh) || emotionTag(Emotion::cool) || std::wstring(personaDelivery(Persona::kanedere)).find(L"sleepy") == std::wstring::npos) return 434;
        const std::string voices_json = R"({"voices":[{"voice_id":"cgSgspJ2msm6clMCkdW9","name":"Jessica - Playful, Bright, Warm","category":"premade",)"
                                        R"("labels":{"gender":"female","age":"young"}},{"voice_id":"bad id","name":"X"},{"voice_id":"abc123","name":"Own"}]})";
        const auto listed = parseVoices(voices_json);
        if (!listed || listed->size() != 2 || (*listed)[0].gender != "female" || shortName((*listed)[0].name) != L"Jessica" || parseVoices("[]") ||
            !parseVoices(R"({"voices":[]})")) return 435;
        // Her chain: Settings' pick, her character.txt voice and free fallback, then a built-in one (no repeats).
        companion::Character her;
        her.voice.voice = "LibVoice1"; her.voice.voice_name = L"Zofia - Fresh and Upbeat"; her.voice.fallback = "cgSgspJ2msm6clMCkdW9";
        her.voice.model = "eleven_v4"; her.voice.settings = "speed=1.1"; her.voice.delivery = L"bubbly";
        const auto who = speaker(&her, Persona::genki, "abc123", {}, *listed);
        if (who.voices != std::vector<std::string>{"abc123", "LibVoice1", "cgSgspJ2msm6clMCkdW9"} || who.names[0] != L"Own" || who.names[1] != L"Zofia" ||
            who.names[2] != L"Jessica" || who.model != "eleven_v4" || who.settings.speed != 1.1f || who.settings.stability != personaSettings(Persona::genki).stability ||
            who.delivery != L"bubbly" || speaker(&her, Persona::genki, {}, "eleven_flash_v2_5", {}).model != "eleven_flash_v2_5") return 436;
        const auto plain = speaker(nullptr, Persona::kuudere, {}, {}, {});
        if (plain.voices != std::vector<std::string>{builtinVoice(Persona::kuudere).id} || plain.model != elevenlabs::default_model || plain.delivery != personaDelivery(Persona::kuudere)) return 437;
        // Sentences, and what a voice can say.
        if (sentences(L"Hm? Zagraj w Hades 2.5! Serio... Dobra.") != std::vector<std::wstring>{L"Hm? Zagraj w Hades 2.5!", L"Serio...", L"Dobra."} || !sentences(L"  ").empty()) return 438;
        if (speakable(L"*macha* Hej~ [[x]] \U0001F600 \u2728 ok  ") != L"macha Hej x ok") return 439;
        // Chunks: her first sentence alone, then the rest of the message; tags with v3/v4, a chunk per feeling with v2.
        const std::vector<companion::Line> said{{hana_look, L"Hejka, gramy! Drugie zdanie tutaj. A to trzecie.", {{0, Emotion::excited}, {14, Emotion::sad}, {35, Emotion::laugh}}, {}},
                                                {rin_look, L"Ja też tak myślę!", {{0, Emotion::smile}}, {}}};
        Speaker tagged; tagged.model = "eleven_v4_turbo"; tagged.delivery = L"bubbly"; tagged.voices = {"cgSgspJ2msm6clMCkdW9"};
        Speaker steady = tagged; steady.model = "eleven_multilingual_v2";
        const auto parts = chunks(said, [&](companion::Look look) { return look == hana_look ? tagged : steady; });
        if (parts.size() != 3 || parts[0].text != L"[bubbly, excited] Hejka, gramy!" || !parts[0].first || parts[0].next != L"Drugie zdanie tutaj. A to trzecie." ||
            parts[1].text != L"[bubbly, sad] Drugie zdanie tutaj. [laughs] A to trzecie." || parts[1].previous != L"Hejka, gramy!" || parts[1].first ||
            parts[2].speaker != rin_look || parts[2].text != L"Ja też tak myślę!" || parts[2].emotion != Emotion::smile) return 440;
        // Her cues and a feeling that changes mid-chunk land exactly where they stand (v3/v4); a cue
        // that repeats her opening feeling is said once; the v2 models drop them.
        const std::vector<companion::Line> acting{{hana_look, L"Serio? No dobra. Gramy.", {{0, Emotion::smile}, {7, Emotion::smug}}, {}, {{7, L"laughs"}, {16, L"sighs"}}},
                                                  {hana_look, L"Hejka. Ha ha.", {{0, Emotion::cool}, {7, Emotion::laugh}}, {}, {{7, L"laughs"}, {13, L"[bad]"}}}};
        const auto acted_parts = chunks(acting, [&](companion::Look) { return tagged; });
        if (acted_parts.size() != 4 || acted_parts[0].text != L"[bubbly, happy] Serio?" || acted_parts[1].text != L"[bubbly, smug] [laughs] No dobra. [sighs] Gramy." ||
            acted_parts[2].text != L"[bubbly] Hejka." || acted_parts[3].text != L"[bubbly, laughs] Ha ha. [bad]") return 456;
        // How she sounds and her feeling join without saying a word twice.
        Speaker drowsy = tagged; drowsy.delivery = L"sleepy, drawling, playful";
        const auto drowsed = chunks({{hana_look, L"Nudzi mi się.", {{0, Emotion::sleepy}}, {}}, {hana_look, L"Hehe, no to graj.", {{0, Emotion::playful}}, {}}},
                                    [&](companion::Look) { return drowsy; });
        if (drowsed.size() != 2 || drowsed[0].text != L"[sleepy, drawling, playful, yawning] Nudzi mi się." ||
            drowsed[1].text != L"[sleepy, drawling, playful] Hehe, no to graj.") return 458;
        const auto plain_parts = chunks(acting, [&](companion::Look) { return steady; });
        if (plain_parts.size() != 4 || plain_parts[1].text != L"No dobra. Gramy." || plain_parts[3].text != L"Ha ha." ||
            std::any_of(plain_parts.begin(), plain_parts.end(), [](const Chunk& c) { return c.text.find(L'[') != std::wstring::npos; })) return 457;
        const auto split = chunks({said[0]}, [&](companion::Look) { return steady; });
        if (split.size() != 3 || split[1].text != L"Drugie zdanie tutaj." || split[1].emotion != Emotion::sad || split[2].emotion != Emotion::laugh) return 441;
        // The request body: language, settings, stitching (not v3); the v2 models carry her feeling in the settings.
        const auto sent = json::parse(elevenlabs::body(parts[1], "eleven_v4_turbo", Settings{0.35f, 0.75f, 0.45f, 1.06f}, false));
        if (!sent || sent->get("language_code")->stringOr() != "pl" || sent->get("model_id")->stringOr() != "eleven_v4_turbo" ||
            sent->get("voice_settings")->get("stability")->numeric != 0.35 || sent->get("previous_text")->stringOr() != "Hejka, gramy!" || sent->get("next_text")) return 442;
        const auto v3 = json::parse(elevenlabs::body(parts[1], "eleven_v3", Settings{}, true));
        const auto v2 = json::parse(elevenlabs::body(split[1], "eleven_multilingual_v2", Settings{0.5f, 0.75f, 0.2f, 1.0f}, true));
        if (!v3 || v3->get("previous_text") || v3->get("language_code")->stringOr() != "en" || !v2 || v2->get("voice_settings")->get("speed")->numeric >= 1.0) return 443;
        // Speaking: a voice her plan refuses (402) gives way to the next; refused ones are skipped later.
        std::vector<std::wstring> asked;
        std::wstring used_key;
        const auto refusal = [](uint32_t status, const std::string& reason) {
            const std::string text = R"({"detail":{"status":")" + reason + R"(","message":"x"}})";
            return HttpResponse{status, {text.begin(), text.end()}};
        };
        const Request service = [&](const std::wstring& key, const std::wstring& path, const std::string* payload) {
            asked.push_back(path); used_key = key;
            if (!payload) return HttpResponse{200, {voices_json.begin(), voices_json.end()}};
            if (path.find(L"LibVoice1") != std::wstring::npos) return refusal(402, "payment_required");
            return HttpResponse{200, wav};
        };
        Speaker chain; chain.voices = {"LibVoice1", "cgSgspJ2msm6clMCkdW9"};
        auto spoken = speak(service, L"sk_test", chain, parts[0], false, {});
        if (spoken.wav != wav || spoken.voice != "cgSgspJ2msm6clMCkdW9" || spoken.refused != std::vector<std::string>{"LibVoice1"} || spoken.problem != Problem::none ||
            asked.size() != 2 || asked[1] != L"v1/text-to-speech/cgSgspJ2msm6clMCkdW9?output_format=wav_24000" || used_key != L"sk_test") return 444;
        asked.clear();
        if (speak(service, L"sk_test", chain, parts[0], false, {"LibVoice1"}).wav != wav || asked.size() != 1) return 445;
        if (speak(service, L"sk_test", chain, parts[0], false, {"LibVoice1", "cgSgspJ2msm6clMCkdW9"}).problem != Problem::paid_voice ||
            speak(service, L"", chain, parts[0], false, {}).problem != Problem::no_key) return 446;
        // A used-up quota stops at once; a body the model refuses (400) is tried once more without stitching.
        asked.clear();
        const auto quota = speak([&](const std::wstring&, const std::wstring& path, const std::string*) { asked.push_back(path); return refusal(401, "quota_exceeded"); },
                                 L"sk_test", chain, parts[0], false, {});
        if (quota.problem != Problem::quota || asked.size() != 1 || !quota.wav.empty()) return 447;
        int attempts = 0;
        const auto retried = speak([&](const std::wstring&, const std::wstring&, const std::string* payload) {
            ++attempts;
            return payload && payload->find("next_text") != std::string::npos ? refusal(400, "unsupported_model") : HttpResponse{200, wav};
        }, L"sk_test", chain, parts[0], false, {});
        if (retried.wav != wav || attempts != 2) return 448;
        if (problem(HttpResponse{}) != Problem::offline || problem(refusal(401, "invalid_api_key")) != Problem::key_refused ||
            problem(refusal(401, "missing_permissions")) != Problem::permission || problem(HttpResponse{429, {}}) != Problem::busy) return 449;
        // The key's check: its voices; a key that may not list them still speaks.
        const auto checked = check(service, L"sk_test");
        if (!checked.ok || checked.voices.size() != 2 || check(service, L"").problem != Problem::no_key) return 450;
        const auto unlisted = check([&](const std::wstring&, const std::wstring&, const std::string*) { return refusal(401, "missing_permissions"); }, L"sk_test");
        const auto refused = check([&](const std::wstring&, const std::wstring&, const std::string*) { return refusal(401, "invalid_api_key"); }, L"sk_test");
        if (!unlisted.ok || !unlisted.voices.empty() || refused.ok || refused.problem != Problem::key_refused) return 451;
        for (int i = 0; i < persona_count; ++i)
            for (const bool english : {false, true}) if (sentences(sample(static_cast<Persona>(i), english).text).empty()) return 452;
        // The key: encrypted in a private folder; saved empty, it is forgotten.
        const auto keys = std::filesystem::temp_directory_path() / (L"launchme-elevenlabs-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(keys);
        if (!storedKey(keys).empty() || !saveKey(keys, L"  sk_stored_0123  ") || storedKey(keys) != L"sk_stored_0123" || saveKey(keys, L"bad key") ||
            !saveKey(keys, L"") || !storedKey(keys).empty()) return 453;
        std::error_code removed; std::filesystem::remove_all(keys, removed);
    }
    // WAV: 16-bit PCM only, extra chunks skipped, odd tails cut to whole frames.
    const auto clip = parseWav(wav);
    if (!clip || clip->samples.size() != 4800 || reinterpret_cast<const WAVEFORMATEX*>(clip->format.data())->nSamplesPerSec != 24000) return 410;
    auto stereo = wavFixture(48000, 2, 10); stereo.push_back(0);
    if (!parseWav(stereo) || parseWav(stereo)->samples.size() != 40) return 411;
    auto eight_bit = wav; eight_bit[34] = 8;
    if (parseWav(eight_bit) || parseWav(std::vector<uint8_t>(wav.begin(), wav.begin() + 30)) || parseWav(std::vector<uint8_t>{})) return 412;
    // The old VOICEVOX voice's Japanese line (<ja>…</ja>, still in older conversations) is never shown,
    // and no prompt asks for one any more.
    auto spoken = parseText(L"[excited]Hej! Zagraj w [[Hades]]. <ja>[excited]やあ！[[Hades]]で遊ぼう！</ja>");
    if (spoken.text != L"Hej! Zagraj w Hades." || spoken.raw.find(L"<ja>") != std::wstring::npos || spoken.games.size() != 1) return 424;
    if (parseText(L"Tekst <ja>途中").text != L"Tekst" || systemPrompt(Persona::genki, hana_look, library, false, 0, 0, 9, {true}).find(L"<ja>") != std::wstring::npos) return 425;
    // Her voice's cues ({laughs}): heard where they stand (offsets into the shown text), never shown;
    // braces that are no cue (JSON, numbers) stay; chips and her thought lose them too.
    const auto acted = parseText(L"[smile]Serio? {laughs} No dobra{sighs}. {nervous laugh}");
    if (acted.text != L"Serio? No dobra." || acted.cues != std::vector<Cue>{{7, L"laughs"}, {15, L"sighs"}, {16, L"nervous laugh"}} ||
        acted.emotions.front().emotion != Emotion::smile) return 426;
    if (const auto braces = parseText(L"Wynik {\"a\":1} i {3 punkty}."); braces.text != L"Wynik {\"a\":1} i {3 punkty}." || !braces.cues.empty()) return 427;
    if (parseText(L"Hej. <opcje>Jasne {laughs}|Nie</opcje>").prompts != std::vector<std::wstring>{L"Jasne", L"Nie"} ||
        parseText(L"<mysl>[sad]Smutno {sighs} mi.</mysl>Hej.").subconscious != L"Smutno mi.") return 428;
    // In messages: each speaker keeps her cues, also when neighbouring messages join.
    const auto acted_lines = lines(L"Hana: [smug]Ha! {laughs} Wygrałam.\nRin: {sighs} Znowu.", hana_look, rin_look);
    if (acted_lines.size() != 2 || acted_lines[0].text != L"Ha! Wygrałam." || acted_lines[0].cues != std::vector<Cue>{{4, L"laughs"}} ||
        acted_lines[1].text != L"Znowu." || acted_lines[1].cues != std::vector<Cue>{{0, L"sighs"}}) return 429;
    const auto joined = lines(L"Hana: Raz. {laughs}\nHana: Dwa {sighs} trzy.", hana_look, std::nullopt, 1);
    if (joined.size() != 1 || joined[0].text != L"Raz. Dwa trzy." || joined[0].cues != std::vector<Cue>{{4, L"laughs"}, {9, L"sighs"}}) return 454;
    // The prompt offers cues only while a voice that acts speaks.
    PromptExtras acting; acting.voice_cues = true;
    if (systemPrompt(Persona::genki, hana_look, library, false, 0, 0, 9, acting).find(L"{laughs}") == std::wstring::npos ||
        systemPrompt(Persona::genki, hana_look, library, true, 0, 0, 9, acting).find(L"{whispers}") == std::wstring::npos ||
        systemPrompt(Persona::genki, hana_look, library, false, 0, 0, 9).find(L"{laughs}") != std::wstring::npos) return 455;
    // DPAPI token storage in a private folder.
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-companion-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    if (hasStoredToken(folder) || !saveToken(folder, L"  stored.token.value.0123456789  ") || storedToken(folder) != L"stored.token.value.0123456789") return 180;
    if (saveToken(folder, L"bad token with spaces") || !saveToken(folder, L"") || hasStoredToken(folder)) return 181;
    std::error_code ec; std::filesystem::remove_all(folder, ec);

    // Rig: syllables hop and nod her head and open the mouth frame; silence settles; a new feeling
    // cross-fades; she blinks; a flash returns to calm; the pose stays within its bounds.
    avatar::Rig rig(3);
    const auto rest = rig.pose();
    rig.speak(L'a');
    for (int i = 0; i < 4; ++i) rig.step(0.016f);
    if (!rig.talking() || !rig.pose().mouth || rig.pose().talk < 0.3f || rig.pose().head_nod <= rest.head_nod) return 190;
    rig.silence();
    for (int i = 0; i < 60; ++i) rig.step(0.016f);
    if (rig.pose().mouth || rig.pose().talk > 0.05f || std::abs(rig.pose().head_nod) > 1.5f) return 191;
    rig.emote(Emotion::angry);
    if (rig.emotion() != Emotion::angry || rig.pose().previous != calm || rig.pose().blend > 0.01f || rig.pose().emotion_age > 0.01f) return 192;
    for (int i = 0; i < 12; ++i) rig.step(0.016f);
    if (rig.pose().blend < 1 || std::abs(rig.pose().emotion_age - 0.192f) > 0.01f) return 193; // Her marks pop in by this age.
    bool blinked = false;
    for (int i = 0; i < 600 && !blinked; ++i) { rig.step(0.016f, false); blinked = rig.pose().blink; }
    if (!blinked) return 194;
    rig.flash(Emotion::shock, 0.5f);
    for (int i = 0; i < 20; ++i) rig.step(0.016f);
    if (rig.emotion() != Emotion::shock) return 195;
    for (int i = 0; i < 20; ++i) rig.step(0.016f);
    if (rig.emotion() != calm) return 196;
    for (int i = 0; i < 4000; ++i) {
        if (i % 7 == 0) rig.speak(i % 2 ? L'o' : L'k');
        if (i % 500 == 0) rig.emote(static_cast<Emotion>((i / 500) % emotion_count));
        rig.step(0.016f, i % 1000 > 500);
        const auto pose = rig.pose();
        if (std::abs(pose.tilt + pose.head_tilt) > 14 || pose.head_nod < -4 || pose.head_nod > 11 || std::abs(pose.head_x) > 5 ||
            pose.scale_y < 0.9f || pose.scale_y > 1.1f || pose.y > 40 || pose.y < -60) return 197;
    }    return 0;
}

// A small library index for her tools.
game_index::Index toolIndex() {
    using namespace game_index;
    Index index;
    const auto game = [](const wchar_t* title, bool installed, uint64_t played, std::optional<uint32_t> minutes) {
        Patch patch; patch.title = title; patch.store = L"Steam"; patch.installed = installed; patch.last_played = played;
        if (minutes) patch.playtime_minutes = minutes;
        return patch;
    };
    index.syncLibrary({{"steam:70", game(L"Half-Life", true, 1'759'000'000, 720)}, {"steam:620", game(L"Portal 2", true, 0, std::nullopt)},
                       {"gog:1", game(L"Wiedźmin 3", false, 0, std::nullopt)}, {"custom:x", game(L"Bez daty", true, 0, std::nullopt)}}, 1'760'000'000);
    Patch half; half.release = Date{1998, 11, 19}; half.genres = std::vector<std::wstring>{L"Action"}; half.tags = std::vector<std::wstring>{L"FPS", L"Classic"};
    half.developers = std::vector<std::wstring>{L"Valve"}; half.review_percent = 96;
    index.merge("steam:70", Source::steam_appinfo, half);
    Patch store; store.description = L"Black Mesa."; // Descriptions come from store pages.
    index.merge("steam:70", Source::steam_store, store);
    Patch portal; portal.release = Date{2011, 4, 19}; portal.genres = std::vector<std::wstring>{L"Action", L"Adventure"}; portal.tags = std::vector<std::wstring>{L"Puzzle", L"Classic"};
    portal.developers = std::vector<std::wstring>{L"Valve"};
    index.merge("steam:620", Source::steam_appinfo, portal);
    Patch witcher; witcher.release = Date{2015, 5, 18}; witcher.genres = std::vector<std::wstring>{L"RPG"};
    index.merge("gog:1", Source::rawg, witcher);
    Patch hltb; hltb.hltb_main = 1500; index.merge("gog:1", Source::hltb, hltb);
    return index;
}

// Her tools on the game index, the function-calling loop (her calls sent back verbatim with their
// thought signatures) and her memory.
int toolsAndMemory() {
    using namespace companion;
    const auto declared = json::parse(tools::declarations(false));
    if (!declared || declared->type != json::Value::Type::array || declared->elements.size() != 4) return 300;
    const auto with_notes = json::parse(tools::declarations(true));
    if (!with_notes || with_notes->elements.size() != 5 || with_notes->elements[4].get("name")->stringOr() != tools::remember) return 301;
    for (const auto& item : with_notes->elements) if (!item.get("name") || !item.get("description") || !item.get("parameters")) return 302;
    const auto index = toolIndex();
    tools::Context context{false, 1'760'000'000, 1, {}};
    const auto args = [](const char* text) { return *json::parse(text); };
    // "Jaka jest moja najstarsza gra?"
    auto result = tools::run(tools::search, args(R"({"sortuj":"oldest","limit":1})"), index, context);
    const auto* games = result.get("games");
    if (!games || games->elements.size() != 1 || games->elements[0].get("title")->stringOr() != "Half-Life" ||
        games->elements[0].get("released")->stringOr() != "1998-11-19" || result.get("total_matches")->intOr() != 4 ||
        result.get("note")->stringOr().find("1 matching") == std::string::npos) return 303;
    if (games->elements[0].get("played")->stringOr() != "12 h" || games->elements[0].get("steam_reviews")->stringOr() != "96% positive") return 304;
    result = tools::run(tools::search, args(R"({"gatunki":["przygodowe"]})"), index, context);
    if (result.get("total_matches")->intOr() != 1) return 305;
    result = tools::run(tools::search, args(R"({"stan":"never_played","sortuj":"title","limit":"50"})"), index, context);
    if (result.get("total_matches")->intOr() != 3 || result.get("games")->elements.size() != 3) return 306;
    result = tools::run(tools::details, args(R"({"tytul":"half life"})"), index, context);
    if (!result.get("game") || result.get("game")->get("description")->stringOr() != "Black Mesa.") return 307;
    if (!tools::run(tools::details, args(R"({"tytul":"Zelda"})"), index, context).get("error")) return 308;
    result = tools::run(tools::statistics, args(R"({"grupuj":"genre"})"), index, context);
    if (result.get("games")->intOr() != 4 || result.get("groups")->elements[0].get("name")->stringOr() != "Action" || result.get("hours_played")->numeric != 12) return 309;
    result = tools::run(tools::similar, args(R"({"tytul":"Portal 2"})"), index, context);
    if (result.get("games")->elements.empty() || result.get("games")->elements[0].get("title")->stringOr() != "Half-Life") return 310;
    std::vector<std::wstring> notes;
    context.remember = [&](const std::wstring& note) { notes.push_back(note); return true; };
    result = tools::run(tools::remember, args(R"({"notatka":"Uwielbia metroidvanie."})"), index, context);
    if (!result.get("saved") || !result.get("saved")->flag || notes != std::vector<std::wstring>{L"Uwielbia metroidvanie."}) return 311;
    if (!tools::run("nieznane", args("{}"), index, context).get("error") || !tools::run(tools::search, args("{}"), game_index::Index{}, context).get("error")) return 312;

    // The loop: a call (with its thought signature), its answer, then her words.
    Connection connection;
    connection.host = L"https://stack.example.palantirfoundry.test/"; connection.token = L"abcdefghijklmnopqrstuvwxyz.0123456789";
    const std::string call = R"({"candidates":[{"content":{"role":"model","parts":[{"thoughtSignature":"SIG/abc+==","functionCall":{"id":"call_1","name":"szukaj_w_bibliotece","args":{"sortuj":"oldest","limit":1}}}]},"finishReason":"STOP"}]})";
    const std::string words = R"({"candidates":[{"content":{"role":"model","parts":[{"thoughtSignature":"SIG2","text":"[smug]Najstarsza? [[Half-Life]] z 1998!"}]},"finishReason":"STOP"}]})";
    std::vector<std::string> bodies;
    Tools set;
    set.declarations = tools::declarations(false);
    std::vector<std::string> ran;
    set.run = [&](const std::string& name, const json::Value& call_args) { ran.push_back(name); return tools::run(name, call_args, index, context); };
    auto reply = ask(connection, L"sys", {{true, L"Jaka jest moja najstarsza gra?"}}, [&](const std::wstring&, const std::wstring&, const std::string& body) {
        bodies.push_back(body);
        return bodies.size() == 1 ? HttpResponse{200, {call.begin(), call.end()}} : HttpResponse{200, {words.begin(), words.end()}};
    }, 400, 1.0f, &set);
    if (!reply.ok || reply.text != L"Najstarsza? Half-Life z 1998!" || reply.tools != std::vector<std::string>{tools::search} || ran.size() != 1 || bodies.size() != 2) return 313;
    const auto first = json::parse(bodies[0]), second = json::parse(bodies[1]);
    if (!first || !second || !first->get("tools") || first->get("toolConfig")->get("functionCallingConfig")->get("mode")->stringOr() != "AUTO" ||
        !first->get("generationConfig")->get("thinkingConfig")) return 314;
    const auto& contents = second->get("contents")->elements;
    if (contents.size() != 3 || contents[1].get("role")->stringOr() != "model" ||
        contents[1].get("parts")->elements[0].get("thoughtSignature")->stringOr() != "SIG/abc+==" || contents[2].get("role")->stringOr() != "user") return 315;
    const auto* answer = contents[2].get("parts")->elements[0].get("functionResponse");
    if (!answer || answer->get("id")->stringOr() != "call_1" || answer->get("name")->stringOr() != tools::search ||
        answer->get("response")->get("games")->elements[0].get("title")->stringOr() != "Half-Life") return 316;
    // A model that keeps calling: after the last round she must answer in words (mode NONE).
    bodies.clear();
    set.rounds = 2;
    reply = ask(connection, L"sys", {{true, L"?"}}, [&](const std::wstring&, const std::wstring&, const std::string& body) {
        bodies.push_back(body);
        return bodies.size() <= 2 ? HttpResponse{200, {call.begin(), call.end()}} : HttpResponse{200, {words.begin(), words.end()}};
    }, 400, 1.0f, &set);
    if (!reply.ok || bodies.size() != 3 || json::parse(bodies[2])->get("toolConfig")->get("functionCallingConfig")->get("mode")->stringOr() != "NONE") return 317;
    // No words after her tools (an empty STOP): once more, now without tools.
    bodies.clear();
    set.rounds = 3;
    const std::string silent = R"({"candidates":[{"content":{"role":"model","parts":[{"text":""}]},"finishReason":"STOP"}]})";
    reply = ask(connection, L"sys", {{true, L"?"}}, [&](const std::wstring&, const std::wstring&, const std::string& body) {
        bodies.push_back(body);
        const auto& answer_body = bodies.size() == 1 ? call : bodies.size() == 2 ? silent : words;
        return HttpResponse{200, {answer_body.begin(), answer_body.end()}};
    }, 400, 1.0f, &set);
    if (!reply.ok || bodies.size() != 3 || json::parse(bodies[2])->get("toolConfig")->get("functionCallingConfig")->get("mode")->stringOr() != "NONE") return 332;
    // Without tools the request has none.
    if (requestBody(L"s", {{true, L"a"}}, 100, 1.0f).find("\"tools\"") != std::string::npos) return 318;
    if (functionCalls({200, {words.begin(), words.end()}}) || !functionCalls({200, {call.begin(), call.end()}})) return 319;

    // Memory: recent remarks and suggestions, notes, the prompt section and the saved form.
    Memory memory;
    const uint64_t now = 1'760'000'000;
    memory.said(L"[smug] Znowu   Elden Ring?\nSerio?", now - 86400);
    memory.suggested("steam:70", L"Half-Life", now - 3600);
    memory.suggested("steam:620", L"Portal 2", now - 5 * 86400);
    if (memory.remarks.size() != 1 || memory.remarks[0].text != L"[smug] Znowu Elden Ring? Serio?") return 320;
    if (memory.resting(now) != std::vector<std::string>{"steam:70"}) return 321;
    if (!memory.note(L"Lubi metroidvanie.", now) || memory.note(L"lubi METROIDVANIE", now) || memory.note(L"Jego hasło to kot123", now) || memory.note(L"ok", now)) return 322;
    memory.summarize(L"Rozmawialiśmy o Hollow Knight.", now);
    auto prompt = memory.prompt(false, now, true, false);
    if (prompt.find(L"Znowu Elden Ring?") == std::wstring::npos || prompt.find(L"Half-Life") == std::wstring::npos || prompt.find(L"Portal 2") != std::wstring::npos ||
        prompt.find(L"metroidvanie") != std::wstring::npos) return 323;
    prompt = memory.prompt(true, now, false, true);
    if (prompt.find(L"Lubi metroidvanie.") == std::wstring::npos || prompt.find(L"Hollow Knight") == std::wstring::npos || prompt.find(L"Znowu") != std::wstring::npos) return 324;
    if (!Memory().prompt(false, now, true, true).empty()) return 325;
    for (int i = 0; i < 40; ++i) memory.said(L"Uwaga " + std::to_wstring(i), now);
    if (memory.remarks.size() != 30 || memory.remarks.back().text != L"Uwaga 39") return 326;
    const auto loaded = Memory::load(memory.save());
    if (!loaded || loaded->remarks.size() != 30 || loaded->picks.size() != 2 || loaded->notes.size() != 1 || loaded->summaries.size() != 1 ||
        loaded->picks.front().title != L"Half-Life" || loaded->save() != memory.save() || Memory::load("{}")) return 327;
    const auto folder = std::filesystem::temp_directory_path() / (L"launchme-memory-" + std::to_wstring(GetCurrentProcessId()));
    if (!saveMemory(folder, memory) || loadMemory(folder).notes.size() != 1 || !clearMemory(folder) || !loadMemory(folder).empty()) return 328;
    std::filesystem::remove_all(folder);
    // Games she suggested lately give way in the digest's samples and in her offline lines.
    std::vector<GameFact> facts;
    for (int i = 0; i < 10; ++i) {
        GameFact fact; fact.title = L"Gra " + std::to_wstring(i); fact.key = "steam:" + std::to_string(i); fact.installed = true; fact.store = L"Steam";
        facts.push_back(fact);
    }
    const auto rested = digest(facts, 3, {"steam:0", "steam:1"});
    if (rested.untouched.size() != 8 || std::any_of(rested.untouched.begin(), rested.untouched.end(), [](const GameFact& game) { return game.key == "steam:0" || game.key == "steam:1"; }))
        return 329;
    // The prompt: tools and memory, and the looser rule on general knowledge.
    PromptExtras extras; extras.tools = true; extras.memory = L"PAMIĘĆ-TEST\n";
    const auto with_tools = systemPrompt(Persona::genki, hana_look, rested, false, now, 1, 12, extras);
    if (with_tools.find(L"narzędziami") == std::wstring::npos || with_tools.find(L"PAMIĘĆ-TEST") == std::wstring::npos || with_tools.find(L"ogólnej wiedzy") == std::wstring::npos) return 330;
    if (systemPrompt(Persona::genki, hana_look, rested, false, now, 1, 12).find(L"najpierw sprawdź to narzędziami") != std::wstring::npos) return 331;
    return 0;
}

// Her providers: Gemini with a Google key, Claude (Anthropic Messages, newest Opus chosen from the
// model list) and Ollama (first installed model, http on the home network), each through the same
// tool loop; addresses, keys from the environment and errors.
int providers() {
    using namespace companion;
    // Plain HTTP only to this machine and the home network.
    for (const auto* host : {L"127.0.0.1", L"localhost", L"192.168.1.10", L"10.0.0.5", L"172.20.1.1", L"169.254.3.4", L"nas", L"box.local", L"pc.lan", L"::1", L"[::1]", L"fe80::1", L"fd12:3456::1"})
        if (!lanHost(host)) return 500;
    for (const auto* host : {L"172.32.0.1", L"8.8.8.8", L"example.com", L"999.1.1.1", L"ollama.com", L"", L"10.0.0.1.example.com", L"192.168.1.10.sslip.io", L"2606:4700::1111", L"[2001:db8::1]", L"fcd::1"})
        if (lanHost(host)) return 501;
    if (normalizeServer(L"127.0.0.1:11434") != L"http://127.0.0.1:11434/" || normalizeServer(L"ollama.com") != L"https://ollama.com/" ||
        normalizeServer(L"http://192.168.1.5:11434/api/tags") != L"http://192.168.1.5:11434/" || !normalizeServer(L"ftp://x.y").empty() ||
        normalizeServer(L"https://my.server:8443") != L"https://my.server:8443/") return 502;
    if (geminiModel("gemini-3-8-flash") != "gemini-3.8-flash" || geminiModel("gemini-3-1-flash-lite") != "gemini-3.1-flash-lite" ||
        geminiModel("gemini-2.5-pro") != "gemini-2.5-pro") return 503;
    for (int i = 0; i < provider_count; ++i) if (providerFromKey(providerKey(static_cast<Provider>(i))) != static_cast<Provider>(i)) return 504;
    // Keys: Settings first, then the environment.
    std::map<std::wstring, std::wstring> variables{{L"GOOGLE_API_KEY", L"AIzaGoogleKey0123456789abcdefghijklmno"}, {L"ANTHROPIC_API_KEY", L"sk-ant-environment-0123456789abcdef"},
                                                   {L"OLLAMA_HOST", L"192.168.1.20:11434"}};
    Probe probe;
    probe.environment = [&](const wchar_t* name) { const auto found = variables.find(name); return found == variables.end() ? std::wstring{} : found->second; };
    auto gemini = resolve(Provider::gemini, L"", L"", "gemini-3-8-flash", probe);
    if (!gemini.ready() || gemini.token_source != Source::environment || gemini.token != L"AIzaGoogleKey0123456789abcdefghijklmno") return 505;
    variables[L"GEMINI_API_KEY"] = L"AIzaGeminiKey0123456789abcdefghijklmno";
    gemini = resolve(Provider::gemini, L"", L"", "gemini-3-8-flash", probe);
    if (gemini.token != L"AIzaGeminiKey0123456789abcdefghijklmno") return 506;
    auto claude = resolve(Provider::claude, L"", L"sk-ant-settings-0123456789abcdefghij", "", probe);
    if (claude.token_source != Source::settings || claude.model != default_claude_model) return 507;
    auto ollama = resolve(Provider::ollama, L"", L"", "", probe);
    if (!ollama.ready() || ollama.host != L"http://192.168.1.20:11434/" || ollama.host_source != Source::environment || !ollama.token.empty()) return 508;
    variables.erase(L"OLLAMA_HOST");
    if (resolve(Provider::ollama, L"", L"", "", probe).host != L"http://127.0.0.1:11434/" || resolve(Provider::ollama, L"https://ollama.com/", L"", "x", probe).host != L"https://ollama.com/") return 509;
    if (resolve(Provider::claude, L"", L"", "", Probe{}).ready()) return 510;

    const auto index = toolIndex();
    tools::Context context{false, 1'760'000'000, 1, {}};
    Tools set;
    set.declarations = tools::declarations(false);
    set.run = [&](const std::string& name, const json::Value& call_args) { return tools::run(name, call_args, index, context); };
    const auto respond = [](const std::string& body) { return HttpResponse{200, {body.begin(), body.end()}}; };
    struct Seen { std::wstring url, headers; std::string body; };

    // Gemini with Google's key: its endpoint and header, the same body as Foundry's.
    std::vector<Seen> seen;
    const std::string words = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[smile]Hej!"}]},"finishReason":"STOP"}]})";
    auto reply = ask(gemini, L"sys", {{true, L"hej"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        return respond(words);
    });
    if (!reply.ok || seen.size() != 1 || seen[0].url != L"https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:generateContent" ||
        seen[0].headers.find(L"x-goog-api-key: AIzaGeminiKey0123456789abcdefghijklmno\r\n") != 0 || seen[0].headers.find(L"Authorization") != std::wstring::npos ||
        seen[0].body.find("\"thinkingBudget\":0") == std::string::npos) return 511;

    // Google's 503 ("high demand") stays on the newest model for a while: the next Flash answers, a Pro is never tried.
    seen.clear();
    reply = ask(gemini, L"sys", {{true, L"hej"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        if (url.find(L"gemini-3.8-flash") != std::wstring::npos) return HttpResponse{503, {}};
        return respond(words);
    });
    if (!reply.ok || seen.empty() || seen.back().url.find(L"gemini-3.7-flash") == std::wstring::npos) return 9870;
    seen.clear();
    reply = ask(gemini, L"sys", {{true, L"hej"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        return HttpResponse{503, {}};
    });
    for (const auto& call : seen) if (call.url.find(L"pro") != std::wstring::npos) return 9871;
    if (reply.ok || reply.status != 503) return 9872;

    // Claude: the newest Opus of the list, a tool_use round, then words.
    claude.token = L"sk-ant-test-claude-0123456789abcdef";
    seen.clear();
    reply = ask(claude, L"Jesteś Hana.", {{true, L"Jaka jest moja najstarsza gra?"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        if (body.empty()) return respond(R"({"data":[{"id":"claude-sonnet-9","created_at":"2026-09-01T00:00:00Z"},{"id":"claude-opus-5-5","created_at":"2026-08-01T00:00:00Z"},
            {"id":"claude-opus-4-1","created_at":"2025-08-05T00:00:00Z"}],"has_more":false})");
        if (body.find("tool_result") == std::string::npos)
            return respond(R"({"id":"msg_1","type":"message","role":"assistant","content":[{"type":"text","text":"Sprawdzę."},{"type":"tool_use","id":"toolu_1","name":"szukaj_w_bibliotece","input":{"sortuj":"oldest","limit":1}}],"stop_reason":"tool_use"})");
        return respond(R"({"id":"msg_2","type":"message","role":"assistant","content":[{"type":"text","text":"[smug]To [[Half-Life]] z 1998."}],"stop_reason":"end_turn"})");
    }, 400, 1.0f, &set);
    if (!reply.ok || reply.text != L"To Half-Life z 1998." || reply.tools != std::vector<std::string>{tools::search} || seen.size() != 3) return 512;
    if (seen[0].url != L"https://api.anthropic.com/v1/models?limit=100" || !seen[0].body.empty() || seen[1].url != L"https://api.anthropic.com/v1/messages" ||
        seen[1].headers.find(L"x-api-key: sk-ant-test-claude-0123456789abcdef\r\nanthropic-version: 2023-06-01\r\n") != 0) return 513;
    auto first = json::parse(seen[1].body), second = json::parse(seen[2].body);
    if (!first || first->get("model")->stringOr() != "claude-opus-5-5" || first->get("system")->stringOr() != "Jesteś Hana." ||
        first->get("tools")->elements.size() != 4 || !first->get("tools")->elements[0].get("input_schema") ||
        first->get("tool_choice")->get("type")->stringOr() != "auto" || first->get("messages")->elements[0].get("role")->stringOr() != "user") return 514;
    if (!second || second->get("messages")->elements.size() != 3) return 515;
    const auto& assistant = second->get("messages")->elements[1];
    const auto& results = second->get("messages")->elements[2];
    if (assistant.get("role")->stringOr() != "assistant" || assistant.get("content")->elements[1].get("id")->stringOr() != "toolu_1" ||
        results.get("content")->elements[0].get("type")->stringOr() != "tool_result" || results.get("content")->elements[0].get("tool_use_id")->stringOr() != "toolu_1" ||
        results.get("content")->elements[0].get("content")->stringOr().find("Half-Life") == std::string::npos) return 516;
    // The chosen model is remembered; a cut reply keeps its complete sentences; errors read.
    seen.clear();
    reply = ask(claude, L"sys", {{true, L"hej"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        return respond(R"({"content":[{"type":"text","text":"[smile]Cześć, graczu! Dziś zagramy w [[Ha"}],"stop_reason":"max_tokens"})");
    });
    if (!reply.ok || !reply.truncated || reply.text != L"Cześć, graczu!" || seen.size() != 1) return 517;
    reply = ask(claude, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string&) {
        const std::string body = R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})";
        return HttpResponse{401, {body.begin(), body.end()}};
    });
    if (reply.ok || reply.status != 401 || reply.error.find(L"invalid x-api-key") == std::wstring::npos) return 518;

    // Ollama on the home network: the first installed model, tool calls, a tool message, its thoughts dropped.
    ollama.host = L"http://192.168.1.30:11434/";
    seen.clear();
    reply = ask(ollama, L"sys", {{true, L"Co jest podobne do Portal 2?"}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
        seen.push_back({url, headers, body});
        if (body.empty()) return respond(R"({"models":[{"name":"qwen3:8b","size":5000},{"name":"llama3.1:8b"}]})");
        if (body.find("\"role\":\"tool\"") == std::string::npos)
            return respond(R"({"model":"qwen3:8b","message":{"role":"assistant","content":"","tool_calls":[{"function":{"name":"podobne_gry","arguments":{"tytul":"Portal 2"}}}]},"done":true,"done_reason":"stop"})");
        return respond(R"({"model":"qwen3:8b","message":{"role":"assistant","content":"<think>Half-Life shares Valve.</think>[cool]Najbliżej jest [[Half-Life]]."},"done":true,"done_reason":"stop"})");
    }, 400, 1.0f, &set);
    if (!reply.ok || reply.text != L"Najbliżej jest Half-Life." || seen.size() != 3 || seen[0].url != L"http://192.168.1.30:11434/api/tags" ||
        seen[1].url != L"http://192.168.1.30:11434/api/chat" || seen[1].headers.find(L"Authorization") != std::wstring::npos) return 519;
    first = json::parse(seen[1].body); second = json::parse(seen[2].body);
    if (!first || first->get("model")->stringOr() != "qwen3:8b" || first->get("stream")->flag || first->get("messages")->elements[0].get("role")->stringOr() != "system" ||
        first->get("tools")->elements[0].get("function")->get("name")->stringOr() != tools::search || !first->get("options")->get("num_predict")) return 520;
    const auto& messages = second->get("messages")->elements;
    if (messages.size() != 4 || messages[2].get("tool_calls")->elements.size() != 1 || messages[3].get("role")->stringOr() != "tool" ||
        messages[3].get("tool_name")->stringOr() != tools::similar || messages[3].get("content")->stringOr().find("Half-Life") == std::string::npos) return 521;
    // No models on the server: she says so instead of asking.
    ollama.host = L"http://127.0.0.1:11999/";
    reply = ask(ollama, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string& body) {
        return body.empty() ? respond(R"({"models":[]})") : respond("{}");
    });
    if (reply.ok || reply.error != L"no model") return 522;
    ollama.model = "nope";
    reply = ask(ollama, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string&) {
        const std::string body = R"({"error":"model 'nope' not found"})";
        return HttpResponse{404, {body.begin(), body.end()}};
    });
    if (reply.ok || reply.error.find(L"not found") == std::wstring::npos) return 523;
    if (wire::claudeTools("not json") != "[]" || json::parse(wire::ollamaTools(tools::declarations(true)))->elements.size() != 5) return 524;
    // Labels for the chat header and Settings.
    Connection label; label.provider = Provider::claude; label.model = "auto-opus";
    if (label.label() != L"Claude Opus · Anthropic") return 525;
    label.provider = Provider::ollama; label.model = "qwen3:8b";
    if (label.label() != L"qwen3:8b · Ollama") return 526;
    return 0;
}

// Her photos: when she sends one, pictures from her own subfolder (not repeating the recent ones,
// never anyone else's or loose ones), what a picture shows, and the state on disk.
int photoTests() {
    namespace photos = companion::photos;
    for (uint32_t seed = 0; seed < 30; ++seed) { const int turn = photos::turn(seed); if (turn != 2 && turn != 3 && turn != 5) return 600; }
    photos::State state;
    if (!photos::due(state, 20261015)) return 601;
    state.last_shown = 20261015;
    if (photos::due(state, 20261015) || !photos::due(state, 20261016)) return 602;
    const std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 0};
    const std::vector<uint8_t> jpeg{0xFF, 0xD8, 0xFF, 0xE0, 1, 2};
    const std::vector<uint8_t> webp{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P', 'V', 'P'};
    if (!photos::imageBytes(png) || !photos::imageBytes(jpeg) || !photos::imageBytes(webp) || photos::imageBytes(std::vector<uint8_t>{'h', 'i'})) return 603;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-photos-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    const auto pictures = root / L"pictures";
    const auto hers = pictures / L"hana";
    atomic_file::write(hers / L"a.png", std::span<const uint8_t>(png));
    atomic_file::write(hers / L"b.jpg", std::span<const uint8_t>(jpeg));
    atomic_file::write(hers / L"00-09" / L"c.webp", std::span<const uint8_t>(webp));
    atomic_file::write(hers / L"note.txt", "not a picture");
    atomic_file::write(hers / L"fake.png", "not a picture either");
    atomic_file::write(pictures / L"loose.png", std::span<const uint8_t>(png));              // In nobody's folder: never sent.
    atomic_file::write(pictures / L"00-09" / L"range.png", std::span<const uint8_t>(png));   // Neither.
    atomic_file::write(pictures / L"rin" / L"10-19" / L"hers.png", std::span<const uint8_t>(png)); // Rin's, locked below 10.
    // Hers only; not the recent ones while others are left; all of hers reachable.
    std::set<std::wstring> seen;
    for (uint32_t seed = 0; seed < 60; ++seed) {
        const auto file = photos::pick(pictures, hana_look, 1, {L"hana\\a.png", L"hana\\b.jpg"}, seed);
        if (!file || file->filename() == L"a.png" || file->filename() == L"b.jpg" || file->extension() == L".txt" || file->parent_path().filename() == L"pictures") return 604;
        seen.insert(file->filename().wstring());
    }
    if (seen != std::set<std::wstring>{L"c.webp", L"fake.png"}) return 605;
    if (!photos::pick(pictures, hana_look, 1, {L"hana\\a.png", L"hana\\b.jpg", L"hana\\00-09\\c.webp", L"hana\\fake.png"}, 1) ||
        photos::pick(root / L"missing", hana_look, 1, {}, 1) || photos::pick(pictures, rin_look, 9, {}, 1) || !photos::pick(pictures, rin_look, 10, {}, 1)) return 606;
    if (!photos::read(hers / L"a.png") || photos::read(hers / L"fake.png")) return 607; // Only real pictures are sent.
    // What a picture shows: <stem>.txt beside it, on one line.
    atomic_file::write(hers / L"a.txt", std::string_view("\xEF\xBB\xBF" "Selfie w parku.\r\nZ kotem. "));
    if (photos::description(hers / L"a.png") != L"Selfie w parku.  Z kotem." || !photos::description(hers / L"b.jpg").empty()) return 610;
    // The state round-trips.
    state.recent = {L"hana\\a.png"};
    const auto folder = photos::folderOf(root);
    if (!photos::save(folder, state)) return 608;
    const auto loaded = photos::load(folder);
    if (loaded.last_shown != 20261015 || loaded.recent != state.recent) return 609;
    // Her line as she sends it, in every character.
    for (int i = 0; i < companion::persona_count; ++i)
        for (const bool english : {false, true})
            if (!companion::parseText(companion::photoLine(static_cast<companion::Persona>(i), english, 1)).ok) return 620;
    std::filesystem::remove_all(root);
    return 0;
}

// Chat messages from one reply (two short ones, a guest's signed lines), her guest's prompt,
// hidden turns and offline lines.
int guests() {
    using namespace companion;
    // Alone: lines become messages, each with its own feeling (or the last one she showed).
    auto said = lines(L"[smile]No hej.\n[thinking]Grasz w [[Hades]]?", hana_look);
    if (said.size() != 2 || said[0].text != L"No hej." || said[1].text != L"Grasz w Hades?" || said[0].speaker != hana_look ||
        said[1].emotions.front().emotion != Emotion::thinking) return 900;
    if (!said[0].mentions.empty() || said[1].mentions != std::vector<GameMention>{{8, 5}}) return 1404;
    const auto joined = lines(L"Hana: [smile][[Hades]]!\nHana: [thinking]A [[Portal 2]]?", hana_look, std::nullopt, 1);
    if (joined.size() != 1 || joined[0].text != L"Hades! A Portal 2?" || joined[0].mentions != std::vector<GameMention>{{0, 5}, {9, 8}}) return 1405;
    said = lines(L"[angry]Baka!\nNie patrz tak.", rin_look);
    if (said.size() != 2 || said[1].emotions.front().emotion != Emotion::angry || said[1].speaker != rin_look) return 901;
    if (lines(L"[smile]Jedno zdanie, bez podziału.", hana_look).size() != 1 || !lines(L"  \n ", hana_look).empty()) return 902;
    // "||" separates too; a limit joins her neighbouring messages first.
    said = lines(L"[smile]A.||[shy]B.\n[cool]C.\n[smug]D.", hana_look, std::nullopt, 3);
    if (said.size() != 3 || said[2].text != L"C. D." || said[2].emotions.size() != 2 || said[2].text.substr(said[2].emotions[1].at) != L"D.") return 903;
    // With a guest: each line signed (also in bold or after a tag), unsigned ones keep the speaker.
    said = lines(L"Hana: [smug]Znowu ty?\n**Rin**: [angry]A co, nie wolno?\n[playful] Hana: Nie.\nI tak zostaniesz.", hana_look, rin_look);
    if (said.size() != 4 || said[0].speaker != hana_look || said[1].speaker != rin_look || said[1].text != L"A co, nie wolno?" ||
        said[2].speaker != hana_look || said[2].text != L"Nie." || said[2].emotions.front().emotion != Emotion::playful ||
        said[3].speaker != hana_look || said[3].emotions.front().emotion != Emotion::playful) return 904;
    // Someone who is not here: her lines go; a lone signature of her becomes the host's; the host's own name is dropped.
    said = lines(L"Hana: [smile]Hej!\nRin: [smug]Jestem!", hana_look);
    if (said.size() != 1 || said[0].text != L"Hej!") return 905;
    said = lines(L"Rin: [smug]Tylko ja.", hana_look);
    if (said.size() != 1 || said[0].speaker != hana_look || said[0].text != L"Tylko ja.") return 906;
    if (lines(L"Hanako: [smile]To nie podpis.", hana_look)[0].text != L"Hanako: To nie podpis.") return 907;

    // The prompt: both characters, their names on each line, how they get on.
    const auto library = digest(fixtureGames(), 5);
    PromptExtras duo; duo.guest = Guest{rin_look, Persona::kuudere};
    const auto prompt = systemPrompt(Persona::tsundere, hana_look, library, false, 0, 1, 12, duo);
    if (prompt.find(L"Teraz w czacie jest też Rin") == std::wstring::npos || prompt.find(L"kuudere") == std::wstring::npos ||
        prompt.find(L"tsundere") == std::wstring::npos || prompt.find(L"„Hana: [smug]") == std::wstring::npos || prompt.find(L"zazdrosne") == std::wstring::npos ||
        prompt.find(L"<ja>") != std::wstring::npos || prompt.find(L"karmazynowe włosy") == std::wstring::npos) return 910;
    const auto english = systemPrompt(Persona::genki, rin_look, library, true, 0, 1, 12, PromptExtras{false, {}, Guest{hana_look, Persona::dandere}});
    if (english.find(L"Right now Hana") == std::wstring::npos || english.find(L"\"Rin: [smug]") == std::wstring::npos || english.find(L"dandere") == std::wstring::npos) return 911;
    // A guest with the host's own look is no guest.
    if (systemPrompt(Persona::genki, rin_look, library, false, 0, 1, 12, PromptExtras{false, {}, Guest{rin_look, Persona::genki}}).find(L"Teraz w czacie") != std::wstring::npos) return 912;
    // Two short messages now and then; most replies vary in length.
    PromptExtras burst; burst.burst = true;
    if (systemPrompt(Persona::genki, hana_look, library, false, 0, 1, 12, burst).find(L"komunikatorze") == std::wstring::npos ||
        systemPrompt(Persona::genki, hana_look, library, false, 0, 1, 12).find(L"komunikatorze") != std::wstring::npos ||
        systemPrompt(Persona::genki, hana_look, library, true, 0, 1, 12, burst).find(L"messenger") == std::wstring::npos) return 913;
    // Hidden turns and offline lines for every moment and character.
    for (const auto moment : {GuestMoment::drop_in, GuestMoment::called, GuestMoment::leave, GuestMoment::dismissed, GuestMoment::gone})
        for (const bool en : {false, true}) {
            const auto told = guestKickoff(moment, hana_look, rin_look, en);
            if (told.front() != L'(' || told.back() != L')' || told.find(L"Rin") == std::wstring::npos) return 914;
            for (int host = 0; host < persona_count; ++host)
                for (int guest = 0; guest < persona_count; ++guest) {
                    const auto offline = guestLine(moment, hana_look, static_cast<Persona>(host), rin_look, static_cast<Persona>(guest), en, 0);
                    if (moment == GuestMoment::gone) { if (!offline.empty()) return 915; continue; }
                    const auto parts = lines(offline, hana_look, rin_look);
                    const bool arriving = moment == GuestMoment::drop_in || moment == GuestMoment::called;
                    if (parts.size() != (arriving ? 2u : 1u) || parts[0].speaker != rin_look || (arriving && parts[1].speaker != hana_look) ||
                        offline.find(L'{') != std::wstring::npos) return 916;
                }
        }
    return 0;
}

// A slow call gets a copy of the same request; the first good answer wins (hedged()).
int hedging() {
    using namespace companion;
    struct Count { std::atomic<int> calls{0}; };
    const auto text = [](const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); };
    // The original is slow: the copy sent after 100 ms answers first; no second copy is needed.
    auto count = std::make_shared<Count>();
    const Post slow_original = [count](const std::wstring&, const std::wstring&, const std::string&) {
        const int n = count->calls++;
        if (n == 0) Sleep(1500);
        const std::string body = n == 0 ? "original" : "copy";
        return HttpResponse{200, {body.begin(), body.end()}};
    };
    auto log = std::make_shared<HedgeLog>();
    auto started = GetTickCount64();
    auto response = hedged(slow_original, {100, 400}, log)(L"https://x/", L"", "{}");
    if (text(response) != "copy" || GetTickCount64() - started > 1000 || count->calls != 2 || log->calls != std::vector<std::pair<int, int>>{{1, 1}}) return 940;
    // A quick answer goes alone; so does a model list (GET).
    count = std::make_shared<Count>();
    const Post quick = [count](const std::wstring&, const std::wstring&, const std::string&) { ++count->calls; return HttpResponse{200, {'o', 'k'}}; };
    log = std::make_shared<HedgeLog>();
    if (text(hedged(quick, {100}, log)(L"https://x/", L"", "{}")) != "ok" || hedged(quick, {0})(L"https://x/", L"", "").status != 200 || count->calls != 2 ||
        log->calls != std::vector<std::pair<int, int>>{{0, 0}}) return 941;
    // A failure waits while a copy is still trying; a failure with nothing else running returns at once.
    count = std::make_shared<Count>();
    const Post failing_original = [count](const std::wstring&, const std::wstring&, const std::string&) {
        if (count->calls++ == 0) { Sleep(150); return HttpResponse{503, {}}; }
        Sleep(200);
        return HttpResponse{200, {'o', 'k'}};
    };
    if (text(hedged(failing_original, {50})(L"https://x/", L"", "{}")) != "ok") return 942;
    count = std::make_shared<Count>();
    const Post refused = [count](const std::wstring&, const std::wstring&, const std::string&) { ++count->calls; return HttpResponse{401, {}}; };
    started = GetTickCount64();
    if (hedged(refused, {500, 1000})(L"https://x/", L"", "{}").status != 401 || GetTickCount64() - started > 300 || count->calls != 1) return 943;
    // A cancelled worker does not wait for any of them.
    {
        std::atomic_bool cancelled{true};
        HttpCancellation cancellation(cancelled);
        const Post slow = [](const std::wstring&, const std::wstring&, const std::string&) { Sleep(1500); return HttpResponse{200, {'o', 'k'}}; };
        started = GetTickCount64();
        if (hedged(slow, {100})(L"https://x/", L"", "{}").status != 0 || GetTickCount64() - started > 500) return 944;
    }
    // ask() records each call and tool in its trace.
    Connection gemini; gemini.provider = Provider::gemini;
    Trace trace;
    const std::string ok = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[smile]Hej!"}]},"finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":812,"candidatesTokenCount":9}})";
    gemini.token = L"abcdefghijklmnopqrstuvwxyz.0123456789";
    const auto reply = ask(gemini, L"sys", {{true, L"hej"}}, [&](const std::wstring&, const std::wstring&, const std::string&) { return HttpResponse{200, {ok.begin(), ok.end()}}; },
                           100, 1.0f, nullptr, &trace);
    if (!reply.ok || trace.calls.size() != 1 || trace.calls[0].prompt_tokens != 812 || trace.calls[0].output_tokens != 9 || trace.calls[0].status != 200) return 946;
    return 0;
}

// Manual: one real request with this machine's Foundry credentials (FOUNDRY_* or opencode).
// The model of the manual runs: LAUNCHME_LIVE_MODEL (e.g. grok-4-7 for Grok through Foundry), else the default.
std::string liveModel() {
    char value[128]{};
    const DWORD length = GetEnvironmentVariableA("LAUNCHME_LIVE_MODEL", value, sizeof(value));
    return length && length < sizeof(value) ? std::string(value, length) : std::string(companion::default_model);
}

int live() {
    using namespace companion;
    const auto connection = resolve(L"", L"", liveModel(), systemProbe());
    if (!connection.ready()) { std::cerr << "No Foundry host/token found\n"; return 200; }
    const auto library = digest(fixtureGames(), 9);
    const auto started = GetTickCount64();
    const auto reply = ask(connection, systemPrompt(Persona::tsundere, hana_look, library, false, 1'760'100'000, 3, 21),
                           {{true, kickoff(Kickoff::chat, false)}}, post);
    std::cout << "status " << reply.status << " in " << GetTickCount64() - started << " ms, ok=" << reply.ok << "\n";
    std::cout << text_encoding::utf8Lenient(reply.ok ? reply.text : reply.error) << "\n";
    for (const auto& game : reply.games) std::cout << "  game: " << text_encoding::utf8Lenient(game) << (library.find(game) ? " (in library)" : " (UNKNOWN)") << "\n";
    for (const auto& segment : reply.emotions) std::cout << "  emotion@" << segment.at << ": " << emotionKey(segment.emotion) << "\n";
    return reply.ok ? 0 : 201;
}

// Manual: the real model with a guest dropping in (Rin, yandere, with Hana the tsundere), then
// Hana alone answering in two short messages; prints the messages as the chat would show them.
int liveDuo() {
    using namespace companion;
    const auto connection = resolve(L"", L"", default_model, systemProbe());
    if (!connection.ready()) { std::cerr << "No Foundry host/token found\n"; return 220; }
    const auto library = digest(fixtureGames(), 9);
    const std::vector<Turn> opening{{true, kickoff(Kickoff::chat, false)}, {false, L"[smug]Znowu ty? W co dziś grasz, baka?"}, {true, L"Chyba w Elden Ring."}};
    for (int round = 0; round < 2; ++round) {
        PromptExtras extras;
        auto turns = opening;
        std::optional<Look> guest;
        if (round == 0) {
            extras.guest = Guest{rin_look, Persona::yandere}; guest = rin_look;
            turns.push_back({true, guestKickoff(GuestMoment::drop_in, hana_look, rin_look, false)});
        } else extras.burst = true;
        const auto started = GetTickCount64();
        const auto reply = ask(connection, systemPrompt(Persona::tsundere, hana_look, library, false, 1'760'100'000, 3, 21, extras), turns, post, guest ? 560 : 420);
        std::cout << (guest ? "duo" : "burst") << ": status " << reply.status << " in " << GetTickCount64() - started << " ms\n";
        if (!reply.ok) { std::cout << text_encoding::utf8Lenient(reply.error) << "\n"; return 221; }
        std::cout << text_encoding::utf8Lenient(reply.raw) << "\n--\n";
        for (const auto& line : lines(reply.raw, hana_look, guest, guest ? 6 : 3))
            std::cout << "  " << text_encoding::utf8Lenient(name(line.speaker)) << " [" << emotionKey(line.emotions.front().emotion) << "]: " << text_encoding::utf8Lenient(line.text) << "\n";
    }
    return 0;
}

// Manual: where the time goes on the real model — the same chat turn three times each with the
// default model, her tools declared, the Japanese voice line asked for, a guest, and other models.
int liveLatency() {
    using namespace companion;
    const auto connection = resolve(L"", L"", default_model, systemProbe());
    if (!connection.ready()) { std::cerr << "No Foundry host/token found\n"; return 230; }
    const auto library = digest(fixtureGames(), 9);
    const auto index = toolIndex();
    tools::Context context{false, 1'760'000'000, 7, {}};
    Tools set;
    set.declarations = tools::declarations(false);
    const std::vector<Turn> turns{{true, kickoff(Kickoff::chat, false)}, {false, L"[smug]Znowu ty? W co dziś grasz, baka?"}, {true, L"Nie wiem, co polecasz na wieczór?"}};
    struct Case { const char* label; std::string model; bool tools, guest, hedge; int tokens, runs; };
    const Case cases[]{{"flash 3.8", default_model, false, false, false, 420, 3}, {"flash 3.8 + tools", default_model, true, false, false, 420, 3},
                       {"flash 3.8 + guest", default_model, false, true, false, 560, 3},
                       {"flash 3.5", "gemini-3-5-flash", false, false, false, 420, 3}, {"flash lite 3.1", "gemini-3-1-flash-lite", false, false, false, 420, 3},
                       {"flash 3.8 hedged", default_model, false, false, true, 420, 8}};
    for (const auto& item : cases) {
        auto chosen = connection; chosen.model = item.model;
        PromptExtras extras; extras.tools = item.tools;
        if (item.guest) extras.guest = Guest{rin_look, Persona::yandere};
        const auto system = systemPrompt(Persona::tsundere, hana_look, library, false, 1'760'100'000, 3, 21, extras);
        set.run = [&](const std::string& name, const json::Value& args) { return tools::run(name, args, index, context); };
        for (int run = 0; run < item.runs; ++run) {
            Trace trace;
            const auto log = std::make_shared<HedgeLog>();
            const auto started = GetTickCount64();
            const auto reply = ask(chosen, system, turns, item.hedge ? hedged(post, {6000, 12000}, log) : Post(post), item.tokens, 1.0f, item.tools ? &set : nullptr, &trace);
            std::cout << item.label << " #" << run << ": " << GetTickCount64() - started << " ms, " << (reply.ok ? "ok" : "FAILED") << ", "
                      << reply.text.size() << " chars, system " << system.size() << " chars, tools " << reply.tools.size() << " |";
            for (size_t i = 0; i < trace.calls.size(); ++i) {
                const auto& call = trace.calls[i];
                std::cout << " [" << call.ms << " ms, in " << call.prompt_tokens << " tok, out " << call.output_tokens << ", thought " << call.thought_tokens;
                if (i < log->calls.size() && log->calls[i].first) std::cout << ", copies " << log->calls[i].first << ", answered by " << log->calls[i].second;
                std::cout << "]";
            }
            std::cout << "\n";
        }
    }
    return 0;
}

// Manual: the real model with her tools on a small index ("Jaka jest moja najstarsza gra?").
int liveTools() {
    using namespace companion;
    const auto connection = resolve(L"", L"", liveModel(), systemProbe());
    if (!connection.ready()) { std::cerr << "No Foundry host/token found\n"; return 210; }
    const auto index = toolIndex();
    tools::Context context{false, 1'760'000'000, 7, {}};
    Tools set;
    set.declarations = tools::declarations(false);
    set.run = [&](const std::string& name, const json::Value& args) {
        std::cout << "  tool " << name << " " << json::serialize(args) << "\n";
        return tools::run(name, args, index, context);
    };
    PromptExtras extras; extras.tools = true;
    const auto library = digest(fixtureGames(), 9);
    for (const auto* question : {L"Jaka jest moja najstarsza gra?", L"Ile mam gier akcji i która z nich jest najlepiej oceniana?", L"Co jest podobne do Portal 2?"}) {
        const auto started = GetTickCount64();
        std::string last;
        const auto reply = ask(connection, systemPrompt(Persona::kuudere, hana_look, library, false, 1'760'100'000, 3, 21, extras),
                               {{true, question}}, [&](const std::wstring& url, const std::wstring& headers, const std::string& body) {
                                   auto response = post(url, headers, body);
                                   last = std::string(response.body.begin(), response.body.end());
                                   return response;
                               }, 420, 1.0f, &set);
        std::cout << text_encoding::utf8Lenient(question) << " -> status " << reply.status << " in " << GetTickCount64() - started << " ms, tools " << reply.tools.size() << "\n";
        std::cout << text_encoding::utf8Lenient(reply.ok ? reply.text : reply.error) << "\n";
        if (!reply.ok) { std::cout << "last response: " << last.substr(0, 1500) << "\n"; return 211; }
    }
    return 0;
}

// Her conversations between starts (companion_chats.hpp): the file format, names, listing and
// pruning, days that start at 4:00, her rest, goodbyes and the note when the player is back.
int conversations() {
    using namespace companion;
    namespace chats = companion::chats;
    // Local time: dates round-trip through file names; a late night belongs to the day before.
    const auto started = chats::localSeconds(2026, 10, 14, 15, 30, 5);
    if (chats::fileStem(started) != L"20261014-153005" || chats::parseStem(L"20261014-153005") != started ||
        chats::parseStem(L"20261014_153005") || chats::parseStem(L"20261314-153005") || chats::parseStem(L"x")) return 1101;
    if (chats::dayOf(chats::localSeconds(2026, 10, 15, 3, 59, 0)) != chats::dayOf(started) ||
        chats::dayOf(chats::localSeconds(2026, 10, 15, 4, 0, 0)) != chats::dayOf(started) + 1) return 1102;
    if (chats::label(started, false) != L"14.10 · 15:30" || chats::label(started, true) != L"Oct 14 · 15:30" ||
        chats::ago(started, started + 86400, false) != L"wczoraj" || chats::ago(started, started + 3 * 86400, true) != L"3 days ago") return 1103;
    if (chats::duration(25 * 60, false) != L"25 min" || chats::duration(70 * 60, false) != L"1 godz. 10 min" || chats::duration(120 * 60, true) != L"2 h" ||
        chats::duration(1, false) != L"1 min") return 1104;
    // Her rest: shorter as she grows fond of the player, each character at her own pace, always 5–120 minutes.
    for (int persona = 0; persona <= static_cast<int>(Persona::kanedere); ++persona)
        for (const int level : {1, 20, 40, affinity::max_level})
            for (const uint32_t seed : {0u, 7u, 123456u, 0xffffffffu}) {
                const auto rest = chats::restSeconds(static_cast<Persona>(persona), level, seed);
                if (rest < 5 * 60 || rest > 120 * 60) return 1105;
            }
    if (chats::restSeconds(Persona::tsundere, 40, 25) >= chats::restSeconds(Persona::tsundere, 1, 25) ||
        chats::restSeconds(Persona::tsundere, affinity::max_level, 25) >= chats::restSeconds(Persona::tsundere, 40, 25) ||
        chats::restSeconds(Persona::yandere, 10, 25) >= chats::restSeconds(Persona::himedere, 10, 25)) return 1106;
    // Goodbyes (and goodnights) in every character (tagged, both languages), the hidden notes.
    for (int persona = 0; persona <= static_cast<int>(Persona::kanedere); ++persona)
        for (const bool en : {false, true})
            for (const uint32_t seed : {0u, 1u})
                for (const bool bedtime : {false, true}) {
                    const auto line = parseText(chats::farewellLine(static_cast<Persona>(persona), en, seed, bedtime));
                    if (!line.ok || line.emotions.empty() || line.text.find(L'[') != std::wstring::npos) return 1107;
                    if (bedtime && line.text.find(en ? L"ight" : L"pa") == std::wstring::npos && line.text.find(en ? L"bed" : L"Dobranoc") == std::wstring::npos) return 1107;
                }
    if (chats::farewellKickoff(false).find(L"pożegnaj") == std::wstring::npos || chats::returnNote(3 * 3600, false).find(L"3 godz.") == std::wstring::npos ||
        chats::returnNote(40 * 60, true).find(L"40 min") == std::wstring::npos || chats::farewellKickoff(false, true).find(L"idziesz teraz spać") == std::wstring::npos ||
        chats::farewellKickoff(false).find(L"spać") != std::wstring::npos || chats::bedtimeKickoff(false).find(L"dobranoc") == std::wstring::npos) return 1108;
    // The file: messages (a roulette, her photo by its relative path), her context and the rest.
    chats::Conversation conversation;
    conversation.look = rin_look;
    conversation.started = started; conversation.updated = started + 600;
    conversation.lines.push_back({true, L"Hej, zażółć gęślą jaźń", {}, false, rin_look});
    conversation.lines.push_back({false, L"Cześć!", {{0, Emotion::smile}, {3, Emotion::shy}}, false, rin_look});
    chats::Line spin; spin.speaker = rin_look; spin.roulette = L"Hades"; spin.roulette_key = "steam:12";
    conversation.lines.push_back(spin);
    chats::Line photo; photo.speaker = rin_look; photo.photo = true;
    conversation.lines.push_back(photo);
    conversation.lines.push_back({false, L"Zagraj w Hades!", {}, false, hana_look});
    conversation.lines.back().mentions = {{9, 5}};
    conversation.turns = {{true, L"(kickoff)"}, {false, L"[smile]Cześć!"}};
    conversation.summarized = 1;
    conversation.photo = L"rin/00-09/a.png";
    conversation.ended = true; conversation.rest_until = started + 1800;
    const auto loaded = chats::Conversation::load(conversation.save());
    if (!loaded || loaded->look != rin_look || loaded->started != started || loaded->updated != started + 600 || loaded->lines.size() != 5 ||
        loaded->lines[0].text != L"Hej, zażółć gęślą jaźń" || !loaded->lines[0].user || loaded->lines[1].emotions.size() != 2 ||
        loaded->lines[1].emotions[1].emotion != Emotion::shy || loaded->lines[2].roulette != L"Hades" || loaded->lines[2].roulette_key != "steam:12" ||
        !loaded->lines[3].photo || loaded->lines[4].speaker != hana_look || loaded->turns.size() != 2 || loaded->turns[1].user ||
        loaded->summarized != 1 || loaded->photo != L"rin/00-09/a.png" || !loaded->ended || loaded->rest_until != started + 1800 ||
        loaded->save() != conversation.save()) return 1109;
    if (loaded->lines.back().mentions != std::vector<GameMention>{{9, 5}} || !loaded->lines[0].mentions.empty()) return 1406;
    const auto malformed = chats::Conversation::load(R"({"version":1,"look":"rin","lines":[{"text":"Hades","mentions":[[-1,5],[0,99],[0,5],[1,2]]},{"user":true,"text":"Hades","mentions":[[0,5]]},{"text":"Old chat"}]})");
    if (!malformed || malformed->lines.size() != 3 || malformed->lines[0].mentions != std::vector<GameMention>{{0, 5}} ||
        !malformed->lines[1].mentions.empty() || !malformed->lines[2].mentions.empty()) return 1407;
    if (chats::Conversation::load("{}") || chats::Conversation::load(R"({"version":1,"look":"nobody"})")) return 1110;
    // On disk (DPAPI): newest first by name, the latest read back, older ones pruned.
    const auto config = std::filesystem::temp_directory_path() / (L"launchme-chats-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(config);
    for (int i = 0; i < 4; ++i) {
        auto copy = conversation;
        copy.started = started + static_cast<uint64_t>(i) * 3600;
        copy.lines[0].text = L"Rozmowa " + std::to_wstring(i);
        if (!chats::write(config, copy)) return 1111;
    }
    atomic_file::write(chats::folder(config, rin_look) / L"notes.txt", std::string("x")); // Foreign files are ignored.
    const auto all = chats::list(config, rin_look);
    if (all.size() != 4 || all.front() != started + 3 * 3600 || all.back() != started || !chats::list(config, hana_look).empty()) return 1112;
    const auto latest = chats::latest(config, rin_look);
    if (!latest || latest->lines[0].text != L"Rozmowa 3" || chats::latest(config, hana_look)) return 1113;
    if (!std::filesystem::exists(chats::file(config, rin_look, started)) || chats::folder(config, rin_look).filename() != L"rin") return 1114;
    chats::prune(config, rin_look, 2);
    const auto kept = chats::list(config, rin_look);
    if (kept.size() != 2 || kept.back() != started + 2 * 3600 || !std::filesystem::exists(chats::folder(config, rin_look) / L"notes.txt")) return 1115;
    std::filesystem::remove_all(config);
    return 0;
}
// What makes her livelier: her examples and taste from character.txt, her mood as a conversation
// starts, the week's small talk, her new tools (plan, recall, news), each companion's own
// summaries, and her words streaming in (server-sent events, whole words only).
int intelligence() {
    using namespace companion;
    constexpr auto npos = std::wstring::npos;
    const uint64_t now = 1'760'000'000;
    const auto args = [](const char* text) { return *json::parse(text); };

    // character.txt: example and taste lines; in her prompt only when she is alone.
    const auto parsed = parseCharacters("[ex]\nname=Ex\nexample_pl=Gracz: Hej || Ty: [smile]Cześć!\nexample_pl=Gracz: A? || Ty: B.\n"
                                        "example_en=Player: Hi || You: [smile]Hello!\nlikes=Horror, Souls-like | Gothic\ndislikes=Sports\n");
    if (parsed.size() != 1 || parsed[0].examples_pl.size() != 2 || parsed[0].examples(true).size() != 1 ||
        parsed[0].likes != std::vector<std::wstring>{L"Horror", L"Souls-like", L"Gothic"} || parsed[0].dislikes != std::vector<std::wstring>{L"Sports"}) return 1200;
    {
        const auto previous = characters();
        setCharacters(parsed);
        const auto prompt = personalize(systemPrompt(Persona::genki, static_cast<Look>(0), digest({}, 1, {}), false, now, 1, 12), Gender::female);
        setCharacters(previous);
        // His/her turn in an example is the person, never "Gracz:" (here a woman).
        if (prompt.find(L"  Ona: Hej\n  Ty: [smile]Cześć!") == npos || prompt.find(L"uwielbiasz Horror, Souls-like, Gothic") == npos ||
            prompt.find(L"nie przepadasz za: Sports") == npos) return 1201;
    }
    PromptExtras moody; moody.mood = L"\nNASTRÓJ"; moody.playing = L"\nW-TRAKCIE"; moody.week = L"\nTYDZIEŃ";
    if (systemPrompt(Persona::genki, hana_look, digest({}, 1, {}), false, now, 1, 12, moody).find(L"NASTRÓJ\nW-TRAKCIE\nTYDZIEŃ") == npos) return 1202;

    // Her mood: nothing the first time or the next day; missed after days away; jealous of another;
    // pleased with a played suggestion (over a late night); her notes are used up.
    const std::function<std::wstring(std::string_view)> name = [](std::string_view key) { return key == "rin" ? std::wstring(L"Rin") : std::wstring(L"Hana"); };
    mood::State state;
    const uint64_t day = mood::day, t0 = 20000 * day;
    if (!mood::begin(state, "hana", t0, false, name).empty() || !mood::begin(state, "hana", t0 + day, false, name).empty()) return 1210;
    if (mood::begin(state, "hana", t0 + 5 * day, false, name).find(L"od 4 dni") == npos) return 1211;
    if (!mood::begin(state, "rin", t0 + 5 * day + 3600, false, name).empty()) return 1212;
    if (mood::begin(state, "hana", t0 + 5 * day + 7200, true, name).find(L"chatted with Rin") == npos) return 1213;
    state.note("hana", mood::Kind::pleased, t0 + 6 * day, L"Hades");
    state.note("hana", mood::Kind::late, t0 + 6 * day);
    state.note("rin", mood::Kind::late, t0 + 6 * day);
    const auto pleased = mood::begin(state, "hana", t0 + 6 * day + 60, false, name);
    if (pleased.find(L"[[Hades]]") == npos || pleased.find(L"do późna") != npos || pleased.find(L"stęskniłaś") != npos) return 1214;
    if (std::any_of(state.notes.begin(), state.notes.end(), [](const mood::Note& note) { return note.look == "hana"; })) return 1215;
    const auto saved = mood::serialize(state);
    const auto reloaded = mood::parse(saved);
    if (mood::serialize(reloaded) != saved || reloaded.talked.size() != 2 || reloaded.notes.size() != 1 || reloaded.notes[0].kind != mood::Kind::late) return 1216;
    if (mood::begin(state, "rin", t0 + 6 * day + 120, false, name).find(L"do późna") == npos) return 1217;
    state.note("rin", mood::Kind::duo, t0 + 7 * day, L"Hana");
    const auto saved_duo = mood::serialize(state);
    if (mood::parse(saved_duo).notes.size() != 1 || mood::parse(saved_duo).notes[0].kind != mood::Kind::duo) return 1218;
    if (personalize(mood::begin(state, "rin", t0 + 7 * day + 60, false, name), Gender::male).find(L"rozmawiał z Hana") == npos) return 1219;
    // A near stranger neither misses him nor minds whom else he talks with; a light flirt only notices
    // the absence; an open one at the top does not mind at all.
    {
        mood::State close;
        const mood::Closeness stranger{0, 0}, light{1, 1}, open_top{5, 0};
        mood::begin(close, "hana", t0, false, name, stranger);
        if (!mood::begin(close, "hana", t0 + 5 * day, false, name, stranger).empty()) return 1225;
        const auto noticed = personalize(mood::begin(close, "hana", t0 + 10 * day, false, name, light), Gender::female);
        if (noticed.find(L"nie pisała od 5 dni") == npos || noticed.find(L"stęskniłaś") != npos) return 1226;
        mood::begin(close, "rin", t0 + 10 * day + 60, false, name);
        if (!mood::begin(close, "hana", t0 + 10 * day + 120, false, name, stranger).empty()) return 1227;
        mood::begin(close, "rin", t0 + 10 * day + 180, false, name);
        const auto relaxed = mood::begin(close, "hana", t0 + 10 * day + 240, true, name, open_top);
        if (relaxed.find(L"chatted with Rin") == npos || relaxed.find(L"jealous") != npos) return 1228;
    }

    // The week: a snapshot, a week later her small talk in words (never numbers).
    if (!week::due({}, 20261018)) return 1220;
    const auto before = week::snapshot({{"a", L"Hades", 600}, {"b", L"Celeste", 100}, {"c", L"Inside", 0}}, 20261011);
    if (week::due(before, 20261017) || !week::due(before, 20261018)) return 1221;
    const auto week_file = week::parse(week::serialize(before));
    if (week_file.day != 20261011 || week_file.minutes.size() != 3 || week_file.minutes.at("a") != 600) return 1222;
    const std::vector<week::Game> lately{{"a", L"Hades", 900}, {"b", L"Celeste", 190}, {"c", L"Inside", 40}, {"d", L"Tunic", 0}};
    const auto talk = personalize(week::text(week_file, lately, {{"c", L"Inside"}}, false), Gender::male);
    if (talk.find(L"grał sporo w [[Hades]] i parę godzin w [[Celeste]]") == npos || talk.find(L"dostał nową grę, [[Tunic]]") == npos ||
        talk.find(L"zagrał w [[Inside]], którą mu poleciłaś") == npos || talk.find(L"300") != npos) return 1223;
    if (!week::text({}, lately, {}, false).empty() || !week::text(week::snapshot(lately, 20261018), lately, {}, true).empty()) return 1224;

    // Her new tools: declared on request; plan_grania and the game in progress from the index.
    const auto all = json::parse(tools::declarations(tools::Features{true, true, true, true}));
    if (!all || all->elements.size() != 8 || all->elements[5].get("name")->stringOr() != tools::recall ||
        all->elements[6].get("name")->stringOr() != tools::news || all->elements[7].get("name")->stringOr() != tools::plan) return 1230;
    if (json::parse(tools::declarations(tools::Features{}))->elements.size() != 4) return 1231;
    game_index::Index planned_index;
    {
        using namespace game_index;
        const auto game = [](const wchar_t* title, bool installed, uint64_t played, std::optional<uint32_t> minutes) {
            Patch patch; patch.title = title; patch.store = L"Steam"; patch.installed = installed; patch.last_played = played;
            if (minutes) patch.playtime_minutes = minutes;
            return patch;
        };
        planned_index.syncLibrary({{"steam:1", game(L"Hollow Knight", true, now - 2 * 86400, 300)}, {"steam:2", game(L"Celeste", true, 0, std::nullopt)},
                                   {"steam:3", game(L"Inside", false, 0, std::nullopt)}}, now);
        Patch a; a.hltb_main = 600; planned_index.merge("steam:1", game_index::Source::hltb, a);
        Patch b; b.hltb_main = 120; planned_index.merge("steam:2", game_index::Source::hltb, b);
        Patch c; c.hltb_main = 200; planned_index.merge("steam:3", game_index::Source::hltb, c);
    }
    tools::Context context{false, now, 1, {}};
    const auto plan = tools::run(tools::plan, args(R"({"minuty":150})"), planned_index, context);
    if (!plan.get("in_progress") || plan.get("in_progress")->elements.size() != 1 ||
        plan.get("in_progress")->elements[0].get("story_left_about")->stringOr() != "5 h" || plan.get("in_progress")->elements[0].get("could_finish_now")->flag ||
        plan.get("short_unplayed")->elements.size() != 1 || plan.get("short_unplayed")->elements[0].get("title")->stringOr() != "Celeste") return 1232;
    if (tools::playingText(planned_index, now, false).find(L"[[Hollow Knight]], mniej więcej w połowie") == npos ||
        !tools::playingText(planned_index, now + 30 * 86400, false).empty()) return 1233;
    // przypomnij and wiadomosci: answered by the app, without the index.
    if (!tools::run(tools::recall, args(R"({"temat":"Hades"})"), game_index::Index{}, context).get("error")) return 1234;
    context.recall = [](const std::wstring& topic) { return tools::recallAnswer({{L"14.10 · 15:30", true, L"Kocham " + topic}}, {}); };
    const auto recalled = tools::run(tools::recall, args(R"({"temat":"Hades"})"), game_index::Index{}, context);
    if (!recalled.get("conversations") || recalled.get("conversations")->elements[0].get("who")->stringOr() != "them" ||
        recalled.get("conversations")->elements[0].get("text")->stringOr() != "Kocham Hades") return 1235;
    if (!tools::recallAnswer({}, {}).get("note")) return 1236;
    const auto news = tools::newsAnswer({}, {{L"Gra X", {}, 20261020, 0}}, {{L"Nagłówek", L"Eurogamer", 0, now - 86400}}, 20261018, now);
    if (news.get("premieres")->elements[0].get("release")->stringOr() != "in 2 days" ||
        news.get("headlines")->elements[0].get("published")->stringOr() != "yesterday" || news.get("your_games")) return 1237;
    if (tools::newsAnswer({}, {}, {}, 20261018, now).get("note")->stringOr().find("No news") == std::string::npos) return 1238;

    // Her earlier conversations: the lines that share the topic's words (endings aside), not today's.
    const auto config = std::filesystem::temp_directory_path() / (L"launchme-recall-" + std::to_wstring(GetCurrentProcessId()));
    chats::Conversation old_chat;
    old_chat.look = hana_look; old_chat.started = old_chat.updated = chats::localSeconds(2026, 10, 1, 12, 0, 0);
    old_chat.lines = {{true, L"Wczoraj skończyłem Hadesa!"}, {false, L"Brawo! A Celeste?"}};
    chats::Conversation today_chat = old_chat;
    today_chat.started = today_chat.updated = chats::localSeconds(2026, 10, 18, 12, 0, 0);
    today_chat.lines = {{true, L"Hades jest super."}};
    if (!chats::write(config, old_chat) || !chats::write(config, today_chat)) return 1240;
    const auto found = chats::search(config, hana_look, L"o Hadesie", today_chat.started);
    std::filesystem::remove_all(config);
    if (found.size() != 1 || !found[0].user || found[0].started != old_chat.started || found[0].text != L"Wczoraj skończyłem Hadesa!") return 1241;
    if (chats::searchWords(L"o Hadesie, Hades!") != std::vector<std::wstring>{L"hades"}) return 1242;

    // Each companion's own summaries in her prompt (shared ones too), the latest few.
    Memory memory;
    memory.summarize(L"Hana: o Hadesie.", now - 300, "hana");
    for (int i = 0; i < 4; ++i) memory.summarize(L"Rin " + std::to_wstring(i), now - 200 + i, "rin");
    memory.summarize(L"Wspólne.", now - 100);
    const auto hana_memory = memory.prompt(false, now, false, true, "hana");
    if (hana_memory.find(L"o Hadesie") == npos || hana_memory.find(L"Wspólne") == npos || hana_memory.find(L"Rin 0") != npos) return 1250;
    const auto rin_memory = memory.prompt(false, now, false, true, "rin");
    if (rin_memory.find(L"Rin 3") == npos || rin_memory.find(L"Rin 2") == npos || rin_memory.find(L"Rin 1") != npos || rin_memory.find(L"Hadesie") != npos) return 1251;
    const auto memory_file = Memory::load(memory.save());
    if (!memory_file || memory_file->summaries.front().look != "hana" || memory_file->summaries.back().look != "") return 1252;

    // Streaming: events cut anywhere, whole words only, chunks joined as one response.
    EventReader reader;
    auto events = reader.feed("data: {\"a\":1}\r\n\r\ndata: {\"b\"");
    if (events != std::vector<std::string>{"{\"a\":1}"}) return 1260;
    events = reader.feed(":2}\n\n");
    if (events != std::vector<std::string>{"{\"b\":2}"} || !reader.finish().empty()) return 1261;
    if (speakablePrefix(L"[smile]Hej, co") != 11 || speakablePrefix(L"Zagraj w [[Hollow Kni") != 8 || speakablePrefix(L"Hej [smi") != 3 || speakablePrefix(L"[smile]Hej") != 0) return 1262;
    if (streamWords(R"({"candidates":[{"content":{"parts":[{"text":"x","thought":true},{"text":"Hej"}]}}]})") != L"Hej") return 1263;
    const auto joined = json::parse(joinStream({R"({"candidates":[{"content":{"parts":[{"text":"x","thought":true},{"thoughtSignature":"S1","functionCall":{"name":"statystyki","args":{}}}]}}]})",
                                                R"({"candidates":[{"content":{"parts":[{"text":"A"}]}}]})",
                                                R"({"candidates":[{"content":{"parts":[{"text":"B","thoughtSignature":"S2"}]},"finishReason":"STOP"}],"usageMetadata":{"candidatesTokenCount":3}})"}));
    if (!joined) return 1264;
    const auto& candidate = joined->get("candidates")->elements[0];
    const auto& parts = candidate.get("content")->get("parts")->elements;
    if (parts.size() != 2 || !parts[0].get("functionCall") || parts[0].get("thoughtSignature")->stringOr() != "S1" || parts[1].get("text")->stringOr() != "AB" ||
        parts[1].get("thoughtSignature")->stringOr() != "S2" || candidate.get("finishReason")->stringOr() != "STOP" || !joined->get("usageMetadata")) return 1265;
    // ask() with a stream: her words as they come, the reply as usual, the plain transport unused.
    Connection connection;
    connection.host = L"https://stack.example.palantirfoundry.test/"; connection.token = L"abcdefghijklmnopqrstuvwxyz.0123456789";
    std::wstring heard, stream_url;
    Stream stream;
    stream.transport = [&](const std::wstring& url, const std::wstring&, const std::string&, const Stream::Piece& piece) {
        stream_url = url;
        const std::string body = "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[{\"text\":\"[smile]Hej, \"}]}}]}\r\n\r\n"
                                 "data: {\"candidates\":[{\"content\":{\"role\":\"model\",\"parts\":[{\"text\":\"zagraj w [[Hades]]!\"}]},\"finishReason\":\"STOP\"}]}\r\n\r\n";
        piece(std::string_view(body).substr(0, 40));
        piece(std::string_view(body).substr(40));
        return HttpResponse{200, {body.begin(), body.end()}};
    };
    stream.text = [&](std::wstring_view words) { heard += words; };
    bool whole = false;
    const Post plain = [&](const std::wstring&, const std::wstring&, const std::string&) { whole = true; return HttpResponse{}; };
    const auto streamed = ask(connection, L"sys", {{true, L"?"}}, plain, 400, 1.0f, nullptr, nullptr, &stream);
    if (!streamed.ok || streamed.text != L"Hej, zagraj w Hades!" || heard != L"[smile]Hej, zagraj w [[Hades]]!" || whole ||
        stream_url.find(L":streamGenerateContent?alt=sse") == npos) return 1266;
    if (streamed.mentions != std::vector<GameMention>{{14, 5}}) return 1408;
    // A proxy that refuses streaming: the whole answer instead, and no more streaming tries (last here).
    stream.transport = [](const std::wstring&, const std::wstring&, const std::string&, const Stream::Piece&) { return HttpResponse{404, {}}; };
    const std::string hello = R"({"candidates":[{"content":{"role":"model","parts":[{"text":"[smile]Hej!"}]},"finishReason":"STOP"}]})";
    const Post answers = [&](const std::wstring&, const std::wstring&, const std::string&) { whole = true; return HttpResponse{200, {hello.begin(), hello.end()}}; };
    const auto refused = ask(connection, L"sys", {{true, L"?"}}, answers, 400, 1.0f, nullptr, nullptr, &stream);
    if (!refused.ok || refused.text != L"Hej!" || !whole) return 1267;

    // Contextual reply chips (<opcje> / <options>), 5 new tools (achievements, friends, collections,
    // history, roulette), and unified companion::tts (sentences PL/EN/JA, duet utterances, styles).
    {
        const auto with_opts = parseText(L"[smile]W co dziś gramy? <opcje>Mam 45 minut|Zakręć ruletką</opcje>");
        if (!with_opts.ok || with_opts.text != L"W co dziś gramy?" || with_opts.prompts.size() != 2 ||
            with_opts.prompts[0] != L"Mam 45 minut" || with_opts.prompts[1] != L"Zakręć ruletką") return 1270;
        if (speakablePrefix(L"[smile]Hej! <opcje>Tak") != 11 || completeSentences(L"[smile]Hej! <opcje>Tak") != L"[smile]Hej!") return 1271;

        const auto full_decls = json::parse(tools::declarations(tools::Features{true, true, true, true, true, true, true, true, true, true}));
        if (!full_decls || full_decls->elements.size() != 14) return 1272;

        // osiagniecia_gry: fresh unlocks and rare trophies (<10%) surfaced, old common ones skipped.
        context.achievements = [&](uint32_t, const std::string&) {
            tools::AchievementSummary sum{2, 4, {}};
            sum.items.push_back({L"First Step", L"Start the game", true, false, now - 200 * 86400, 85.0f});
            sum.items.push_back({L"Steel Soul", L"No deaths", true, false, now - 86400, 2.4f});
            sum.items.push_back({L"Easy One", L"Jump once", false, false, 0, 72.0f});
            sum.items.push_back({L"Secret", L"Hidden", false, true, 0, 5.0f});
            return sum;
        };
        const auto ach = tools::run(tools::achievements, args(R"({"tytul":"Hollow Knight"})"), planned_index, context);
        if (ach.get("unlocked")->intOr() != 2 || ach.get("recent_unlocks")->elements.size() != 1 ||
            ach.get("recent_unlocks")->elements[0].get("name")->stringOr() != "Steel Soul" ||
            ach.get("rarest_unlocked")->elements.size() != 1 || ach.get("easiest_missing")->elements.size() != 1) return 1273;

        // znajomi_online: matches friends' active games against the player's library.
        context.friends = [&] {
            return std::vector<tools::FriendOnline>{{L"Marek", L"in_game", L"Hollow Knight", 1}, {L"Ania", L"online", L"", 0}};
        };
        const auto fr = tools::run(tools::friends, args(R"({})"), planned_index, context);
        if (fr.get("playing_now")->elements.size() != 1 || !fr.get("playing_now")->elements[0].get("you_own_it")->flag ||
            fr.get("joinable_from_your_library")->elements.size() != 1 || fr.get("online_idle")->elements.size() != 1) return 1274;

        // kolekcje_i_ulubione: defaults to a Steam collection ("steam").
        tools::CollectionMutation last_mut;
        context.collections = [&] { return std::vector<tools::CollectionInfo>{{L"Metroidvanie", true, false, {L"Hollow Knight"}}}; };
        context.mutate_collection = [&](const tools::CollectionMutation& m) { last_mut = m; return true; };
        const auto col_list = tools::run(tools::collections, args(R"({"akcja":"list"})"), planned_index, context);
        if (col_list.get("collections")->elements.size() != 1 || col_list.get("collections")->elements[0].get("type")->stringOr() != "steam") return 1275;
        const auto col_create = tools::run(tools::collections, args(R"({"akcja":"create","nazwa":"Na wieczór","gry":["Hollow Knight","Celeste"]})"), planned_index, context);
        if (!col_create.get("ok")->flag || col_create.get("type")->stringOr() != "steam" || !last_mut.steam ||
            last_mut.keys != std::vector<std::string>{"steam:1", "steam:2"}) return 1276;

        // historia_grania and zakrec_ruletka.
        const auto hist = tools::run(tools::history, args(R"({"dni":7})"), planned_index, context);
        if (hist.get("total_played_in_window")->intOr() != 1 || hist.get("active_games")->elements.size() != 1) return 1277;
        std::vector<std::wstring> spun_reel;
        std::string spun_key;
        context.roulette = [&](const std::vector<std::wstring>& r, const std::string& k) { spun_reel = r; spun_key = k; };
        const auto roul = tools::run(tools::roulette, args(R"({"max_minut":150})"), planned_index, context);
        if (spun_reel.size() != 28 || spun_key != "steam:2" || roul.get("picked")->get("title")->stringOr() != "Celeste") return 1278;

        // Her voice: PL/EN sentence splitting and a two-speaker duet, each with her own voice and feeling.
        const auto pl_sents = elevenlabs::sentences(L"Hej! Wersja 1.5 działa super. Gramy?");
        if (pl_sents.size() != 2 || pl_sents[0].find(L"Wersja 1.5") == npos || pl_sents[1] != L"Gramy?") return 1279;
        const auto duo_lines = lines(L"Hana: [smug]To mój czat! || Rin: [excited]Teraz nasz!", hana_look, rin_look, 6);
        const auto duo_chunks = elevenlabs::chunks(duo_lines, [](Look l) {
            return elevenlabs::speaker(character(l), l == rin_look ? Persona::yandere : Persona::himedere, {}, {}, {});
        });
        if (duo_chunks.size() != 2 || duo_chunks[0].speaker != hana_look || duo_chunks[1].speaker != rin_look || duo_chunks[1].emotion != Emotion::excited ||
            duo_chunks[1].text.find(L"excited") == npos || duo_chunks[0].text.find(L"haughty") == npos) return 1280;

        // Expanded memory (500 notes / 500 summaries), near-duplicate consolidation, auto-recall, and single-entry removal.
        Memory big_mem;
        if (!big_mem.note(L"Gracz ma na imię Piotr.", now - 500) || !big_mem.note(L"Gracz ma na imię Piotr i pracuje zdalnie.", now - 400)) return 1281;
        if (big_mem.notes.size() != 1 || big_mem.notes[0].text != L"Gracz ma na imię Piotr i pracuje zdalnie.") return 1282;
        for (int i = 0; i < 40; ++i) big_mem.note(L"Zupełnie inny fakt numer " + std::to_wstring(i * 101 + 7), now - 300 + i);
        // Without query, note[0] ("Piotr") is outside the latest 30 core notes; with query="Piotr", auto-recall surfaces it!
        if (big_mem.prompt(false, now, false, true, "hana").find(L"Piotr") != npos) return 1283;
        if (big_mem.prompt(false, now, false, true, "hana", L"Piotr pyta").find(L"Piotr i pracuje zdalnie") == npos) return 1284;
        if (!big_mem.removeNote(now - 400, L"Gracz ma na imię Piotr i pracuje zdalnie.")) return 1285;
        if (big_mem.prompt(false, now, false, true, "hana", L"Piotr pyta").find(L"Piotr") != npos) return 1286;

        // Cognitive engine: <mysl> subconscious + emotion, Sleep consolidation, belief evolution,
        // multi-hop Spreading Activation, gossip blackboard, and daily Tavily reading.
        const auto sub = parseText(L"[angry]Znowu ty?! <mysl>[shy]Tak naprawdę czekałam na niego cały wieczór.</mysl>");
        if (!sub.ok || sub.text != L"Znowu ty?!" || sub.subconscious != L"Tak naprawdę czekałam na niego cały wieczór." ||
            sub.subconscious_emotion != Emotion::shy || sub.emotions.front().emotion != Emotion::angry) return 1287;
        if (speakablePrefix(L"[angry]Hej! <mysl>Ukryte") != 11 || completeSentences(L"[angry]Hej! <mysl>Ukryte") != L"[angry]Hej!") return 1288;

        const auto cons = cognition::parseConsolidation(L"<podsumowanie>Rozmawialiśmy o Silent Hill.</podsumowanie><refleksja>Gracz szuka dziś spokojniejszych gier po ciężkim tygodniu.</refleksja>");
        if (cons.summary != L"Rozmawialiśmy o Silent Hill." || cons.reflection.find(L"spokojniejszych") == npos) return 1289;

        cognition::State cog;
        cog.reflect(L"Gracz porzuca długie gry RPG w połowie.", now - 200, "hana");
        cog.reflect(L"Gracz jednak kończy długie gry RPG, gdy wciągnie go fabuła.", now - 100, "hana");
        if (cog.reflections.size() != 1 || cog.reflections[0].evolved_from.find(L"porzuca długie gry") == npos) return 1290;
        cog.think(L"Cieszę się, że przyszedł.", Emotion::shy, now - 50, "hana");
        cog.postGossip("rin", "level", L"25", now - 30);
        cog.readOnline("hana", 20261019, L"Hollow Knight", L"Nowy patch poprawia bossów.", now - 20);
        // Up to six a day, spread out (reading_gap apart); what she looked up in a chat does not count.
        if (cog.dailyReadingDue("hana", 20261019, now) || !cog.dailyReadingDue("rin", 20261019, now) ||
            !cog.dailyReadingDue("hana", 20261019, now - 20 + cognition::State::reading_gap)) return 1291;
        cog.readOnline("hana", 20261019, L"Souls-like", L"Zapowiedziano nowe DLC.", now - 10, true);
        if (!cog.dailyReadingDue("hana", 20261019, now - 20 + cognition::State::reading_gap)) return 1291;
        {
            cognition::State busy;
            for (int i = 0; i < cognition::State::readings_per_day; ++i)
                busy.readOnline("rin", 20261019, L"Temat " + std::to_wstring(i), L"Ciekawostka numer " + std::to_wstring(i) + L" z sieci.", now - 100000 + i * 10000);
            if (busy.dailyReadingDue("rin", 20261019, now + 100000) || !busy.dailyReadingDue("rin", 20261020, now + 100000)) return 1291;
        }

        // Multi-hop Spreading Activation: "deszcz" -> "jesienny" -> "Silent Hill" note.
        Memory assoc_mem;
        assoc_mem.note(L"W jesienny wieczór gracz uwielbia grać w Silent Hill.", now - 100);
        cog.learnHebbian(L"deszcz jesienny wieczór", now - 80);
        const auto activated = cog.spreadActivation(assoc_mem, "hana", L"pada deszcz", now);
        if (activated.empty() || activated.front().text.find(L"Silent Hill") == npos) return 1292;

        const auto cog_prompt = cog.prompt(assoc_mem, "hana", L"pada deszcz", now, false, [](std::string_view k) { return k == "rin" ? std::wstring(L"Rin") : std::wstring(L"Hana"); });
        if (cog_prompt.find(L"zmieniłaś zdanie") == npos || cog_prompt.find(L"Cieszę się, że przyszedł") == npos ||
            cog_prompt.find(L"Silent Hill") == npos || cog_prompt.find(L"poziom 25 u Rin") == npos ||
            cog_prompt.find(L"Nowy patch") == npos) return 1293;

        const auto reloaded_cog = cognition::State::load(cog.save());
        if (!reloaded_cog || reloaded_cog->reflections.size() != 1 || reloaded_cog->thoughts.size() != 1 ||
            reloaded_cog->readings.size() != 2 || reloaded_cog->gossip.size() != 1 || !reloaded_cog->readings[1].asked ||
            reloaded_cog->readings[0].asked) return 1294;

        // Affective memory (emotion + personal feeling on notes) and long-term evolving disposition.
        Memory aff_mem;
        if (!aff_mem.note(L"Gracz zażartował, że woli futrzaki.", now - 10, "kiko", "shy", L"zrobiło mi się gorąco, bo mam lisie uszy")) return 1295;
        const auto kiko_prompt = aff_mem.prompt(false, now, false, true, "kiko");
        const auto yuki_prompt = aff_mem.prompt(false, now, false, true, "yuki");
        if (kiko_prompt.find(L"jak się wtedy poczułaś [shy]: zrobiło mi się gorąco") == npos ||
            yuki_prompt.find(L"zrobiło mi się gorąco") != npos) return 1296;
        const auto aff_loaded = Memory::load(aff_mem.save());
        if (!aff_loaded || aff_loaded->notes.size() != 1 || aff_loaded->notes[0].emotion != "shy" ||
            aff_loaded->notes[0].feeling.find(L"lisie uszy") == npos) return 1297;

        const auto cons_disp = cognition::parseConsolidation(
            L"<podsumowanie>Żarty o lisach.</podsumowanie><refleksja>Lubi się ze mną droczyć.</refleksja><nastawienie>Mam do niego słabość, chociaż ciągle mnie zawstydza.</nastawienie>");
        if (cons_disp.disposition.find(L"słabość") == npos) return 1298;
        cog.setDisposition("hana", cons_disp.disposition);
        if (cog.dispositionOf("hana") != cons_disp.disposition ||
            cog.prompt(aff_mem, "hana", {}, now, false).find(L"długofalowe nastawienie") == npos ||
            cognition::emotionFeelingLabel(Emotion::shy, false) != L"Zawstydzona") return 1299;

        // Relationship roles, attraction, style (traditional vs casual), dilemmas, breakthroughs and distance caps.
        const auto rel_chars = parseCharacters(
            "[rin]\nname=Rin\ngender=female\nattraction=female\nattraction_flex=rare_exception\nstyle=traditional\nrole=coworker\n"
            "[mika]\nname=Mika\ngender=female\nattraction=male\nattraction_flex=strict\nstyle=casual\nrole=auto\n");
        if (rel_chars.size() != 2 || rel_chars[0].attraction != Attraction::female ||
            rel_chars[0].attraction_flex != AttractionFlex::rare_exception ||
            rel_chars[0].style != RelationStyle::traditional || rel_chars[0].default_role != RelationRole::coworker ||
            rel_chars[1].attraction_flex != AttractionFlex::strict || rel_chars[1].style != RelationStyle::casual) return 1300;

        affinity::State rel_state;
        rel_state.points["hana"] = affinity::threshold(60);
        rel_state.player_gender = Gender::male;
        rel_state.relation("hana").role = RelationRole::coworker;
        // At the top on coworker track, [granica] still books a yellow card (unlike default romantic >= 30).
        if (rel_state.book("hana", 20261019).card != affinity::Card::yellow) return 1301;
        // Enter dilemma -> tension rises; then breakthrough -> coworker becomes secret_romance!
        if (!rel_state.applyStance("hana", RelationStance::dilemma, RelationRole::coworker) ||
            rel_state.relationOf("hana").stance != RelationStance::dilemma ||
            rel_state.relationOf("hana").tension < 55) return 1302;
        if (!rel_state.applyStance("hana", RelationStance::broken, RelationRole::coworker) ||
            rel_state.relationOf("hana").role != RelationRole::secret_romance ||
            rel_state.relationOf("hana").stance != RelationStance::broken) return 1303;
        // Companion catches distance on her own -> effectiveStage steps down by one stage.
        rel_state.applyStance("hana", RelationStance::distanced, RelationRole::coworker);
        if (affinity::effectiveStage(55, rel_state.relationOf("hana"), RelationStyle::balanced) != affinity::Stage::crush) return 1304;
        // Round-trip serialization of _gender and _rel in companion_affinity.txt.
        rel_state.relation("hana").player_cap = 9;
        const auto rel_roundtrip = affinity::parse(affinity::serialize(rel_state));
        if (rel_roundtrip.player_gender != Gender::male ||
            rel_roundtrip.relationOf("hana") != rel_state.relationOf("hana") ||
            affinity::effectiveStage(80, rel_roundtrip.relationOf("hana"), RelationStyle::balanced) != affinity::Stage::strangers) return 1305;
        rel_state.applyStance("hana", RelationStance::open, RelationRole::coworker);
        if (affinity::effectiveStage(80, rel_state.relationOf("hana"), RelationStyle::balanced) != affinity::Stage::light_flirt) return 1305;
        // His distance cap: where the stage is now, between a light flirt's top and a flirt's.
        if (affinity::distanceCap(2) != 9 || affinity::distanceCap(14) != 14 || affinity::distanceCap(45) != 19) return 1380;
        // Jealousy by stage and stance: none with a near stranger, a joke while flirting, an open one never minds at the top,
        // a monogamous one is really jealous in a crush but less so near the top.
        if (affinity::jealousy(affinity::Stage::strangers, Exclusivity::monogamous, 3) != affinity::Jealousy::none ||
            affinity::jealousy(affinity::Stage::light_flirt, Exclusivity::monogamous, 7) != affinity::Jealousy::joke ||
            affinity::jealousy(affinity::Stage::crush, Exclusivity::monogamous, 32) != affinity::Jealousy::real ||
            affinity::jealousy(affinity::Stage::crush, Exclusivity::monogamous, 45) != affinity::Jealousy::light ||
            affinity::jealousy(affinity::Stage::long_distance, Exclusivity::open, 50) != affinity::Jealousy::none ||
            affinity::jealousy(affinity::Stage::crush, Exclusivity::open, 35) != affinity::Jealousy::joke) return 1381;
        // Slow-burn later, casual sooner; the long-distance relationship only at the top.
        if (affinity::effectiveStage(5, {}, RelationStyle::traditional) != affinity::Stage::strangers ||
            affinity::effectiveStage(4, {}, RelationStyle::casual) != affinity::Stage::light_flirt ||
            affinity::effectiveStage(49, {}, RelationStyle::casual) != affinity::Stage::crush ||
            affinity::effectiveStage(50, {}, RelationStyle::traditional) != affinity::Stage::long_distance) return 1382;
        // Tender feelings capped by the stage.
        if (affinity::feelingCap(affinity::Stage::strangers) > 0.35f || affinity::feelingCap(affinity::Stage::crush) < 1.0f) return 1383;

        // Reply markers: [dylemat], [dystans], [przełamanie], [otwarcie], [relacja:praca], [relacja:dystans].
        const auto r_dilemma = parseText(L"[dylemat][shy]Szefie... nie powinniśmy tak rozmawiać po godzinach.");
        const auto r_break = parseText(L"[przełamanie][love]Chrzanić regulamin biura.");
        const auto r_work = parseText(L"[relacja:praca][cool]Jasne, trzymajmy się spraw służbowych.");
        const auto r_dist = parseText(L"[relacja:dystans][sad]Rozumiem, nie będę się narzucać.");
        if (r_dilemma.stance_change != RelationStance::dilemma || r_dilemma.text.find(L"dylemat") != npos ||
            r_break.stance_change != RelationStance::broken || r_break.text.find(L"przełamanie") != npos ||
            r_work.role_change != RelationRole::coworker || r_work.text.find(L"relacja") != npos ||
            r_dist.player_distance_change != true) return 1306;

        // Prompt generation for coworker dilemma, player distance cap, and secret romance.
        PromptExtras rel_extras;
        rel_extras.bond.level = 60;
        rel_extras.bond.role = RelationRole::coworker;
        rel_extras.bond.stance = RelationStance::dilemma;
        rel_extras.bond.player_cap = 24;
        const auto prompt_dilemma = systemPrompt(Persona::kuudere, hana_look, {}, false, 0, 1, 14, rel_extras);
        if (prompt_dilemma.find(L"DYLEMAT") == npos || prompt_dilemma.find(L"trzyma dystans") == npos ||
            prompt_dilemma.find(L"[przelamanie]") == npos) return 1307;
        rel_extras.bond.role = RelationRole::secret_romance;
        rel_extras.bond.stance = RelationStance::broken;
        rel_extras.bond.player_cap = 0;
        rel_extras.bond.health = LifeHealth::sick;
        rel_extras.bond.work = LifeWork::between_jobs;
        rel_extras.bond.wealth = LifeWealth::tight;
        rel_extras.bond.family = LifeFamily::homesick;
        rel_extras.bond.social = LifeSocial::lonely;
        const auto prompt_secret = systemPrompt(Persona::kuudere, hana_look, {}, false, 0, 1, 14, rel_extras);
        if (prompt_secret.find(L"zakazany/ukryty romans") == npos ||
            prompt_secret.find(L"przeziębiona") == npos ||
            prompt_secret.find(L"między zleceniami") == npos ||
            prompt_secret.find(L"krucho u ciebie z kasą") == npos ||
            prompt_secret.find(L"tęsknota za domem") == npos ||
            prompt_secret.find(L"samotna w mieście") == npos) return 1308;

        // Life-state markers ([zycie:...]) and persistence in companion_affinity.txt.
        const auto r_life = parseText(L"[zycie:chora][zycie:krucho][zycie:samotna][sad]Siedzę pod kocem z herbatą i liczę drobne do końca miesiąca.");
        if (r_life.health_change != LifeHealth::sick || r_life.wealth_change != LifeWealth::tight ||
            r_life.social_change != LifeSocial::lonely || r_life.text.find(L"zycie") != npos) return 1309;
        rel_state.relation("hana").health = LifeHealth::sick;
        rel_state.relation("hana").wealth = LifeWealth::tight;
        rel_state.relation("hana").family = LifeFamily::homesick;
        const auto rel_life_rt = affinity::parse(affinity::serialize(rel_state));
        if (rel_life_rt.relationOf("hana").health != LifeHealth::sick ||
            rel_life_rt.relationOf("hana").wealth != LifeWealth::tight ||
            rel_life_rt.relationOf("hana").family != LifeFamily::homesick) return 1310;
    }
    return 0;
}

// Her mind and her life outside the chat (companion_mind.hpp): her life's state under her mood,
// a turn in the relationship, episodes planned and filled in (shared with another companion,
// private ones, dates by her track), what she told, the gaps worth filling, her diary's plans,
// gossip about another's posture and her own browsing.
int mindAndLife() {
    using namespace companion;
    constexpr auto npos = std::wstring::npos;
    const uint64_t now = 1'760'000'000; // 08:53 UTC.
    // Her texts speak of him in both genders (‹on|ona›); here as a man.
    const auto has = [](const std::wstring& text, const wchar_t* part) { return personalize(text, Gender::male).find(part) != std::wstring::npos; };

    // Her life under her mood: ill lowers where it rests and caps her energy.
    mind::Context ctx;
    ctx.look = "hana";
    ctx.persona = Persona::himedere;
    ctx.now = now;
    ctx.role = RelationRole::friend_role;
    {
        auto ill = ctx;
        ill.life.health = LifeHealth::sick;
        mind::Self body;
        body.drives.energy = 1;
        if (!(mind::baseline(ill).valence < mind::baseline(Persona::himedere).valence) || mind::energyNow(body, ill) > 0.41f ||
            mind::energyNow(body, ctx) < 0.99f) return 1320;
        // A breakthrough in the relationship: a jolt up and a milestone of her life.
        mind::Self turned;
        mind::Shift shift;
        shift.stance = RelationStance::broken;
        const float before = mind::settled(turned.affect, ctx).valence;
        mind::onShift(turned, ctx, shift);
        if (turned.milestones.size() != 1 || !(turned.affect.valence > before + 0.2f)) return 1321;
    }
    // Her clock and chronotype: a night owl is asleep at eight in her morning; a lark late at night.
    {
        const auto chars = parseCharacters("[owl]\nname=Owl\ntimezone=+09:00\ndst=none\nchronotype=night\n[lark]\nname=Lark\ntimezone=+00:00\ndst=none\nchronotype=day\n");
        if (chars.size() != 2) return 1322;
        auto at = ctx;
        at.character = &chars[0];
        if (!mind::awake(at)) return 1322; // 17:53 in her city.
        at.now = 1'759'964'400;            // 08:00 in hers, 23:00 UTC.
        if (mind::awake(at) && !mind::nightAt(at)->sleepless) return 1322;
        at.character = &chars[1];
        at.now = 1'759'968'000;            // Midnight UTC, the lark's too.
        if (mind::awake(at) && !mind::nightAt(at)->sleepless) return 1322;
        at.now = now;
        if (!mind::awake(at)) return 1322;
        // Her nights: in bed 23:00–7:00 (the owl 2:00–10:00 hers), each ±30 min and the same all
        // night; asleep in it unless sleepless (1.5% of nights); after her morning, the next one.
        const uint64_t lark_noon = 1'759'924'800, owl_noon = lark_noon - 9 * 3600; // Noon in each city.
        int sleepless = 0;
        std::set<uint64_t> bedtimes;
        std::optional<mind::Night> restless, settled;
        for (uint64_t d = 0; d < 1000; ++d)
            for (int c = 0; c < 2; ++c) {
                const uint64_t noon = (c == 0 ? owl_noon : lark_noon) + d * 86400;
                const uint64_t bed = noon + (c == 0 ? 14 : 11) * 3600, up = noon + (c == 0 ? 22 : 19) * 3600;
                at.character = &chars[c];
                at.now = noon + 3600;
                const auto night = mind::nightAt(at);
                if (!night || night->from + 1800 < bed || night->from > bed + 1800 || night->to + 1800 < up || night->to > up + 1800 || mind::asleep(at)) return 1361;
                auto in_bed = at;
                in_bed.now = night->from + 3600;
                const auto same = mind::nightAt(in_bed);
                if (!same || same->from != night->from || same->to != night->to || same->sleepless != night->sleepless || mind::asleep(in_bed) == night->sleepless) return 1362;
                in_bed.now = night->to + 60;
                if (mind::asleep(in_bed) || mind::nightAt(in_bed)->from <= night->to) return 1362;
                sleepless += night->sleepless ? 1 : 0;
                bedtimes.insert(night->from - noon);
                if (c == 1 && night->sleepless && !restless) restless = night;
                if (c == 1 && !night->sleepless && !settled) settled = night;
            }
        if (sleepless < 10 || sleepless > 60 || bedtimes.size() < 30 || !restless || !settled) return 1363;
        // Her prompt: bedtime soon; a sleepless night.
        const Memory nobody;
        const mind::Self rested{};
        const auto said = [&](uint64_t when, const wchar_t* part) {
            auto then = at;
            then.character = &chars[1];
            then.now = when;
            const auto cands = mind::candidates(rested, then, nobody, {});
            return std::any_of(cands.begin(), cands.end(), [&](const mind::Candidate& c) { return c.source == "sleep" && has(c.text, part); });
        };
        if (!said(restless->from + 3600, L"nieprzespana noc") || said(restless->from - 1800, L"pora snu")) return 1364;
        if (!said(settled->from - 1800, L"pora snu") || said(settled->from - 3 * 3600, L"pora snu") || said(settled->from + 3600, L"nieprzespana")) return 1364;
    }

    // Her plans as her diary writes them.
    const auto lines = mind::parseEpisodes(L"<plany>\n1) jutro 18:30 | z bratem | targ Marché des Enfants Rouges | zakupy na kolację\n"
                                           L"- dziś wieczorem | sama | w domu | pranie i serial | prywatne\n"
                                           L"- jutro 20:00 | z Rin | online | gramy w Overcooked\n"
                                           L"- jutro 21:00 | z Tomkiem | bar | randka | randka\n</plany>");
    if (lines.size() != 4 || lines[0].day != 1 || lines[0].minute != 18 * 60 + 30 || lines[0].with != L"z bratem" || lines[0].hidden ||
        lines[1].day != 0 || lines[1].minute != 19 * 60 || !lines[1].hidden || lines[1].date || !lines[3].date || !lines[3].hidden) return 1323;
    const auto companion_of = [](std::wstring_view with) { return with.find(L"Rin") != std::wstring_view::npos ? std::string("rin") : std::string{}; };
    {
        // With the player (romance): no date with anyone else.
        mind::Self faithful;
        auto together = ctx;
        together.role = RelationRole::romantic;
        if (mind::addEpisodes(faithful, together, lines, true, 0, companion_of).size() != 3) return 1324;
    }
    mind::Self self;
    const auto added = mind::addEpisodes(self, ctx, lines, true, 0, companion_of);
    if (added.size() != 4 || self.episodes.size() != 4 || added[2].partner != "rin" || added[2].where != L"online" ||
        added[0].state != mind::Episode::State::planned || added[0].at != 1'760'121'000) return 1325;
    // Only one date in two weeks; the same plan again is not added twice.
    if (!mind::addEpisodes(self, ctx, lines, true, 0, companion_of).empty()) return 1326;
    {
        // Her diary restating a plan in other words, or an hour off: still the one plan. Half an
        // hour later something else is a plan of its own.
        mind::Self restated;
        const auto first = mind::parseEpisodes(L"<plany>\ndziś 22:45 | sama | w domu | Zaparzę melisę i sprawdzę notatki na jutro\n</plany>");
        const auto again = mind::parseEpisodes(L"<plany>\ndziś 22:45 | sama | w domu | Zaparzę melisę i przejrzę notatki przed snem\n"
                                               L"dziś 23:30 | sama | w domu | Melisa i notatki na jutro\n"
                                               L"jutro 08:15 | sama | przystanek Plac Wolnica | tramwaj na zajęcia\n"
                                               L"jutro 08:45 | ze znajomymi z roku | uczelnia | wykład z ekonomii\n</plany>");
        if (mind::addEpisodes(restated, ctx, first, true).size() != 1 || mind::addEpisodes(restated, ctx, again, true).size() != 2 ||
            restated.episodes.size() != 3) return 1358;
        // An older save with one plan written three times: one stays (the one she told him about).
        mind::State older;
        auto& written = older.selves["hana"];
        for (int i = 0; i < 3; ++i) {
            mind::Episode e;
            e.id = "e00000" + std::to_string(i);
            e.at = now + 14 * 3600;
            e.what = i == 0 ? L"Zaparzę melisę i sprawdzę notatki" : i == 1 ? L"Zaparzę melisę i przejrzę notatki" : L"Melisa, notatki na jutro";
            e.told = i == 1;
            e.made = now;
            written.episodes.push_back(std::move(e));
        }
        const auto folded = mind::State::load(older.save());
        if (!folded || folded->selves.at("hana").episodes.size() != 1 || folded->selves.at("hana").episodes[0].id != "e000001") return 1359;
        // Her day planned (two or more ahead): her diary is not asked for plans; one: not to repeat it.
        mind::Self one_plan;
        mind::addEpisodes(one_plan, ctx, first, true);
        const auto fresh = mind::planRules(mind::Self{}, ctx, {});
        if (fresh.empty() || has(fresh, L"nie powtarzaj") || !has(mind::planRules(one_plan, ctx, {}), L"nie powtarzaj") ||
            !mind::planRules(restated, ctx, {}).empty()) return 1360;
    }
    // Shared with Rin: hers too, with the same id (and once).
    mind::Self rin;
    if (!mind::shareEpisode(rin, added[2], "hana", L"Hana") || mind::shareEpisode(rin, added[2], "hana", L"Hana") ||
        rin.episodes.size() != 1 || rin.episodes[0].id != added[2].id || rin.episodes[0].partner != "hana" || rin.episodes[0].with != L"Hana") return 1327;

    // Their time passes: they happened (or fell through) — the same way in both minds.
    auto later = ctx;
    later.now = now + 2 * 86400;
    mind::tick(self, later);
    mind::tick(rin, later);
    if (std::any_of(self.episodes.begin(), self.episodes.end(), [](const mind::Episode& e) { return e.state == mind::Episode::State::planned; })) return 1328;
    const auto shared = std::find_if(self.episodes.begin(), self.episodes.end(), [&](const mind::Episode& e) { return e.id == added[2].id; });
    if (shared == self.episodes.end() || shared->state != rin.episodes[0].state) return 1328;
    // Asked what she did: what happened leads her workspace; a private one keeps its #id and rule.
    const Memory none;
    const auto cands = mind::candidates(self, later, none, L"Co robiłaś wczoraj?");
    const auto lived = std::find_if(cands.begin(), cands.end(), [](const mind::Candidate& c) { return c.source == "episode" && c.status == mind::Status::remember; });
    if (lived == cands.end() || lived->salience < 0.79f || !has(lived->text, L"Enfants Rouges") || !has(lived->text, L"[prywatne #e") ||
        !has(lived->text, L"sama nie zaczynaj")) return 1329;
    // She tells about the date (her appraisal) and the laundry (her words): told, no longer closed.
    const auto told = mind::parseAppraisal(L"{\"mowie\":[\"#" + text_encoding::wideLenient(added[3].id) + L"\"]}");
    Memory memory;
    mind::apply(self, memory, told, later, "");
    if (mind::noticeTold(self, L"Wieczorem robiłam pranie i oglądałam serial w domu, sama.") != 1) return 1330;
    if (std::any_of(self.episodes.begin(), self.episodes.end(), [](const mind::Episode& e) { return e.hidden && !e.told; })) return 1330;
    // The Mind view's Odkryj: a private one opened (she does not know).
    if (!mind::peekEpisode(self, added[1].id) || mind::peekEpisode(self, "e000000")) return 1331;

    // In her words, by her clock.
    mind::Episode yesterday;
    yesterday.at = 1'759'950'000; // Yesterday 19:00 UTC.
    if (mind::episodeWhen(yesterday, ctx) != L"wczoraj wieczorem (ok. 19:00)") return 1332;
    // A long gap is worth filling in once.
    mind::Self quiet;
    quiet.last_talk = now - 20 * 3600;
    const auto gap = mind::lifeGap(quiet, ctx);
    if (!gap || gap->from != now - 20 * 3600 || gap->to != now) return 1333;
    quiet.lived_until = now;
    if (mind::lifeGap(quiet, ctx)) return 1333;
    const auto request = mind::lifeRequest(self, ctx, *gap, L"Rin (Tokio)");
    const auto rules = mind::episodeRules(ctx, L"Rin (Tokio)", true);
    auto with_player = ctx;
    with_player.role = RelationRole::secret_romance;
    if (!has(request, L"<epizody>") || !has(request, L"Rin (Tokio)") || !has(rules, L"randka (zawsze prywatne)") ||
        !has(mind::episodeRules(with_player, {}, true), L"żadnych randek")) return 1334;
    // A shared diary entry stays across a restart.
    mind::State state;
    state.selves["hana"] = self;
    const auto reloaded = mind::State::load(state.save());
    if (!reloaded || reloaded->selves.at("hana").episodes.size() != self.episodes.size()) return 1335;
    for (size_t i = 0; i < self.episodes.size(); ++i) {
        const auto& a = self.episodes[i];
        const auto& b = reloaded->selves.at("hana").episodes[i];
        if (a.id != b.id || a.at != b.at || a.what != b.what || a.partner != b.partner || a.state != b.state || a.hidden != b.hidden || a.date != b.date ||
            a.told != b.told || a.peeked != b.peeked) return 1335;
    }

    // Her diary asks for plans and keeps their lines.
    const auto sleep = cognition::sleepConsolidationPrompt(false, {}, {}, {}, false, L"REGUŁY PLANÓW");
    const auto cons = cognition::parseConsolidation(L"<podsumowanie>Gadaliśmy o lisach.</podsumowanie><plany>\njutro 10:00 | sama | park | bieganie\n</plany>");
    if (!has(sleep, L"<plany>") || !has(sleep, L"REGUŁY PLANÓW") || cons.summary != L"Gadaliśmy o lisach." || mind::parseEpisodes(cons.plans).size() != 1) return 1336;
    // Her diary knows the real-world rule and her life now.
    PromptExtras extras;
    extras.bond.health = LifeHealth::sick;
    const auto diary = diaryPrompt(Persona::himedere, companion_fixture::hana, false, &extras);
    if (!has(diary, L"Prawdziwy świat") || !has(diary, L"przeziębiona") || has(diaryPrompt(Persona::himedere, companion_fixture::hana, false), L"Prawdziwy świat")) return 1337;
    // Gossip about another's posture towards the player.
    cognition::State cog;
    cog.postGossip("rin", "stance", L"broken", now);
    const auto heard = cog.candidates(none, "hana", {}, now, false, [](std::string_view k) { return k == "rin" ? std::wstring(L"Rin") : std::wstring(L"Hana"); });
    if (std::none_of(heard.begin(), heard.end(), [&](const mind::Candidate& c) { return has(personalize(c.text, Gender::male), L"Rin a nim coś zaiskrzyło"); })) return 1338;
    // Her own browsing: her goal is among what she looks up.
    const auto queries = cognition::dailyReadingQueries(companion_fixture::hana, none, {}, false, 0, L"Kupić używany motocykl Kawasaki");
    if (std::none_of(queries.begin(), queries.end(), [&](const std::wstring& q) { return q.find(L"otocyk") != npos; })) return 1339;

    // 1340: First meeting / acquaintance (lvl 1 / 0 points, empty memory) + referral hook.
    {
        mind::Self fresh_self;
        Memory empty_mem;
        if (!mind::isFirstMeeting(fresh_self, empty_mem, "hana", 0)) return 1340;
        if (mind::isFirstMeeting(fresh_self, empty_mem, "hana", 10)) return 1340;
        PromptExtras fm_extras;
        fm_extras.bond.first_meet = true;
        fm_extras.bond.known_companion = L"Saki";
        const auto fm_prompt = systemPrompt(Persona::himedere, companion_fixture::hana, {}, false, now, 1, 14, fm_extras);
        const auto fm_kick = kickoff(Kickoff::chat, false, 14, true, L"Saki");
        if (!has(fm_prompt, L"PIERWSZE ZAPOZNANIE") || !has(fm_kick, L"jeszcze się NIE znacie") || !has(fm_kick, L"Saki")) return 1340;
    }

    // 1341: Amygdala vs. vmPFC cognitive control ("drop the act" when exhausted & high arousal) + code-switching.
    {
        mind::Self tired_self;
        tired_self.drives.energy = 0.25f;
        tired_self.affect = {-0.2f, 0.85f, now};
        if (mind::cognitiveControl(tired_self, ctx) >= 0.42f) return 1341;
        const auto ctrl_cands = mind::candidates(tired_self, ctx, none, L"Hej");
        if (std::none_of(ctrl_cands.begin(), ctrl_cands.end(), [&](const mind::Candidate& c) { return c.source == "control" && has(c.text, L"kontrola poznawcza"); })) return 1341;
        PromptExtras cs_extras;
        cs_extras.cognitive_control = 0.25f;
        const auto cs_prompt = systemPrompt(Persona::himedere, companion_fixture::hana, {}, false, now, 1, 2, cs_extras);
        if (!has(cs_prompt, L"niska kontrola poznawcza")) return 1341;
    }

    // 1342: Message length alone must not reinforce conversational policy; semantic feedback does.
    {
        mind::Self bg;
        mind::onReply(bg, ctx, false, L"A ty co o tym naprawdę myślisz?", false);
        if (bg.policy.last_style != "deep") return 1342;
        const float before_deep = bg.policy.deep_questions;
        const std::wstring enthusiastic = L"Wiesz co, moim zdaniem ta fabuła w drugim akcie zupełnie zmienia sens całej historii! A tobie która scena najbardziej zapadła w pamięć?";
        const auto q = mind::quick(enthusiastic, bg);
        mind::onPlayerMessage(bg, ctx, q, enthusiastic);
        if (bg.policy.deep_questions != before_deep) return 1342;
    }

    // 1343: Default Mode Network (DMN) offline rumination during high tension or broken expectation.
    {
        mind::Self dmn;
        dmn.last_talk = now - 6 * 3600;
        auto tense_ctx = ctx;
        tense_ctx.stance = RelationStance::dilemma;
        tense_ctx.tension = 65;
        mind::tick(dmn, tense_ctx);
        if (dmn.ruminations.empty() || !has(dmn.ruminations.back().text, L"mieliłaś w głowie")) return 1343;
    }

    // 1344: CLS Neocortical causal schemas (<schemat>) & consolidation.
    {
        const auto cons_sc = cognition::parseConsolidation(
            L"<podsumowanie>Skakał po grach.</podsumowanie><schemat>Gdy gracz odpala kilka gier na 5 minut → jest przemęczony i szuka łatwej dopaminy.</schemat>"
            L"<nasz_zart>Hrabia Nietoperz jako nasz główny doradca taktyczny.</nasz_zart>");
        if (!has(cons_sc.schema, L"szuka łatwej dopaminy") || !has(cons_sc.inside_joke, L"Hrabia Nietoperz")) return 1344;
        cognition::State sc_cog;
        sc_cog.addSchema(cons_sc.schema, now, "hana");
        const auto sc_cands = sc_cog.candidates(none, "hana", L"odpala gry", now, false);
        if (std::none_of(sc_cands.begin(), sc_cands.end(), [&](const mind::Candidate& c) { return c.source == "schema" && has(c.text, L"schemat przyczynowy"); })) return 1344;
    }

    // 1345: Memory reconsolidation (affective tinting on recall) & topic rethinking / re-conceptualization after time.
    {
        Memory recon_mem;
        recon_mem.note(L"Pokłóciliśmy się o poziom trudności w Dark Souls.", now - 10 * 86400, "hana", "angry", L"wkurzył mnie");
        if (recon_mem.reconsolidate("hana", L"pamiętasz Dark Souls", 0.65f, "playful", L"dziś wspominam tę sprzeczkę z uśmiechem", now) != 1) return 1345;
        if (recon_mem.notes[0].emotion != "playful" || !has(recon_mem.notes[0].feeling, L"z uśmiechem")) return 1345;
    }

    // 1346: Learned concepts ("nauczyl") + rethinking ("przemyslenie") + inside jokes ("nasz_zart") + deliberation ("waha_sie").
    {
        mind::Self learner;
        Memory mem;
        const auto app = mind::parseAppraisal(
            L"{\"nauczyl\":[{\"pojecie\":\"frame trap\",\"znaczenie\":\"pułapka z przewagi klatek w bijatyce\"}],"
            L"\"przemyslenie\":{\"temat\":\"Souls-like\",\"nowe_spojrzenie\":\"jednak dają satysfakcję, gdy ktoś cierpliwie tłumaczy\"},"
            L"\"nasz_zart\":\"Taktyczny Nietoperz\","
            L"\"waha_sie\":[\"Prychnąć na niego z góry\",\"Podziękować mu po cichu za cierpliwość\"]}");
        if (app.taught.size() != 1 || !app.rethink || app.inside_joke != L"Taktyczny Nietoperz" || app.hesitation.size() != 2) return 1346;
        mind::apply(learner, mem, app, ctx, "");
        if (learner.learned_concepts.size() != 1 || learner.rethought.size() != 1 || learner.shared_idioms.size() != 1 ||
            learner.counterfactual != L"Prychnąć na niego z góry") return 1346;
        const float m0 = learner.learned_concepts[0].mastery;
        mind::onReply(learner, ctx, true, L"Zrobiłam dziś idealny frame trap!", false);
        if (!(learner.learned_concepts[0].mastery > m0)) return 1346;
    }

    // 1348: Multi-dimensional relationship axes (trust, respect, security) + anti-sycophancy.
    {
        affinity::State ax;
        ax.adjustAxes("hana", -15, 12, -8);
        const auto ax_rt = affinity::parse(affinity::serialize(ax));
        const auto rel = ax_rt.relationOf("hana");
        if (rel.trust != 35 || rel.respect != 72 || rel.security != 42) return 1348;
        mind::Self s_ax;
        if (mind::quick(L"Masz we wszystkim rację, zgadzam się ze wszystkim.", s_ax).respect_delta >= 0) return 1348;
        if (mind::quick(L"Nie zgadzam się, moim zdaniem ta gra jest świetna.", s_ax).respect_delta <= 0) return 1348;
    }

    // 1349: Peeked private episode leak detection in candidates() + trust drop.
    {
        mind::Self peek_self;
        const auto ep = mind::parseEpisodes(L"<epizody>\nwczoraj 19:00 | z bratem | klinika | badania krwi | prywatne\n</epizody>");
        const auto ep_added = mind::addEpisodes(peek_self, ctx, ep, false, now - 2 * 86400);
        if (ep_added.size() != 1 || !mind::peekEpisode(peek_self, ep_added[0].id)) return 1349;
        const auto leak_cands = mind::candidates(peek_self, ctx, none, L"Jak wyszły twoje badania krwi w klinice?");
        if (std::none_of(leak_cands.begin(), leak_cands.end(), [&](const mind::Candidate& c) { return c.source == "leak" && c.salience >= 0.95f; })) return 1349;
        if (!mind::noticePeekedLeak(peek_self, L"Słyszałem, że robiłaś badania krwi w klinice", now) || peek_self.trust_delta > -15) return 1349;
    }

    // 1350: Dynamic 7x7 peer_tension matrix between companions.
    {
        cognition::State pt_cog;
        pt_cog.adjustPeerTension("yuki", "saki", 45);
        if (pt_cog.peerTension("saki", "yuki") != 45) return 1350;
        const auto pt_cands = pt_cog.candidates(none, "yuki", {}, now, false, [](std::string_view k) { return k == "saki" ? std::wstring(L"Saki") : std::wstring(L"Yuki"); });
        if (std::none_of(pt_cands.begin(), pt_cands.end(), [&](const mind::Candidate& c) { return c.source == "peer" && has(c.text, L"na pieńku z Saki"); })) return 1350;
    }

    // 1351: Micro-chronemics (dismissive 1-word reply after vulnerability vs. long confession) & deliberation mode.
    {
        mind::Self vuln;
        mind::onReply(vuln, ctx, false, L"Wiesz, czasem czuję się tu strasznie samotna...", true);
        if (!vuln.last_reply_vulnerable) return 1351;
        const auto q_dism = mind::quick(L"ok", vuln);
        if (!q_dism.dismissive || q_dism.flash != Emotion::sad || !(q_dism.valence < -0.15f)) return 1351;
        mind::onPlayerMessage(vuln, ctx, q_dism, L"ok");
        if (!mind::needsDeliberation(vuln, ctx)) return 1351;
        PromptExtras delib_extras;
        delib_extras.inner = true;
        delib_extras.deliberate = true;
        const auto delib_prompt = systemPrompt(Persona::himedere, companion_fixture::hana, {}, false, now, 1, 20, delib_extras);
        if (!has(delib_prompt, L"waha_sie")) return 1351;
    }

    // 1352: Full persistence round-trip for all new mind & cognition fields.
    {
        mind::State m_state;
        auto& s = m_state.of("hana");
        s.policy.deep_questions = 0.82f;
        s.policy.last_style = "deep";
        mind::learnConcept(s, L"wave dash", L"ślizg w bijatyce", now);
        mind::addSharedIdiom(s, L"Hrabia Nietoperz", now);
        s.ruminations.push_back({now, L"Czemu tak nagle zniknął?"});
        s.rethought.push_back({now, L"Soulsy: jednak dają frajdę"});
        s.counterfactual = L"Rzucić słuchawką";
        s.counterfactual_at = now;
        s.last_reply_vulnerable = true;
        const auto m_loaded = mind::State::load(m_state.save());
        if (!m_loaded) return 1352;
        const auto& ls = m_loaded->selves.at("hana");
        if (std::abs(ls.policy.deep_questions - 0.82f) > 0.01f || ls.policy.last_style != "deep" ||
            ls.learned_concepts.size() != 1 || ls.learned_concepts[0].term != L"wave dash" ||
            ls.shared_idioms.size() != 1 || ls.ruminations.size() != 1 || ls.rethought.size() != 1 ||
            ls.counterfactual != L"Rzucić słuchawką" || !ls.last_reply_vulnerable) return 1352;

        cognition::State c_state;
        c_state.addSchema(L"Gdy gra w nocy → ma stres w pracy.", now, "hana");
        c_state.adjustPeerTension("hana", "rin", 50);
        const auto c_loaded = cognition::State::load(c_state.save());
        if (!c_loaded || c_loaded->schemas.size() != 1 || c_loaded->peerTension("rin", "hana") != 50) return 1352;
    }

    // 1353: Conscious fibs / bluffs / excuses (Untruth), DMN guilt rumination, confession on trust growth & Odkryj leak.
    {
        mind::Self fibber;
        Memory mem;
        auto fib_ctx = ctx;
        fib_ctx.trust = 45;
        const auto app = mind::parseAppraisal(
            L"{\"o_sobie\":[\"Wcale nie zaspałam, od rana pracowałam nad projektem\"],"
            L"\"klamstwo\":{\"powiedziala\":\"Wcale nie zaspałam, od rana pracowałam nad projektem\","
            L"\"prawda\":\"Zaspałam do południa i było mi strasznie głupio\",\"powod\":\"wstyd\",\"odczucie\":\"wina\"}}");
        if (!app.untruth || app.untruth->reason != "shame" || app.untruth->feeling != "guilt") return 1353;
        mind::apply(fibber, mem, app, fib_ctx, "");
        // The fib must NOT be stored as a permanent true claim in Self::claims!
        if (!fibber.claims.empty() || fibber.untruths.size() != 1 || fibber.untruths[0].confessed) return 1353;
        const std::string uid = fibber.untruths[0].id;
        // Not ready to confess immediately at same trust and normal control.
        auto soon_ctx = fib_ctx;
        soon_ctx.now = now + 600;
        if (mind::readyToConfess(fibber.untruths[0], fibber, soon_ctx)) return 1353;
        const auto cover_cands = mind::candidates(fibber, soon_ctx, none, {});
        if (std::none_of(cover_cands.begin(), cover_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "untruth" && has(c.text, L"niewyznane kłamstewko");
            })) return 1353;
        // DMN ruminates on the guilty fib while apart.
        fibber.last_talk = now;
        auto apart_ctx = fib_ctx;
        apart_ctx.now = now + 5 * 3600;
        mind::tick(fibber, apart_ctx);
        if (fibber.ruminations.empty() || !has(fibber.ruminations.back().text, L"kłamstwo")) return 1353;
        // Once trust grows (+10), she is ready to confess and candidates() prompts her to own up.
        auto trusted_ctx = fib_ctx;
        trusted_ctx.now = now + 3600;
        trusted_ctx.trust = 56;
        if (!mind::readyToConfess(fibber.untruths[0], fibber, trusted_ctx)) return 1353;
        const auto conf_cands = mind::candidates(fibber, trusted_ctx, none, {});
        if (std::none_of(conf_cands.begin(), conf_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "untruth" && c.salience >= 0.8f && has(c.text, L"powiedzieć mu prawdę");
            })) return 1353;
        // Natural confession in reply text marks it confessed, adds the real truth to claims and boosts trust.
        if (mind::noticeConfessed(fibber, L"Wiesz co, głupio mi, tak naprawdę zaspałam do południa...", trusted_ctx.now) != 1) return 1353;
        if (!fibber.untruths[0].confessed || fibber.claims.empty() || !has(fibber.claims.back().text, L"Zaspałam do południa") || fibber.trust_delta <= 0)
            return 1353;
        // Odkryj leak on an unconfessed fib.
        mind::Self peek_fib;
        mind::apply(peek_fib, mem, app, fib_ctx, "");
        if (!mind::peekEpisode(peek_fib, peek_fib.untruths[0].id) || !peek_fib.untruths[0].peeked) return 1353;
        if (!mind::noticePeekedLeak(peek_fib, L"Wiem, że zaspałaś do południa i było ci głupio!", now + 120) || !peek_fib.peek_caught) return 1353;
        // Caught is not confessed: the truth is out, but she did not own up herself (the Mind view tells them apart).
        if (!peek_fib.untruths[0].confessed || !peek_fib.untruths[0].caught || fibber.untruths[0].caught) return 1353;
        mind::State caught_state;
        caught_state.of("hana") = peek_fib;
        if (const auto caught_loaded = mind::State::load(caught_state.save()); !caught_loaded || !caught_loaded->selves.at("hana").untruths[0].caught) return 1353;
        (void)uid;
    }

    // 1354: Affective realism friction (snapping when burnt out / low valence) & later apology after cooling down.
    {
        mind::Self snappy;
        auto bad_ctx = ctx;
        bad_ctx.life.work = LifeWork::burnout;
        snappy.affect = {-0.35f, 0.6f, now};
        mind::onReply(snappy, bad_ctx, false, L"Daj mi spokój, nie mam dziś głowy do gadania.", false);
        if (snappy.harsh_at != now || !has(snappy.harsh_reason, L"wypalenie")) return 1354;
        auto cooled_ctx = ctx;
        cooled_ctx.now = now + 2 * 3600;
        snappy.affect = {0.15f, 0.35f, cooled_ctx.now};
        const auto apol_cands = mind::candidates(snappy, cooled_ctx, none, {});
        if (std::none_of(apol_cands.begin(), apol_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "apology" && c.salience >= 0.75f && has(c.text, L"przeproś");
            })) return 1354;
        mind::onReply(snappy, cooled_ctx, false, L"Hej, przepraszam, że wcześniej byłam taka oschła.", true);
        if (snappy.harsh_at != 0 || snappy.trust_delta <= 0) return 1354;
    }

    // 1355: Shower thought (Esprit d'escalier) & Ongoing episode multitasking in candidates().
    {
        mind::Self thinker;
        thinker.counterfactual = L"Trzeba było mu powiedzieć, że to zakończenie wcale nie ma sensu";
        thinker.counterfactual_at = now - 3600;
        const auto ep = mind::parseEpisodes(L"<plany>\ndziś 09:00 | sama | w domu | pieczenie zapiekanki\n</plany>");
        mind::addEpisodes(thinker, ctx, ep, true, 0);
        const auto esc_cands = mind::candidates(thinker, ctx, none, L"Co tam?");
        if (std::none_of(esc_cands.begin(), esc_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "escalier" && has(c.text, L"esprit d'escalier");
            })) return 1355;
        if (std::none_of(esc_cands.begin(), esc_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "multitask" && has(c.text, L"pieczenie zapiekanki");
            })) return 1355;
    }

    // 1356: Co-regulation on player sadness/confession & Fuzzy/hazy memory cue for older notes.
    {
        mind::Self hyper;
        hyper.affect = {0.4f, 0.82f, now};
        auto trusted = ctx;
        trusted.trust = 65;
        const auto q_sad = mind::quick(L"Jestem dziś strasznie zmęczony i smutno mi po całym dniu.", hyper);
        if (!q_sad.sad_or_stressed) return 1356;
        mind::onPlayerMessage(hyper, trusted, q_sad, L"Jestem dziś strasznie zmęczony i smutno mi po całym dniu.");
        if (hyper.affect.arousal > 0.36f || !has(hyper.subtext_note, L"spokojnym oparciem")) return 1356;

        Memory fuzzy_mem;
        fuzzy_mem.note(L"Bolały go plecy od starego fotela biurowego.", now - 9 * 86400, "hana");
        const auto prompt = fuzzy_mem.prompt(false, now, false, true, "hana", L"fotel");
        if (!has(prompt, L"przez mgłę")) return 1356;
    }

    // 1357: Spontaneous initiative (spontaneousPrompt) modes & full persistence of new fields.
    {
        mind::Self init_self;
        const auto p_check = mind::spontaneousPrompt(init_self, ctx, 2);
        if (!has(p_check, L"W czacie zapadła cisza")) return 1357;
        init_self.counterfactual = L"Czemu zawsze wybiera najtrudniejszy poziom?";
        init_self.counterfactual_at = now - 1800;
        const auto p_thought = mind::spontaneousPrompt(init_self, ctx, 1);
        if (!has(p_thought, L"Właśnie sobie siedzę i myślę o")) return 1357;

        mind::State st;
        auto& s = st.of("hana");
        s.untruths.push_back({"u1", now, L"Znam tę mechanikę na pamięć", L"Pierwszy raz o niej słyszę", "pride", "satisfaction", 50, false, true});
        s.harsh_at = now - 600;
        s.harsh_reason = L"zmęczenie";
        s.last_initiative = now - 120;
        const auto loaded = mind::State::load(st.save());
        if (!loaded) return 1357;
        const auto& ls = loaded->selves.at("hana");
        if (ls.untruths.size() != 1 || ls.untruths[0].id != "u1" || ls.untruths[0].reason != "pride" || !ls.untruths[0].peeked ||
            ls.harsh_at != now - 600 || ls.harsh_reason != L"zmęczenie" || ls.last_initiative != now - 120)
            return 1357;
    }

    // 1358: Item 10 — ACC Irony / Sarcasm & Skeptical Implicature detector (McGilchrist & Grice).
    {
        mind::Self iron_self;
        const auto q_sarcasm = mind::quick(L"No super, znowu wywaliło mi grę do pulpitu i straciłem zapis!", iron_self);
        if (!q_sarcasm.irony_or_vent || q_sarcasm.flash != Emotion::thinking || q_sarcasm.valence >= 0.0f) return 1358;
        mind::onPlayerMessage(iron_self, ctx, q_sarcasm, L"No super, znowu wywaliło mi grę do pulpitu i straciłem zapis!");
        if (!has(iron_self.subtext_note, L"ironii/sarkazmu")) return 1358;

        const auto q_skept = mind::quick(L"Ta jasne, na pewno przejdę to bez ani jednej śmierci", iron_self);
        if (!q_skept.skeptical_irony || q_skept.flash != Emotion::smug) return 1358;
    }

    // 1359: Item 4 — Eric Berne's "Why Don't You — Yes, But" game rejection detector.
    {
        mind::Self berne;
        mind::onSuggested(berne, ctx, {{"steam:1", L"Hades"}, {"steam:2", L"Celeste"}});
        const auto q_rej1 = mind::quick(L"Nie mam ochoty, za trudna dla mnie dziś", berne);
        if (!q_rej1.rejected_pick) return 1359;
        mind::onPlayerMessage(berne, ctx, q_rej1, L"Nie mam ochoty, za trudna dla mnie dziś");
        const auto q_rej2 = mind::quick(L"Raczej nie, za długa i nie chce mi się teraz", berne);
        mind::onPlayerMessage(berne, ctx, q_rej2, L"Raczej nie, za długa i nie chce mi się teraz");
        if (berne.rejected_picks_in_row < 2) return 1359;
        const auto berne_cands = mind::candidates(berne, ctx, none, L"A masz coś jeszcze?");
        if (std::none_of(berne_cands.begin(), berne_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "yes_but" && c.salience >= 0.8f && has(c.text, L"Tak, ale");
            })) return 1359;
        // Playing a game or accepting resets the counter.
        mind::onPlayed(berne, ctx, "steam:1", L"Hades", 20);
        if (berne.rejected_picks_in_row != 0) return 1359;
    }

    // 1360: Item 3 — Altman & Taylor's Disclosure Reciprocity (non-equal, persona-dependent asymmetry).
    {
        mind::Self disc;
        disc.untruths.push_back({"u1", now - 3600, L"A", L"B", "shame", "guilt", 50, true, false});
        disc.untruths.push_back({"u2", now - 1800, L"C", L"D", "pride", "relief", 50, true, false});
        auto guarded_ctx = ctx;
        guarded_ctx.persona = Persona::tsundere;
        const auto cands_guarded = mind::candidates(disc, guarded_ctx, none, L"Co tam?");
        if (std::none_of(cands_guarded.begin(), cands_guarded.end(), [&](const mind::Candidate& c) {
                return c.source == "reciprocity" && has(c.text, L"Asymetria zwierzeń");
            })) return 1360;

        // Open/warm persona (genki) does NOT complain at 2 disclosures (tolerates larger asymmetry).
        auto open_ctx = ctx;
        open_ctx.persona = Persona::genki;
        const auto cands_open = mind::candidates(disc, open_ctx, none, L"Co tam?");
        if (std::any_of(cands_open.begin(), cands_open.end(), [](const mind::Candidate& c) { return c.source == "reciprocity"; }))
            return 1360;
    }

    // 1361: Item 5 — Richard Schwartz's Internal Family Systems (IFS: Exile sore spot vs. Firefighter / Self-unblending).
    {
        mind::Self ifs_self;
        auto low_trust = ctx;
        low_trust.persona = Persona::himedere;
        low_trust.trust = 45;
        const auto cands_def = mind::candidates(ifs_self, low_trust, none, L"Zachowujesz się jak mała, dziecinna młodsza siostra");
        if (std::none_of(cands_def.begin(), cands_def.end(), [&](const mind::Candidate& c) {
                return c.source == "ifs" && has(c.text, L"Obrona Strażaka");
            })) return 1361;

        auto high_trust = low_trust;
        high_trust.trust = 75;
        const auto cands_unblend = mind::candidates(ifs_self, high_trust, none, L"Czemu zawsze denerwuje cię temat młodszej siostry?");
        if (std::none_of(cands_unblend.begin(), cands_unblend.end(), [&](const mind::Candidate& c) {
                return c.source == "ifs" && has(c.text, L"Odsłonięcie");
            })) return 1361;
    }

    // 1362: Item 6 & 7 — Panksepp's PLAY circuit (momentum & gating) + Sapolsky's sleep_debt & HPA stress_load.
    {
        mind::Self play_self;
        const auto q1 = mind::quick(L"Hahaha no nie mogę xd", play_self);
        const auto q2 = mind::quick(L"Hehe ale wymyśliłaś :p", play_self);
        mind::onPlayerMessage(play_self, ctx, q1, L"Hahaha no nie mogę xd");
        mind::onPlayerMessage(play_self, ctx, q2, L"Hehe ale wymyśliłaś :p");
        if (play_self.play_momentum < 0.5f) return 1362;
        const auto cands_play = mind::candidates(play_self, ctx, none, L"Co jeszcze?");
        if (std::none_of(cands_play.begin(), cands_play.end(), [&](const mind::Candidate& c) {
                return c.source == "play" && has(c.text, L"Obwód PLAY");
            })) return 1362;
        // Sadness immediately gates PLAY off (Panksepp's rule).
        const auto q_sad = mind::quick(L"Smutno mi bardzo i nie mam siły", play_self);
        mind::onPlayerMessage(play_self, ctx, q_sad, L"Smutno mi bardzo i nie mam siły");
        if (play_self.play_momentum != 0.0f) return 1362;

        // Sapolsky's sleep_debt & stress_load: lower daytime energy and cognitive control.
        play_self.drives.energy = 0.9f;
        const float fresh_energy = mind::energyNow(play_self, ctx);
        play_self.sleep_debt = 0.6f;
        play_self.stress_load = 0.55f;
        const float tired_energy = mind::energyNow(play_self, ctx);
        if (tired_energy >= fresh_energy - 0.2f) return 1362;
        const auto cands_phys = mind::candidates(play_self, ctx, none, L"Hej");
        if (std::none_of(cands_phys.begin(), cands_phys.end(), [&](const mind::Candidate& c) {
                return c.source == "physiology" && has(c.text, L"Dług snu");
            })) return 1362;
    }

    // 1363: Item 8 & 9 — McAdams/Tulving Key Memories (FIFO protection & Mental Time Travel) + Hawkins' Player World Reference Frame.
    {
        Memory mem_fifo;
        // First summary for "hana" is automatically marked important (foundational memory) and survives FIFO eviction!
        mem_fifo.summarize(L"Nasze pierwsze spotkanie i rozmowa o starych RPG-ach", now - 30 * 86400, "hana");
        if (mem_fifo.summaries.empty() || !mem_fifo.summaries.front().important) return 1363;
        for (int i = 0; i < 110; ++i)
            mem_fifo.summarize(L"Zwykła rozmowa numer " + std::to_wstring(i), now - (109 - i) * 3600, "hana");
        if (std::none_of(mem_fifo.summaries.begin(), mem_fifo.summaries.end(), [&](const Memory::Entry& e) {
                return e.important && has(e.text, L"pierwsze spotkanie");
            })) return 1363;

        // Appraisal with "wazne_wspomnienie" and "swiat" (Hawkins' reference frame).
        mind::Self world_self;
        const auto app = mind::parseAppraisal(
            L"{\"wazne_wspomnienie\":\"Wieczór, gdy razem wybraliśmy grę po jego ciężkim dniu\","
            L"\"swiat\":[{\"nazwa\":\"Tomek (brat)\",\"opis\":\"grają razem w co-opy w weekendy\"},{\"nazwa\":\"Pies Burek\",\"opis\":\"budzi go o 6 rano\"}]}");
        if (!app.ok || app.key_memory.empty() || app.player_world.size() != 2) return 1363;
        mind::apply(world_self, mem_fifo, app, ctx, "");
        if (world_self.key_memories.size() != 1 || world_self.player_world.size() != 2) return 1363;

        const auto world_cands = mind::candidates(world_self, ctx, mem_fifo, L"Tomek znowu chce grać wieczorem");
        if (std::none_of(world_cands.begin(), world_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "world" && c.salience >= 0.7f && has(c.text, L"Tomek (brat)");
            })) return 1363;
        if (std::none_of(world_cands.begin(), world_cands.end(), [&](const mind::Candidate& c) {
                return c.source == "time_travel" && has(c.text, L"Ważne wspomnienie");
            })) return 1363;

        // Sleep consolidation <wazne_wspomnienie> tag + State::save/load roundtrip.
        const auto key_cons = cognition::parseConsolidation(
            L"<podsumowanie>Rozmowa o bracie.</podsumowanie><wazne_wspomnienie>Opowiedział mi o swoim bracie Tomku.</wazne_wspomnienie>");
        if (!has(key_cons.key_memory, L"bracie Tomku")) return 1363;

        mind::State st;
        st.of("hana") = world_self;
        st.of("hana").play_momentum = 0.65f;
        st.of("hana").sleep_debt = 0.45f;
        st.of("hana").stress_load = 0.5f;
        st.of("hana").rejected_picks_in_row = 3;
        const auto loaded = mind::State::load(st.save());
        if (!loaded) return 1363;
        const auto& ls = loaded->selves.at("hana");
        if (ls.key_memories.size() != 1 || ls.player_world.size() != 2 || ls.player_world[0].name != L"Tomek (brat)" ||
            std::abs(ls.play_momentum - 0.65f) > 0.02f || std::abs(ls.sleep_debt - 0.45f) > 0.02f ||
            std::abs(ls.stress_load - 0.5f) > 0.02f || ls.rejected_picks_in_row != 3)
            return 1363;
    }

    // 1364: Curiosity — two separate paths after his message: asking back when she is not sure she
    // understood him (clarify) and carrying the talk on herself (lead); a question set aside for later.
    {
        // System 1: what may leave her unsure, what closes a topic, what opens one of his life.
        const mind::Self blank;
        const auto q_amb = mind::quick(L"Sam nie wiem, tak i nie w sumie", blank);
        const auto q_vague = mind::quick(L"No i znowu ta cała sprawa z wczoraj, wiesz o co chodzi", blank);
        const auto q_cut = mind::quick(L"Bo myślałem, że może...", blank);
        const auto q_ok = mind::quick(L"ok spoko", blank);
        const auto q_hook = mind::quick(L"Wczoraj byłem z bratem na koncercie w Krakowie", blank);
        const auto q_asks = mind::quick(L"A ty co dziś robiłaś?", blank);
        if (q_amb.unclear != mind::Quick::Unclear::ambivalent || q_vague.unclear != mind::Quick::Unclear::vague ||
            q_cut.unclear != mind::Quick::Unclear::unfinished || !q_ok.closed || q_ok.unclear != mind::Quick::Unclear::none ||
            !q_hook.hook || q_hook.closed || !q_asks.asks || q_asks.closed) return 1364;
        // An opinion or an answer is not a closed topic; only acknowledgements are.
        if (mind::quick(L"nie lubię horrorów", blank).closed || mind::quick(L"Hades", blank).closed || mind::quick(L"a ty", blank).closed ||
            !mind::quick(L"w sumie nic ciekawego", blank).closed || !mind::quick(L"nie wiem", blank).closed) return 1364;
        // The urge: now and then (her seed), never on top of hurt, not twice in a row, no lead when he asked her.
        auto cctx = ctx;
        cctx.persona = Persona::genki;
        mind::Self curious_self;
        curious_self.drives.curiosity = 0.7f;
        const auto u_clarify = mind::curiosityUrge(curious_self, cctx, q_amb, 0);
        if (u_clarify.move != CuriousMove::clarify || !has(u_clarify.why, L"niejednoznacznie")) return 1364;
        if (mind::curiosityUrge(curious_self, cctx, q_amb, 999u << 7).move != CuriousMove::none) return 1364;
        const auto u_lead = mind::curiosityUrge(curious_self, cctx, q_ok, 0);
        if (u_lead.move != CuriousMove::lead || u_lead.deeper || !has(u_lead.why, L"zamknął temat")) return 1364;
        const auto u_deep = mind::curiosityUrge(curious_self, cctx, q_hook, 0);
        if (u_deep.move != CuriousMove::lead || !u_deep.deeper) return 1364;
        if (mind::curiosityUrge(curious_self, cctx, q_asks, 0).move != CuriousMove::none) return 1364;
        auto just_asked = curious_self;
        just_asked.curious_gap = 1;
        if (mind::curiosityUrge(just_asked, cctx, q_amb, 0).move != CuriousMove::none) return 1364;
        mind::Self hurt;
        hurt.last_reply_vulnerable = true;
        if (mind::curiosityUrge(hurt, cctx, mind::quick(L"ok", hurt), 0).move != CuriousMove::none) return 1364;

        // Her prompt: the <ocena> key always; this time's directive only with an urge, alone; in a shorter
        // style the one intended question replaces "no automatic question".
        PromptExtras cx;
        cx.inner = true;
        const auto plain = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, cx);
        if (!has(plain, L"\"ciekawosc\"") || has(plain, L"Tym razem DOPYTAJ") || has(plain, L"POCIĄGNIJ ROZMOWĘ")) return 1364;
        cx.reply_length = ReplyLength::concise;
        cx.curiosity = CuriousMove::clarify;
        cx.curiosity_why = u_clarify.why;
        const auto ask_back = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, cx);
        if (!has(ask_back, L"Tym razem DOPYTAJ") || !has(ask_back, L"niejednoznacznie") || has(ask_back, L"Nie dodawaj automatycznie pytania") ||
            !has(ask_back, L"zamierzone pytanie")) return 1364;
        cx.curiosity = CuriousMove::lead;
        cx.curiosity_why = u_lead.why;
        cx.curiosity_carry = L"Jak poszła rozmowa z szefem?";
        const auto carry = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, cx);
        if (!has(carry, L"POCIĄGNIJ ROZMOWĘ") || !has(carry, L"nowy wątek") || !has(carry, L"Jak poszła rozmowa z szefem?")) return 1364;
        if (!has(systemPrompt(Persona::genki, companion_fixture::hana, {}, true, now, 1, 20, cx), L"This time CARRY THE TALK ON")) return 1364;
        cx.guest = Guest{companion_fixture::rin, Persona::tsundere};
        if (has(systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, cx), L"POCIĄGNIJ ROZMOWĘ")) return 1364;

        // Asking back: her appraisal records it, her words show what she asked; his answer settles it.
        Memory cmem;
        mind::Self asker;
        asker.urge = u_clarify;
        const auto ap = mind::parseAppraisal(L"{\"czuje\":\"zaciekawiona\",\"ciekawosc\":{\"ruch\":\"dopytuje\",\"o_co\":\"czy chodzi mu o pracę\",\"rozumiem\":0.4}}");
        if (!ap.curious || ap.curious->move != CuriousMove::clarify || !ap.curious->understood || *ap.curious->understood > 0.5f || ap.curious->later) return 1364;
        mind::apply(asker, cmem, ap, cctx, "");
        mind::onReply(asker, cctx, false, L"Hmm. Czekaj, chodzi ci o pracę czy o grę?");
        if (asker.curious.size() != 1 || asker.curious[0].move != CuriousMove::clarify || asker.curious[0].at != now || asker.curious[0].outcome != -1 ||
            asker.curious[0].about != L"Czekaj, chodzi ci o pracę czy o grę?" || !has(asker.curious[0].why, L"niejednoznacznie") ||
            asker.curious_gap != 0 || asker.urge.move != CuriousMove::none) return 1364;
        auto later_ctx = cctx;
        later_ctx.now = now + 60;
        const std::wstring explained = L"O pracę, szef znowu marudzi o raporty";
        mind::onPlayerMessage(asker, later_ctx, mind::quick(explained, asker), explained);
        if (asker.curious[0].outcome != 1 || asker.curious[0].settled != later_ctx.now) return 1364;

        // Carrying the talk on without an appraisal: her question after the urge is the act; "nie" lets it pass.
        mind::Self leader;
        leader.urge = u_lead;
        mind::onReply(leader, cctx, false, L"Haha, dobra. A widziałeś już nowy odcinek Frieren?");
        if (leader.curious.size() != 1 || leader.curious[0].move != CuriousMove::lead || leader.curious[0].about != L"A widziałeś już nowy odcinek Frieren?") return 1364;
        mind::onPlayerMessage(leader, later_ctx, mind::quick(L"nie", leader), L"nie");
        if (leader.curious[0].outcome != 0) return 1364;
        mind::Self no_question; // No question in her words: the urge just goes.
        no_question.urge = u_lead;
        mind::onReply(no_question, cctx, false, L"Haha, dobra.");
        if (!no_question.curious.empty() || no_question.urge.move != CuriousMove::none || no_question.curious_gap != 99) return 1364;

        // A question set aside for a better moment: in her workspace later, back with a lead, then asked.
        mind::Self saver;
        const auto set_aside = mind::parseAppraisal(L"{\"ciekawosc\":{\"ruch\":\"ciagne\",\"o_co\":\"Jak poszła rozmowa z szefem\",\"kiedy\":\"pozniej\"}}");
        mind::apply(saver, cmem, set_aside, cctx, "");
        mind::onReply(saver, cctx, false, L"Trzymaj się, jestem tu.");
        if (saver.curious.size() != 1 || !saver.curious[0].deferred() || saver.curious[0].later_at != now || saver.curious_gap != 99) return 1364;
        auto calm_ctx = cctx;
        calm_ctx.now = now + 40 * 60;
        const auto later_cands = mind::candidates(saver, calm_ctx, cmem, {});
        if (std::none_of(later_cands.begin(), later_cands.end(), [&](const mind::Candidate& c) { return c.source == "curious" && has(c.text, L"Jak poszła rozmowa z szefem"); }))
            return 1364;
        const auto back = mind::curiosityUrge(saver, calm_ctx, mind::quick(L"spoko", saver), 0);
        if (back.move != CuriousMove::lead || back.carry != CuriousCarry::question || back.carry_id != saver.curious[0].id || back.carried != L"Jak poszła rozmowa z szefem") return 1364;
        saver.urge = back;
        mind::apply(saver, cmem, mind::parseAppraisal(L"{\"ciekawosc\":{\"ruch\":\"ciagne\"}}"), calm_ctx, "");
        mind::onReply(saver, calm_ctx, false, L"A właśnie, jak poszła rozmowa z szefem?");
        if (saver.curious.size() != 1 || saver.curious[0].at != calm_ctx.now || saver.curious[0].later_at != now || saver.curious[0].outcome != -1 ||
            saver.curious[0].about != L"A właśnie, jak poszła rozmowa z szefem?") return 1364;

        // Kept between starts; an unanswered one goes by after hours, one set aside too long is let go.
        mind::State cs;
        cs.of("hana") = saver;
        cs.of("hana").curious.push_back(asker.curious[0]);
        const auto cl = mind::State::load(cs.save());
        if (!cl || cl->selves.at("hana").curious.size() != 2 || cl->selves.at("hana").curious[1].outcome != 1 ||
            cl->selves.at("hana").curious[0].later_at != now || cl->selves.at("hana").curious[0].move != CuriousMove::lead ||
            cl->selves.at("hana").curious[1].about != asker.curious[0].about) return 1364;
        auto aging = saver;
        auto hours_ctx = cctx;
        hours_ctx.now = calm_ctx.now + 7 * 3600;
        mind::tick(aging, hours_ctx);
        if (aging.curious[0].outcome != 0) return 1364;
        mind::Self forgetful;
        mind::apply(forgetful, cmem, set_aside, cctx, "");
        auto days_ctx = cctx;
        days_ctx.now = now + 4 * 86400;
        mind::tick(forgetful, days_ctx);
        if (forgetful.curious.size() != 1 || forgetful.curious[0].outcome != 2) return 1364;
        // Saying so is not asking: an appraisal that claims curiosity without a question in her words
        // records nothing and does not hold back her next urge.
        mind::Self claimer;
        claimer.curious_gap = 5;
        mind::apply(claimer, cmem, mind::parseAppraisal(L"{\"ciekawosc\":{\"ruch\":\"ciagne\",\"o_co\":\"jak spędza dzień\"}}"), cctx, "");
        mind::onReply(claimer, cctx, false, L"Zawiń się w koc i odpal coś miłego.");
        if (!claimer.curious.empty() || claimer.curious_gap != 6) return 1364;
        // Her curiosity comes back by itself (halfway to her character's level in two hours), however spent.
        mind::Self spent;
        spent.drives.curiosity = 0.02f;
        spent.drives.at = now;
        spent.last_talk = now;
        auto two_hours = cctx;
        two_hours.now = now + 2 * 3600;
        mind::tick(spent, two_hours);
        if (!(spent.drives.curiosity > 0.28f)) return 1364;
    }

    // 1365: Conversation craft — follow-ups on his life, reflecting, bids (Gottman), coming back to an
    // unanswered question, accommodation, topic fatigue and threads, what he knows about her,
    // attachment, interest (reward prediction error), reciprocal disclosure, natural closing, metrics.
    {
        auto cctx = ctx;
        cctx.persona = Persona::genki;
        const auto has_source = [&](const std::vector<mind::Candidate>& list, const char* source, const wchar_t* part) {
            return std::any_of(list.begin(), list.end(), [&](const mind::Candidate& c) { return c.source == source && (!part || has(c.text, part)); });
        };
        Memory mem;
        // When something he mentioned is over by (now: a Thursday).
        if (mind::eventDue(L"jutro", now) != now + 86400 + 6 * 3600 || mind::eventDue(L"w piątek", now) != now + 86400 + 6 * 3600 ||
            mind::eventDue(L"w czwartek", now) != now + 7 * 86400 + 6 * 3600 || mind::eventDue(L"w weekend", now) != now + 3 * 86400 + 6 * 3600) return 1365;
        // 1. Follow-up: luck before it, "how did it go?" after it — carried by a lead and settled by her question.
        mind::Self f_self;
        const auto plan = mind::parseAppraisal(L"{\"jego_plan\":[{\"t\":\"rozmowa o pracę\",\"kiedy\":\"jutro\"}]}");
        if (plan.plans.size() != 1) return 1365;
        mind::apply(f_self, mem, plan, cctx, "");
        if (f_self.follow_ups.size() != 1 || f_self.follow_ups[0].due != now + 30 * 3600) return 1365;
        auto before = cctx; before.now = now + 20 * 3600;
        if (!has_source(mind::candidates(f_self, before, mem, {}), "follow_up", L"powodzenia")) return 1365;
        auto after = cctx; after.now = now + 31 * 3600;
        if (!has_source(mind::candidates(f_self, after, mem, {}), "follow_up", L"jak poszło")) return 1365;
        f_self.urge = mind::curiosityUrge(f_self, after, mind::quick(L"spoko", f_self), 0);
        if (f_self.urge.move != CuriousMove::lead || f_self.urge.carry != CuriousCarry::follow_up || f_self.urge.carried != L"rozmowa o pracę") return 1365;
        mind::onReply(f_self, after, false, L"A właśnie! Jak poszła rozmowa o pracę?");
        if (f_self.follow_ups[0].state != 1 || f_self.curious.empty() || !has(f_self.curious.back().why, L"pamiętała")) return 1365;
        // 2. Reflecting (active listening) when he opens up; its prompt; recorded without a question.
        mind::Self listener;
        const std::wstring tired = L"Jestem dziś strasznie zmęczony i smutno mi po całym dniu.";
        listener.urge = mind::curiosityUrge(listener, cctx, mind::quick(tired, listener), 0);
        if (listener.urge.move != CuriousMove::reflect) return 1365;
        PromptExtras rx;
        rx.inner = true; rx.curiosity = CuriousMove::reflect; rx.curiosity_why = listener.urge.why;
        if (!has(systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, rx), L"Tym razem ODBIJ")) return 1365;
        mind::onReply(listener, cctx, false, L"Czyli najbardziej dobiło cię to, że nikt nie docenił twojej pracy.", true);
        if (listener.curious.size() != 1 || listener.curious[0].move != CuriousMove::reflect || !has(listener.curious[0].about, L"Czyli")) return 1365;
        // Reciprocal disclosure: she opened up, he opens up right after — closeness and calm.
        mind::onPlayerMessage(listener, after, mind::quick(tired, listener), tired);
        if (listener.curious[0].outcome != 1 || listener.bonded_at != after.now || listener.trust_delta <= 0 ||
            !has_source(mind::candidates(listener, after, mem, {}), "bonding", nullptr)) return 1365;
        // 3. Bids (Gottman): her reaching out answered toward / away / against; his own bid noticed.
        mind::Self bidder;
        mind::onReply(bidder, cctx, false, L"A ty co dziś robiłeś?");
        if (!bidder.bids.open) return 1365;
        mind::onPlayerMessage(bidder, cctx, mind::quick(L"ok", bidder), L"ok");
        mind::onReply(bidder, cctx, false, L"Słuchaj, a lubisz horrory?");
        mind::onPlayerMessage(bidder, cctx, mind::quick(L"zamknij się", bidder), L"zamknij się");
        mind::onReply(bidder, cctx, false, L"Ej, co jest?");
        mind::onPlayerMessage(bidder, cctx, mind::quick(L"Sorry, miałem ciężki dzień w pracy", bidder), L"Sorry, miałem ciężki dzień w pracy");
        if (bidder.bids.away < 0.99f || bidder.bids.against < 0.99f || bidder.bids.toward < 0.99f || bidder.security_delta >= 0) return 1365;
        if (!mind::quick(L"Patrz, co znalazłem: https://example.com", bidder).bid || !has_source(mind::candidates(bidder, cctx, mem, L"Patrz, co znalazłem: https://example.com"), "bid", nullptr))
            return 1365;
        bidder.bids = {0.5f, 5.0f, 0.0f, 0, false, now};
        if (!has_source(mind::candidates(bidder, cctx, mem, {}), "bids", L"zbywa")) return 1365;
        // 4. A question he left unanswered: a persistent character comes back to it once.
        auto yctx = cctx;
        yctx.persona = Persona::yandere;
        mind::Self persistent;
        mind::CuriousAct ignored;
        ignored.id = "c50"; ignored.move = CuriousMove::lead; ignored.about = L"A widziałeś już Frieren?";
        ignored.at = now - 20 * 60; ignored.outcome = 0; ignored.settled = now - 15 * 60;
        persistent.curious.push_back(ignored);
        persistent.urge = mind::curiosityUrge(persistent, yctx, mind::quick(L"ok", persistent), 0);
        if (persistent.urge.carry != CuriousCarry::returning || persistent.urge.carry_id != "c50") return 1365;
        mind::onReply(persistent, yctx, false, L"Hej, a nie odpowiedziałeś mi: widziałeś Frieren?");
        if (!persistent.curious[0].returned || persistent.curious.size() != 2 || !has(persistent.curious[1].why, L"nie odpowiedziałeś")) return 1365;
        // 5. Accommodation: how he writes, tuned in to.
        mind::Self tuner;
        for (int i = 0; i < 4; ++i) mind::onPlayerMessage(tuner, cctx, mind::quick(L"no to gramy dzis wieczorem xd", tuner), L"no to gramy dzis wieczorem xd");
        const auto style = mind::styleText(tuner.his_style, false);
        if (!has(style, L"pisze krótko") || !has(style, L"małymi literami") || !has(style, L"bez polskich znaków") || !has(style, L"śmieje") ||
            !has_source(mind::candidates(tuner, cctx, mem, {}), "style", nullptr)) return 1365;
        // 6. Topic fatigue (habituation) and a thread of his to come back to.
        mind::Self topical;
        for (int i = 0; i < 6; ++i) mind::onPlayerMessage(topical, cctx, mind::quick(L"Uwielbiam horrory psychologiczne", topical), L"Uwielbiam horrory psychologiczne");
        if (topical.topic.turns < 6 || !has_source(mind::candidates(topical, cctx, mem, L"Uwielbiam horrory psychologiczne"), "topic_fatigue", nullptr)) return 1365;
        mind::Self threader;
        threader.threads.push_back({now - 10 * 60, L"W weekend byłem w górach z kumplami", false});
        if (!has_source(mind::candidates(threader, cctx, mem, {}), "thread", L"w górach")) return 1365;
        threader.urge = mind::curiosityUrge(threader, cctx, mind::quick(L"spoko", threader), 0);
        if (threader.urge.carry != CuriousCarry::thread) return 1365;
        // 8. What he knows about her: not news again; a retelling is noticed (a callback is not one).
        mind::Self teller;
        teller.claims.push_back({now - 2 * 86400, L"Mam kota Bajtka, rudego dachowca"});
        if (!has_source(mind::candidates(teller, cctx, mem, L"A jak tam twój kot?"), "he_knows", L"Bajtka")) return 1365;
        mind::onReply(teller, cctx, false, L"Mam kota Bajtka, to rudy dachowiec, uwielbia spać na klawiaturze.");
        if (!teller.retold) return 1365;
        mind::onReply(teller, cctx, false, L"Jak ci mówiłam, mam kota Bajtka, rudego dachowca, i znowu spał na klawiaturze.");
        if (teller.retold) return 1365;
        // 9. Attachment: by character (-dere) or character.txt; avoidant pulls back after opening up, anxious wants a sign.
        if (mind::attachmentOf(ctx) != mind::Attachment::avoidant || mind::attachmentOf(yctx) != mind::Attachment::anxious) return 1365;
        Character anxious_character;
        anxious_character.attachment = "anxious";
        auto actx = ctx;
        actx.character = &anxious_character;
        actx.security = 40;
        if (mind::attachmentOf(actx) != mind::Attachment::anxious) return 1365;
        mind::Self clingy;
        clingy.drives.bond = 0.7f;
        if (!has_source(mind::candidates(clingy, actx, mem, {}), "attachment", L"niepokoisz")) return 1365;
        mind::Self distant;
        distant.opened_at = now - 10 * 3600;
        if (!has_source(mind::candidates(distant, ctx, mem, {}), "attachment", L"dystans")) return 1365;
        // Gemini: interest as a reward prediction error — a reply richer than expected intrigues her.
        mind::Self curious_one;
        const std::wstring rich = L"Wczoraj byłem z bratem na koncercie w Krakowie i było naprawdę świetnie";
        mind::onPlayerMessage(curious_one, cctx, mind::quick(rich, curious_one), rich);
        if (!(curious_one.interest_rpe > 0.2f) || !(curious_one.interest > 0.5f) || !has_source(mind::candidates(curious_one, cctx, mem, {}), "interest", L"zaintrygował")) return 1365;
        // 7. Natural closing: a signal before her break; a thread left open in her goodbye.
        PromptExtras px;
        px.inner = true; px.pre_closing = true;
        if (!has(systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, px), L"dobiega końca") ||
            !has(chats::farewellKickoff(false, false), L"następny raz")) return 1365;
        // 10. Her workspace's choice, for the diagnostics.
        const auto sources = mind::workspaceSources({{"a", L"x", 0.9f, mind::Status::know}, {"b", L"y", 0.01f, mind::Status::know}, {"c", L"z", 0.5f, mind::Status::feel}}, 0.3f);
        if (sources != std::vector<std::string>{"a", "c"}) return 1365;
        // Kept between starts.
        mind::State ks;
        auto& kept = ks.of("hana");
        kept.follow_ups = f_self.follow_ups; kept.bids = bidder.bids; kept.his_style = tuner.his_style; kept.threads = threader.threads;
        kept.interest = 0.8f; kept.bonded_at = now; kept.opened_at = now - 5; kept.curious = persistent.curious;
        const auto kl = mind::State::load(ks.save());
        if (!kl) return 1365;
        const auto& k = kl->selves.at("hana");
        if (k.follow_ups.size() != 1 || k.follow_ups[0].state != 1 || std::abs(k.bids.away - 5.0f) > 0.01f || k.his_style.samples != 4 ||
            k.threads.size() != 1 || std::abs(k.interest - 0.8f) > 0.01f || k.bonded_at != now || k.opened_at != now - 5 || !k.curious[0].returned) return 1365;
    }

    // 1366: The real weather where she is (Open-Meteo, offline fixtures): her home city or a trip's
    // town, parsing, wording, notable weather, a lookup with a fake transport, her prompt.
    {
        Character home;
        home.home_en = L"Kraków, Kazimierz - a flat full of books";
        if (weather::homeCity(home) != L"Kraków" || !has(weather::forecastUrl(50.06143, 19.93658), L"latitude=50.0614&longitude=19.9366") ||
            !has(weather::geocodeUrl(L"Kraków", false), L"name=Krak%C3%B3w") || !has(weather::geocodeUrl(L"Kraków", false), L"language=pl")) return 1366;
        const auto parsed_chars = parseCharacters("[x]\nname=X\nlocation=50.06,19.94\nattachment=avoidant\n");
        if (parsed_chars.size() != 1 || !parsed_chars[0].location || std::abs(parsed_chars[0].location->first - 50.06) > 1e-6 || parsed_chars[0].attachment != "avoidant")
            return 1366;
        const std::string place_json = R"({"results":[{"name":"Kraków","latitude":50.06143,"longitude":19.93658,"country":"Poland","timezone":"Europe/Warsaw"}]})";
        const std::string forecast_json = R"({"current":{"temperature_2m":6.6,"apparent_temperature":2.9,"relative_humidity_2m":88,"precipitation":0.4,"weather_code":53,)"
                                          R"("wind_speed_10m":27.4,"is_day":1},"daily":{"weather_code":[53,95],"temperature_2m_max":[9.1,12.3],"temperature_2m_min":[4.8,6.0],)"
                                          R"("precipitation_sum":[2.1,8.5]}})";
        const auto place = weather::parsePlace(place_json, false);
        const auto forecast = weather::parseForecast(forecast_json);
        if (!place || place->name != L"Kraków" || std::abs(place->lat - 50.06143) > 1e-4 || !forecast || forecast->code != 53 || !forecast->tomorrow ||
            forecast->tomorrow_code != 95 || weather::parsePlace(R"({"results":[]})", false) || weather::parseForecast("{}")) return 1366;
        int calls = 0;
        const weather::Get get = [&](const std::wstring& url) {
            ++calls;
            const std::string& body = url.find(L"geocoding-api") != std::wstring::npos ? place_json : forecast_json;
            return HttpResponse{200, {body.begin(), body.end()}};
        };
        auto report = weather::fetch(get, L"Kraków", std::nullopt, false, now);
        if (!report || calls != 2 || report->at != now || report->asked != L"Kraków" || report->place.name != L"Kraków") return 1366;
        const auto line = weather::text(*report, false);
        if (!has(line, L"Kraków") || !has(line, L"7°C") || !has(line, L"odczuwalnie 3°C") || !has(line, L"mżawka") || !has(line, L"wiatr 27 km/h") ||
            !has(line, L"jutro burza") || !has(line, L"nie wymyślaj")) return 1366;
        if (!weather::notable(*report, false).empty()) return 1366; // Drizzle is not news.
        report->now.code = 95;
        if (!has(weather::notable(*report, false), L"burza")) return 1366;
        calls = 0;
        if (!weather::fetch(get, L"Kraków", std::pair{50.06, 19.94}, false, now) || calls != 1) return 1366; // Her coordinates: no lookup.
        if (weather::fetch([](const std::wstring&) { return HttpResponse{}; }, L"Kraków", std::nullopt, false, now)) return 1366;
        // Her prompt carries it.
        PromptExtras wx;
        wx.weather = weather::text(*report, false);
        if (!has(systemPrompt(Persona::genki, companion_fixture::hana, {}, false, now, 1, 20, wx), L"Prawdziwa pogoda tam, gdzie jesteś")) return 1366;
        // On a trip: the town she travelled to, until the trip is over; an ordinary outing is no trip.
        mind::Self traveller;
        mind::Episode trip;
        trip.id = "e1"; trip.at = now - 3600; trip.what = L"wyjazd w góry na 3 dni"; trip.where = L"Zakopane, hostel przy Krupówkach";
        trip.state = mind::Episode::State::happened;
        traveller.episodes.push_back(trip);
        if (mind::travelPlace(traveller, ctx) != L"Zakopane") return 1366;
        auto back_home = ctx;
        back_home.now = now + 4 * 86400;
        if (!mind::travelPlace(traveller, back_home).empty()) return 1366;
        mind::Self homebody;
        mind::Episode shopping;
        shopping.id = "e2"; shopping.at = now - 3600; shopping.what = L"zakupy na kolację"; shopping.where = L"targ Marché des Enfants Rouges";
        shopping.state = mind::Episode::State::happened;
        homebody.episodes.push_back(shopping);
        if (!mind::travelPlace(homebody, ctx).empty()) return 1366;
    }

    // 1367: Where they live (Settings → Gdzie mieszkają): a city for all of them, hers over it or
    // her own home again; the city's clock (both summers), home, coordinates and her prompt.
    {
        const auto& list = cities();
        if (list.size() < 20 || !cityFromKey("osaka") || cityFromKey("atlantis")) return 1367;
        for (size_t i = 0; i < list.size(); ++i) {
            if (i && list[i - 1].utc_offset > list[i].utc_offset) return 1367; // West to east.
            if (cityFromKey(list[i].key) != &list[i] || list[i].home_pl.empty() || list[i].home_en.empty() ||
                list[i].home_en.find_first_of(L"-–—(;:") != std::wstring::npos) return 1367; // Unique; the weather's lookup ends at the first comma.
        }
        Character own;
        own.key = "zed"; own.home_en = L"Kraków, Kazimierz - a flat"; own.home_pl = L"Kraków, Kazimierz - mieszkanie";
        own.zoned = true; own.utc_offset = 60; own.dst = Character::Dst::eu; own.location = std::pair{50.06, 19.94};
        const uint64_t january = 1'768'478'400, july = 1'784'116'800; // 2026-01-15 and 2026-07-15, 12:00 UTC.
        const auto at = [&](uint64_t utc) { const auto clock = localClock(own, utc); return clock ? clock->hour * 100 + clock->minute : -1; };
        if (homeCity(own) || own.home(true) != L"Kraków, Kazimierz - a flat" || at(january) != 1300 || at(july) != 1400 || own.coordinates()->first != 50.06) return 1367;
        setHomeCities("los-angeles", {});
        if (homeCity(own) != cityFromKey("los-angeles") || own.home(true) != L"Los Angeles, USA" || own.home(false) != L"Los Angeles, USA" ||
            weather::homeCity(own) != L"Los Angeles" || std::abs(own.coordinates()->first - 34.0522) > 1e-6 || at(january) != 400 || at(july) != 500) return 1367;
        setHomeCities("sydney", {});
        if (at(january) != 2300 || at(july) != 2200) return 1367; // The southern summer spans the new year: UTC+11 in January.
        setHomeCities("auckland", {});
        if (at(january) != 100 || at(july) != 0) return 1367;
        setHomeCities("mumbai", {});
        if (at(january) != 1730 || at(july) != 1730) return 1367;
        setHomeCities("los-angeles", {{"zed", "tokyo"}, {"other", "home"}});
        if (own.home(false) != L"Tokio, Japonia" || at(january) != 2100) return 1367;
        setHomeCities("los-angeles", {{"zed", std::string(own_home)}});
        if (homeCity(own) || own.home(true) != L"Kraków, Kazimierz - a flat" || at(january) != 1300) return 1367;
        setHomeCities("atlantis", {{"zed", "nowhere"}}); // Unknown: her own.
        if (homeCity(own) || at(july) != 1400) return 1367;
        // Her prompt: the city and its clock; her corner of it she makes up herself.
        PromptExtras when;
        when.utc = july;
        const auto before = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, july, 1, 20, when);
        if (has(before, L"Gdzie mieszkasz")) return 1367; // Hana has no home: none in her prompt.
        setHomeCities("paris", {});
        const auto moved = systemPrompt(Persona::genki, companion_fixture::hana, {}, false, july, 1, 20, when);
        const auto moved_en = systemPrompt(Persona::genki, companion_fixture::hana, {}, true, july, 1, 20, when);
        setHomeCities({}, {});
        if (!has(moved, L"Gdzie mieszkasz: Paryż, Francja.") || !has(moved, L"godz. 14:00") || !has(moved, L"nie są podane") ||
            !has(moved_en, L"Where you live: Paris, France.") || !has(moved_en, L"are not given")) return 1367;
    }
    return 0;
}

// `--live-voice` (manual, never in CTest): each built-in companion says her sample with this
// machine's ElevenLabs key (Settings or ELEVENLABS_API_KEY); the WAVs go to %TEMP%\launchme-voice-live.
int liveVoice(const std::vector<companion::Character>& built, bool english) {
    using namespace companion;
    const auto key = elevenlabs::key(userConfigDirectory());
    if (key.empty()) { std::cerr << "No ElevenLabs key (Settings or ELEVENLABS_API_KEY)\n"; return 240; }
    const auto status = elevenlabs::check(elevenlabs::request, key);
    std::cout << "key: " << (status.ok ? "ok" : "refused") << ", HTTP " << status.status << ", " << status.voices.size() << " voices listed\n";
    if (!status.ok) return 241;
    const auto folder = std::filesystem::temp_directory_path() / L"launchme-voice-live";
    std::filesystem::create_directories(folder);
    // Each request with its status and time.
    const elevenlabs::Request timed = [](const std::wstring& k, const std::wstring& path, const std::string* payload) {
        const auto t0 = GetTickCount64();
        auto response = elevenlabs::request(k, path, payload);
        std::cout << "  HTTP " << response.status << " in " << GetTickCount64() - t0 << " ms, " << response.body.size() << " B: " << text_encoding::utf8Lenient(path) << '\n';
        return response;
    };
    std::set<std::string> blocked;
    int failures = 0;
    for (const auto& c : built) {
        if (c.soon) continue;
        const auto who = elevenlabs::speaker(&c, c.persona, {}, {}, status.voices);
        const auto sample = elevenlabs::sample(c.persona, english);
        const auto parsed = parseText(sample.text); // Its cue acted, as in her replies.
        const std::vector<Line> said{{static_cast<Look>(&c - built.data()), parsed.text, {{0, sample.emotion}}, {}, parsed.cues}};
        std::cout << c.key << ":\n";
        const auto parts = elevenlabs::chunks(said, [&](Look) { return who; });
        std::vector<uint8_t> all;
        std::string used;
        const auto started = GetTickCount64();
        uint64_t first_ms = 0;
        for (const auto& part : parts) {
            std::cout << "  > " << text_encoding::utf8Lenient(part.text) << '\n';
            auto spoken = elevenlabs::speak(timed, key, who, part, english, blocked);
            blocked.insert(spoken.refused.begin(), spoken.refused.end());
            if (!first_ms) first_ms = GetTickCount64() - started;
            if (spoken.wav.empty()) { std::cout << c.key << ": FAILED (problem " << static_cast<int>(spoken.problem) << ", HTTP " << spoken.status << ")\n"; ++failures; break; }
            used = spoken.voice;
            const auto clip = parseWav(spoken.wav);
            if (clip) all.insert(all.end(), clip->samples.begin(), clip->samples.end());
        }
        if (all.empty()) continue;
        // One WAV per companion: 24 kHz 16-bit mono.
        std::vector<uint8_t> file;
        const auto put = [&](uint32_t v, int n) { for (int i = 0; i < n; ++i) file.push_back(static_cast<uint8_t>(v >> (8 * i))); };
        const auto tag = [&](const char* t) { file.insert(file.end(), t, t + 4); };
        tag("RIFF"); put(36 + static_cast<uint32_t>(all.size()), 4); tag("WAVE"); tag("fmt "); put(16, 4); put(1, 2); put(1, 2); put(24000, 4); put(48000, 4); put(2, 2); put(16, 2);
        tag("data"); put(static_cast<uint32_t>(all.size()), 4); file.insert(file.end(), all.begin(), all.end());
        atomic_file::write(folder / (text_encoding::wideLenient(c.key) + (english ? L"_en.wav" : L"_pl.wav")), file);
        std::cout << c.key << ": " << who.model << ", voice " << used << ", " << parts.size() << " chunks, first " << first_ms << " ms, total "
                  << GetTickCount64() - started << " ms, " << all.size() / 48000.0 << " s\n";
    }
    std::cout << "refused voices: " << blocked.size() << ", WAVs in " << text_encoding::utf8Lenient(folder.wstring()) << '\n';
    return failures ? 242 : 0;
}

// `--live-weather` (manual, never in CTest): the real weather where each built-in companion lives
// (Open-Meteo, no key), worded as her prompt gets it.
int liveWeather(const std::vector<companion::Character>& built) {
    using namespace companion;
    const weather::Get get = [](const std::wstring& url) { return httpGet(url, L"", 256 * 1024); };
    const auto now = static_cast<uint64_t>(std::time(nullptr));
    int failures = 0;
    for (const auto& c : built) {
        if (c.soon) continue;
        const auto city = weather::homeCity(c);
        const auto report = weather::fetch(get, city, c.location, false, now);
        if (!report) { std::cout << c.key << ": FAILED (" << text_encoding::utf8Lenient(city) << ")\n"; ++failures; continue; }
        std::cout << c.key << ":" << text_encoding::utf8Lenient(weather::text(*report, false)) << "\n";
        if (const auto notable = weather::notable(*report, false); !notable.empty()) std::cout << "  " << text_encoding::utf8Lenient(notable) << "\n";
    }
    return failures ? 243 : 0;
}

// The built-in characters (res/companion, whatever is there; nothing about who they are): keys
// the app takes, names, the basic outfit first, costumes following their character, each found
// again by her key.
int builtIn() {
    using namespace companion;
    const auto& built = characters();
    if (built.size() > 20) return 7840;
    for (size_t i = 0; i < built.size(); ++i) {
        const auto& c = built[i];
        const auto look = static_cast<Look>(i);
        if (!characterKey(c.key) || c.name.empty() || lookFromKey(c.key) != look || lookKey(look) != c.key) return 7841;
        if (!c.soon && (c.costumes.empty() || c.costumes[0].slot != 0 || c.costumes[0].folder != "costume_00")) return 7842;
        for (const auto& costume : c.costumes)
            if (costume.look != look || costume.slot < 0 || costume.slot >= 32 || costume.level < 0 || costume.level > affinity::max_level) return 7843;
        // Her ElevenLabs voice: a free account must hear her too, so a library voice comes with a built-in fallback.
        if (!c.voice.voice.empty() && std::none_of(elevenlabs::builtin_voices.begin(), elevenlabs::builtin_voices.end(),
                                                   [&](const elevenlabs::Builtin& b) { return c.voice.fallback == b.id; })) return 7845;
        if (!c.voice.model.empty() && !elevenlabs::modelId(c.voice.model)) return 7846;
        // Her stance on his other "pairs": Akane and Yuki open, Saki and Mika monogamous, Kiko undecided.
        const std::map<std::string, Exclusivity> stances{{"akane", Exclusivity::open}, {"yuki", Exclusivity::open}, {"saki", Exclusivity::monogamous},
                                                         {"mika", Exclusivity::monogamous}, {"kiko", Exclusivity::fluid}};
        if (const auto stance = stances.find(c.key); stance != stances.end() && c.exclusivity != stance->second) return 7848;
        // Her own words of him: a person ("on"/"ona"), never "gracz"/"the player".
        for (const auto* text : {&c.about_pl, &c.about_en, &c.about_her_pl, &c.about_her_en})
            if (text->find(L"gracz") != std::wstring::npos || text->find(L"the player") != std::wstring::npos) return 7849;
    }
    const auto available = availableLooks();
    if (available.size() != static_cast<size_t>(std::count_if(built.begin(), built.end(), [](const Character& c) { return !c.soon; })) ||
        lookCount() != static_cast<int>(built.size())) return 7844;
    return 0;
}
// Who she talks with and how close they are: "on"/"ona" (never "gracz"), the stage's ceiling over
// her character, jealousy by her stance on exclusivity, tender feelings capped by the stage, and
// her question back as a message of its own.
int closeness() {
    using namespace companion;
    constexpr auto npos = std::wstring::npos;
    const uint64_t now = 1'760'000'000;
    // The gendered alternatives: man, woman, unknown (the first without a third); a ‹…› without a bar stays.
    if (personalize(L"‹On|Ona› ‹napisał|napisała›.", Gender::female) != L"Ona napisała." ||
        personalize(L"‹On|Ona› ‹napisał|napisała›.", Gender::unknown) != L"On napisał." ||
        personalize(L"‹he is|she is|they are› here", Gender::unknown) != L"they are here" ||
        personalize(L"‹he is|she is|they are› here", Gender::male) != L"he is here" ||
        personalize(L"‹ Wróć", Gender::female) != L"‹ Wróć" || personalize(L"a ‹b› c", Gender::male) != L"a ‹b› c") return 1400;
    const auto who = personText({Gender::male, L"Marek"}, false, false);
    if (who.find(L"Marek") == npos || who.find(L"To mężczyzna") == npos || who.find(L"‹") != npos || who.find(L"o nim „on”") == npos) return 1401;
    if (personText({Gender::female, L"Ola"}, false, true).find(L"widzisz tylko jej nick „Ola”") == npos ||
        personText({}, true, false).find(L"don't know their name") == npos) return 1402;
    // The prompt names the person and every stage sets her ceiling.
    const auto library = digest(fixtureGames(), 5);
    const auto prompt = [&](int level, Gender gender = Gender::male, std::vector<PromptExtras::Bond::Other> others = {}) {
        PromptExtras extras;
        extras.bond.level = level;
        extras.bond.others = std::move(others);
        extras.inner = true;
        extras.person = {gender, L"Marek"};
        return personalize(systemPrompt(Persona::yandere, hana_look, library, false, 0, 4, 12, extras), gender);
    };
    const auto stranger = prompt(2);
    if (stranger.find(L"Z kim piszesz: Marek") == npos || stranger.find(L"dopiero się poznajecie") == npos ||
        stranger.find(L"nie mówisz o nim „mój”") == npos || stranger.find(L"najwyżej ok. 0,3") == npos ||
        stranger.find(L"Inne towarzyszki") != npos || stranger.find(L"‹") != npos) return 1403;
    if (prompt(7).find(L"lekko flirtujesz") == npos || prompt(12).find(L"flirtujecie śmielej") == npos ||
        prompt(22).find(L"przyjaciółmi, ale z flirtem") == npos) return 1404;
    const auto crush = prompt(32, Gender::female, {{rin_look, 50}});
    if (crush.find(L"masz do niej słabość") == npos || crush.find(L"najwyżej ok.") != npos ||
        crush.find(L"Rin (związek na odległość)") == npos || crush.find(L"wolisz mieć ją dla siebie") == npos ||
        crush.find(L"naprawdę zazdrosna") == npos) return 1405;
    if (prompt(50).find(L"jesteście razem, na odległość") == npos || prompt(50).find(L"bez wspólnego mieszkania") == npos) return 1406;
    // An open one does not mind his other pairs; a monogamous one near the top minds less.
    {
        auto open_cast = companion_fixture::cast();
        open_cast[0].exclusivity = Exclusivity::open;
        const auto previous = characters();
        setCharacters(open_cast);
        const auto relaxed = prompt(35, Gender::male, {{rin_look, 32}});
        setCharacters(previous);
        if (relaxed.find(L"nie potrzebujesz wyłączności") == npos || relaxed.find(L"Rin (zauroczenie)") == npos || relaxed.find(L"teatralna zazdrość") == npos) return 1407;
    }
    if (parseCharacters("[x]\nname=X\nexclusivity=fluid\n")[0].exclusivity != Exclusivity::fluid ||
        parseCharacters("[x]\nname=X\n")[0].exclusivity != Exclusivity::monogamous) return 1408;
    // Her question back after her answer: a message of its own (alone only; not when it is all she says).
    const auto split = lines(L"[smile]Ha, dobre, też to lubię. [thinking]A ty co dziś robiłeś?", hana_look, std::nullopt, 3, true);
    if (split.size() != 2 || split[0].text != L"Ha, dobre, też to lubię." || split[1].text != L"A ty co dziś robiłeś?" ||
        split[1].emotions.front().emotion != Emotion::thinking || split[0].emotions.back().emotion != Emotion::smile) return 1409;
    if (lines(L"[smile]Ha, dobre, też to lubię. A ty co dziś robiłeś?", hana_look, std::nullopt, 3).size() != 1 ||
        lines(L"[smile]Serio? Nie wierzę.", hana_look, std::nullopt, 3, true).size() != 1 ||
        lines(L"[smile]Ha. A ty?", hana_look, std::nullopt, 3, true).size() != 1 ||
        lines(L"[smile]Ha, dobre, też to lubię. A grałeś w [[Hades]]?", hana_look, std::nullopt, 1, true).size() != 2 ||
        lines(L"Hana: [smile]Ha, dobre, też to lubię. A ty co robiłeś?", hana_look, rin_look, 6, true).size() != 1) return 1410;
    {
        const auto with_game = lines(L"[smile]Ha, dobre, też to lubię. A grałeś w [[Hades]]?", hana_look, std::nullopt, 1, true);
        if (with_game[1].mentions.size() != 1 || with_game[1].text.substr(with_game[1].mentions[0].at, with_game[1].mentions[0].length) != L"Hades" ||
            !with_game[0].mentions.empty()) return 1411;
    }
    // Her mind: a declaration from a near stranger flusters her; longing and tender feelings only as far as the stage goes.
    mind::Self self;
    if (mind::quick(L"kocham cię", self, 0).flash != Emotion::confused || mind::quick(L"kocham cię", self, 0).valence > 0.05f ||
        mind::quick(L"kocham cię", self, 4).flash != Emotion::shy || mind::quick(L"kocham cię", self, 4).valence < 0.19f) return 1412;
    mind::Context context;
    context.look = "hana";
    context.persona = Persona::genki;
    context.now = now;
    Memory memory;
    const auto tender = mind::parseAppraisal(L"{\"v\":0.9,\"uczucie\":\"affection\",\"sila_uczucia\":0.95}");
    context.stage = 0;
    mind::apply(self, memory, tender, context, "");
    if (self.feelings.empty() || !self.feelings.back().intensity || *self.feelings.back().intensity > 0.31f) return 1413;
    context.stage = 5;
    context.now = now + 60;
    mind::apply(self, memory, tender, context, "");
    if (!self.feelings.back().intensity || *self.feelings.back().intensity < 0.94f) return 1414;
    mind::Self missing;
    missing.drives.bond = 0.9f;
    missing.drives.at = now;
    const auto longing = [&](int stage) {
        auto at = context;
        at.stage = stage;
        at.now = now;
        const auto found = mind::candidates(missing, at, memory, {});
        for (const auto& c : found)
            if (c.source == "drive" && (c.text.find(L"Brakowało ci") != npos || c.text.find(L"Dawno ‹go|jej› nie było") != npos)) return c.text;
        return std::wstring{};
    };
    if (!longing(0).empty() || longing(1).find(L"bez tęsknoty") == npos || longing(4).find(L"Brakowało ci ‹go|jej›") == npos) return 1415;
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // The built-in characters first; everything after uses the test's own two (companion_fixture.hpp).
    int code = argc == 2 ? 0 : builtIn();
    const companion_fixture::Cast cast;
    if (argc == 2 && std::string_view(argv[1]) == "--live") code = live();
    else if (argc == 2 && std::string_view(argv[1]) == "--live-tools") code = liveTools();
    else if (argc == 2 && std::string_view(argv[1]) == "--live-duo") code = liveDuo();
    else if (argc == 2 && std::string_view(argv[1]) == "--live-latency") code = liveLatency();
    else if (argc == 2 && std::string_view(argv[1]) == "--live-weather") code = liveWeather(cast.built_in);
    else if (argc == 2 && (std::string_view(argv[1]) == "--live-voice" || std::string_view(argv[1]) == "--live-voice-en"))
        code = liveVoice(cast.built_in, std::string_view(argv[1]) == "--live-voice-en");
    else if (!code) {
        code = units();
        if (!code) code = toolsAndMemory();
        if (!code) code = providers();
        if (!code) code = photoTests();
        if (!code) code = guests();
        if (!code) code = hedging();
        if (!code) code = conversations();
        if (!code) code = intelligence();
        if (!code) code = mindAndLife();
        if (!code) code = closeness();
    }
    if (code) std::cerr << "Companion test failed at checkpoint " << code << '\n';
    else std::cout << "Companion: done\n";
    return code;
}
