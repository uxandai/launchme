#include "metadata_overrides.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <span>
#include <vector>

namespace metadata {
namespace {
constexpr size_t max_file = 4 * 1024 * 1024;
const std::pair<const char*, std::wstring Override::*> fields[] = {
    {"title", &Override::title}, {"description", &Override::description}, {"developer", &Override::developer},
    {"publisher", &Override::publisher}, {"release_date", &Override::release_date}, {"genres", &Override::genres}, {"hours", &Override::hours},
};
} // namespace

bool Overrides::load(const std::filesystem::path& file) {
    items_.clear();
    readable_ = false;
    std::error_code ec;
    const bool exists = std::filesystem::exists(file, ec);
    if (ec) return false;
    if (!exists) return readable_ = true; // None yet.
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(max_file)) return false;
    std::string text(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(text.size()))) return false;
    const auto root = json::parse(text, max_file);
    const auto* games = root ? root->get("games") : nullptr;
    if (!games || games->type != json::Value::Type::object) return false;
    readable_ = true;
    for (const auto& [key, entry] : games->properties) {
        Override value;
        for (const auto& [name, member] : fields)
            if (const auto* field = entry.get(name)) value.*member = text_encoding::wideLenient(field->stringOr());
        if (const auto* flag = entry.get("looked_up")) value.looked_up = flag->type == json::Value::Type::boolean && flag->flag;
        const auto minutes = [&](const char* name) -> std::optional<uint32_t> {
            const auto* field = entry.get(name);
            if (!field || field->type != json::Value::Type::number || field->numeric < 0 || field->numeric > 6000000) return std::nullopt;
            return static_cast<uint32_t>(field->numeric);
        };
        value.playtime = minutes("playtime");
        if (value.playtime) value.playtime_base = minutes("playtime_base").value_or(0);
        if (!key.empty() && !value.empty()) items_[key] = std::move(value);
    }
    return true;
}

bool Overrides::save(const std::filesystem::path& file) {
    if (!readable_) {
        // Unreadable when loaded: read again, this session's entries over the file's own; still
        // unreadable, the file is kept beside the new one rather than replaced unseen.
        Overrides stored;
        if (stored.load(file)) {
            for (auto& [key, value] : items_) stored.items_[key] = std::move(value);
            items_ = std::move(stored.items_);
        } else {
            auto kept = file;
            kept.replace_extension(L".unreadable.json");
            std::error_code ec;
            std::filesystem::copy_file(file, kept, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) return false;
        }
        readable_ = true;
    }
    auto games = json::object();
    for (const auto& [key, value] : items_) {
        auto entry = json::object();
        for (const auto& [name, member] : fields)
            if (!(value.*member).empty()) entry.properties[name] = json::string(text_encoding::utf8Lenient(value.*member));
        if (value.looked_up) entry.properties["looked_up"] = json::boolean(true);
        if (value.playtime) {
            entry.properties["playtime"] = json::number(static_cast<double>(*value.playtime));
            entry.properties["playtime_base"] = json::number(static_cast<double>(value.playtime_base));
        }
        games.properties[key] = std::move(entry);
    }
    auto root = json::object();
    root.properties["version"] = json::number(1);
    root.properties["games"] = std::move(games);
    const auto text = json::serialize(root);
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    return atomic_file::writeDurable(file, text); // The player's own edits: flushed before the rename.
}

const Override* Overrides::find(const std::string& key) const {
    const auto found = items_.find(key);
    return found == items_.end() ? nullptr : &found->second;
}

void Overrides::set(const std::string& key, Override value) {
    if (key.empty()) return;
    if (value.empty()) items_.erase(key);
    else items_[key] = std::move(value);
}

bool Overrides::erase(const std::string& key) { return items_.erase(key) > 0; }

uint32_t playtime(const Override& value, uint32_t automatic) {
    if (!value.playtime) return automatic;
    const uint64_t since = automatic > value.playtime_base ? automatic - value.playtime_base : 0;
    return static_cast<uint32_t>(std::min<uint64_t>(UINT32_MAX, *value.playtime + since));
}

std::optional<uint32_t> parsePlaytime(std::wstring_view text) {
    std::vector<double> numbers;
    bool fraction = false, colon = false;
    for (size_t i = 0; i < text.size() && numbers.size() < 3;) {
        if (text[i] == L':' && !numbers.empty()) colon = true;
        if (!std::iswdigit(text[i])) { ++i; continue; }
        double value = 0;
        while (i < text.size() && std::iswdigit(text[i]) && value < 1e7) value = value * 10 + (text[i++] - L'0');
        if (i + 1 < text.size() && (text[i] == L',' || text[i] == L'.') && std::iswdigit(text[i + 1])) {
            fraction = true;
            double scale = 0.1;
            for (++i; i < text.size() && std::iswdigit(text[i]); ++i) { value += (text[i] - L'0') * scale; scale *= 0.1; }
        }
        numbers.push_back(value);
    }
    if (numbers.empty() || numbers.size() > 2) return std::nullopt;
    std::wstring folded(text);
    for (auto& c : folded) c = static_cast<wchar_t>(std::towlower(c));
    double minutes = 0;
    if (numbers.size() == 2) {
        if (fraction || numbers[1] >= 60) return std::nullopt;
        minutes = numbers[0] * 60 + numbers[1]; // "12:30", "12 h 30 min".
    } else if (!colon && folded.find(L"min") != std::wstring::npos && folded.find(L'h') == std::wstring::npos &&
               folded.find(L"godz") == std::wstring::npos) minutes = numbers[0];   // "45 min".
    else minutes = numbers[0] * 60;                                                 // "12", "12,5", "12 h".
    if (minutes < 0 || minutes > 100000.0 * 60) return std::nullopt;
    return static_cast<uint32_t>(std::lround(minutes));
}

std::wstring playtimeText(uint32_t minutes) {
    const auto rest = minutes % 60;
    return std::to_wstring(minutes / 60) + (rest < 10 ? L":0" : L":") + std::to_wstring(rest);
}

void apply(const Override& value, GameDetails& details) {
    if (!value.description.empty()) details.description = value.description;
    if (!value.developer.empty()) details.developer = value.developer;
    if (!value.publisher.empty()) details.publisher = value.publisher;
    if (!value.release_date.empty()) details.release_date = value.release_date;
    if (!value.genres.empty()) details.genres = value.genres;
}

} // namespace metadata
