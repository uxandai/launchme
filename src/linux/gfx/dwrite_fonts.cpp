// DirectWrite fonts on fontconfig and FreeType (Impeller's typography reads the same system fonts):
// the application fonts registered with it, family
// names (Windows families the launcher asks for map to installed Linux stand-ins), the system and
// file-built collections, and font metrics in design units.
#include "dwrite_internal.hpp"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include <fontconfig/fcfreetype.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <set>
#include <span>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace lm_dwrite {
namespace {
// fontconfig and FreeType are used from any thread; their calls are serialized.
std::recursive_mutex& fcLock() { static std::recursive_mutex lock; return lock; }

// ASCII case folding: family names DirectWrite compares ignoring case.
std::string fold(std::string name) {
    for (char& c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

// Windows families and their stand-ins, tried in order after the family itself. The last of
// each list is a fontconfig generic, which always resolves to some installed font.
constexpr const char* sans_names[] = {"Segoe UI", "Segoe UI Variable Display", "Segoe UI Variable", "Arial", "Calibri", "Tahoma", "Verdana"};
constexpr const char* sans_fallback[] = {"Inter", "Noto Sans", "Cantarell", "DejaVu Sans", "sans-serif"};
constexpr const char* print_names[] = {"Segoe Print"};
constexpr const char* print_fallback[] = {"Comic Neue", "Noto Sans", "sans-serif"};
constexpr const char* serif_names[] = {"Georgia", "Cambria", "Constantia", "Palatino Linotype", "Book Antiqua", "Gabriola"};
constexpr const char* serif_fallback[] = {"Noto Serif", "DejaVu Serif", "serif"};
constexpr const char* mono_names[] = {"Consolas"};
constexpr const char* mono_fallback[] = {"Noto Sans Mono", "DejaVu Sans Mono", "monospace"};
// The icon fonts: the launcher's own (res/fonts/launchme-icons.ttf, beside the program), whose
// icons sit at Segoe MDL2's code points.
constexpr const char* icon_names[] = {"Segoe Fluent Icons", "Segoe MDL2 Assets"};
constexpr const char* icon_family = "Launchme Icons";
constexpr const char* icon_fallback[] = {icon_family};
struct Alias { std::span<const char* const> names, fallback; };
constexpr Alias aliases[] = {{sans_names, sans_fallback}, {print_names, print_fallback}, {serif_names, serif_fallback},
                             {mono_names, mono_fallback}, {icon_names, icon_fallback}};
// A family DirectWrite does not know falls back to the system's UI font.
constexpr const Alias& default_alias = aliases[0];
constexpr const Alias& icon_alias = aliases[4];

bool bundledIcons();

// Windows' icon fonts are known only when the launcher's icon font is there to stand in.
const Alias* aliasOf(const std::string& folded) {
    for (const auto& alias : aliases)
        for (const char* name : alias.names)
            if (fold(name) == folded) return &alias != &icon_alias || bundledIcons() ? &alias : nullptr;
    return nullptr;
}

bool generic(const char* family) {
    return !std::strcmp(family, "sans-serif") || !std::strcmp(family, "serif") || !std::strcmp(family, "monospace");
}

// Casefolded names of the installed (system, not application) font families.
bool installed(const std::string& folded) {
    static const auto families = [] {
        std::lock_guard fc(fcLock());
        std::unordered_set<std::string> result;
        FcInit();
        if (const FcFontSet* fonts = FcConfigGetFonts(nullptr, FcSetSystem))
            for (int i = 0; i < fonts->nfont; ++i) {
                FcChar8* name = nullptr;
                for (int n = 0; FcPatternGetString(fonts->fonts[i], FC_FAMILY, n, &name) == FcResultMatch; ++n)
                    result.insert(fold(reinterpret_cast<const char*>(name)));
            }
        return result;
    }();
    return families.count(folded) != 0;
}

// The family fontconfig picks for a generic name (or any name it can only approximate); empty when the
// system has no font for it to pick.
std::string concreteFamily(const std::string& name) {
    std::lock_guard fc(fcLock());
    std::string result;
    FcInit();
    if (FcPattern* pattern = FcPatternCreate()) {
        FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(name.c_str()));
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult status;
        if (FcPattern* match = FcFontMatch(nullptr, pattern, &status)) {
            FcChar8* family = nullptr;
            if (FcPatternGetString(match, FC_FAMILY, 0, &family) == FcResultMatch) result = reinterpret_cast<const char*>(family);
            FcPatternDestroy(match);
        }
        FcPatternDestroy(pattern);
    }
    return result;
}

std::string lastResortFamily();

std::string resolveSystem(const std::string& family) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::string> resolved;
    const std::string key = fold(family);
    std::lock_guard lock(mutex);
    if (const auto found = resolved.find(key); found != resolved.end()) return found->second;
    std::string result = family;
    if (const Alias* alias = aliasOf(key); alias == &icon_alias) {
        result = icon_family;
    } else if (!installed(key)) {
        const auto fallback = (alias ? *alias : default_alias).fallback;
        const auto chosen = std::find_if(fallback.begin(), fallback.end(), [](const char* name) { return generic(name) || installed(fold(name)); });
        result = chosen != fallback.end() ? *chosen : fallback.back();
        if (generic(result.c_str())) {
            const auto concrete = concreteFamily(result);
            if (!concrete.empty()) result = concrete;
            else if (const auto last_resort = lastResortFamily(); !last_resort.empty()) result = last_resort;
        }
    }
    resolved.emplace(key, result);
    return result;
}

// One face of an application font file.
struct Face {
    std::string path;
    int index = 0;
    std::vector<std::string> families;
    int weight = 400, width = 100;
    bool italic = false;
};

// Application fonts (files of IDWriteFontSet collections): their faces.
struct AppFonts {
    std::mutex mutex;
    std::set<std::string> files;
    std::vector<Face> faces;
    std::atomic<unsigned> generation{0};
};
AppFonts& appFonts() { static AppFonts fonts; return fonts; }

// The faces of a font file with every family name they answer to (typographic and legacy, every language).
std::vector<Face> facesOf(const std::string& path) {
    std::lock_guard fc(fcLock());
    std::vector<Face> faces;
    FcInit();
    for (int index = 0; index < 64; ++index) {
        int count = 0;
        FcPattern* pattern = FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(path.c_str()), index, nullptr, &count);
        if (!pattern) break;
        Face face;
        face.path = path;
        face.index = index;
        FcChar8* name = nullptr;
        for (int n = 0; FcPatternGetString(pattern, FC_FAMILY, n, &name) == FcResultMatch; ++n) {
            const std::string family = reinterpret_cast<const char*>(name);
            if (std::find(face.families.begin(), face.families.end(), family) == face.families.end()) face.families.push_back(family);
        }
        int value = 0;
        if (FcPatternGetInteger(pattern, FC_WEIGHT, 0, &value) == FcResultMatch) face.weight = FcWeightToOpenType(value);
        if (FcPatternGetInteger(pattern, FC_WIDTH, 0, &value) == FcResultMatch) face.width = value;
        if (FcPatternGetInteger(pattern, FC_SLANT, 0, &value) == FcResultMatch) face.italic = value != FC_SLANT_ROMAN;
        FcPatternDestroy(pattern);
        if (!face.families.empty()) faces.push_back(std::move(face));
        if (index + 1 >= count) break;
    }
    return faces;
}

// Registers a file with the typography and lists its faces as application fonts (once per file).
bool addAppFont(const std::string& path, std::vector<std::string>* families) {
    auto faces = facesOf(path);
    if (faces.empty()) return false;
    auto& fonts = appFonts();
    std::lock_guard lock(fonts.mutex);
    const bool fresh = fonts.files.insert(path).second;
    if (families)
        for (const Face& face : faces)
            for (const auto& family : face.families)
                if (std::find(families->begin(), families->end(), family) == families->end()) families->push_back(family);
    if (fresh) {
        gx::registerFontFile(path.c_str());
        for (auto& face : faces) fonts.faces.push_back(std::move(face));
        fonts.generation.fetch_add(1, std::memory_order_release);
    }
    return true;
}

// A system that has no font for fontconfig to hand out (a bare container, a fresh install, fonts.conf without
// the font folders) would show no text at all. The first plain text font found in the usual font folders and in
// the places that ship some anyway (Steam's interface, Proton, Wine) becomes an application font instead.
std::string lastResortFamily() {
    static const std::string family = [] {
        namespace fs = std::filesystem;
        std::vector<fs::path> roots{"/usr/share/fonts", "/usr/local/share/fonts", "/usr/share/wine/fonts"};
        const char* home = std::getenv("HOME");
        const char* data = std::getenv("XDG_DATA_HOME");
        const fs::path user_data = data && *data ? fs::path(data) : home ? fs::path(home) / ".local/share" : fs::path();
        if (home) roots.push_back(fs::path(home) / ".fonts");
        if (!user_data.empty()) roots.push_back(user_data / "fonts");
        std::vector<fs::path> steam;
        if (home) {
            steam = {fs::path(home) / ".steam/steam", fs::path(home) / ".local/share/Steam", fs::path(home) / ".var/app/com.valvesoftware.Steam/.local/share/Steam"};
            roots.push_back(fs::path(home) / ".steam/root/steamui/fonts");
        }
        std::error_code ec;
        for (const auto& base : steam) {
            for (const char* folder : {"steamui/fonts", "clientui/fonts"}) roots.push_back(base / folder);
            // Proton and the compatibility tools carry Liberation and the like in their own prefix files.
            for (const char* folder : {"steamapps/common", "compatibilitytools.d"})
                for (fs::directory_iterator it(base / folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
                    if (const auto name = it->path().filename().string(); name.starts_with("Proton") || std::strcmp(folder, "compatibilitytools.d") == 0)
                        roots.push_back(it->path() / "files/share/fonts");
        }
        const auto plain = [](const fs::path& file) {
            const auto extension = fold(file.extension().string());
            if (extension != ".ttf" && extension != ".otf") return false;
            const auto name = fold(file.filename().string());
            for (const char* skip : {"symbol", "icon", "emoji", "dingbat", "awesome", "mono", "bold", "italic", "oblique", "light", "thin", "black", "cjk"})
                if (name.find(skip) != std::string::npos) return false;
            return true;
        };
        int tried = 0;
        for (const auto& root : roots) {
            if (!fs::is_directory(root, ec)) continue;
            int visited = 0;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && visited < 5000 && tried < 60; it.increment(ec), ++visited) {
                if (it.depth() > 6) it.disable_recursion_pending();
                if (!it->is_regular_file(ec) || !plain(it->path())) continue;
                ++tried;
                std::vector<std::string> families;
                if (!addAppFont(it->path().string(), &families) || families.empty()) continue;
                std::fprintf(stderr, "launchme: no system fonts found for fontconfig; using %s from %s\n", families.front().c_str(), it->path().c_str());
                return families.front();
            }
        }
        std::fprintf(stderr, "launchme: no fonts found: text cannot be drawn (install a font package, e.g. ttf-dejavu or fonts-dejavu)\n");
        return std::string();
    }();
    return family;
}

// The launcher's icon font, an application font from the program's assets (once).
bool bundledIcons() {
    static const bool loaded = [] {
        char program[4096];
        const ssize_t length = readlink("/proc/self/exe", program, sizeof(program) - 1);
        if (length <= 0) return false;
        program[length] = 0;
        const std::string folder = std::string(program).substr(0, std::string(program).rfind('/'));
        for (const char* place : {"/assets/fonts/launchme-icons.ttf", "/../share/launchme/assets/fonts/launchme-icons.ttf"})
            if (access((folder + place).c_str(), R_OK) == 0 && addAppFont(folder + place, nullptr)) return true;
        return false;
    }();
    return loaded;
}

// A font file and face.
struct Matched {
    std::string path;
    int index = 0;
};

int fcWidthOf(DWRITE_FONT_STRETCH stretch) {
    static constexpr int widths[] = {100, 50, 63, 75, 87, 100, 112, 125, 150, 200};
    return widths[std::clamp<int>(stretch, 0, 9)];
}

bool matchFace(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, Matched& out) {
    {
        auto& fonts = appFonts();
        std::lock_guard lock(fonts.mutex);
        const std::string key = fold(family);
        const Face* best = nullptr;
        int best_distance = std::numeric_limits<int>::max();
        for (const Face& face : fonts.faces) {
            if (std::none_of(face.families.begin(), face.families.end(), [&](const std::string& name) { return fold(name) == key; })) continue;
            const int distance = std::abs(face.weight - static_cast<int>(weight)) + (face.italic != (style != DWRITE_FONT_STYLE_NORMAL)) * 1000 +
                                 std::abs(face.width - fcWidthOf(stretch)) * 2;
            if (distance < best_distance) { best = &face; best_distance = distance; }
        }
        if (best) {
            out = {best->path, best->index};
            return true;
        }
    }
    std::lock_guard fc(fcLock());
    FcInit();
    FcPattern* pattern = FcPatternCreate();
    if (!pattern) return false;
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcPatternAddInteger(pattern, FC_WEIGHT, FcWeightFromOpenType(static_cast<int>(weight)));
    FcPatternAddInteger(pattern, FC_SLANT, style == DWRITE_FONT_STYLE_ITALIC ? FC_SLANT_ITALIC : style == DWRITE_FONT_STYLE_OBLIQUE ? FC_SLANT_OBLIQUE : FC_SLANT_ROMAN);
    FcPatternAddInteger(pattern, FC_WIDTH, fcWidthOf(stretch));
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult status;
    bool found = false;
    if (FcPattern* match = FcFontMatch(nullptr, pattern, &status)) {
        FcChar8* file = nullptr;
        int index = 0;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch) {
            FcPatternGetInteger(match, FC_INDEX, 0, &index);
            out = {reinterpret_cast<const char*>(file), index & 0xFFFF};
            found = true;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return found;
}

// Design-unit metrics of the font matched for the family and style.
bool fontMetrics(const std::string& family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,
                 DWRITE_FONT_METRICS& metrics) {
    Matched matched;
    if (!matchFace(family, weight, style, stretch, matched)) return false;
    static std::mutex mutex;
    static FT_Library library = [] { FT_Library created = nullptr; return FT_Init_FreeType(&created) == 0 ? created : nullptr; }();
    if (!library) return false;
    std::lock_guard fc(fcLock());
    std::lock_guard lock(mutex);
    FT_Face face = nullptr;
    if (FT_New_Face(library, matched.path.c_str(), matched.index, &face) != 0 || !face) return false;
    const int units = face->units_per_EM;
    bool ok = units > 0;
    if (ok) {
        const auto u16 = [](double value) { return static_cast<UINT16>(std::clamp<long>(std::lround(value), 0, std::numeric_limits<UINT16>::max())); };
        const auto i16 = [](double value) {
            return static_cast<INT16>(std::clamp<long>(std::lround(value), std::numeric_limits<INT16>::min(), std::numeric_limits<INT16>::max()));
        };
        const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2));
        metrics.designUnitsPerEm = u16(units);
        metrics.ascent = u16(face->ascender);
        metrics.descent = u16(-face->descender);
        metrics.lineGap = i16(std::max(0, face->height - (face->ascender - face->descender)));
        metrics.capHeight = u16(os2 && os2->version >= 2 && os2->sCapHeight ? os2->sCapHeight : face->ascender * 0.7);
        metrics.xHeight = u16(os2 && os2->version >= 2 && os2->sxHeight ? os2->sxHeight : face->ascender * 0.5);
        metrics.underlinePosition = i16(face->underline_position ? face->underline_position : -units / 10.0); // DirectWrite: negative below the baseline.
        metrics.underlineThickness = u16(face->underline_thickness > 0 ? face->underline_thickness : units / 20.0);
        metrics.strikethroughPosition = i16(os2 && os2->yStrikeoutPosition ? os2->yStrikeoutPosition : units / 4.0);
        metrics.strikethroughThickness = u16(os2 && os2->yStrikeoutSize ? os2->yStrikeoutSize : units / 20.0);
    }
    FT_Done_Face(face);
    return ok;
}

class MatchedFont final : public lm_com::Object<IDWriteFont> {
public:
    explicit MatchedFont(const DWRITE_FONT_METRICS& metrics) : metrics_(metrics) {}
    void STDMETHODCALLTYPE GetMetrics(DWRITE_FONT_METRICS* metrics) override { if (metrics) *metrics = metrics_; }
private:
    DWRITE_FONT_METRICS metrics_;
};

// A collection's family, named as the font manager knows it.
class FontFamily final : public lm_com::Object<IDWriteFontFamily> {
public:
    explicit FontFamily(std::string family) : family_(std::move(family)) {}
    HRESULT STDMETHODCALLTYPE GetFirstMatchingFont(DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,
                                                   IDWriteFont** font) override {
        if (!font) return E_POINTER;
        *font = nullptr;
        DWRITE_FONT_METRICS metrics{};
        if (!fontMetrics(family_, weight, style, stretch, metrics)) return E_FAIL;
        *font = new MatchedFont(metrics);
        return S_OK;
    }
private:
    std::string family_;
};

// The system collection holds every installed family and the aliased Windows ones; a family
// gets its index when it is first found. A file-built collection holds its files' families.
class FontCollection final : public lm_com::Object<IDWriteFontCollection1> {
public:
    FontCollection(bool system, const std::vector<std::string>& families) : system_(system) {
        for (const auto& family : families) families_.push_back({family, fold(family)});
    }
    HRESULT STDMETHODCALLTYPE GetFontFamily(UINT32 index, IDWriteFontFamily** family) override {
        if (!family) return E_POINTER;
        *family = nullptr;
        std::string name;
        {
            std::lock_guard lock(mutex_);
            if (index >= families_.size()) return E_INVALIDARG;
            name = families_[index].name;
        }
        *family = new FontFamily(system_ ? resolveSystem(name) : name);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindFamilyName(const WCHAR* familyName, UINT32* index, BOOL* exists) override {
        if (!familyName || !index || !exists) return E_INVALIDARG;
        *index = std::numeric_limits<UINT32>::max();
        *exists = FALSE;
        const std::string name = toUtf8(familyName), key = fold(name);
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(families_.begin(), families_.end(), [&](const Family& family) { return family.folded == key; });
        if (found != families_.end()) {
            *index = static_cast<UINT32>(found - families_.begin());
            *exists = TRUE;
        } else if (system_ && (installed(key) || aliasOf(key))) {
            families_.push_back({name, key});
            *index = static_cast<UINT32>(families_.size() - 1);
            *exists = TRUE;
        }
        return S_OK;
    }
    // A file-built collection's own family named `family` (any case); empty when it has none.
    std::string member(const std::string& family) const {
        if (system_) return {};
        const std::string key = fold(family);
        std::lock_guard lock(mutex_);
        for (const auto& own : families_)
            if (own.folded == key) return own.name;
        return {};
    }
private:
    struct Family { std::string name, folded; };
    const bool system_;
    mutable std::mutex mutex_;
    std::vector<Family> families_;
};

class FontFaceReference final : public lm_com::Object<IDWriteFontFaceReference> {
public:
    explicit FontFaceReference(std::string file) : path(std::move(file)) {}
    const std::string path;
};

class FontSet final : public lm_com::Object<IDWriteFontSet> {
public:
    explicit FontSet(std::vector<ComPtr<FontFaceReference>> list) : faces(std::move(list)) {}
    const std::vector<ComPtr<FontFaceReference>> faces;
};

class FontSetBuilder final : public lm_com::Object<IDWriteFontSetBuilder> {
public:
    HRESULT STDMETHODCALLTYPE AddFontFaceReference(IDWriteFontFaceReference* fontFaceReference) override {
        auto* own = dynamic_cast<FontFaceReference*>(fontFaceReference);
        if (!own) return E_INVALIDARG;
        faces_.emplace_back(own);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateFontSet(IDWriteFontSet** fontSet) override {
        if (!fontSet) return E_POINTER;
        *fontSet = new FontSet(faces_);
        return S_OK;
    }
private:
    std::vector<ComPtr<FontFaceReference>> faces_;
};
} // namespace

unsigned fontGeneration() { return appFonts().generation.load(std::memory_order_acquire); }

void appendUtf8(std::string& out, char32_t c) {
    if (c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) c = 0xFFFD;
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | c >> 6);
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | c >> 12);
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | c >> 18);
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string toUtf8(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t c : text) appendUtf8(result, static_cast<char32_t>(c));
    return result;
}

std::string resolveFamily(const std::wstring& family, IDWriteFontCollection* collection) {
    const std::string name = toUtf8(family);
    if (const auto* own = dynamic_cast<const FontCollection*>(collection))
        if (std::string member = own->member(name); !member.empty()) return member;
    return resolveSystem(name);
}

IDWriteFontCollection* systemFontCollection() {
    static auto* collection = new FontCollection(true, {}); // Lives as long as the process.
    return collection;
}

HRESULT createFontFaceReference(std::string path, IDWriteFontFaceReference** reference) {
    if (!reference) return E_POINTER;
    *reference = nullptr;
    if (access(path.c_str(), R_OK) != 0) return E_INVALIDARG;
    *reference = new FontFaceReference(std::move(path));
    return S_OK;
}

HRESULT createFontSetBuilder(IDWriteFontSetBuilder** builder) {
    if (!builder) return E_POINTER;
    *builder = new FontSetBuilder;
    return S_OK;
}

// Every face of each file becomes an application font (once per file); the collection lists
// their families.
HRESULT createFontCollection(IDWriteFontSet* set, IDWriteFontCollection1** collection) {
    if (!collection) return E_POINTER;
    *collection = nullptr;
    const auto* own = dynamic_cast<const FontSet*>(set);
    if (!own) return E_INVALIDARG;
    std::vector<std::string> families;
    for (const auto& face : own->faces) addAppFont(face->path, &families);
    *collection = new FontCollection(false, families);
    return S_OK;
}
} // namespace lm_dwrite
