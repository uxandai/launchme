// "Znajdź" in Edytuj metadane (src/library/metadata_sources.cpp): the parsers on trimmed real answers of
// the stores, the title ID, dates and the searches through an injected transport (no network).
#include "../src/library/metadata_sources.hpp"

#include "../src/platform/text_encoding.hpp"

#include <iostream>
#include <string_view>

using namespace metadata_sources;

namespace {
HttpResponse reply(std::string body, uint32_t status = 200) {
    HttpResponse response;
    response.status = status;
    response.body.assign(body.begin(), body.end());
    return response;
}
constexpr const char* nintendo_star_fox = R"({"response":{"numFound":2,"docs":[
 {"title":"The Fox Awaits Me","application_id_s":"0100aaaa00000000","excerpt":"A cosy tale.","publisher":"Other","dates_released_dts":["2020-04-16T00:00:00Z"],"pretty_game_categories_txt":["Adventure"],"system_names_txt":["Nintendo Switch"]},
 {"title":"Star Fox","application_id_s":"0400e88025b1a000","excerpt":"Fox McCloud and his wingmen are back in Star Fox, only on Nintendo Switch 2!","product_catalog_description_s":"Longer","publisher":"Nintendo","dates_released_dts":["2026-06-25T00:00:00Z"],"pretty_date_s":"25/06/2026","pretty_game_categories_txt":["Shooter"],"system_names_txt":["Nintendo Switch 2"]}]}})";
constexpr const char* steam_search = R"({"total":2,"items":[{"type":"app","name":"ELDEN RING","id":1245620},{"type":"app","name":"ELDEN RING Shadow of the Erdtree","id":2778580}]})";
constexpr const char* steam_details = R"({"1245620":{"success":true,"data":{"name":"ELDEN RING","short_description":"THE CRITICALLY ACCLAIMED FANTASY ACTION RPG. &quot;Rise&quot;, Tarnished.","developers":["FromSoftware, Inc."],"publishers":["FromSoftware, Inc.","Bandai Namco Entertainment"],"genres":[{"id":"1","description":"Action"},{"id":"3","description":"RPG"}],"release_date":{"coming_soon":false,"date":"24 Feb, 2022"}}}})";
constexpr const char* gog_catalog = R"({"products":[{"id":"1207664643","title":"The Witcher 3: Wild Hunt","developers":["CD PROJEKT RED"],"publishers":["CD PROJEKT RED"],"genres":[{"name":"Role-playing","slug":"rpg"},{"name":"Adventure","slug":"adventure"}],"releaseDate":"2015.05.18"},{"id":"2","title":"Something Else","releaseDate":"2001.01.02"}]})";
constexpr const char* gog_game = R"({"overview":"\n\n<p>\nThe Witcher is a role-playing game.&nbsp;<br>\n<br>\nBecome The Witcher.</p><ul><li>One</li><li>Two</li></ul>","description":"x"})";
constexpr const char* xbox_suggest = R"({"Results":[{"Source":"Games","Products":[{"ProductId":"9NKX70BBCDRN","Title":"Forza Horizon 5 Standard Edition"},{"ProductId":"9P1HX37NMJLT","Title":"Forza Horizon 5 Deluxe Edition"}]}]})";
constexpr const char* xbox_products = R"({"Products":[{"ProductId":"9NKX70BBCDRN","LocalizedProperties":[{"ProductTitle":"Forza Horizon 5 Standard Edition","ShortDescription":"Limitless driving fun.","ProductDescription":"Your Ultimate Horizon Adventure awaits!","DeveloperName":"Playground Games","PublisherName":"Xbox Game Studios"}],"Properties":{"Category":"Racing & flying"},"MarketProperties":[{"OriginalReleaseDate":"2021-11-09T05:00:00.0000000Z"}]}]})";
constexpr const char* titledb = R"({"id":"0100F2C0115B6000","name":"The Legend of Zelda: Tears of the Kingdom","publisher":"Nintendo","description":"An epic adventure.\n\nSecond paragraph.","releaseDate":"2023-05-12","category":["Adventure","Action"],"developer":null,"intro":"Intro"})";
} // namespace

int main(int argc, char** argv) {
    // A diagnostic run on the real services: metadata_sources_tests --live "Star Fox" [Switch title ID] (nothing is checked).
    if (argc >= 3 && std::string_view(argv[1]) == "--live") {
        Query query;
        query.title = text_encoding::wideLenient(argv[2]);
        query.switch_id = argc >= 4 ? argv[3] : titleId(query.title);
        for (const auto source : all) {
            const auto found = find(source, query);
            std::cout << text_encoding::utf8Lenient(name(source)) << ": " << found.offers.size() << " found";
            if (!found.error.empty()) std::cout << " (" << text_encoding::utf8Lenient(found.error) << ")";
            std::cout << '\n';
            for (const auto& offer : found.offers)
                std::cout << "  [" << offer.score << "] " << text_encoding::utf8Lenient(offer.title) << " | " << text_encoding::utf8Lenient(offer.note) << " | dev: " << text_encoding::utf8Lenient(offer.details.developer)
                          << " | pub: " << text_encoding::utf8Lenient(offer.details.publisher) << " | " << text_encoding::utf8Lenient(offer.details.release_date) << " | " << text_encoding::utf8Lenient(offer.details.genres)
                          << "\n      " << text_encoding::utf8Lenient(offer.details.description.substr(0, 110)) << '\n';
        }
        return 0;
    }
    // IDs, dates, text.
    if (titleId(L"Star Fox [0400E88025B1A000].nup") != "0400E88025B1A000") return 1;
    if (titleId(L"Zelda [0100F2C0115B6800][v65536]") != "0100F2C0115B6000") return 2; // An update: its base game.
    if (!titleId(L"Game 1234567890123456").empty() || !titleId(L"plain name").empty()) return 3;
    if (isoDate("25/06/2026") != L"2026-06-25" || isoDate("2016.08.30") != L"2016-08-30" || isoDate("2021-11-09T05:00:00.0000000Z") != L"2021-11-09") return 4;
    if (isoDate("24 Feb, 2022") != L"2022-02-24" || isoDate("Feb 2022") != L"2022-02" || isoDate("2022") != L"2022" || !isoDate("Coming soon").empty() || !isoDate("").empty()) return 5;
    if (plainText("<p>One &amp; two&nbsp;three</p><p>Four<br>five</p><ul><li>a</li><li>b</li></ul>") != L"One & two three\n\nFour\nfive\n\n• a\n• b") return 6;

    // The Nintendo eShop: the title ID of the file name is an exact match; else the list is by title.
    Query star{L"Star Fox", "0400E88025B1A000"};
    auto offers = parseNintendo(nintendo_star_fox, star);
    if (offers.size() != 2) return 10;
    if (offers[1].title != L"Star Fox" || offers[1].score != 1000 || offers[1].details.release_date != L"2026-06-25" || offers[1].details.publisher != L"Nintendo" ||
        offers[1].details.genres != L"Shooter" || offers[1].note != L"Nintendo Switch 2" || offers[1].details.description.find(L"Fox McCloud") != 0) return 11;
    if (offers[0].score != 0) return 12;
    {
        std::vector<std::wstring> urls;
        const ArtworkHttp http = [&](const std::wstring& url, const std::wstring&, size_t) {
            urls.push_back(url);
            return url.find(L"application_id_s:0400e88025b1a000") != std::wstring::npos ? reply(R"({"response":{"docs":[{"title":"Star Fox","application_id_s":"0400e88025b1a000","excerpt":"Fox"}]}})") : reply("{}", 404);
        };
        const auto found = find(Source::nintendo, star, http);
        if (found.offers.size() != 1 || found.offers[0].title != L"Star Fox" || found.offers[0].score != 1000 || urls.size() != 1) return 13; // By ID alone: one request.
        const ArtworkHttp titled = [&](const std::wstring&, const std::wstring&, size_t) { return reply(nintendo_star_fox); };
        const auto by_title = find(Source::nintendo, Query{L"Star Fox"}, titled);
        if (by_title.offers.empty() || by_title.offers.front().title != L"Star Fox") return 14;
        const auto none = find(Source::nintendo, Query{L"Zzzz Unknown Thing"}, titled);
        if (!none.offers.empty() || none.error.empty()) return 15;
    }

    // Steam.
    const auto hits = parseSteamSearch(steam_search, Query{L"Elden Ring"});
    if (hits.empty() || hits.front().id != "1245620") return 20;
    const auto details = parseSteamDetails(steam_details, 1245620);
    if (!details || details->details.developer != L"FromSoftware, Inc." || details->details.publisher != L"FromSoftware, Inc., Bandai Namco Entertainment" ||
        details->details.genres != L"Action, RPG" || details->details.release_date != L"2022-02-24" || details->details.description.find(L"\"Rise\"") == std::wstring::npos) return 21;
    // A remaster or an edition of the game is still offered (the list shows its name); another game is not.
    {
        const auto remaster = parseSteamSearch(R"({"items":[{"name":"The Witcher 3: Wild Hunt - Remastered","id":292030},{"name":"Thronebreaker: The Witcher Tales","id":973760}]})", Query{L"The Witcher 3 Wild Hunt"});
        if (remaster.size() != 1 || remaster[0].id != "292030") return 25;
    }
    if (parseSteamDetails(R"({"1":{"success":false}})", 1) || parseSteamDetails("not json", 1)) return 22;
    {
        int requests = 0;
        const ArtworkHttp http = [&](const std::wstring& url, const std::wstring&, size_t) {
            ++requests;
            if (url.find(L"storesearch") != std::wstring::npos) return reply(steam_search);
            return url.find(L"appids=1245620") != std::wstring::npos ? reply(steam_details) : reply(R"({"2778580":{"success":false}})");
        };
        const auto found = find(Source::steam, Query{L"Elden Ring"}, http);
        if (found.offers.size() != 1 || found.offers[0].title != L"ELDEN RING" || requests < 2) return 23;
        // A known AppID skips the search.
        requests = 0;
        Query known{L"Anything"}; known.steam_app = 1245620;
        if (find(Source::steam, known, http).offers.size() != 1 || requests != 1) return 24;
    }

    // GOG.
    const auto gog = parseGogCatalog(gog_catalog, Query{L"The Witcher 3"});
    if (gog.size() != 2 || gog[0].id != "1207664643" || gog[0].details.genres != L"Role-playing, Adventure" || gog[0].details.release_date != L"2015-05-18" ||
        gog[0].details.developer != L"CD PROJEKT RED") return 30;
    if (parseGogDescription(gog_game) != L"The Witcher is a role-playing game.\n\nBecome The Witcher.\n\n• One\n• Two") return 31;
    {
        const ArtworkHttp http = [&](const std::wstring& url, const std::wstring&, size_t) {
            return url.find(L"catalog.gog.com") != std::wstring::npos ? reply(gog_catalog) : reply(gog_game);
        };
        const auto found = find(Source::gog, Query{L"The Witcher 3 Wild Hunt"}, http);
        if (found.offers.empty() || found.offers[0].id != "1207664643" || found.offers[0].details.description.find(L"role-playing") == std::wstring::npos) return 32;
    }

    // Microsoft Store.
    const auto suggest = parseXboxSuggest(xbox_suggest);
    if (suggest.size() != 2 || suggest[0].first != "9NKX70BBCDRN") return 40;
    const auto xbox = parseXboxProducts(xbox_products, Query{L"Forza Horizon 5"});
    if (xbox.size() != 1 || xbox[0].details.developer != L"Playground Games" || xbox[0].details.publisher != L"Xbox Game Studios" ||
        xbox[0].details.release_date != L"2021-11-09" || xbox[0].details.genres != L"Racing & flying" || xbox[0].details.description != L"Your Ultimate Horizon Adventure awaits!") return 41;
    {
        const ArtworkHttp http = [&](const std::wstring& url, const std::wstring&, size_t) { return url.find(L"autosuggest") != std::wstring::npos ? reply(xbox_suggest) : reply(xbox_products); };
        const auto found = find(Source::xbox, Query{L"Forza Horizon 5"}, http);
        if (found.offers.size() != 1 || found.offers[0].title != L"Forza Horizon 5 Standard Edition") return 42;
    }

    // TitleDB: only with a Switch title ID.
    const auto zelda = parseTitleDb(titledb);
    if (!zelda || zelda->details.genres != L"Adventure, Action" || zelda->details.release_date != L"2023-05-12" || zelda->details.description != L"An epic adventure.\n\nSecond paragraph." ||
        !zelda->details.developer.empty() || zelda->details.publisher != L"Nintendo") return 50;
    if (parseTitleDb(R"({"success":false,"error":"Game not found"})")) return 51;
    if (applicable(Source::titledb, Query{L"x"}) || applicable(Source::titledb, star) || !applicable(Source::titledb, Query{L"x", "0100F2C0115B6000"}) || !applicable(Source::steam, Query{L"x"})) return 52;
    {
        const ArtworkHttp http = [&](const std::wstring&, const std::wstring&, size_t) { return reply(titledb); };
        if (find(Source::titledb, Query{L"Zelda", "0100F2C0115B6000"}, http).offers.size() != 1 || find(Source::titledb, Query{L"Zelda"}, http).error.empty()) return 53;
    }

    // A failing transport is an error text, never a crash.
    {
        const ArtworkHttp down = [](const std::wstring&, const std::wstring&, size_t) { return reply("", 503); };
        for (const auto source : all) {
            Query query{L"Anything", "0100F2C0115B6000"};
            const auto found = find(source, query, down);
            if (!found.offers.empty() || found.error.empty()) return 60 + static_cast<int>(source);
        }
    }
    std::cout << "metadata_sources passed\n";
    return 0;
}
