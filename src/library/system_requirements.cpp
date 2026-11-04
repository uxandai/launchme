#include "system_requirements.hpp"
#include "hardware_rating.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <iterator>

namespace sysreq {
namespace {
bool digit(wchar_t c) { return c >= L'0' && c <= L'9'; }

std::wstring trim(std::wstring text) {
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0xA0; };
    while (!text.empty() && space(text.back())) text.pop_back();
    size_t first = 0;
    while (first < text.size() && space(text[first])) ++first;
    return text.substr(first);
}

// ASCII lower case, other characters become '?'.
std::string ascii(std::wstring_view text) {
    std::string result;
    for (const wchar_t c : text) result += c >= L'A' && c <= L'Z' ? static_cast<char>(c - L'A' + 'a') : c < 128 ? static_cast<char>(c) : '?';
    return result;
}

bool has(const std::string& text, std::initializer_list<std::string_view> parts) {
    return std::any_of(parts.begin(), parts.end(), [&](std::string_view part) { return text.find(part) != std::string::npos; });
}

// The page's HTML as lines of plain text: tags removed (a <br>, <li> or <p> ends a line), entities decoded.
std::vector<std::wstring> htmlLines(std::string_view html) {
    std::string text;
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '<') {
            const auto end = html.find('>', i);
            if (end == std::string_view::npos) break;
            auto tag = std::string(html.substr(i + 1, end - i - 1));
            std::transform(tag.begin(), tag.end(), tag.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
            if (!tag.empty() && tag[0] == '/') tag.erase(0, 1);
            const auto name = tag.substr(0, tag.find_first_of(" /"));
            if (name == "br" || name == "li" || name == "p" || name == "ul" || name == "h1" || name == "h2" || name == "h3" || name == "div" || name == "tr") text += '\n';
            i = end;
        } else if (html[i] == '&') {
            const auto end = html.find(';', i);
            const auto entity = end == std::string_view::npos || end - i > 8 ? std::string_view{} : html.substr(i + 1, end - i - 1);
            std::string replacement;
            bool known = true;
            if (entity == "reg" || entity == "trade" || entity == "copy") replacement = "";
            else if (entity == "amp") replacement = "&";
            else if (entity == "quot") replacement = "\"";
            else if (entity == "apos") replacement = "'";
            else if (entity == "lt") replacement = "<";
            else if (entity == "gt") replacement = ">";
            else if (entity == "nbsp") replacement = " ";
            else if (entity.size() > 1 && entity[0] == '#') {
                const int code = std::atoi(std::string(entity.substr(1)).c_str());
                if (code > 0 && code < 128) replacement = std::string(1, static_cast<char>(code));
                else replacement = " ";
            }
            else known = false;
            if (!known || entity.empty()) text += html[i];
            else { text += replacement; i = end; }
        } else text += html[i];
    }
    std::vector<std::wstring> lines;
    const auto wide = text_encoding::wideLenient(text);
    size_t start = 0;
    while (start <= wide.size()) {
        const auto end = wide.find(L'\n', start);
        auto line = trim(wide.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (!line.empty()) lines.push_back(std::move(line));
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return lines;
}

enum class Field { other, cpu, gpu, ram, storage };
// Which table row a label names, in the languages the store pages come in.
Field fieldOf(const std::wstring& label) {
    const auto name = ascii(label);
    if (has(name, {"graph", "grafi", "ficos", "fica", "video", "v?deo", "gpu"})) return Field::gpu;
    if (has(name, {"process", "procesor", "cpu"})) return Field::cpu;
    if (has(name, {"storage", "disk", "dysk", "miejsce", "almacen", "armazen", "espacio", "espa?o", "hdd"})) return Field::storage;
    if (has(name, {"memor", "pami", "ram"})) return Field::ram;
    return Field::other;
}

void put(Spec& spec, Field field, const std::wstring& raw) {
    auto value = trim(raw);
    // GOG repeats the label inside the text now and then ("Processsor: Intel Core 2 Duo").
    if (const auto colon = value.find(L':'); colon != std::wstring::npos && colon < 24 &&
        std::all_of(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(colon), [](wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L' '; }))
        value = trim(value.substr(colon + 1));
    if (value.empty()) return;
    switch (field) {
    case Field::cpu: if (spec.cpu.empty()) spec.cpu = value; break;
    case Field::gpu: if (spec.gpu.empty()) spec.gpu = value; break;
    case Field::ram: if (spec.ram.empty()) { spec.ram = value; spec.ram_mb = megabytesIn(value); } break;
    case Field::storage: if (spec.storage.empty()) { spec.storage = value; spec.storage_mb = megabytesIn(value); } break;
    case Field::other: break;
    }
}

// "OS: Windows 10", "Processor: Core i7-6700 or Ryzen 5 1600", ... one line each.
Spec specFromHtml(std::string_view html) {
    Spec spec;
    for (const auto& line : htmlLines(html)) {
        const auto colon = line.find(L':');
        if (colon == std::wstring::npos) continue;
        put(spec, fieldOf(line.substr(0, colon)), line.substr(colon + 1));
    }
    return spec;
}

std::string text(const json::Value* value) { return value ? value->stringOr() : std::string{}; }
}  // namespace

std::optional<uint32_t> megabytesIn(std::wstring_view text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (!digit(text[i]) || (i && (digit(text[i - 1]) || text[i - 1] == L'.' || text[i - 1] == L','))) continue;
        double value = 0;
        size_t at = i;
        while (at < text.size() && digit(text[at])) value = value * 10 + (text[at++] - L'0');
        if (at + 1 < text.size() && (text[at] == L'.' || text[at] == L',') && digit(text[at + 1])) {
            size_t places = 0;
            double fraction = 0;
            size_t cursor = at + 1;
            while (cursor < text.size() && digit(text[cursor])) { fraction = fraction * 10 + (text[cursor++] - L'0'); ++places; }
            if (places == 3 && text[at] == L',') value = value * 1000 + fraction;  // 1,024 MB
            else { value += fraction / std::pow(10.0, static_cast<double>(places)); }
            at = cursor;
        }
        while (at < text.size() && text[at] == L' ') ++at;
        std::string unit;
        const auto read_unit = [&] {
            unit.clear();
            while (at < text.size() && unit.size() < 4 && ((text[at] >= L'A' && text[at] <= L'Z') || (text[at] >= L'a' && text[at] <= L'z'))) {
                unit += static_cast<char>(text[at] >= L'a' ? text[at] - L'a' + 'A' : text[at] - L'A' + 'A'); ++at;
            }
        };
        read_unit();
        if (unit == "SSD" || unit == "HDD") { while (at < text.size() && text[at] == L' ') ++at; read_unit(); }  // "60 SSD GB"
        if (unit == "GB" || unit == "GIB") return static_cast<uint32_t>(value * 1024.0 + 0.5);
        if (unit == "MB" || unit == "MIB") return static_cast<uint32_t>(value + 0.5);
        if (unit == "TB") return static_cast<uint32_t>(value * 1024.0 * 1024.0 + 0.5);
        i = at;
    }
    return std::nullopt;
}

Requirements fromSteam(uint32_t app_id, std::string_view bytes) {
    Requirements result;
    const auto root = json::parse(bytes);
    const auto* entry = root ? root->get(std::to_string(app_id)) : nullptr;
    const auto* data = entry ? entry->get("data") : nullptr;
    const auto* needs = data && data->type == json::Value::Type::object ? data->get("pc_requirements") : nullptr;
    if (!needs || needs->type != json::Value::Type::object) return result;  // [] when the page has none.
    for (const auto [name, slot] : {std::pair{"minimum", &result.minimum}, std::pair{"recommended", &result.recommended}}) {
        const auto html = text(needs->get(name));
        if (html.empty()) continue;
        if (auto spec = specFromHtml(html); !spec.empty()) *slot = std::move(spec);
    }
    return result;
}

Requirements fromGog(std::string_view bytes) {
    Requirements result;
    const auto root = json::parse(bytes);
    const auto* embedded = root ? root->get("_embedded") : nullptr;
    const auto* systems = embedded ? embedded->get("supportedOperatingSystems") : nullptr;
    if (!systems || systems->type != json::Value::Type::array) return result;
    for (const auto& system : systems->elements) {
        const auto* os = system.get("operatingSystem");
        if (!os || text(os->get("name")) != "windows") continue;
        const auto* list = system.get("systemRequirements");
        if (!list || list->type != json::Value::Type::array) continue;
        for (const auto& column : list->elements) {
            const auto type = text(column.get("type"));
            auto& slot = type == "minimum" ? result.minimum : type == "recommended" ? result.recommended : result.minimum;
            if (type != "minimum" && type != "recommended") continue;
            Spec spec;
            if (const auto* fields = column.get("requirements"); fields && fields->type == json::Value::Type::array)
                for (const auto& field : fields->elements) {
                    const auto id = text(field.get("id"));
                    const auto value = text_encoding::wideLenient(text(field.get("description")));
                    put(spec, id == "processor" ? Field::cpu : id == "graphics" ? Field::gpu : id == "memory" ? Field::ram : id == "storage" ? Field::storage : Field::other, value);
                }
            if (spec.empty()) spec = specFromHtml(text(column.get("description")));
            if (!spec.empty()) slot = std::move(spec);
        }
    }
    return result;
}

namespace {
// "Core i7-6700 or Ryzen 5 1600" -> the options, brackets dropped; "A / B", "A, B" and "A or B" all separate.
std::vector<std::wstring> options(const std::wstring& text) {
    std::wstring plain;
    int depth = 0;
    for (const wchar_t c : text) {
        if (c == L'(') ++depth;
        else if (c == L')') { if (depth) --depth; }
        else if (!depth) plain += c;
    }
    std::wstring lower = plain;
    for (auto& c : lower) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    for (size_t at = lower.find(L" or "); at != std::wstring::npos; at = lower.find(L" or ", at + 1)) plain.replace(at, 4, L"\n   ");
    std::vector<std::wstring> parts;
    std::wstring current;
    for (const wchar_t c : plain) {
        if (c == L'/' || c == L',' || c == L';' || c == L'|' || c == L'\n') { if (!trim(current).empty()) parts.push_back(trim(current)); current.clear(); }
        else current += c;
    }
    if (!trim(current).empty()) parts.push_back(trim(current));
    return parts;
}

// The slowest of the options the rating knows ("A or B": either will do), if it knows any.
std::optional<float> threshold(const std::wstring& text, std::optional<float> (*score)(std::wstring_view)) {
    std::optional<float> lowest;
    for (const auto& option : options(text))
        if (const auto value = score(option)) lowest = lowest ? std::min(*lowest, *value) : *value;
    return lowest;
}

std::wstring gigabytes(uint64_t megabytes) {
    wchar_t buffer[32]{};
    if (megabytes >= 1024 * 10) swprintf(buffer, std::size(buffer), L"%llu GB", static_cast<unsigned long long>((megabytes + 512) / 1024));
    else if (megabytes >= 1024) { swprintf(buffer, std::size(buffer), L"%.1f GB", static_cast<double>(megabytes) / 1024.0); std::wstring t = buffer; if (t.size() > 5 && t.substr(t.size() - 5) == L".0 GB") return t.substr(0, t.size() - 5) + L" GB"; return t; }
    else swprintf(buffer, std::size(buffer), L"%llu MB", static_cast<unsigned long long>(megabytes));
    return buffer;
}

std::wstring shorten(const std::wstring& text, size_t limit = 120) {
    auto plain = trim(text);
    return plain.size() <= limit ? plain : plain.substr(0, limit - 1) + L"…";
}

// Scores: slower than 3/4 of the minimum is poor, up to 9/10 of it borderline (yellow); the
// recommended column (else 1.25 x the minimum) makes it green.
Level judgeScore(float mine, std::optional<float> lo, std::optional<float> hi) {
    if (lo && mine < *lo * 0.75f) return Level::poor;
    if (!lo && hi && mine < *hi * 0.5f) return Level::poor;
    if (lo && mine < *lo * 0.9f) return Level::ok;
    if (hi) return mine >= *hi * 0.9f ? Level::good : Level::ok;
    return mine >= *lo * 1.25f ? Level::good : Level::ok;
}

// Amounts (RAM, video memory) are exact: below the minimum is poor.
Level judgeAmount(double mine, std::optional<double> lo, std::optional<double> hi) {
    if (lo && mine + 0.5 < *lo) return Level::poor;
    if (hi) return mine + 0.5 >= *hi ? Level::good : Level::ok;
    return mine >= *lo * 1.25 ? Level::good : Level::ok;
}

std::wstring cleanName(std::wstring name) {
    for (const wchar_t* mark : {L"(R)", L"(TM)", L"(r)", L"(tm)", L"®", L"™"}) {
        for (size_t at = name.find(mark); at != std::wstring::npos; at = name.find(mark)) name.erase(at, std::wstring_view(mark).size());
    }
    std::wstring result;
    for (const wchar_t c : name) if (!(c == L' ' && !result.empty() && result.back() == L' ')) result += c;
    return trim(result);
}
}  // namespace

Verdict assess(const Machine& machine, const Requirements& needs, bool installed) {
    Verdict verdict;
    if (!needs.any()) return verdict;
    const Spec empty;
    const Spec& lo = needs.minimum ? *needs.minimum : empty;
    const Spec& hi = needs.recommended ? *needs.recommended : empty;
    const auto add = [&](Row::Part part, Level level, std::wstring yours, std::wstring minimum, std::wstring recommended) {
        verdict.rows.push_back({part, level, std::move(yours), shorten(minimum), shorten(recommended)});
    };

    // CPU.
    if (!lo.cpu.empty() || !hi.cpu.empty()) {
        const auto mine = hardware::cpuScore(machine.cpu);
        const auto low = threshold(lo.cpu, hardware::cpuScore), high = threshold(hi.cpu, hardware::cpuScore);
        Level level = Level::unknown;
        if (mine && (low || high)) level = judgeScore(*mine, low, high);
        add(Row::Part::cpu, level, cleanName(machine.cpu), lo.cpu, hi.cpu);
    }
    // GPU: a model, else (a card "with 1 GB") its video memory.
    if (!lo.gpu.empty() || !hi.gpu.empty()) {
        const auto mine = hardware::gpuScore(machine.gpu);
        const auto low = threshold(lo.gpu, hardware::gpuScore), high = threshold(hi.gpu, hardware::gpuScore);
        Level level = Level::unknown;
        std::wstring yours = cleanName(machine.gpu);
        if (mine && (low || high)) level = judgeScore(*mine, low, high);
        else if (!low && !high && machine.vram_mb) {
            const auto memory = [](const std::wstring& t) -> std::optional<double> {
                const auto name = ascii(t);
                if (!has(name, {"video", "vram", "v?deo"})) return std::nullopt;
                const auto mb = megabytesIn(t);
                return mb ? std::optional<double>(*mb) : std::nullopt;
            };
            const auto need_low = memory(lo.gpu), need_high = memory(hi.gpu);
            if (need_low || need_high) { level = judgeAmount(machine.vram_mb, need_low, need_high); yours += L"  ·  " + gigabytes(machine.vram_mb); }
        }
        add(Row::Part::gpu, level, yours, lo.gpu, hi.gpu);
    }
    // RAM: the installed amount rounded to whole gigabytes (Windows reports a little less than is fitted).
    if (lo.ram_mb || hi.ram_mb) {
        Level level = Level::unknown;
        std::wstring yours;
        if (machine.ram_mb) {
            const double nominal = std::max(1.0, std::round(machine.ram_mb / 1024.0)) * 1024.0;
            level = judgeAmount(nominal, lo.ram_mb ? std::optional<double>(*lo.ram_mb) : std::nullopt, hi.ram_mb ? std::optional<double>(*hi.ram_mb) : std::nullopt);
            yours = gigabytes(static_cast<uint64_t>(nominal));
        }
        add(Row::Part::ram, level, yours, lo.ram_mb ? gigabytes(*lo.ram_mb) : lo.ram, hi.ram_mb ? gigabytes(*hi.ram_mb) : hi.ram);
    }
    // Disk space: only for a game that is not installed yet.
    if (!installed && (lo.storage_mb || hi.storage_mb)) {
        const uint64_t need = std::max<uint64_t>(lo.storage_mb.value_or(0), hi.storage_mb.value_or(0));
        Level level = Level::unknown;
        if (machine.free_mb) level = machine.free_mb >= need ? Level::good : Level::poor;
        add(Row::Part::storage, level, machine.free_mb ? gigabytes(machine.free_mb) : std::wstring{},
            lo.storage_mb ? gigabytes(*lo.storage_mb) : lo.storage, hi.storage_mb ? gigabytes(*hi.storage_mb) : hi.storage);
    }

    // Overall: any sure failure is red; green or yellow needs both the CPU and the GPU judged.
    bool poor = false, ok = false, cpu = false, gpu = false;
    for (const auto& row : verdict.rows) {
        poor = poor || row.level == Level::poor;
        ok = ok || row.level == Level::ok;
        if (row.part == Row::Part::cpu && row.level != Level::unknown) cpu = true;
        if (row.part == Row::Part::gpu && row.level != Level::unknown) gpu = true;
    }
    verdict.level = poor ? Level::poor : (cpu && gpu) ? (ok ? Level::ok : Level::good) : Level::unknown;
    return verdict;
}
}  // namespace sysreq
