#include "text_crop.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"
#include "../platform/system.hpp"
#include "image_codec.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <locale>
#include <mutex>

namespace {
std::mutex crop_image_mutex;
struct Region { float start, end, weight; };

float bestWindow(int length, int visible, const std::vector<Region>& regions, float fallback) {
    const float max_start = static_cast<float>(length - visible);
    if (max_start <= 0) return 0;
    fallback = std::clamp(fallback, 0.0f, max_start);
    if (regions.empty()) return fallback;
    float first = static_cast<float>(length), last = 0;
    for (const auto& region : regions) {
        first = std::min(first, region.start);
        last = std::max(last, region.end);
    }
    // Keep the whole lettering comfortably inside the tile. Clamping a center
    // crop just far enough to fit one OCR box left the logo under the border.
    const float margin = visible * 0.075f;
    const float centered_text = std::clamp((first + last - visible) * 0.5f, 0.0f, max_start);
    if (last - first <= visible) {
        const float low = std::max(0.0f, last - visible + (last - first + 2 * margin <= visible ? margin : 0));
        const float high = std::min(max_start, first - (last - first + 2 * margin <= visible ? margin : 0));
        if (low <= high) return std::clamp(centered_text, low, high);
    }
    // Conflicting text on opposite edges: prefer complete, confident words.
    // Evaluate crop endpoints and centering, not a fixed 50/50 center-crop.
    std::vector<float> candidates{0, max_start, fallback};
    for (const auto& region : regions) {
        candidates.push_back(std::clamp(region.start - margin, 0.0f, max_start));
        candidates.push_back(std::clamp(region.end + margin - visible, 0.0f, max_start));
        candidates.push_back(std::clamp((region.start + region.end - visible) / 2, 0.0f, max_start));
    }
    float best = fallback, score = -1;
    for (float start : candidates) {
        float total = 0;
        for (const auto& region : regions) {
            const float overlap = std::max(0.0f, std::min(start + visible, region.end) -
                                                 std::max(start, region.start));
            const float portion = overlap / std::max(1.0f, region.end - region.start);
            total += region.weight * portion * portion;
        }
        total -= 0.0001f * std::abs(start - fallback);
        if (total > score) { score = total; best = start; }
    }
    return best;
}

#ifdef _WIN32
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE && value) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::filesystem::path tesseractExecutable() {
    wchar_t buffer[4096]{};
    DWORD size = GetEnvironmentVariableW(L"TESSERACT_EXE", buffer, 4096);
    if (size && size < 4096 && GetFileAttributesW(buffer) != INVALID_FILE_ATTRIBUTES) return buffer;
    size = SearchPathW(nullptr, L"tesseract.exe", nullptr, 4096, buffer, nullptr);
    if (size && size < 4096) return buffer;
    const std::filesystem::path standard = L"C:\\Program Files\\Tesseract-OCR\\tesseract.exe";
    if (std::filesystem::exists(standard)) return standard;
    return {};
}

std::string tesseractTsv(const std::filesystem::path& executable, const std::filesystem::path& image) {
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE reader = INVALID_HANDLE_VALUE, writer = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&reader, &writer, &security, 0)) return {};
    Handle output(reader), child_output(writer);
    SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = child_output.value;
    startup.hStdError = child_output.value;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    auto command = L"\"" + executable.wstring() + L"\" \"" + image.wstring() +
                   L"\" stdout -l eng --psm 11 tsv";
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process_info)) return {};
    Handle process(process_info.hProcess), thread(process_info.hThread);
    CloseHandle(child_output.value);
    child_output.value = INVALID_HANDLE_VALUE;
    std::string tsv;
    const auto deadline = GetTickCount64() + 6000;
    while (GetTickCount64() < deadline && tsv.size() < 2 * 1024 * 1024) {
        DWORD available = 0;
        if (!PeekNamedPipe(output.value, nullptr, 0, nullptr, &available, nullptr)) break;
        if (available) {
            char buffer[8192];
            DWORD read = 0;
            if (!ReadFile(output.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)) break;
            tsv.append(buffer, read);
        } else if (WaitForSingleObject(process.value, 10) == WAIT_OBJECT_0) break;
    }
    if (WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) TerminateProcess(process.value, 1);
    DWORD available = 0;
    while (tsv.size() < 2 * 1024 * 1024 &&
           PeekNamedPipe(output.value, nullptr, 0, nullptr, &available, nullptr) && available) {
        char buffer[8192];
        DWORD read = 0;
        if (!ReadFile(output.value, buffer, std::min<DWORD>(available, sizeof(buffer)), &read, nullptr)) break;
        tsv.append(buffer, read);
    }
    return tsv;
}
#else
std::filesystem::path tesseractExecutable() {
    std::error_code ec;
    if (const auto configured = system_info::environment(L"TESSERACT_EXE"); configured && !configured->empty() &&
        std::filesystem::is_regular_file(*configured, ec)) return *configured;
    const auto path = system_info::environment(L"PATH").value_or(L"/usr/local/bin:/usr/bin:/bin");
    for (size_t at = 0; at <= path.size();) {
        const auto end = std::min(path.find(L':', at), path.size());
        const auto candidate = std::filesystem::path(path.substr(at, end - at)) / L"tesseract";
        if (end > at && candidate.is_absolute() && std::filesystem::is_regular_file(candidate, ec) && ::access(candidate.c_str(), X_OK) == 0)
            return candidate;
        at = end + 1;
    }
    return {};
}

// Tesseract's TSV on stdout (stderr too, as on Windows), stopped after 6 s.
std::string tesseractTsv(const std::filesystem::path& executable, const std::filesystem::path& image) {
    int pipe_ends[2];
    if (::pipe2(pipe_ends, O_CLOEXEC) != 0) return {};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[1], STDERR_FILENO);
    const std::string program = executable.string(), input = image.string();
    const char* arguments[]{program.c_str(), input.c_str(), "stdout", "-l", "eng", "--psm", "11", "tsv", nullptr};
    pid_t child = -1;
    const int spawned = posix_spawn(&child, program.c_str(), &actions, nullptr, const_cast<char* const*>(arguments), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_ends[1]);
    if (spawned != 0) { ::close(pipe_ends[0]); return {}; }
    std::string tsv;
    const auto deadline = GetTickCount64() + 6000;
    while (tsv.size() < 2 * 1024 * 1024) {
        const auto now = GetTickCount64();
        if (now >= deadline) break;
        pollfd wait{pipe_ends[0], POLLIN, 0};
        const int ready = ::poll(&wait, 1, static_cast<int>(std::min<uint64_t>(deadline - now, 100)));
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        char buffer[8192];
        const ssize_t read = ::read(pipe_ends[0], buffer, sizeof(buffer));
        if (read < 0 && errno == EINTR) continue;
        if (read <= 0) break; // End of output: tesseract has finished.
        tsv.append(buffer, static_cast<size_t>(read));
    }
    ::close(pipe_ends[0]);
    int status = 0;
    if (::waitpid(child, &status, WNOHANG) == 0) { ::kill(child, SIGKILL); ::waitpid(child, &status, 0); }
    return tsv;
}
#endif

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= line.size() && result.size() < 11) {
        size_t end = line.find('\t', start);
        if (end == std::string::npos) break;
        result.push_back(line.substr(start, end - start));
        start = end + 1;
    }
    result.push_back(line.substr(start));
    return result;
}

int asInt(const std::string& text) {
    try { return std::stoi(text); } catch (...) { return 0; }
}

float asFloat(const std::string& text) {
    try { return std::stof(text); } catch (...) { return -1; }
}

std::wstring normalized(std::wstring_view input) {
    std::wstring result;
    for (const wchar_t ch : unicode_text::lower(input)) if (unicode_text::isLetterOrDigit(ch)) result += ch;
    return result;
}
std::vector<std::wstring> titleTokens(std::wstring_view title) {
    std::wstring separated(title);
    for (auto& ch : separated) {
        if (!unicode_text::isLetterOrDigit(ch)) ch = L' ';
    }
    std::wistringstream input(separated);
    std::vector<std::wstring> result;
    for (std::wstring token; input >> token;) {
        token = normalized(token);
        if (token.size() >= 3 && token != L"the" && token != L"and" && token != L"for" && token != L"with") result.push_back(std::move(token));
    }
    const auto full = normalized(title);
    if (full.size() >= 3) result.push_back(full);
    return result;
}
uint64_t titleSignature(std::wstring_view title) {
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : normalized(title)) { hash ^= static_cast<uint16_t>(ch); hash *= 1099511628211ull; }
    return hash;
}
} // namespace

std::vector<TextBox> parseCropWords(std::string_view tsv, std::wstring_view title) {
    std::vector<TextBox> words;
    const auto tokens = titleTokens(title);
    std::istringstream input{std::string(tsv)};
    std::string line;
    while (std::getline(input, line)) {
        const auto columns = fields(line);
        if (columns.size() != 12 || columns[0] != "5") continue;
        const float confidence = asFloat(columns[10]);
        const auto text = normalized(text_encoding::wide(columns[11]));
        const bool match = !text.empty() && std::find(tokens.begin(), tokens.end(), text) != tokens.end();
        if (!std::isfinite(confidence) || confidence < (match ? 25 : 60) || text.size() < 3) continue;
        TextBox word{asInt(columns[6]), asInt(columns[7]), asInt(columns[8]),
                     asInt(columns[9]), confidence, match};
        if (word.width >= 10 && word.height >= 8) words.push_back(word);
    }
    return words;
}

namespace {
// Some colored logos have almost the same luminance as their backdrop (yellow
// letters on cyan in Dave the Diver). Luminance-only OCR loses them entirely.
// A red/blue opponent channel exposes their color contrast as grayscale text.
bool writeColorContrast(const std::filesystem::path& image, const std::filesystem::path& output) {
    const auto decoded = image_codec::decodeFile(image, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (info.width > 4096 || info.height > 4096) return {};
        return image_codec::Size{info.width, info.height};
    });
    if (!decoded) return false;
    const uint32_t width = decoded->width, height = decoded->height;
    const auto& rgba = decoded->pixels; // BGRA: blue minus red below.
    std::vector<uint8_t> gray(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < gray.size(); ++i)
        gray[i] = static_cast<uint8_t>(std::clamp(128 + static_cast<int>(rgba[i * 4]) - rgba[i * 4 + 2], 0, 255));
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    file << "P5\n" << width << ' ' << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(gray.data()), static_cast<std::streamsize>(gray.size()));
    return file.good();
}

unsigned long threadId() {
#ifdef _WIN32
    return GetCurrentThreadId();
#else
    return static_cast<unsigned long>(::gettid());
#endif
}

std::pair<int, int> imageSize(const std::filesystem::path& file) {
    const auto info = image_codec::probeFile(file);
    return info ? std::pair{static_cast<int>(info->width), static_cast<int>(info->height)} : std::pair{0, 0};
}
} // namespace

CropFocus chooseTextAwareCrop(int width, int height, const std::vector<TextBox>& words) {
    if (width <= 0 || height <= 0) return {};
    const int side = std::min(width, height);
    const float default_y = (height - side) * 0.5f;
    std::vector<Region> horizontal, vertical;
    const auto valid = [&](const TextBox& word) {
        return word.width > 0 && word.height > 0 && word.left >= 0 && word.top >= 0 &&
            static_cast<int64_t>(word.left) + word.width <= width && static_cast<int64_t>(word.top) + word.height <= height &&
            std::isfinite(word.confidence) && word.confidence >= (word.title_match ? 25 : 60);
    };
    const bool known_title = std::any_of(words.begin(), words.end(), [&](const auto& word) { return valid(word) && word.title_match; });
    for (const auto& word : words) {
        if (!valid(word) || (known_title && !word.title_match)) continue;
        const float weight = (word.confidence / 100.0f) *
                             std::clamp(std::sqrt(static_cast<float>(word.width) * word.height) / 24.0f,
                                        0.65f, 2.5f);
        horizontal.push_back({static_cast<float>(word.left), static_cast<float>(word.left + word.width), weight});
        vertical.push_back({static_cast<float>(word.top), static_cast<float>(word.top + word.height), weight});
    }
    const float crop_x = bestWindow(width, side, horizontal, (width - side) * 0.5f);
    const float crop_y = bestWindow(height, side, vertical, default_y);
    return {(crop_x + side * 0.5f) / width, (crop_y + side * 0.5f) / height};
}

CropFocus chooseDetailCrop(uint32_t width, uint32_t height, std::span<const uint8_t> bgra) {
    if (width < 3 || height < 3 || width == height || bgra.size() < static_cast<size_t>(width) * height * 4) return {};
    std::vector<float> luma(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < luma.size(); ++i) luma[i] = 0.114f * bgra[i * 4] + 0.587f * bgra[i * 4 + 1] + 0.299f * bgra[i * 4 + 2];
    // A Laplacian of the luminance (edges, lettering, faces), capped so one hard edge does not win,
    // summed along the rows of a poster or the columns of a capsule.
    const bool portrait = height > width;
    const uint32_t length = portrait ? height : width, side = portrait ? width : height;
    std::vector<float> line(length, 0.0f);
    for (uint32_t y = 1; y + 1 < height; ++y)
        for (uint32_t x = 1; x + 1 < width; ++x) {
            const size_t at = static_cast<size_t>(y) * width + x;
            const float edge = std::abs(4 * luma[at] - luma[at - 1] - luma[at + 1] - luma[at - width] - luma[at + width]);
            line[portrait ? y : x] += std::min(edge, 96.0f) / 96.0f;
        }
    double total = 0;
    for (float value : line) total += value;
    if (total <= 0) return {};
    // The window holding the most detail; the middle wins a tie (a small pull towards it).
    const float middle = (length - side) * 0.5f;
    const double pull = 0.002 * total / length;
    double window = 0;
    for (uint32_t i = 0; i < side; ++i) window += line[i];
    float best = 0;
    double score = window - pull * middle;
    for (uint32_t start = 1; start + side <= length; ++start) {
        window += line[start + side - 1] - line[start - 1];
        const double value = window - pull * std::abs(static_cast<float>(start) - middle);
        if (value > score) { score = value; best = static_cast<float>(start); }
    }
    const float center = (best + side * 0.5f) / length;
    return portrait ? CropFocus{0.5f, center} : CropFocus{center, 0.5f};
}

CropFocus detailCrop(const std::filesystem::path& image) {
    const auto decoded = image_codec::decodeFile(image, [](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (!info.width || !info.height || info.width > 8192 || info.height > 8192) return {};
        const float scale = 96.0f / static_cast<float>(std::min(info.width, info.height));
        if (scale >= 1) return image_codec::Size{info.width, info.height};
        return image_codec::Size{std::max(1u, static_cast<uint32_t>(std::lround(info.width * scale))),
                                 std::max(1u, static_cast<uint32_t>(std::lround(info.height * scale)))};
    });
    if (!decoded) return {};
    return chooseDetailCrop(decoded->width, decoded->height, decoded->pixels);
}

namespace {
bool centred(CropFocus focus) { return std::abs(focus.x - 0.5f) < 1e-4f && std::abs(focus.y - 0.5f) < 1e-4f; }
// Version 5 kept the middle of art without readable lettering: its centred, non-square crops take
// the most detailed square now (no OCR again); the rest stay as they were.
std::optional<CropFocus> upgradedCrop(int version, CropFocus focus, const std::filesystem::path& image) {
    if (version == crop_cache_version) return focus;
    if (version != crop_cache_version - 1) return std::nullopt;
    if (!centred(focus)) return focus;
    const auto [width, height] = imageSize(image);
    if (width <= 0 || width == height) return focus;
    return detailCrop(image);
}
}

CropFocus cachedAutomaticCrop(const std::filesystem::path& image, const std::filesystem::path& metadata, std::wstring_view title, bool force) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(image, ec);
    if (ec) return {};
    const auto modified = std::filesystem::last_write_time(image, ec).time_since_epoch().count();
    if (ec) return {};
    const auto signature = titleSignature(title);
    if (!force) {
        std::ifstream input(metadata);
        int version = 0;
        uint64_t cached_bytes = 0, cached_title = 0;
        int64_t cached_modified = 0;
        CropFocus cached;
        input.imbue(std::locale::classic());
        if (input >> version >> cached_bytes >> cached_modified >> cached_title >> cached.x >> cached.y &&
            cached_bytes == bytes && cached_modified == modified && cached_title == signature &&
            cached.x >= 0 && cached.x <= 1 && cached.y >= 0 && cached.y <= 1)
            if (const auto upgraded = upgradedCrop(version, cached, image)) {
                if (version != crop_cache_version) storeAutomaticTextCrop(image, metadata, title, *upgraded);
                return *upgraded;
            }
    }
    const auto [width, height] = imageSize(image);
    if (!width || !height) return {};
    std::vector<TextBox> words;
    if (width != height) {
        const auto executable = tesseractExecutable();
        if (executable.empty()) return detailCrop(image); // Not kept: OCR may come later.
        words = parseCropWords(tesseractTsv(executable, image), title);
        const auto matched = [](const auto& items) { return std::any_of(items.begin(), items.end(), [](const auto& word) { return word.title_match; }); };
        if (!matched(words)) {
            const auto contrast = metadata.wstring() + L".ocr." + std::to_wstring(system_info::processId()) + L'.' + std::to_wstring(threadId()) + L".pgm";
            struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{contrast};
            if (writeColorContrast(image, contrast)) {
                auto colored = parseCropWords(tesseractTsv(executable, contrast), title);
                if (matched(colored) || words.empty()) words = std::move(colored);
            }
        }
    }
    // No lettering read: the most detailed square rather than the middle.
    const bool lettering = std::any_of(words.begin(), words.end(), [](const TextBox& word) { return word.width > 0 && word.height > 0; });
    const auto focus = width != height && !lettering ? detailCrop(image) : chooseTextAwareCrop(width, height, words);
    std::ostringstream output; output.imbue(std::locale::classic()); output.precision(9);
    output << crop_cache_version << ' ' << bytes << ' ' << modified << ' ' << signature << ' ' << focus.x << ' ' << focus.y << '\n';
    atomic_file::write(metadata, output.str());
    return focus;
}

namespace {
std::filesystem::path manualFile(std::filesystem::path image) { return image.replace_extension(L".manual-focus"); }
uint64_t imageFingerprint(const std::filesystem::path& image) {
    std::ifstream input(image, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 20 * 1024 * 1024) return 0;
    input.seekg(0);
    uint64_t hash = 14695981039346656037ull;
    char bytes[16384];
    while (input) {
        input.read(bytes, sizeof(bytes));
        for (std::streamsize i = 0; i < input.gcount(); ++i) { hash ^= static_cast<uint8_t>(bytes[i]); hash *= 1099511628211ull; }
    }
    return input.eof() ? hash : 0;
}
bool validFocus(CropFocus focus) { return std::isfinite(focus.x) && std::isfinite(focus.y) && focus.x >= 0 && focus.x <= 1 && focus.y >= 0 && focus.y <= 1; }
}
std::pair<int, int> cropImageSize(const std::filesystem::path& image) { return imageSize(image); }
std::optional<CropFocus> manualTextCrop(const std::filesystem::path& image) {
    std::ifstream input(manualFile(image)); input.imbue(std::locale::classic());
    int version = 0; uint64_t fingerprint = 0; CropFocus focus;
    if (!(input >> version >> fingerprint >> focus.x >> focus.y) || version != 1 || !fingerprint || !validFocus(focus)) return {};
    input.close();
    if (imageFingerprint(image) != fingerprint) return {};
    return focus;
}
bool saveManualTextCrop(const std::filesystem::path& image, CropFocus focus) {
    std::lock_guard lock(crop_image_mutex);
    if (!validFocus(focus)) return false;
    const auto fingerprint = imageFingerprint(image);
    if (!fingerprint) return false;
    std::ostringstream output; output.imbue(std::locale::classic());
    output.precision(9); output << "1 " << fingerprint << ' ' << focus.x << ' ' << focus.y << '\n';
    return atomic_file::write(manualFile(image), output.str());
}
bool clearManualTextCrop(const std::filesystem::path& image) {
    std::lock_guard lock(crop_image_mutex);
    std::error_code ec; std::filesystem::remove(manualFile(image), ec); return !ec;
}
bool writeImageWithoutManualCrop(const std::filesystem::path& image, std::span<const uint8_t> bytes) {
    std::lock_guard lock(crop_image_mutex);
    std::error_code ec;
    if (std::filesystem::exists(manualFile(image), ec) || ec) return false;
    return atomic_file::write(image, bytes);
}
CropFocus cachedTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata, std::wstring_view title, bool force) {
    if (const auto manual = manualTextCrop(image)) return *manual;
    return cachedAutomaticCrop(image, metadata, title, force);
}
std::optional<CropFocus> storedTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata, std::wstring_view title) {
    if (const auto manual = manualTextCrop(image)) return manual;
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(image, ec);
    if (ec) return {};
    const auto modified = std::filesystem::last_write_time(image, ec).time_since_epoch().count();
    if (ec) return {};
    std::ifstream input(metadata); input.imbue(std::locale::classic());
    int version = 0; uint64_t cached_bytes = 0, title_hash = 0; int64_t cached_modified = 0; CropFocus focus;
    if (input >> version >> cached_bytes >> cached_modified >> title_hash >> focus.x >> focus.y &&
        cached_bytes == bytes && cached_modified == modified && title_hash == titleSignature(title) && validFocus(focus))
        if (const auto upgraded = upgradedCrop(version, focus, image)) {
            if (version != crop_cache_version) storeAutomaticTextCrop(image, metadata, title, *upgraded);
            return upgraded;
        }
    // Square originals need neither OCR nor a crop sidecar.
    const auto [width, height] = imageSize(image);
    if (width > 0 && width == height) return CropFocus{};
    return {};
}
bool storeAutomaticTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata, std::wstring_view title, CropFocus focus) {
    if (!validFocus(focus)) return false;
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(image, ec);
    if (ec) return false;
    const auto modified = std::filesystem::last_write_time(image, ec).time_since_epoch().count();
    if (ec) return false;
    std::ostringstream output; output.imbue(std::locale::classic()); output.precision(9);
    output << crop_cache_version << ' ' << bytes << ' ' << modified << ' ' << titleSignature(title) << ' ' << focus.x << ' ' << focus.y << '\n';
    return atomic_file::write(metadata, output.str());
}
