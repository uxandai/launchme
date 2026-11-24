#include "../src/platform/flac.hpp"
#include "../src/platform/music_themes.hpp"
#include "../src/platform/sound_cues.hpp"

#include "../src/platform/wave_format.hpp"
#ifdef _WIN32
#include <span>
#include <Windows.h>
#include <mfapi.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
#ifdef _WIN32
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) { CoUninitialize(); return 1; }
    const auto finish = [](int code) { MFShutdown(); CoUninitialize(); return code; };
#else
    const auto finish = [](int code) { return code; }; // FFmpeg needs no start or end.
#endif
    // Every built-in theme ships in assets/ and decodes; "Brak" has no file.
    for (const auto& theme : music_themes) {
        if (!theme.file) continue;
        const auto file = localSoundFile(theme.file);
        const auto clip = decodeFlac(file, music_pcm_limit);
        if (file.parent_path().filename() != L"assets" || clip.format.size() < sizeof(WAVEFORMATEX) || clip.samples.empty()) {
            std::wcerr << L"Bundled theme missing or failed to decode: " << theme.file << L'\n';
            return finish(5);
        }
        const auto* wave = reinterpret_cast<const WAVEFORMATEX*>(clip.format.data());
        if (!wave->nAvgBytesPerSec || !decodeFlac(file, music_pcm_limit, [] { return true; }).samples.empty()) return finish(6);
        std::wcout << theme.file << L": " << double(clip.samples.size()) / wave->nAvgBytesPerSec << L" seconds\n";
    }
    // The interface sounds name no file that Launchme does not ship; UI SFX's cues beside its focus one too.
    const auto decodes = [](const std::filesystem::path& file) {
        const auto clip = decodeFlac(file, 8 * 1024 * 1024);
        return !file.empty() && clip.format.size() >= sizeof(WAVEFORMATEX) && !clip.samples.empty();
    };
    for (const std::span<const SoundOption> options : {std::span<const SoundOption>(ui_sounds), std::span<const SoundOption>(toast_sounds)})
        for (const auto& option : options) {
            if (!option.file) continue;
            const auto file = localSoundFile(option.file);
            if (!decodes(file)) { std::wcerr << L"Interface sound missing or failed to decode: " << option.file << L'\n'; return finish(7); }
            if (uiSoundPack(&option))
                for (const auto* name : {L"open.mp3", L"back.mp3", L"start.mp3"})
                    if (!decodes(file.parent_path() / name)) { std::wcerr << L"UI SFX cue missing: " << name << L'\n'; return finish(7); }
        }
    for (const auto& option : boot_sounds) if (option.file && localSoundFile(option.file).empty()) return finish(8);
    // Only assets/ are searched for built-in names; a sound folder lists its own files.
    if (!localSoundFile(L"2-04. (Bonus) Home Menu - No Intro.flac").empty()) return finish(9);
    const auto folder = std::filesystem::temp_directory_path() / L"launchme-soundtrack-test";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder);
    if (!soundFolderFile(folder, L"start.wav").empty() || !soundFolderFile({}, L"start.wav").empty()) return finish(10);
    { std::ofstream(folder / L"Start.WAV", std::ios::binary); }
    { std::ofstream(folder / L"notes.txt", std::ios::binary); }
    const auto files = soundFolderFiles(folder);
    if (files.size() != 1 || files.front() != L"Start.WAV" || soundFolderFile(folder, L"start.wav") != folder / L"Start.WAV" ||
        !soundFolderFile(folder, L"notes.txt").empty() || !soundFolderFile(folder, L"..\\Start.WAV").empty()) return finish(11);
    std::filesystem::remove_all(folder, error);
    return finish(0);
}
