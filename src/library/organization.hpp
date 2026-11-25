#pragma once
#include "collections.hpp"
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

// Local overlays never change Steam's files. Steam IDs are scoped to their account;
// local collection IDs and favourites of other stores survive a Steam account change.
struct CollectionEdits {
    std::map<std::string, std::wstring> names;
    std::map<std::string, std::map<std::string, bool>> members;
    std::set<std::string> creates; // Account-scoped Steam collections created offline, awaiting native confirmation.
    // Kolekcje dynamiczne: an own collection's rules (smart_filter::encode) by its id ("local:…"). Its
    // games are the ones the rules match, then its member overrides (a game added or taken out by hand).
    std::map<std::string, std::string> rules;
    bool operator==(const CollectionEdits&) const = default;
};
// The games each dynamic collection's rules match now, by collection id: gameKeys. The launcher
// computes it (it knows the game index); the functions below take it as it is.
using SmartMembers = std::unordered_map<std::string, std::unordered_set<std::string>>;
inline bool favoriteCollection(const SteamCollection& c) { return c.id == "user-collections.favorite"; }
inline bool localCollection(const SteamCollection& c) { return c.id.starts_with("local:"); }
inline bool smartCollection(const CollectionEdits& edits, const SteamCollection& c) { return localCollection(c) && edits.rules.contains(c.id); }
std::vector<SteamCollection> managedCollections(uint64_t account, const std::vector<SteamCollection>& steam, const CollectionEdits& edits);
bool collectionContains(const CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game, const SmartMembers* smart = nullptr);
void setCollectionMember(CollectionEdits& edits, uint64_t account, const SteamCollection& c, const Game& game, bool member);
std::vector<uint32_t> localSteamMembers(const CollectionEdits& edits, uint64_t account);
std::vector<SteamShelf> managedShelves(uint64_t account, const std::vector<SteamCollection>& collections,
    const CollectionEdits& edits, const std::vector<Game>& visible, bool include_private = false, const SmartMembers* smart = nullptr);
