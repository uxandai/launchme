#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../artwork/esde_art.hpp"
#include "../artwork/failure_cache.hpp"
#include "../library/metadata_sources.hpp"
#include "../library/screenscraper.hpp"
#include <algorithm>

int Launcher::identifyDetailAction() const {
    // Only while nothing names the game yet; once identified, "…" has it (menuRows) for another match.
    if (screen_ != ScreenMode::details || cards_.empty() || !unidentifiedGame(cards_[0].game)) return -2;
    return 2;
}
void Launcher::openIdentify() {
    if (identifyDetailAction() < 0) return;
    openIdentifyFor(cards_[0].game);
}
bool Launcher::unidentifiedGame(const Game& game) const {
    if (!isGame(game) || steamArtwork(game) || game.kind == GameKind::catalog) return false;
    const auto key = gameKey(game);
    if (key.empty() || rawg_metadata_.contains(key) || emulation_metadata_.contains(key)) return false;
    // A store's description taken by "Zidentyfikuj" (the eShop for a Nintendo game) is its identification too.
    const auto* own = metadataOverride(game);
    return !own || own->description.empty();
}
void Launcher::openIdentifyFor(const Game& game) {
    if (!isGame(game) || steamArtwork(game) || gameKey(game).empty()) return;
    identify_state_ = {}; identify_state_.open = true; identify_state_.game = game; identify_state_.query = game.name;
    identify_state_.query_focus = false; // The lookup view has no field yet.
    identify_state_.emulated = game.kind == GameKind::nonsteam && game.platform != L"PC";
    // One click: every provider is asked in turn and the best match is taken; "Dopasuj ręcznie" is the way out.
    closeFriends(); dismissMenu(); identifySearch();
}
// "Dopasuj ręcznie": the title field, the provider choice and the list of matches.
void Launcher::identifyManual() {
    auto& state = identify_state_;
    if (!state.open || state.applying || state.manual) return;
    state.manual = true; state.query_focus = true; state.select_all = true; state.back_focus = false;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// The provider: Wszyscy dostawcy, RAWG, ScreenScraper, Nintendo eShop, Steam, GOG, Microsoft Store, TitleDB.
void Launcher::identifySetSource(identify_ui::State::Source source) {
    auto& state = identify_state_;
    if (!state.open || state.applying) return;
    state.menu = false;
    state.source = source;
    ++*identify_token_; state.busy = false; state.games.clear(); state.found.clear(); state.offers.clear(); state.status.clear(); state.selected = 0;
    audio_.context();
    identifySearch();
}
// ←/→ and F2: the next provider of the dropdown.
void Launcher::identifySwitchSource() {
    auto& state = identify_state_;
    if (!state.open || state.applying) return;
    const auto list = identify_ui::providers(state.emulated);
    size_t at = 0;
    for (size_t i = 0; i < list.size(); ++i) if (list[i] == state.source) at = i;
    identifySetSource(list[(at + 1) % list.size()]);
}
void Launcher::identifyToggleMenu() {
    auto& state = identify_state_;
    if (!state.open || state.applying || !state.manual) return;
    state.menu = !state.menu;
    if (state.menu) {
        const auto list = identify_ui::providers(state.emulated);
        state.menu_hover = 0;
        for (size_t i = 0; i < list.size(); ++i) if (list[i] == state.source) state.menu_hover = static_cast<int>(i);
    }
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifyPickProvider(int index) {
    const auto list = identify_ui::providers(identify_state_.emulated);
    if (index < 0 || index >= static_cast<int>(list.size())) { identify_state_.menu = false; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    identifySetSource(list[static_cast<size_t>(index)]);
}
void Launcher::closeIdentify() {
    if (identify_state_.applying) return;
    ++*identify_token_; identify_state_ = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifySearch() {
    using Source = identify_ui::State::Source;
    auto& state = identify_state_;
    if (!state.open || state.busy || state.query.empty()) return;
    state.busy = true; state.status = tr(L"Szukanie gry…"); state.games.clear(); state.found.clear(); state.offers.clear(); state.selected = 0;
    const auto token = identify_token_; const auto generation = ++*token;
    const auto alive = alive_; const auto hwnd = hwnd_; const auto query = state.query;
    const auto mode = state.source; const bool emulated = state.emulated;
    const bool automatic = !state.manual; // Nobody chooses: the stores' text details are the last resort.
    const auto game = state.game;
    if (runtime_started_) work_.start([token, generation, alive, hwnd, query, mode, emulated, automatic, game] {
        auto message = std::make_unique<IdentifyMessage>(); message->generation = generation;
        namespace ss = screenscraper;
        const auto searchScreenscraper = [&] {
            const auto config = ss::load();
            const auto metadata = userConfigDirectory() / L"metadata";
            if (!config.ready()) message->search.error = tr(L"Dodaj dane ScreenScraper w Ustawieniach → Media i dane.");
            else if (ss::paused(metadata)) message->search.error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później.");
            else {
                HttpBudget budget(20000);
                const auto systems = ss::systemIds(game.platform, game.rom);
                auto found = ss::search(config, query, systems.empty() ? 0 : systems.front(), i18n::language() == i18n::Language::pl ? L"pl" : L"en");
                switch (found.status) {
                case ss::Status::found:
                case ss::Status::missing:
                    for (auto& info : found.games) {
                        if (message->found.size() >= 30) break;
                        message->search.games.push_back({info.id, info.name, info.details.release_date,
                            info.details.developer.empty() ? info.details.publisher : info.details.developer});
                        message->found.push_back(std::move(info));
                    }
                    break;
                case ss::Status::rejected:
                    ss::pause(metadata, 60 * 60 * 1000);
                    message->search.error = tr(L"ScreenScraper odrzucił dane dewelopera (devid/devpassword) lub konto. Sprawdź Ustawienia → Biblioteka."); break;
                case ss::Status::quota:
                    ss::pause(metadata, 12ull * 60 * 60 * 1000);
                    message->search.error = tr(L"Dzienny limit zapytań ScreenScraper wyczerpany. Spróbuję jutro."); break;
                case ss::Status::throttled: ss::pause(metadata, 2 * 60 * 1000); [[fallthrough]];
                default: message->search.error = tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później.");
                }
            }
        };
        const auto lookupOf = [&] {
            metadata_sources::Query lookup;
            lookup.title = query; lookup.polish = i18n::language() == i18n::Language::pl;
            const bool own_title = query == game.name; // The game's own ids only for its own title.
            if (own_title && !game.rom.empty()) {
                lookup.switch_id = metadata_sources::titleId(game.rom.filename().wstring());
                if (lookup.switch_id.empty()) lookup.switch_id = metadata_sources::titleId(game.rom.parent_path().filename().wstring());
            }
            if (lookup.switch_id.empty()) lookup.switch_id = metadata_sources::titleId(query);
            if (own_title) {
                lookup.steam_app = game.app_id;
                if (game.source == GameSource::gog) lookup.gog_id = game.source_id;
                lookup.xbox_product = game.xbox_product;
            }
            return lookup;
        };
        // Wszyscy dostawcy, a Nintendo console's game: Nintendo's own eShop first; a sure match (its title ID,
        // or the very title) is the answer at once, without waiting for the databases.
        const bool nintendo = game.platform == L"Switch" || game.platform == L"Switch 2" || game.platform == L"Wii U" || game.platform == L"3DS" ||
                              game.platform == L"Nintendo 3DS";
        if (mode == Source::all && nintendo) {
            auto shop = metadata_sources::find(metadata_sources::Source::nintendo, lookupOf());
            for (auto& offer : shop.offers)
                if (offer.score > 0 && !offer.details.description.empty()) message->texts.push_back(std::move(offer));
            message->texts_first = !message->texts.empty();
            if (automatic && !message->texts.empty() && message->texts.front().score >= 930) {
                message->texts.resize(1);
                if (alive->load() && token->load() == generation && PostMessageW(hwnd, identify_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
                return;
            }
        }
        if (!alive->load() || token->load() != generation) return;
        // Wszyscy dostawcy: ScreenScraper first for an emulated game (its search knows the console) when it is set up.
        const bool store_only = identify_ui::storeSource(mode); // One store: its text details only.
        if (!store_only && (mode == Source::screenscraper || (mode == Source::all && emulated && ss::load().ready()))) {
            searchScreenscraper();
            message->screenscraper = !message->search.games.empty();
        }
        if (!store_only && !message->screenscraper && mode != Source::screenscraper) {
            message->search = rawg::search(query, userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt"));
            message->found.clear();
        }
        // Wszyscy dostawcy: also the stores' descriptions (Nintendo eShop, Steam, GOG, Microsoft Store, TitleDB), below the
        // databases' games; on its own (nobody chooses) only when no database knows the game.
        if (store_only || (mode == Source::all && (!automatic || message->search.games.empty() || message->texts_first))) {
            const auto lookup = lookupOf();
            for (const auto source : metadata_sources::all) {
                if (!alive->load() || token->load() != generation) break;
                if (store_only && source != identify_ui::storeOf(mode)) continue;
                if (source == metadata_sources::Source::nintendo && mode == Source::all && nintendo) continue; // Asked first, above.
                if (!metadata_sources::applicable(source, lookup)) continue;
                for (auto& offer : metadata_sources::find(source, lookup).offers)
                    if (offer.score > 0 && !offer.details.description.empty()) message->texts.push_back(std::move(offer));
            }
            std::stable_sort(message->texts.begin(), message->texts.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
            if (message->texts.size() > 8) message->texts.resize(8);
        }
        if (alive->load() && token->load() == generation && PostMessageW(hwnd, identify_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifyAccept() {
    auto& state = identify_state_;
    if (state.open && state.back_focus && !state.applying) { closeIdentify(); return; }
    if (!state.open || state.busy) return;
    if (!state.manual && state.games.empty()) { identifyManual(); return; } // Enter on the lookup: choose by hand.
    if (state.query_focus || state.games.empty()) { identifySearch(); return; }
    if (static_cast<size_t>(state.selected) < state.offers.size() && state.offers[static_cast<size_t>(state.selected)]) {
        identifyApplyText(*state.offers[static_cast<size_t>(state.selected)]); // A store's description: nothing to download (copied: the call closes the dialog).
        return;
    }
    const auto candidate = state.games[state.selected]; const auto game = state.game;
    state.busy = state.applying = true; state.status = tr(L"Pobieranie metadanych i brakujących grafik…");
    const auto token = identify_token_; const auto generation = ++*token;
    const auto alive = alive_; const auto hwnd = hwnd_;
    const auto row = static_cast<size_t>(state.selected);
    if (state.resolved == identify_ui::State::Source::screenscraper && row >= state.found_offset && row - state.found_offset < state.found.size()) {
        // ScreenScraper: the chosen game becomes this ROM's identification (it wins over ES-DE's match),
        // replacing a RAWG one; automatic artwork found by the old title goes.
        const auto picked = state.found[row - state.found_offset];
        if (runtime_started_) work_.start([token, generation, alive, hwnd, game, picked] {
            namespace ss = screenscraper;
            auto message = std::make_unique<IdentifyMessage>(); message->generation = generation; message->identified = true;
            const auto root = userConfigDirectory();
            const auto rom = ss::describeRom(game.rom, 0);
            ss::Cached entry{rom ? ss::fingerprint(game, *rom) : std::wstring{}, ss::unixMs(), picked, true};
            if (!ss::saveCached(game, entry, root / L"metadata")) message->error = tr(L"Nie można zapisać metadanych.");
            else {
                std::error_code ec;
                std::filesystem::remove(root / L"metadata" / (gameCacheKey(game) + ".rawg.json"), ec);
                removeGameArtwork(game, coverCacheDirectory(), false);
                for (const auto kind : {EmuArtKind::cover, EmuArtKind::background, EmuArtKind::logo})
                    std::filesystem::remove(esdeArtCachePath(game.app_id, kind, root / L"emulation"), ec);
                message->picked = picked;
            }
            if (alive->load() && token->load() == generation && PostMessageW(hwnd, identify_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (runtime_started_) work_.start([token, generation, alive, hwnd, game, candidate] {
        HttpBudget budget(25000);
        const auto active = [&] { return alive->load() && token->load() == generation; };
        auto message = std::make_unique<IdentifyMessage>(); message->generation = generation; message->identified = true;
        const auto key = userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt");
        auto details = rawg::fetch(candidate.id, key);
        message->error = details.error;
        if (details.data && active()) {
            const auto root = userConfigDirectory();
            const auto previous = rawg::load(game, root / L"metadata");
            if (previous && previous->id == candidate.id && previous->steam_app_id) details.data->steam_app_id = previous->steam_app_id;
            else details.data->steam_app_id = rawg::stores(candidate.id, key).steam_app_id;
            if (!active()) return;
            if (!rawg::save(game, details, root / L"metadata")) message->error = tr(L"Nie można zapisać metadanych RAWG.");
            else {
                message->metadata = std::move(details.data);
                ratings::saveIdentified(root / L"ratings", game, message->metadata->rating);
                // Automatic artwork was matched by the old title and may show another game of
                // that name: only the user's own picks survive an identification.
                removeGameArtwork(game, coverCacheDirectory(), false);
                const auto& released = message->metadata->details.release_date;
                const int year = released.size() >= 4 ? _wtoi(released.substr(0, 4).c_str()) : 0;
                message->artwork = fillIdentifiedArtwork(game, message->metadata->name, message->metadata->steam_app_id,
                    coverCacheDirectory(), userSecret(L"STEAMGRIDDB_API_KEY", L"steamgriddb_api_key.txt"), active, httpGet, year);
            }
        }
        if (alive->load() && token->load() == generation && PostMessageW(hwnd, identify_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::onIdentify(std::unique_ptr<IdentifyMessage> message) {
    auto& state = identify_state_;
    if (!state.open || message->generation != identify_token_->load()) return;
    state.busy = state.applying = false;
    if (!message->identified) {
        state.status = std::move(message->search.error); state.selected = 0;
        state.found = std::move(message->found);
        state.games.clear(); state.offers.clear();
        const auto addTexts = [&] {
            for (auto& offer : message->texts) {
                std::wstring subtitle = metadata_sources::name(offer.source);
                if (!offer.note.empty()) subtitle += L" · " + offer.note;
                state.games.push_back({0, offer.title, offer.details.release_date, std::move(subtitle)});
                state.offers.push_back(std::move(offer));
            }
        };
        // A Nintendo game: the eShop's rows lead (the automatic lookup takes the first row).
        if (message->texts_first) addTexts();
        const size_t database_row = state.games.size();
        for (auto& row : message->search.games) { state.games.push_back(std::move(row)); state.offers.push_back(std::nullopt); }
        if (!message->texts_first) addTexts();
        state.found_offset = database_row;
        state.resolved = message->screenscraper ? identify_ui::State::Source::screenscraper : identify_ui::State::Source::rawg;
        state.provider = message->screenscraper ? L"ScreenScraper" : L"RAWG";
        if (!state.manual && !state.games.empty()) { // The best match, taken: no click.
            state.query_focus = false;
            identifyAccept();
            return;
        }
        if (state.status.empty() && state.games.empty()) state.status = tr(L"Brak dopasowań. Zmień tytuł i wyszukaj ponownie.");
        state.query_focus = state.games.empty();
        if (state.games.empty()) { state.manual = true; state.select_all = true; } // Nothing found by itself: the player types.
    } else if (message->picked) {
        // ScreenScraper identification (emulated games): details, name and every artwork slot again.
        const auto game = state.game; const auto key = gameKey(game);
        rawg_metadata_.erase(key);
        emulation_metadata_[key] = message->picked->details;
        emulation_names_[key] = message->picked->name;
        manual_names_.insert(key); // The player's pick: its name shows even with ROM titles off.
        if (screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == key) details_ = message->picked->details;
        const auto failures = coverCacheDirectory().parent_path() / L"artwork" / L"failures";
        // The worker removed every automatic kind: posters and wide tiles load again when a view asks.
        for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::hero, CoverMessage::Kind::logo,
                                CoverMessage::Kind::poster, CoverMessage::Kind::landscape}) {
            if (auto* cached = art_.find(key)) {
                (kind == CoverMessage::Kind::cover ? cached->cover : kind == CoverMessage::Kind::hero ? cached->hero :
                 kind == CoverMessage::Kind::logo ? cached->logo : kind == CoverMessage::Kind::poster ? cached->poster : cached->landscape) = {};
                art_.resize(key);
            }
            artwork_failure::retry(failures, artRequestKey(game, kind), artwork_failure::now());
            invalidateArtwork(game, kind);
            if (kind == CoverMessage::Kind::cover || kind == CoverMessage::Kind::hero || kind == CoverMessage::Kind::logo) requestArt(game, kind);
        }
        closeIdentify(); audio_.context();
        applyEmulationNames(); // The new name everywhere: tiles, the game's menu, details.
        requestVisibleArt();
    } else if (!message->metadata) { state.status = std::move(message->error); state.manual = true; state.query_focus = false; }
    else {
        const auto game = state.game; const auto key = gameKey(game);
        rawg_metadata_[key] = std::move(*message->metadata);
        if (screen_ == ScreenMode::details && !detail_games_.empty() && gameKey(detail_games_[0]) == key) details_ = rawg_metadata_[key].details;
        for (auto& image : message->artwork.images) {
            const auto kind = image.kind == ArtworkKind::cover ? CoverMessage::Kind::cover :
                image.kind == ArtworkKind::hero ? CoverMessage::Kind::hero : image.kind == ArtworkKind::logo ? CoverMessage::Kind::logo :
                image.kind == ArtworkKind::poster ? CoverMessage::Kind::poster : CoverMessage::Kind::landscape;
            invalidateArtwork(game, kind);
            const auto request = artRequestKey(game, kind);
            artwork_failure::retry(coverCacheDirectory().parent_path() / L"artwork" / L"failures", request, artwork_failure::now());
            auto cover = std::make_unique<CoverMessage>();
            cover->key = key; cover->kind = kind; cover->generation = jobs_->generation.load();
            cover->revision = art_revisions_[request]; cover->bytes = std::move(image.bytes); cover->focus = image.focus;
            onCover(std::move(cover)); // Same immediate binding for cover, hero, logo and both Flix shapes.
        }
        // The worker dropped artwork matched by the old title; slots it could not fill now
        // load again from the confirmed source instead of showing the other game's images.
        const auto filled = [&](ArtworkKind kind) {
            return std::any_of(message->artwork.images.begin(), message->artwork.images.end(), [&](const auto& image) { return image.kind == kind; });
        };
        const std::pair<ArtworkKind, CoverMessage::Kind> slots[]{{ArtworkKind::cover, CoverMessage::Kind::cover},
            {ArtworkKind::hero, CoverMessage::Kind::hero}, {ArtworkKind::logo, CoverMessage::Kind::logo},
            {ArtworkKind::poster, CoverMessage::Kind::poster}, {ArtworkKind::landscape, CoverMessage::Kind::landscape}};
        for (const auto& [artwork, kind] : slots) {
            if (filled(artwork)) continue;
            if (auto* cached = art_.find(key)) {
                (kind == CoverMessage::Kind::cover ? cached->cover : kind == CoverMessage::Kind::hero ? cached->hero :
                 kind == CoverMessage::Kind::logo ? cached->logo : kind == CoverMessage::Kind::poster ? cached->poster : cached->landscape) = {};
                art_.resize(key);
            }
            invalidateArtwork(game, kind);
            artwork_failure::retry(coverCacheDirectory().parent_path() / L"artwork" / L"failures", artRequestKey(game, kind), artwork_failure::now());
            if (kind == CoverMessage::Kind::cover || kind == CoverMessage::Kind::hero || kind == CoverMessage::Kind::logo) requestArt(game, kind);
        }
        if (game.source == GameSource::drmfree) drmIdentified(game, rawg_metadata_[key]);
        else if (game.kind == GameKind::nonsteam && !game.rom.empty()) applyEmulationNames(); // An emulated game takes the RAWG title.
        closeIdentify(); audio_.context();
        customEditorIdentified(key); // Opened from Dodaj / Edytuj grę: its poster is the identified game's now.
        requestVisibleArt(); // Posters and wide tiles the views show (the desktop grid's) load again.
        if (!message->artwork.error.empty()) showNotification(NotificationKind::launch, tr(L"Metadane zapisane"), message->artwork.error);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// No database knew the game: the best store's description, developer, publisher, date and genres become its own
// details (the same place as Edytuj metadane; what the player typed there stays).
void Launcher::identifyApplyText(metadata_sources::Offer offer) {
    const auto game = identify_state_.game; const auto key = gameKey(game);
    metadataOverride(game); // Loads the file.
    metadata::Override value;
    if (const auto* existing = metadata_overrides_.find(key)) value = *existing;
    const auto fill = [](std::wstring& field, const std::wstring& text) { if (field.empty()) field = text; };
    fill(value.description, offer.details.description); fill(value.developer, offer.details.developer);
    fill(value.publisher, offer.details.publisher); fill(value.release_date, offer.details.release_date); fill(value.genres, offer.details.genres);
    metadata_overrides_.set(key, std::move(value));
    const bool saved = metadata_overrides_.save(userConfigDirectory() / L"metadata_overrides.json");
    closeIdentify(); audio_.context();
    if (!saved) { showNotification(NotificationKind::error, tr(L"Nie udało się zapisać metadanych"), tr(L"Sprawdź dostęp do katalogu konfiguracji.")); return; }
    desktopMetadataChanged();
    showNotification(NotificationKind::launch, tr(L"Metadane zapisane"), std::wstring(metadata_sources::name(offer.source)) + L": " + offer.title);
}
void Launcher::identifyMove(int amount) {
    auto& state = identify_state_;
    if (!state.open || state.applying || !amount) return;
    // Back above the search field: Up from the field, Down returns to it.
    if (state.back_focus) {
        if (amount > 0) { state.back_focus = false; state.query_focus = state.select_all = true; audio_.navigate(); }
        else navigationBoundary();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.query_focus && amount < 0) {
        state.back_focus = true; state.query_focus = false; audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (state.busy || state.games.empty()) return;
    if (state.query_focus) { if (amount > 0) state.query_focus = false; }
    else if (amount < 0 && state.selected == 0) { state.query_focus = true; state.select_all = true; }
    else {
        const int next = std::clamp(state.selected + amount, 0, static_cast<int>(state.games.size()) - 1);
        if (next == state.selected) navigationBoundary();
        state.selected = next;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifyEdit(wchar_t character) {
    auto& state = identify_state_;
    if (!state.open || state.applying || character < L' ' || character == 127) return;
    if (!state.manual) identifyManual(); // Typing means choosing by hand; the first letter replaces the title.
    if (state.select_all) { state.query.clear(); state.select_all = false; }
    if (state.query.size() < 160) state.query += character;
    ++*identify_token_; state.busy = false; state.query_focus = true; state.back_focus = false; state.games.clear(); state.status.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifyKey(WPARAM key, bool repeated) {
    auto& state = identify_state_;
    if (key == VK_ESCAPE) {
        if (state.menu) { state.menu = false; InvalidateRect(hwnd_, nullptr, FALSE); return; } // The dropdown first.
        closeIdentify();
        return;
    }
    if (state.applying) return;
    if (state.menu) { // The provider dropdown: Up and Down choose, Enter takes.
        const int count = static_cast<int>(identify_ui::providers(state.emulated).size());
        if (key == VK_UP) state.menu_hover = std::max(0, state.menu_hover - 1);
        else if (key == VK_DOWN) state.menu_hover = std::min(count - 1, state.menu_hover + 1);
        else if (key == VK_RETURN && !repeated) { identifyPickProvider(state.menu_hover); return; }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!state.manual) { // Looking the game up on its own: Enter chooses by hand, the rest waits.
        if (key == VK_RETURN && !repeated) identifyManual();
        else if (key == VK_UP || key == VK_DOWN || key == VK_TAB) identifyManual();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (key == VK_F2 && !repeated) { identifySwitchSource(); return; }
    if ((key == VK_LEFT || key == VK_RIGHT) && !state.query_focus && !state.back_focus) { identifySwitchSource(); return; }
    if (key == VK_RETURN && !repeated) identifyAccept();
    else if (key == VK_UP || key == VK_DOWN) identifyMove(key == VK_UP ? -1 : 1);
    else if (key == VK_TAB && !repeated) { state.query_focus = !state.query_focus || state.games.empty(); state.select_all = state.query_focus; state.back_focus = false; }
    else if (key == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) { state.query_focus = true; state.select_all = true; state.back_focus = false; }
    else if (key == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) && OpenClipboard(hwnd_)) {
        const auto data = GetClipboardData(CF_UNICODETEXT);
        const auto* text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
        if (text) {
            const auto count = std::min<size_t>(GlobalSize(data) / sizeof(wchar_t), 160);
            for (size_t i = 0; i < count && text[i]; ++i) identifyEdit(text[i]);
            GlobalUnlock(data);
        }
        CloseClipboard();
    }
    else if (key == VK_BACK || key == VK_DELETE) {
        if (state.select_all) state.query.clear(); else if (!state.query.empty()) state.query.pop_back();
        state.select_all = false; state.query_focus = true; state.back_focus = false; state.busy = false; state.games.clear(); state.status.clear(); ++*identify_token_;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::identifyClick(float x, float y) {
    const auto view = renderer_.viewport(); const identify_ui::Layout layout{view.width, view.height};
    if (layout.back().contains(x, y) || !layout.panel().contains(x, y)) { closeIdentify(); return; }
    if (identify_state_.applying) return;
    if (!identify_state_.manual) {
        if (layout.manualButton().contains(x, y)) identifyManual();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (identify_state_.menu) { // Over the list: a provider; anywhere else closes it.
        const int row = layout.menuHit(x, y, static_cast<int>(identify_ui::providers(identify_state_.emulated).size()));
        if (row >= 0) identifyPickProvider(row); else identify_state_.menu = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (layout.source().contains(x, y)) { identifyToggleMenu(); return; }
    if (layout.submit().contains(x, y)) identifySearch();
    else if (layout.search().contains(x, y)) { identify_state_.query_focus = true; identify_state_.select_all = true; identify_state_.back_focus = false; }
    else if (const int row = layout.hit(x, y, identify_state_); row >= 0 && !identify_state_.busy) {
        identify_state_.selected = row; identify_state_.query_focus = identify_state_.back_focus = false; identifyAccept();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
