// What the case in the hand shows beyond its front (src/artwork/case_extras.hpp): the back of the box,
// its spine and the game's manual, asked for once a game is held; and Instrukcja, the manual read like its
// booklet (src/ui/game_manual_view.hpp), its pages drawn by the system's PDF engine on a worker.
// Without one (Linux) Instrukcja opens the PDF in the desktop's viewer.
#include "launcher.hpp"
#include "../artwork/case_extras.hpp"
#include "../platform/diagnostics.hpp"
#include "../platform/file_io.hpp"
#include "../platform/pdf_pages.hpp"
#include "../platform/user_config.hpp"

#include <algorithm>
#include <cmath>

namespace {
bool emulated(const Game& game) { return game.kind == GameKind::nonsteam && game.app_id && !game.platform.empty() && game.platform != L"PC"; }
}

void Launcher::requestCaseExtras(const Game& game) {
    const auto key = gameKey(game);
    if (case_extras_key_ == key) return; // Asked already (the answers stay with the renderer and case_manuals_).
    case_extras_key_ = key;
    const uint32_t generation = ++case_extras_generation_;
    if (!emulated(game) || !runtime_started_) return;
    work_.start([game, key, generation, alive = alive_, hwnd = hwnd_] {
        const auto post = [&](std::vector<uint8_t> back, std::vector<uint8_t> side, std::filesystem::path manual) {
            auto message = std::make_unique<RemoteMessage>();
            message->run = [generation, key, back = std::move(back), side = std::move(side), manual = std::move(manual)](Launcher& app) mutable {
                app.caseExtrasArrived(generation, key, std::move(back), std::move(side), manual);
            };
            if (alive->load() && PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
        const auto root = userConfigDirectory();
        // The back and the spine first: they show while the manual may still be downloading.
        auto back = case_extras::back(game, root);
        if (!alive->load()) return;
        auto side = case_extras::side(game, root);
        if (!alive->load()) return;
        if (!back.empty() || !side.empty()) post(std::move(back), std::move(side), {});
        auto manual = case_extras::manual(game, root);
        if (!manual.empty()) post({}, {}, std::move(manual));
    });
}

void Launcher::caseExtrasArrived(uint32_t generation, const std::string& key, std::vector<uint8_t> back, std::vector<uint8_t> side,
                                 const std::filesystem::path& manual) {
    if (generation != case_extras_generation_) return;
    if (!back.empty()) renderer_.setCaseBack(key, shareImage(std::move(back)));
    if (!side.empty()) renderer_.setCaseSide(key, shareImage(std::move(side)));
    if (!manual.empty()) {
        case_manuals_[key] = manual;
        auto& state = game_case_;
        if (state.key == key && !state.manual) {
            // Instrukcja comes in before Szczegóły: the focus stays on the button it was on.
            state.manual = true;
            if (state.focus >= 1) ++state.focus;
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::filesystem::path Launcher::gameManualFile(const Game& game) const {
    if (const auto found = case_manuals_.find(gameKey(game)); found != case_manuals_.end()) return found->second;
    if (!emulated(game)) return {};
    auto copy = case_extras::manualCopy(game, userConfigDirectory());
    return file_io::isFile(copy) ? copy : std::filesystem::path{};
}

bool Launcher::gameManualAvailable(const Game& game) const { return !gameManualFile(game).empty(); }

void Launcher::openGameManual(const Game& game) {
    const auto file = gameManualFile(game);
    if (file.empty()) { navigationBoundary(); return; }
    if (!pdf_pages::available()) {
        ShellExecuteW(hwnd_, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    auto& state = manual_;
    state.open = true; state.key = gameKey(game); state.file = file;
    state.pages = 0; state.spread = 0; state.zoomed = false; state.pan_x = state.pan_y = 0; state.failed = false;
    state.stick_x = state.stick_y = 0;
    state.broken.clear();
    ++state.generation;
    manual_pending_.clear();
    renderer_.clearManualPages();
    if (!settings_.screen_transitions) state.alpha = 1;
    audio_.context();
    requestManualPages();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeGameManual() {
    if (!manual_.open) return;
    manual_.open = false;
    if (!settings_.screen_transitions) manual_.alpha = 0;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The spread shown, then the next and the one before: drawn on a worker, one page after another.
void Launcher::requestManualPages() {
    auto& state = manual_;
    if (!state.open || state.failed) return;
    std::vector<int> wanted;
    const auto want = [&](int page) {
        if (page < 0 || (state.pages > 0 && page >= state.pages) || manual_pending_.contains(page) || renderer_.hasManualPage(page) ||
            state.isBroken(page)) return;
        if (std::find(wanted.begin(), wanted.end(), page) == wanted.end()) wanted.push_back(page);
    };
    if (state.pages <= 0) want(0); // Its first page tells how many there are.
    else
        for (const int spread : {state.spread, state.spread + 1, state.spread - 1}) {
            if (spread < 0 || spread >= manual_ui::spreadCount(state.pages)) continue;
            const auto [first, second] = manual_ui::pagesOf(spread, state.pages);
            want(first); want(second);
        }
    if (wanted.empty()) return;
    for (const int page : wanted) manual_pending_.insert(page);
    if (manual_render_) {
        // Tests: drawn at once.
        for (const int page : wanted) {
            auto rendered = manual_render_(state.file, page, manual_ui::page_width);
            manualPageArrived(state.generation, page, rendered.count, std::move(rendered.png));
        }
        return;
    }
    // The pages wanted, from one reading of the document; each posted as it is drawn.
    work_.start([file = state.file, wanted, generation = state.generation, alive = alive_, hwnd = hwnd_] {
        pdf_pages::render(file, wanted, manual_ui::page_width, [&](int page, pdf_pages::Page rendered) {
            if (!alive->load()) return false;
            if (rendered.png.empty())
                diagnostics::log("warning", L"Manual page " + std::to_wstring(page + 1) + L" of " + file.filename().wstring() + L" not drawn: " +
                                                (rendered.error.empty() ? std::wstring(L"no picture") : rendered.error));
            auto message = std::make_unique<RemoteMessage>();
            message->run = [generation, page, count = rendered.count, png = std::move(rendered.png)](Launcher& app) mutable {
                app.manualPageArrived(generation, page, count, std::move(png));
            };
            if (PostMessageW(hwnd, remote_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            return true;
        });
    });
}

void Launcher::manualPageArrived(uint32_t generation, int page, int count, std::vector<uint8_t> png) {
    auto& state = manual_;
    if (generation != state.generation) return;
    manual_pending_.erase(page);
    if (count > 0) state.pages = count;
    else if (page == 0 && state.pages <= 0) state.failed = true;
    if (!png.empty()) renderer_.setManualPage(page, shareImage(std::move(png)));
    else if (!state.isBroken(page)) state.broken.push_back(page); // Shown as such: no waiting on it.
    requestManualPages();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::manualTurn(int spreads) {
    auto& state = manual_;
    const int last = std::max(0, manual_ui::spreadCount(state.pages) - 1);
    const int next = std::clamp(state.spread + spreads, 0, last);
    if (next == state.spread) { navigationBoundary(); return; }
    state.spread = next;
    state.pan_x = state.pan_y = 0;
    audio_.navigate();
    requestManualPages();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::manualKey(WPARAM key, bool repeated) {
    auto& state = manual_;
    switch (key) {
    case VK_LEFT: case VK_PRIOR: manualTurn(-1); break;
    case VK_RIGHT: case VK_NEXT: case VK_TAB: manualTurn(1); break;
    case VK_UP: if (state.zoomed) state.pan_y = std::max(-1.0f, state.pan_y - 0.34f); break;
    case VK_DOWN: if (state.zoomed) state.pan_y = std::min(1.0f, state.pan_y + 0.34f); break;
    case VK_RETURN: case VK_SPACE:
        if (!repeated) { state.zoomed = !state.zoomed; state.pan_x = state.pan_y = 0; audio_.navigate(); }
        break;
    case VK_ESCAPE: case VK_BACK: if (!repeated) closeGameManual(); break;
    default: return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::manualClick(float x, float) {
    const auto view = renderer_.viewport();
    manualTurn(x < view.width * 0.5f ? -1 : 1);
}

bool Launcher::tickGameManual(float dt) {
    auto& state = manual_;
    const float alpha = state.alpha, pan_x = state.pan_x, pan_y = state.pan_y;
    state.alpha = state.open ? std::min(1.0f, state.alpha + dt * 6) : std::max(0.0f, state.alpha - dt * 6);
    if (state.open && state.zoomed && (state.stick_x != 0 || state.stick_y != 0)) {
        state.pan_x = std::clamp(state.pan_x + state.stick_x * dt * 1.4f, -1.0f, 1.0f);
        state.pan_y = std::clamp(state.pan_y - state.stick_y * dt * 1.4f, -1.0f, 1.0f);
    }
    return state.alpha != alpha || state.pan_x != pan_x || state.pan_y != pan_y;
}
