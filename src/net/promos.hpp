#pragma once
// The Stores' Promotions tab (src/app/store_hub.cpp): games that are free to keep right now, the ones
// announced to be free next, and discounts, read from the stores' own public pages:
//   Epic Games   store-site-backend-static.ak.epicgames.com/freeGamesPromotions (this week's and next week's free games)
//   Steam        store.steampowered.com/api/featuredcategories (its "specials")
//   GOG          catalog.gog.com/v1/catalog (discounted, trending first)
//   GamerPower   gamerpower.com/api/giveaways (free-to-keep PC games across stores; named on each of its tiles)
// No account and no key; the requests carry the region (country, currency) and language only. Bodies are
// cached for 30 minutes in <config>/store-hub/promos; a source that fails keeps its last copy.
// Parsing is pure and never throws: a changed or damaged answer yields no offers from that source.
#include "http.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace promos {
enum class Store { epic, steam, gog, other };

struct Offer {
    Store store = Store::other;
    std::wstring title;
    std::wstring url;      // Its store page (https).
    std::wstring image;    // A wide picture (https); may be empty.
    std::wstring where;    // "Epic Games", "Steam", "GOG", or the giveaway's platforms and source ("Itch.io, DRM-Free · GamerPower").
    std::wstring price;    // What it costs now, as the store writes it; empty when unknown or free.
    std::wstring regular;  // Its usual price; empty when unknown.
    int discount = 0;      // Percent off; 100 for a free game.
    bool free = false;     // Free to keep.
    int64_t starts = 0;    // Unix seconds; a game announced to be free begins then.
    int64_t ends = 0;      // 0: unknown.
    bool operator==(const Offer&) const = default;
};

struct Feed {
    std::vector<Offer> free_now, free_soon, deals;
    bool unavailable = false; // Nothing could be read, not even a cached copy.
    bool empty() const { return free_now.empty() && free_soon.empty() && deals.empty(); }
};

struct Region {
    std::string country = "US";   // ISO 3166-1 alpha-2.
    std::string currency = "USD"; // ISO 4217.
    bool polish = false;          // The interface language: Polish store pages, else English.
};

// This machine's region (Windows: the user's location and currency; Linux: the locale's country).
Region systemRegion(bool polish);
// A smaller copy of a store picture where its CDN makes one (Epic, GOG); other addresses unchanged.
std::wstring tilePicture(std::wstring url);

std::vector<Offer> parseEpic(std::string_view body, int64_t now, const Region& region);
std::vector<Offer> parseSteam(std::string_view body);
std::vector<Offer> parseGog(std::string_view body);
std::vector<Offer> parseGamerPower(std::string_view body, int64_t now);
// One feed: giveaways the stores list themselves are not repeated, discounts go deepest first.
Feed combine(std::vector<Offer> epic, std::vector<Offer> steam, std::vector<Offer> gog, std::vector<Offer> giveaways, int64_t now);

// "2026-11-05T16:00:00.000Z" or "2026-11-12 23:59:00" (UTC) as Unix seconds; 0 when it is neither.
int64_t parseTime(std::string_view text);
// 7649, "PLN" → "76,49 zł"; other currencies keep their code ("12.99 USD" as "$12.99", "€12,99").
std::wstring money(int64_t cents, std::string_view currency);
// Titles compared across stores: lowercase letters and digits only.
std::wstring titleKey(std::wstring_view title);

using Get = std::function<HttpResponse(const std::wstring& url)>;
inline constexpr int64_t cache_seconds = 30 * 60;
// Blocking (a worker's): each source from its fresh cached body, else the network, else its stale copy.
Feed load(const std::filesystem::path& cache, const Region& region, bool refresh, int64_t now, const Get& get = {});
}
