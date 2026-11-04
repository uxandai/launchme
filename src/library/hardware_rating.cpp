#include "hardware_rating.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace hardware {
namespace {
using Words = std::vector<std::string>;

bool digit(char c) { return c >= '0' && c <= '9'; }
bool letter(char c) { return c >= 'a' && c <= 'z'; }

// Lower-case words of ASCII letters and digits; every other character separates.
Words split(std::wstring_view text) {
    Words words;
    std::string word;
    const auto flush = [&] { if (!word.empty()) { words.push_back(word); word.clear(); } };
    for (const wchar_t c : text) {
        if (c >= L'A' && c <= L'Z') word += static_cast<char>(c - L'A' + 'a');
        else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) word += static_cast<char>(c);
        else flush();
    }
    flush();
    return words;
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

// ---- GPUs ---------------------------------------------------------------------------------------

struct Model { const char* key; float score; };
// A GTX 1060 6GB is 100. Keys are the words left after the vendor and noise words are removed.
constexpr Model gpus[] = {
    // GeForce RTX 50 / 40 / 30 / 20.
    {"rtx 5090", 900}, {"rtx 5080", 650}, {"rtx 5070 ti", 560}, {"rtx 5070", 430}, {"rtx 5060 ti", 350}, {"rtx 5060", 300}, {"rtx 5050", 215},
    {"rtx 4090", 780}, {"rtx 4080 super", 600}, {"rtx 4080", 570}, {"rtx 4070 ti super", 500}, {"rtx 4070 ti", 450}, {"rtx 4070 super", 420},
    {"rtx 4070", 370}, {"rtx 4060 ti", 300}, {"rtx 4060", 255}, {"rtx 4050", 190},
    {"rtx 3090 ti", 450}, {"rtx 3090", 430}, {"rtx 3080 ti", 410}, {"rtx 3080", 360}, {"rtx 3070 ti", 305}, {"rtx 3070", 280},
    {"rtx 3060 ti", 245}, {"rtx 3060", 190}, {"rtx 3050", 135},
    {"rtx 2080 ti", 300}, {"rtx 2080 super", 260}, {"rtx 2080", 245}, {"rtx 2070 super", 220}, {"rtx 2070", 195}, {"rtx 2060 super", 185}, {"rtx 2060", 160},
    // GeForce GTX 16 / 10 / 9 / 7 / 6 / 5 / 4 / 2.
    {"gtx 1660 super", 135}, {"gtx 1660 ti", 140}, {"gtx 1660", 120}, {"gtx 1650 super", 98}, {"gtx 1650", 65},
    {"gtx 1080 ti", 230}, {"gtx 1080", 175}, {"gtx 1070 ti", 160}, {"gtx 1070", 140}, {"gtx 1060 3gb", 90}, {"gtx 1060", 100},
    {"gtx 1050 ti", 52}, {"gtx 1050", 40}, {"gt 1030", 24},
    {"gtx 980 ti", 110}, {"gtx 980", 84}, {"gtx 970", 72}, {"gtx 960", 45}, {"gtx 950", 38},
    {"gtx 780 ti", 72}, {"gtx 780", 62}, {"gtx 770", 46}, {"gtx 760", 36}, {"gtx 750 ti", 25}, {"gtx 750", 21}, {"gt 740", 12}, {"gt 730", 7},
    {"gtx 680", 46}, {"gtx 670", 40}, {"gtx 660 ti", 33}, {"gtx 660", 26}, {"gtx 650 ti boost", 24}, {"gtx 650 ti", 18}, {"gtx 650", 12}, {"gt 640", 9},
    {"gtx 590", 55}, {"gtx 580", 33}, {"gtx 570", 28}, {"gtx 560 ti", 22}, {"gtx 560", 19}, {"gtx 550 ti", 12},
    {"gtx 480", 27}, {"gtx 470", 21}, {"gtx 460", 14}, {"gts 450", 10},
    {"gtx 285", 15}, {"gtx 280", 14}, {"gtx 275", 13}, {"gtx 260", 11}, {"gts 250", 7},
    {"9800 gtx", 8}, {"9800 gt", 6}, {"9600 gt", 5}, {"8800 gtx", 8}, {"8800 gts", 6}, {"8800 gt", 6}, {"8600 gt", 2.5f},
    {"7950 gt", 3}, {"7900 gt", 3.5f}, {"7800 gtx", 3.5f}, {"7800 gt", 3}, {"7600 gt", 2}, {"6800", 1.8f}, {"6600", 1.2f},
    // Radeon RX 9000 / 7000 / 6000 / 5000 / Vega / 500 / 400.
    {"rx 9070 xt", 520}, {"rx 9070", 460}, {"rx 9060 xt", 310},
    {"rx 7900 xtx", 500}, {"rx 7900 xt", 440}, {"rx 7900 gre", 380}, {"rx 7800 xt", 340}, {"rx 7700 xt", 300}, {"rx 7600 xt", 215}, {"rx 7600", 195},
    {"rx 6950 xt", 390}, {"rx 6900 xt", 370}, {"rx 6800 xt", 340}, {"rx 6800", 300}, {"rx 6750 xt", 260}, {"rx 6700 xt", 245}, {"rx 6700", 215},
    {"rx 6650 xt", 195}, {"rx 6600 xt", 185}, {"rx 6600", 160}, {"rx 6500 xt", 85}, {"rx 6400", 75},
    {"rx 5700 xt", 175}, {"rx 5700", 150}, {"rx 5600 xt", 125}, {"rx 5500 xt", 90},
    {"vii", 210}, {"vega 64", 165}, {"vega 56", 150}, {"vega 11", 35}, {"vega 8", 25}, {"vega 3", 12},
    {"rx 590", 118}, {"rx 580", 108}, {"rx 570", 95}, {"rx 560", 55}, {"rx 550", 40}, {"rx 480", 100}, {"rx 470", 85}, {"rx 460", 50},
    // Radeon R9 / R7 / HD.
    {"r9 fury x", 140}, {"r9 fury", 130}, {"fury x", 140}, {"fury", 130}, {"r9 nano", 125}, {"r9 390x", 115}, {"r9 390", 105}, {"r9 380x", 80}, {"r9 380", 70},
    {"r9 295x2", 140}, {"r9 290x", 95}, {"r9 290", 85}, {"r9 285", 75}, {"r9 280x", 72}, {"r9 280", 65}, {"r9 270x", 60}, {"r9 270", 55},
    {"r7 370", 52}, {"r7 360", 40}, {"r7 265", 52}, {"r7 260x", 45}, {"r7 260", 38}, {"r7 250x", 28}, {"r7 250", 22}, {"r7 240", 14}, {"r5 230", 6},
    {"hd 7970", 68}, {"hd 7950", 58}, {"hd 7870", 52}, {"hd 7850", 40}, {"hd 7770", 28}, {"hd 7750", 22}, {"hd 7730", 13},
    {"hd 6990", 75}, {"hd 6970", 48}, {"hd 6950", 42}, {"hd 6870", 38}, {"hd 6850", 33}, {"hd 6790", 28}, {"hd 6770", 22}, {"hd 6750", 20}, {"hd 6670", 14},
    {"hd 5970", 55}, {"hd 5870", 38}, {"hd 5850", 32}, {"hd 5830", 26}, {"hd 5770", 20}, {"hd 5750", 17}, {"hd 5670", 12}, {"hd 5570", 8},
    {"hd 4890", 25}, {"hd 4870", 22}, {"hd 4850", 14}, {"hd 4670", 8}, {"hd 3870", 11}, {"hd 3850", 8}, {"hd 2900 xt", 10},
    {"x1950 xt", 7}, {"x1900", 6}, {"x1800", 5}, {"x850", 3}, {"x800", 3}, {"9800", 2},
    // Intel Arc and integrated graphics, AMD's integrated Radeons.
    {"arc b580", 235}, {"arc b570", 215}, {"arc a770", 190}, {"arc a750", 175}, {"arc a580", 140}, {"arc a380", 70},
    {"iris xe", 35}, {"iris plus", 25}, {"iris pro", 18}, {"uhd 770", 28}, {"uhd 750", 24}, {"uhd 730", 20}, {"uhd 630", 14}, {"uhd 620", 11}, {"uhd 610", 9},
    {"hd 630", 13}, {"hd 620", 10}, {"hd 530", 11}, {"hd 520", 9}, {"hd 4600", 9}, {"hd 4400", 7}, {"hd 4000", 6}, {"hd 3000", 3}, {"hd 2000", 2.5f},
    {"hd 5000", 11}, {"hd 5500", 10}, {"hd 6000", 14},
    {"890m", 95}, {"780m", 80}, {"760m", 55}, {"680m", 55}, {"660m", 40},
};

// Words a GPU name drops: vendors, marketing and "(R)" / "(TM)".
bool noise(const std::string& word) {
    static constexpr std::array<std::string_view, 17> list{"nvidia", "geforce", "amd", "radeon", "ati", "intel", "graphics", "series", "tm", "r", "gpu",
                                                            "card", "video", "edition", "oc", "gf", "the"};
    return std::find(list.begin(), list.end(), word) != list.end();
}

// "gtx1060" -> "gtx" "1060", "3060ti" -> "3060" "ti"; "970m" (a laptop part) stays whole.
Words gpuWords(std::wstring_view text) {
    Words raw = split(text), words;
    for (auto& word : raw) {
        if (noise(word)) continue;
        size_t letters = 0;
        while (letters < word.size() && letter(word[letters])) ++letters;
        if (letters >= 2 && letters < word.size() && digit(word[letters])) {
            words.push_back(word.substr(0, letters));
            word.erase(0, letters);
        }
        size_t digits = 0;
        while (digits < word.size() && digit(word[digits])) ++digits;
        const auto rest = word.substr(digits);
        if (digits >= 3 && !rest.empty() && (rest == "ti" || rest == "xt" || rest == "xtx" || rest == "gt" || rest == "gs" || rest == "gtx" || rest == "gre" || rest == "super")) {
            words.push_back(word.substr(0, digits));
            words.push_back(rest);
        } else words.push_back(word);
    }
    // "6 gb" -> "6gb", "512 mb" -> "512mb".
    Words joined;
    for (const auto& word : words) {
        if ((word == "gb" || word == "mb") && !joined.empty() && !joined.back().empty() && digit(joined.back().back()) &&
            std::all_of(joined.back().begin(), joined.back().end(), digit)) joined.back() += word;
        else joined.push_back(word);
    }
    return joined;
}

Words keyWords(std::string_view key) {
    Words words;
    std::string word;
    for (const char c : key) { if (c == ' ') { words.push_back(word); word.clear(); } else word += c; }
    words.push_back(word);
    return words;
}

bool hasRun(const Words& text, const Words& key) {
    return std::search(text.begin(), text.end(), key.begin(), key.end()) != text.end();
}

// ---- CPUs ---------------------------------------------------------------------------------------

struct Model2 { const char* key; float score; };
constexpr Model2 old_cpus[] = {
    {"core 2 quad", 42}, {"core 2 duo", 28}, {"core 2 extreme", 40}, {"pentium 4", 10}, {"pentium d", 20}, {"pentium dual core", 22}, {"pentium iii", 4},
    {"athlon 64 x2", 24}, {"athlon x2", 24}, {"athlon 64", 14}, {"athlon xp", 8}, {"athlon ii x4", 48}, {"athlon ii x3", 40}, {"athlon ii x2", 30},
    {"phenom ii x6", 66}, {"phenom ii x4", 55}, {"phenom ii x3", 46}, {"phenom ii x2", 38}, {"phenom x4", 40}, {"phenom x3", 34},
};

struct Number { int value = 0; size_t digits = 0; std::string suffix; };
// "8400" -> {8400, 4, ""}; "6700k" -> {6700, 4, "k"}; not a model number: digits 0.
Number number(const std::string& word) {
    Number result;
    while (result.digits < word.size() && digit(word[result.digits]) && result.digits < 6) {
        result.value = result.value * 10 + (word[result.digits] - '0');
        ++result.digits;
    }
    if (result.digits) result.suffix = word.substr(result.digits);
    return result;
}

float intelSuffix(const std::string& s) {
    if (s.empty() || s == "f") return 1.0f;
    if (s[0] == 'k' || s[0] == 'x') return 1.05f;
    if (s == "s") return 0.92f;
    if (s[0] == 't') return 0.8f;
    if (s == "hx") return 1.0f;
    if (s[0] == 'h') return 0.85f;
    if (s[0] == 'u' || s[0] == 'y') return 0.55f;
    if (s[0] == 'g') return 0.6f;
    if (s[0] == 'm' || s[0] == 'p') return s[0] == 'p' ? 0.7f : 0.5f;
    return 1.0f;
}

std::optional<float> intelCore(const Words& w) {
    // Core Ultra 5 245K.
    for (size_t i = 0; i + 2 < w.size(); ++i)
        if (w[i] == "ultra" && (w[i + 1] == "5" || w[i + 1] == "7" || w[i + 1] == "9") && digit(w[i + 2][0]))
            return (w[i + 1] == "5" ? 290.0f : w[i + 1] == "7" ? 340.0f : 400.0f) * intelSuffix(number(w[i + 2]).suffix);
    for (size_t i = 0; i + 1 < w.size(); ++i) {
        if (w[i].size() != 2 || w[i][0] != 'i' || (w[i][1] != '3' && w[i][1] != '5' && w[i][1] != '7' && w[i][1] != '9') || !digit(w[i + 1][0])) continue;
        const auto model = number(w[i + 1]);
        if (model.digits < 3 || model.digits > 5) return std::nullopt;
        int gen = model.digits == 3 ? 1 : model.value / 1000;
        if (model.digits == 4 && gen == 1 && !model.suffix.empty() && model.suffix[0] == 'g') gen = model.value / 100; // i7-1185G7: 11th gen.
        if (gen < 1 || gen > 15) return std::nullopt;
        static constexpr float base[] = {0, 45, 68, 75, 85, 88, 100, 106, 140, 150, 165, 185, 230, 270, 285, 300};
        const char tier = w[i][1];
        const float tier_factor = tier == '3' ? (gen >= 8 ? 0.75f : 0.62f) : tier == '5' ? 1.0f : tier == '7' ? (gen >= 8 ? 1.22f : 1.18f) : 1.5f;
        return base[gen] * tier_factor * intelSuffix(model.suffix);
    }
    return std::nullopt;
}

std::optional<float> ryzen(const Words& w) {
    for (size_t i = 0; i + 2 < w.size(); ++i) {
        if (w[i] != "ryzen" || w[i + 1].size() != 1) continue;
        const char tier = w[i + 1][0];
        if (tier != '3' && tier != '5' && tier != '7' && tier != '9') return std::nullopt;
        size_t at = i + 2;
        if (at < w.size() && w[at] == "pro") ++at;
        if (at >= w.size() || !digit(w[at][0])) return std::nullopt;
        const auto model = number(w[at]);
        if (model.digits != 4) return std::nullopt;
        const int gen = model.value / 1000;
        static constexpr float base[] = {0, 95, 112, 145, 140, 190, 200, 235, 225, 270};
        if (gen < 1 || gen > 9) return std::nullopt;
        const float tier_factor = tier == '3' ? 0.75f : tier == '5' ? 1.0f : tier == '7' ? 1.22f : 1.45f;
        const auto& s = model.suffix;
        const float suffix = s == "x3d" ? 1.25f : (!s.empty() && s[0] == 'x') ? 1.05f : s == "u" ? 0.6f : s == "g" ? 0.8f : (!s.empty() && s[0] == 'h') ? 0.85f : 1.0f;
        return base[gen] * tier_factor * suffix;
    }
    return std::nullopt;
}
}  // namespace

std::optional<float> gpuScore(std::wstring_view name) {
    const auto words = gpuWords(name);
    if (words.empty()) return std::nullopt;
    const Model* best = nullptr;
    size_t best_words = 0;
    for (const auto& model : gpus) {
        const auto key = keyWords(model.key);
        if (key.size() > best_words && hasRun(words, key)) { best = &model; best_words = key.size(); }
    }
    if (!best) return std::nullopt;
    std::string lower;
    for (const wchar_t c : name) lower += c >= L'A' && c <= L'Z' ? static_cast<char>(c - L'A' + 'a') : c < 128 ? static_cast<char>(c) : ' ';
    const bool laptop = contains(lower, "laptop") || contains(lower, "mobile") || contains(lower, "max-q") || contains(lower, "max q");
    return best->score * (laptop ? 0.75f : 1.0f);
}

std::optional<float> cpuScore(std::wstring_view name) {
    Words words = split(name), spread;
    for (const auto& word : words) {
        if (word == "core2") { spread.push_back("core"); spread.push_back("2"); }
        else if (word != "intel" && word != "amd" && word != "tm" && word != "r" && word != "cpu" && word != "processor") spread.push_back(word);
    }
    if (const auto score = intelCore(spread)) return score;
    if (const auto score = ryzen(spread)) return score;
    // AMD FX-8350: the thousands digit tells the line.
    for (size_t i = 0; i + 1 < spread.size(); ++i) {
        if (spread[i] != "fx" || !digit(spread[i + 1][0])) continue;
        const auto model = number(spread[i + 1]);
        if (model.digits != 4) break;
        const int line = model.value / 1000;
        if (line == 4) return 60.0f;
        if (line == 6) return 72.0f;
        if (line == 8) return 88.0f;
        if (line == 9) return 98.0f;
        break;
    }
    const Words joined = spread;
    const Model2* best = nullptr;
    size_t best_words = 0;
    for (const auto& model : old_cpus) {
        const auto key = keyWords(model.key);
        if (key.size() > best_words && hasRun(joined, key)) { best = &model; best_words = key.size(); }
    }
    return best ? std::optional<float>(best->score) : std::nullopt;
}
}  // namespace hardware
