#include "game_log.hpp"
#include "../platform/system.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

namespace game_log {
namespace {
std::mutex& lock() { static std::mutex value; return value; }
std::wstring stamp(bool file_name) {
    const auto now = system_info::localTime();
    wchar_t text[64]{};
    if (file_name) swprintf(text, std::size(text), L"%04d-%02d-%02d_%02d-%02d-%02d", now.year, now.month, now.day, now.hour, now.minute, now.second);
    else swprintf(text, std::size(text), L"%04d-%02d-%02d %02d:%02d:%02d", now.year, now.month, now.day, now.hour, now.minute, now.second);
    return text;
}
}

std::string slug(std::wstring_view title, std::string_view key) {
    const auto make = [](std::wstring_view text) {
        std::string result;
        bool dash = false;
        for (const wchar_t ch : unicode_text::lower(unicode_text::decomposeCompatible(text))) {
            wchar_t plain = ch;
            if (ch == L'ł') plain = L'l';
            else if (ch == L'ø') plain = L'o';
            else if (ch == L'æ') plain = L'a';
            if ((plain >= L'a' && plain <= L'z') || (plain >= L'0' && plain <= L'9')) {
                if (dash && !result.empty()) result += '-';
                dash = false;
                result += static_cast<char>(plain);
            } else if (!unicode_text::isMark(ch) && ch != L'\'' && ch != L'’') dash = true;
            if (result.size() >= 64) break;
        }
        while (!result.empty() && result.back() == '-') result.pop_back();
        return result.substr(0, 64);
    };
    auto result = make(title);
    if (result.empty()) result = make(text_encoding::wideLenient(key));
    return result.empty() ? std::string("game") : result;
}

std::filesystem::path folder(const std::filesystem::path& data, std::wstring_view title, std::string_view key) {
    return data / L"logs" / L"games" / text_encoding::wideLenient(slug(title, key));
}

std::filesystem::path begin(const std::filesystem::path& data, std::wstring_view title, std::string_view key) {
    try {
        const auto where = folder(data, title, key);
        std::error_code error;
        std::filesystem::create_directories(where, error);
        if (error) return {};
        std::vector<std::filesystem::path> logs;
        for (std::filesystem::directory_iterator it(where, error), end; !error && it != end; it.increment(error))
            if (it->path().extension() == L".log") logs.push_back(it->path());
        std::sort(logs.begin(), logs.end()); // Named by their time: oldest first.
        for (size_t i = 0; i + max_files <= logs.size(); ++i) std::filesystem::remove(logs[i], error);
        auto file = where / (stamp(true) + L".log");
        for (int n = 2; std::filesystem::exists(file, error) && n < 100; ++n) file = where / (stamp(true) + L"-" + std::to_wstring(n) + L".log");
        return file;
    } catch (...) { return {}; }
}

void append(const std::filesystem::path& file, std::string_view bytes) {
    if (file.empty() || bytes.empty()) return;
    try {
        std::lock_guard guard(lock());
        std::ofstream output(file, std::ios::binary | std::ios::app);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    } catch (...) {}
}

void write(const std::filesystem::path& file, std::wstring_view line) {
    std::string text = text_encoding::utf8Lenient(stamp(false)) + "  " + text_encoding::utf8Lenient(line);
    for (auto& ch : text) if (ch == '\r' || ch == '\n') ch = ' ';
    append(file, text + "\n");
}
}
