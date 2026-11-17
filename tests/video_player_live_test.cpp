// Manual check (not in CTest): video_player_live_test.exe <video file>
// Plays the file muted for a few seconds and reports the frames VideoPlayer copied out.
#include "../src/platform/video_player.hpp"
#include "../src/store/nintendo_trailer.hpp"
#include "../src/store/steam_trailer.hpp"
#include "../src/artwork/image_compact.hpp"
#include <fstream>
#include <iterator>

#include <Windows.h>
#include <shellapi.h>

#include <iostream>
#include <thread>

int main(int argc, char**) {
    if (argc < 2) { std::cerr << "usage: video_player_live_test <file>\n"; return 2; }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
    int wide_count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    std::filesystem::path file = wide && wide_count > 1 ? wide[1] : L"";
    // --compact <folder>: what artwork compaction would save there (nothing is written).
    if (file == L"--compact" && wide_count > 2) {
        uint64_t before = 0, after = 0; size_t files = 0;
        std::error_code ec;
        for (const auto& item : std::filesystem::recursive_directory_iterator(wide[2], ec)) {
            ImageRole role{};
            if (!item.is_regular_file() || !imageRoleFor(item.path(), role)) continue;
            std::ifstream input(item.path(), std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            const auto compact = compactImage(bytes, role);
            before += bytes.size(); after += compact.empty() ? bytes.size() : compact.size(); ++files;
        }
        std::cout << "files=" << files << " before_mb=" << before / (1024 * 1024) << " after_mb=" << after / (1024 * 1024) << '\n';
        LocalFree(wide); CoUninitialize(); return 0;
    }
    // --steam <AppID>: the store trailer clip first (into a scratch folder), then playback.
    if (file == L"--steam" && wide_count > 2) {
        const auto scratch = std::filesystem::temp_directory_path() / L"launchme-trailer-live";
        file = steamTrailerFile(static_cast<uint32_t>(_wtoi(wide[2])), scratch, scratch);
        std::wcout << L"trailer=" << file.wstring() << L'\n';
        if (file.empty()) { LocalFree(wide); CoUninitialize(); return 6; }
    }
    // --switch / --switch2 <Title>: the Nintendo store trailer clip first, then playback.
    if ((file == L"--switch" || file == L"--switch2") && wide_count > 2) {
        const auto scratch = std::filesystem::temp_directory_path() / L"launchme-trailer-live";
        Game game;
        game.kind = GameKind::nonsteam;
        game.source = GameSource::custom;
        game.platform = file == L"--switch2" ? L"Switch 2" : L"Switch";
        game.name = wide[2];
        const auto h = static_cast<uint32_t>(std::hash<std::wstring>{}(game.platform + L":" + game.name)) | 1u;
        game.app_id = h;
        game.shortcut_id = (static_cast<uint64_t>(h) << 32) | 0x02000000ULL;
        file = nintendoTrailerFile(game, scratch, scratch);
        std::wcout << L"trailer=" << file.wstring() << L'\n';
        if (file.empty()) { LocalFree(wide); CoUninitialize(); return 7; }
    }
    int result = 0;
    {
        VideoPlayer player;
        if (!player.open(file, 0.0)) { std::cerr << "open failed\n"; result = 4; }
        else {
            std::vector<uint8_t> pixels;
            uint32_t width = 0, height = 0, frames = 0;
            uint64_t lit = 0;
            const auto start = GetTickCount64();
            while (GetTickCount64() - start < 4000 && !player.failed()) {
                if (player.frame(pixels, width, height)) {
                    ++frames;
                    lit = 0;
                    for (size_t i = 0; i + 3 < pixels.size(); i += 4) lit += pixels[i] + pixels[i + 1] + pixels[i + 2] > 60;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            std::cout << "frames=" << frames << " size=" << width << "x" << height << " lit_pixels=" << lit
                      << " failed=" << player.failed() << '\n';
            if (!frames || !lit) result = 5;
        }
    }
    LocalFree(wide);
    CoUninitialize();
    return result;
}
