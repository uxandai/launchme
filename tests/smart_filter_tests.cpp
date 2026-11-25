#include "../src/library/smart_filter.hpp"
#include "../src/library/organization.hpp"
#include "../src/platform/library_state.hpp"
#include <Windows.h>
#include <filesystem>
#include <iostream>

using smart_filter::Field;
using smart_filter::Rule;
using smart_filter::Rules;

namespace {
smart_filter::Facts game(std::wstring title, std::vector<std::wstring> studios = {}, std::vector<std::wstring> labels = {}) {
    smart_filter::Facts facts;
    facts.title = std::move(title); facts.studios = std::move(studios); facts.labels = std::move(labels);
    facts.store = "steam";
    return facts;
}
int fail(int code, const char* what) { std::cerr << "smart_filter " << code << ": " << what << '\n'; return code; }
}

int main() {
    // "tekken" but not "tag tournament": the owner's example.
    const Rules tekken{{Field::title, false, L"tekken"}, {Field::title, true, L"Tag Tournament"}};
    if (!smart_filter::matches(tekken, game(L"TEKKEN 7"))) return fail(1, "tekken 7");
    if (smart_filter::matches(tekken, game(L"Tekken Tag Tournament 2"))) return fail(2, "tag tournament excluded");
    if (smart_filter::matches(tekken, game(L"Soulcalibur VI"))) return fail(3, "other title");
    // Punctuation and case fold away; a comma lists alternatives.
    if (!smart_filter::matches(Rule{Field::title, false, L"street fighter, soulcalibur"}, game(L"SOULCALIBUR™ VI"))) return fail(4, "alternatives");
    if (smart_filter::matches(Rule{Field::title, true, L"street fighter, soulcalibur"}, game(L"Street Fighter 6"))) return fail(5, "none of");
    // A publisher's games, but co-op only.
    auto co_op = game(L"Monster Hunter: World", {L"CAPCOM Co., Ltd."}, {L"Action", L"Co-op"});
    co_op.modes = 1u << static_cast<int>(library_view::Filter::coop);
    auto solo = game(L"Resident Evil 4", {L"CAPCOM Co., Ltd."}, {L"Action", L"Survival Horror"});
    const Rules capcom_coop{{Field::studio, false, L"capcom"}, {Field::mode, false, L"coop"}};
    if (!smart_filter::matches(capcom_coop, co_op)) return fail(6, "capcom co-op");
    if (smart_filter::matches(capcom_coop, solo)) return fail(7, "capcom solo");
    // A Polish genre finds the index's English one.
    if (!smart_filter::matches(Rule{Field::label, false, L"przygodowe"}, game(L"Outer Wilds", {}, {L"Adventure"})) &&
        !smart_filter::matches(Rule{Field::label, false, L"adventure"}, game(L"Outer Wilds", {}, {L"Adventure"}))) return fail(8, "genre");
    // Numbers: an unknown value matches neither side.
    auto rated = game(L"Hades"); rated.rating = 93.0f; rated.minutes = 22 * 60; rated.year = 2020;
    const auto unknown = game(L"Unknown");
    if (!smart_filter::matches(Rule{Field::rating, false, L"90"}, rated) || smart_filter::matches(Rule{Field::rating, true, L"90"}, rated)) return fail(9, "rating");
    if (smart_filter::matches(Rule{Field::rating, false, L"50"}, unknown) || smart_filter::matches(Rule{Field::rating, true, L"50"}, unknown)) return fail(10, "unknown rating");
    if (!smart_filter::matches(Rule{Field::length, false, L"30"}, rated) || smart_filter::matches(Rule{Field::length, false, L"10"}, rated)) return fail(11, "length");
    if (!smart_filter::matches(Rule{Field::year, false, L"2018"}, rated) || !smart_filter::matches(Rule{Field::year, true, L"2020"}, rated)) return fail(12, "year");
    // Choices: store and status.
    if (!smart_filter::matches(Rule{Field::store, false, L"steam"}, rated) || smart_filter::matches(Rule{Field::store, true, L"steam"}, rated)) return fail(13, "store");
    rated.installed = true;
    if (!smart_filter::matches(Rule{Field::status, false, L"installed"}, rated) || smart_filter::matches(Rule{Field::status, false, L"played"}, rated)) return fail(14, "status");
    // Validity: an unfinished rule hides nothing; bad numbers and unknown choices are invalid.
    if (smart_filter::valid(Rule{Field::title, false, L" , "}) || !smart_filter::matches(Rule{Field::title, false, L""}, unknown)) return fail(15, "empty text");
    if (smart_filter::valid(Rule{Field::rating, false, L"150"}) || smart_filter::valid(Rule{Field::year, false, L"abc"}) ||
        smart_filter::valid(Rule{Field::mode, false, L"nope"}) || !smart_filter::valid(Rule{Field::length, false, L"10h"})) return fail(16, "valid");
    if (smart_filter::describe(tekken[1]).find(L"Tag Tournament") == std::wstring::npos) return fail(17, "describe");
    // Encode and decode round-trip, Polish letters included; damage is rejected.
    const Rules mixed{{Field::title, false, L"Wiedźmin"}, {Field::rating, true, L"70"}, {Field::mode, false, L"coop"}, {Field::label, true, L"horror, gore"}};
    const auto encoded = smart_filter::encode(mixed);
    const auto decoded = smart_filter::decode(encoded);
    if (!decoded || *decoded != mixed) return fail(18, "round trip");
    if (!smart_filter::decode("") || !smart_filter::decode("")->empty()) return fail(19, "empty rules");
    if (smart_filter::decode("x=abc") || smart_filter::decode("t?abc") || smart_filter::decode("t")) return fail(20, "damaged");

    // Kolekcje dynamiczne: the rules' matches are members, a hand override wins, and the rules persist.
    Game a; a.name = L"Tekken 7"; a.source = GameSource::steam; a.app_id = 389730;
    Game b; b.name = L"Tekken Tag Tournament 2"; b.source = GameSource::steam; b.app_id = 9999;
    CollectionEdits edits;
    const SteamCollection smart{L"Tekken", {}, "local:abc"};
    edits.names[collectionKey(1, smart)] = L"Tekken";
    edits.rules[smart.id] = encoded;
    const SmartMembers members{{smart.id, {gameKey(a)}}};
    if (!smartCollection(edits, smart)) return fail(21, "smart collection");
    if (!collectionContains(edits, 1, smart, a, &members) || collectionContains(edits, 1, smart, b, &members)) return fail(22, "smart members");
    setCollectionMember(edits, 1, smart, b, true);
    if (!collectionContains(edits, 1, smart, b, &members)) return fail(23, "hand override");
    const auto shelves = managedShelves(1, {smart}, edits, {a, b}, false, &members);
    if (shelves.size() != 1 || shelves[0].games.size() != 2) return fail(24, "smart shelf");

    const auto root = std::filesystem::temp_directory_path() / (L"launchme-smart-filter-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    std::filesystem::create_directories(root);
    LibraryState state;
    state.collections = edits;
    const auto file = root / L"library-state.ini";
    if (!saveLibraryState(state, file)) return fail(25, "save");
    const auto loaded = loadLibraryState(file);
    if (!loaded || loaded->collections.rules != edits.rules) return fail(26, "load rules");
    std::cout << "smart_filter: ok\n";
    return 0;
}
