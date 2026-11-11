// Szukaj gier → ostatnie wyszukiwania: the queries that opened a game (newest first, at most
// recent_search_limit), kept in recent_searches.txt (UTF-8, one per line) and listed under the
// empty field. Up/Down walk them, Enter searches again, X or Delete asks before forgetting one.
#include "launcher.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <sstream>

namespace {
std::filesystem::path recentSearchesFile() { return userConfigDirectory() / L"recent_searches.txt"; }
}

void Launcher::loadRecentSearches() {
    if (recent_searches_loaded_) return;
    recent_searches_loaded_ = true;
    recent_searches_.clear();
    std::istringstream lines(file_io::readTextWithoutBom(recentSearchesFile(), 64 * 1024));
    for (std::string line; std::getline(lines, line) && recent_searches_.size() < recent_search_limit;) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto query = text_encoding::wideLenient(line);
        if (!query.empty() && query.size() <= 70) recent_searches_.push_back(std::move(query));
    }
}

void Launcher::saveRecentSearches() {
    std::string text;
    for (const auto& query : recent_searches_) text += text_encoding::utf8Lenient(query) + "\n";
    atomic_file::write(recentSearchesFile(), text);
}

void Launcher::rememberSearch() {
    auto query = search_query_;
    const auto first = query.find_first_not_of(L' '), last = query.find_last_not_of(L' ');
    if (first == std::wstring::npos) return;
    query = query.substr(first, last - first + 1);
    loadRecentSearches();
    const auto lowered = unicode_text::lower(query);
    std::erase_if(recent_searches_, [&](const std::wstring& known) { return unicode_text::lower(known) == lowered; });
    recent_searches_.insert(recent_searches_.begin(), query);
    if (recent_searches_.size() > recent_search_limit) recent_searches_.resize(recent_search_limit);
    saveRecentSearches();
}

bool Launcher::recentSearchesShown() const {
    return screen_ == ScreenMode::search && search_query_.empty() && !recent_searches_.empty();
}

bool Launcher::recentSearchFocused() const {
    return recentSearchesShown() && !grid_back_focus_ && recent_search_focus_ >= 0 &&
           recent_search_focus_ < static_cast<int>(recent_searches_.size());
}

// Up from the first row: Back; Down from Back (or the field): the first row.
bool Launcher::recentSearchMove(int amount) {
    if (!recentSearchesShown()) return false;
    const int count = static_cast<int>(recent_searches_.size());
    if (grid_back_focus_) {
        if (amount > 0) { grid_back_focus_ = false; recent_search_focus_ = 0; audio_.navigate(); }
        else navigationBoundary();
    } else {
        const int next = recent_search_focus_ + amount;
        if (next < 0) { grid_back_focus_ = true; recent_search_focus_ = -1; audio_.navigate(); }
        else if (next >= count) navigationBoundary();
        else { recent_search_focus_ = next; audio_.navigate(); }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::useRecentSearch() {
    if (!recentSearchFocused()) return;
    search_query_ = recent_searches_[static_cast<size_t>(recent_search_focus_)];
    search_after_ = 0;
    recent_search_focus_ = -1;
    audio_.navigate();
    updateSearch();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::askForgetSearch() {
    if (!recentSearchFocused()) return;
    forget_search_ = static_cast<size_t>(recent_search_focus_);
    prompt_kind_ = PromptKind::forget_search;
    exit_prompt_ = 2; // Anuluj: the safe default.
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::forgetSearch(bool all) {
    if (all) recent_searches_.clear();
    else if (forget_search_ < recent_searches_.size()) recent_searches_.erase(recent_searches_.begin() + static_cast<ptrdiff_t>(forget_search_));
    else return;
    saveRecentSearches();
    recent_search_focus_ = recent_searches_.empty() ? -1 : std::min(recent_search_focus_, static_cast<int>(recent_searches_.size()) - 1);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
