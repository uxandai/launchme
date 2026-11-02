#include "studios.hpp"
#include "search.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace studios {
namespace {
// One logo per studio, the newest when there are several. Names and prefixes are folded
// (studios::fold): lowercase, no punctuation, no legal endings.
const std::vector<Studio> table{
    {"2k", L"2K", L"publisher_2k_games.png", {L"2k"}, {L"2k games", L"2k australia", L"2k boston", L"2k china", L"2k czech", L"2k marin", L"2k play"}},
    {"2k-sports", L"2K Sports", L"publisher_2k_sports.png", {}, {L"2k sports"}},
    {"acclaim", L"Acclaim", L"publisher_acclaim.png", {}, {L"acclaim"}},
    {"accolade", L"Accolade", L"publisher_accolade.png", {}, {L"accolade"}},
    {"activision", L"Activision", L"publisher_activision.png", {}, {L"activision"}},
    {"amazon-games", L"Amazon Games", L"publisher_amazon_games.png", {L"amazon game studios"}, {L"amazon games"}},
    {"annapurna", L"Annapurna Interactive", L"publisher_annapurna.png", {}, {L"annapurna"}},
    {"atari", L"Atari", L"publisher_atari.png", {}, {L"atari"}},
    {"bandai-namco", L"Bandai Namco", L"publisher_bandai_namco.png", {}, {L"bandai namco", L"namco bandai", L"bandai"}},
    {"namco", L"Namco", L"publisher_namco.png", {}, {L"namco"}},
    {"bethesda", L"Bethesda", L"publisher_bethesda.png", {}, {L"bethesda"}},
    {"bitmap-brothers", L"The Bitmap Brothers", L"publisher_bitmap_brothers.png", {}, {L"the bitmap brothers", L"bitmap brothers"}},
    {"blizzard", L"Blizzard", L"publisher_blizzard.png", {}, {L"blizzard"}},
    {"broderbund", L"Br\u00f8derbund", L"publisher_broderbund.png", {}, {L"broderbund"}},
    {"bullfrog", L"Bullfrog", L"publisher_bullfrog.png", {}, {L"bullfrog"}},
    {"capcom", L"Capcom", L"publisher_capcom.png", {}, {L"capcom"}},
    {"codemasters", L"Codemasters", L"publisher_codemasters.png", {}, {L"codemasters"}},
    {"core-design", L"Core Design", L"publisher_core_design.png", {}, {L"core design"}},
    {"data-east", L"Data East", L"publisher_data_east.png", {}, {L"data east"}},
    {"disney", L"Disney", L"publisher_disney.png", {}, {L"disney"}},
    {"domark", L"Domark", L"publisher_domark.png", {}, {L"domark"}},
    {"ea-sports", L"EA Sports", L"publisher_ea_sports.png", {}, {L"ea sports"}},
    {"electronic-arts", L"Electronic Arts", L"publisher_electronic_arts_newer.png", {L"ea"}, {L"electronic arts", L"ea games", L"ea dice", L"ea digital illusions",
                                                                                                L"ea canada", L"ea vancouver", L"ea los angeles", L"ea originals",
                                                                                                L"ea redwood shores", L"ea tiburon", L"ea swiss"}},
    {"eidos", L"Eidos", L"publisher_eidos.png", {}, {L"eidos"}},
    {"epyx", L"Epyx", L"publisher_epyx.png", {}, {L"epyx"}},
    {"gaelco", L"Gaelco", L"publisher_gaelco.png", {}, {L"gaelco"}},
    {"gremlin", L"Gremlin", L"publisher_gremlin.png", {}, {L"gremlin graphics", L"gremlin interactive"}},
    {"infocom", L"Infocom", L"publisher_infocom.png", {}, {L"infocom"}},
    {"infogrames", L"Infogrames", L"publisher_infogrames_1996.png", {}, {L"infogrames"}},
    {"interplay", L"Interplay", L"publisher_interplay.png", {}, {L"interplay"}},
    {"irem", L"Irem", L"publisher_irem.png", {}, {L"irem"}},
    {"jaleco", L"Jaleco", L"publisher_jaleco.png", {}, {L"jaleco"}},
    {"kaneko", L"Kaneko", L"publisher_kaneko.png", {}, {L"kaneko"}},
    {"koei", L"Koei", L"publisher_koei.png", {}, {L"koei"}},
    {"konami", L"Konami", L"publisher_konami.png", {}, {L"konami"}},
    {"lucasarts", L"LucasArts", L"publisher_lucas_arts.png", {}, {L"lucasarts", L"lucas arts", L"lucasfilm"}},
    {"maxis", L"Maxis", L"publisher_maxis.png", {}, {L"maxis"}},
    {"microprose", L"MicroProse", L"publisher_microprose.png", {}, {L"microprose", L"micro prose"}},
    {"microsoft", L"Microsoft", L"publisher_microsoft_2012.png", {}, {L"microsoft", L"xbox game studios"}},
    {"midway", L"Midway", L"publisher_midway_new.png", {}, {L"midway"}},
    {"nintendo", L"Nintendo", L"publisher_nintendo.png", {}, {L"nintendo"}},
    {"ocean", L"Ocean", L"publisher_ocean.png", {L"ocean"}, {L"ocean software"}},
    {"psygnosis", L"Psygnosis", L"publisher_psygnosis.png", {}, {L"psygnosis"}},
    {"rockstar", L"Rockstar Games", L"publisher_rockstar.png", {}, {L"rockstar"}},
    {"sammy", L"Sammy", L"publisher_sammy.png", {}, {L"sammy"}},
    {"sega", L"Sega", L"publisher_sega.png", {}, {L"sega"}},
    {"sierra", L"Sierra", L"publisher_sierra.png", {}, {L"sierra"}},
    {"snk", L"SNK", L"publisher_snk.png", {}, {L"snk"}},
    {"sony", L"Sony", L"publisher_sony.png", {}, {L"sony", L"playstation pc", L"playstation publishing", L"playstation studios", L"playstation mobile"}},
    {"square-enix", L"Square Enix", L"publisher_square_enix.png", {}, {L"square enix", L"squaresoft", L"square soft"}},
    {"sunsoft", L"Sunsoft", L"publisher_sunsoft.png", {}, {L"sunsoft"}},
    {"taito", L"Taito", L"publisher_taito.png", {}, {L"taito"}},
    {"team17", L"Team17", L"publisher_team17.png", {}, {L"team17", L"team 17"}},
    {"technos", L"Techn\u00f4s Japan", L"publisher_technos.png", {}, {L"technos"}},
    {"tecmo", L"Tecmo", L"publisher_tecmo.png", {}, {L"tecmo"}},
    {"telltale", L"Telltale Games", L"publisher_telltale.png", {}, {L"telltale"}},
    {"thq", L"THQ", L"publisher_thq.png", {L"thq"}, {}}, // Not THQ Nordic: another logo.
    {"ubisoft", L"Ubisoft", L"publisher_ubisoft_2017.png", {}, {L"ubisoft", L"ubi soft"}},
    {"westwood", L"Westwood Studios", L"publisher_westwood_studios.png", {L"westwood"}, {L"westwood studios"}},
    {"williams", L"Williams", L"publisher_williams.png", {L"williams", L"williams electronics"}, {}},
};

// Legal endings that say nothing about who it is: "Capcom Co., Ltd." is Capcom.
constexpr std::wstring_view legal[]{L"inc", L"incorporated", L"llc", L"ltd", L"limited", L"co", L"corp", L"corporation", L"company",
                                    L"gmbh", L"ag", L"sa", L"sas", L"srl", L"spa", L"ab", L"as", L"oy", L"bv", L"nv", L"plc", L"pty",
                                    L"kk", L"k", L"sl", L"spolka", L"sp", L"z", L"o", L"oo", L"s", L"a"}; // "S.A." and "Sp. z o.o." fold to single letters.

bool begins(std::wstring_view text, std::wstring_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix &&
           (text.size() == prefix.size() || text[prefix.size()] == L' ');
}
} // namespace

const std::vector<Studio>& catalog() { return table; }

const Studio* byId(std::string_view id) {
    const auto found = std::find_if(table.begin(), table.end(), [&](const Studio& studio) { return studio.id == id; });
    return found == table.end() ? nullptr : &*found;
}

std::wstring fold(std::wstring_view name) {
    auto folded = foldGameTitle(name);
    for (bool cut = true; cut;) {
        cut = false;
        const auto space = folded.rfind(L' ');
        if (space == std::wstring::npos) break;
        const std::wstring_view last(folded.data() + space + 1, folded.size() - space - 1);
        if (std::find(std::begin(legal), std::end(legal), last) != std::end(legal)) { folded.resize(space); cut = true; }
    }
    return folded;
}

const Studio* match(std::wstring_view name) {
    const auto folded = fold(name);
    if (folded.empty()) return nullptr;
    for (const auto& studio : table)
        if (std::find(studio.names.begin(), studio.names.end(), folded) != studio.names.end()) return &studio;
    const Studio* best = nullptr;
    size_t longest = 0;
    for (const auto& studio : table)
        for (const auto prefix : studio.prefixes)
            if (prefix.size() > longest && begins(folded, prefix)) { best = &studio; longest = prefix.size(); }
    return best;
}

std::string key(Role role, std::string_view id) {
    return std::string(role == Role::publisher ? "publisher:" : "developer:") + std::string(id);
}

std::optional<std::pair<Role, const Studio*>> parse(std::string_view key) {
    for (const auto role : {Role::publisher, Role::developer}) {
        const std::string_view prefix = role == Role::publisher ? "publisher:" : "developer:";
        if (key.substr(0, prefix.size()) != prefix) continue;
        if (const auto* studio = byId(key.substr(prefix.size()))) return std::pair{role, studio};
    }
    return std::nullopt;
}

std::vector<Entry> group(const std::vector<GameNames>& games) {
    std::map<const Studio*, std::vector<std::string>> found;
    for (const auto& game : games) {
        std::set<const Studio*> seen;
        for (const auto& name : game.names)
            if (const auto* studio = match(name); studio && seen.insert(studio).second) found[studio].push_back(game.key);
    }
    std::vector<Entry> result;
    result.reserve(found.size());
    for (auto& [studio, keys] : found) result.push_back({studio, std::move(keys)});
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) {
        if (a.games.size() != b.games.size()) return a.games.size() > b.games.size();
        return a.studio->name < b.studio->name;
    });
    return result;
}
} // namespace studios
