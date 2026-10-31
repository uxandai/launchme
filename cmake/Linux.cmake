# Linux build (docs/linux-port.md). Included from the top-level CMakeLists.txt instead of the
# Windows build: the shared core (library scanning, stores, network, companion, i18n), the tests
# that cover it and the launcher itself (cmake/LinuxApp.cmake).

include(FetchContent)
# Strict ISO C++: GNU mode predefines the macro `unix`, which the companion code uses as a name.
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
find_package(CURL REQUIRED)
find_path(SDL3_INCLUDE_DIR SDL3/SDL.h REQUIRED)
find_library(SDL3_LIBRARY NAMES SDL3 REQUIRED)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_PROJECT OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 174573d60290f447c13a2b1b3405de2b96e27d6c)
FetchContent_MakeAvailable(miniz)
include(cmake/QrCode.cmake)

set(LAUNCHME_I18N_SOURCES src/i18n/i18n.cpp src/i18n/strings_en.cpp src/i18n/strings_pt_br.cpp src/i18n/strings_es.cpp)

function(launchme_target_defaults name)
    target_compile_features(${name} PRIVATE cxx_std_20)
    # Partial aggregate initialisation ({200}) is the codebase's style.
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wno-missing-field-initializers -finput-charset=UTF-8
        -include "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/crt_compat.hpp")
    target_compile_definitions(${name} PRIVATE LAUNCHME_VERSION="${PROJECT_VERSION}")
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endfunction()

# The core sources ported so far; each moves here once it builds and its tests pass on Linux.
set(LAUNCHME_CORE_SOURCES
    ${LAUNCHME_I18N_SOURCES}
    src/artwork/background_focus.cpp
    src/artwork/battlenet_art.cpp
    src/artwork/block_compression.cpp
    src/artwork/bulk_covers.cpp
    src/artwork/cover_cache.cpp
    src/artwork/download_art.cpp
    src/artwork/esde_art.cpp
    src/artwork/failure_cache.cpp
    src/artwork/gog_art.cpp
    src/artwork/identified_artwork.cpp
    src/artwork/steamgriddb_search.cpp
    src/friends/steam_friends.cpp
    src/library/achievement_history.cpp
    src/library/binary_kv.cpp
    src/library/collections.cpp
    src/library/drm_free_titles.cpp
    src/library/emulation_scan.cpp
    src/library/esde_gamelist.cpp
    src/library/game_index.cpp
    src/library/game_index_service.cpp
    src/library/game_index_sources.cpp
    src/library/game_ratings.cpp
    src/library/metadata_overrides.cpp
    src/library/organization.cpp
    src/library/owned_games.cpp
    src/library/play_journal.cpp
    src/library/rawg.cpp
    src/library/screenscraper.cpp
    src/library/screenscraper_config.cpp
    src/library/shortcuts.cpp
    src/library/steam.cpp
    src/library/steam_achievements.cpp
    src/library/steam_appinfo.cpp
    src/library/steam_owned.cpp
    src/library/steam_stats.cpp
    src/library/steam_sync_state.cpp
    src/library/steam_tags_builtin.cpp
    src/library/store_adapter.cpp
    src/library/studios.cpp
    src/library/tarot.cpp
    src/library/ubisoft.cpp
    src/library/ubisoft_catalog.cpp
    src/library/versions.cpp
    src/library/view_options.cpp
    src/library/visibility.cpp
    src/net/companion.cpp
    src/net/companion_chats.cpp
    src/net/companion_cognition.cpp
    src/net/companion_events.cpp
    src/net/companion_feed_social.cpp
    src/net/companion_lines.cpp
    src/net/companion_links.cpp
    src/net/companion_memory.cpp
    src/net/companion_mind.cpp
    src/net/companion_mind_appraisal.cpp
    src/net/companion_mind_life.cpp
    src/net/companion_mind_store.cpp
    src/net/companion_mind_turns.cpp
    src/net/companion_mind_workspace.cpp
    src/net/companion_photos.cpp
    src/net/companion_prompt.cpp
    src/net/companion_prompt_character.cpp
    src/net/companion_providers.cpp
    src/net/companion_reply.cpp
    src/net/companion_semantic.cpp
    src/net/companion_social.cpp
    src/net/companion_tools.cpp
    src/net/companion_transport.cpp
    src/net/companion_weather.cpp
    src/net/elevenlabs.cpp
    src/net/http.cpp
    src/net/http_curl.cpp
    src/net/http_download.cpp
    src/net/json.cpp
    src/net/protondb.cpp
    src/net/steam_upcoming.cpp
    src/net/web_search.cpp
    src/platform/hashing.cpp
    src/platform/library_state.cpp
    src/platform/protected_file.cpp
    src/platform/unicode_text.cpp
    src/platform/user_config.cpp
    src/plugins/achievements.cpp
    src/plugins/manager.cpp
    src/plugins/manifest.cpp
    src/plugins/package.cpp
    src/store/ea_account.cpp
    src/store/epic_api.cpp
    src/store/epic_service.cpp
    src/store/epic_vault.cpp
    src/store/game_details.cpp
    src/store/gog_api.cpp
    src/store/gog_config.cpp
    src/store/gog_install.cpp
    src/store/gog_service.cpp
    src/store/gog_vault.cpp
    src/store/steam_app_match.cpp
    src/store/steam_trailer.cpp
    src/store/web_import.cpp)
set(LAUNCHME_CORE_LIBS miniz launchme_qrcodegen Threads::Threads ICU::uc ICU::i18n OpenSSL::Crypto PkgConfig::LIBSECRET CURL::libcurl)

# Sources ported per area (docs/linux-port.md, phase 1). Each block is edited by one piece of
# work at a time; keep additions (and its find_package/pkg_check_modules and
# list(APPEND LAUNCHME_CORE_LIBS ...) lines) inside its own block.

# Processes: launching games and store clients, suspending them, /proc.
set(LAUNCHME_LINUX_PROCESS_SOURCES
    src/library/steam_accounts.cpp
    src/library/steam_registry.cpp
    src/store/epic_legendary.cpp
    src/store/gog_install_native.cpp
    src/platform/game_activity.cpp
    src/platform/game_overlay_monitor_linux.cpp
    src/platform/launch.cpp
    src/platform/proc_fs.cpp
    src/platform/process_freeze.cpp
    src/platform/process_spawn.cpp
)

# Image decoding and encoding (WIC on Windows).
set(LAUNCHME_LINUX_IMAGE_SOURCES
    src/artwork/image_codec_linux.cpp
    src/artwork/image_compact.cpp
    src/artwork/screenscraper_art.cpp
    src/artwork/steamgriddb.cpp
    src/artwork/text_crop.cpp
    src/library/custom_games.cpp
    src/net/companion_feed.cpp
    src/net/glance.cpp
)
# Codecs: libpng, libjpeg-turbo, libwebp. Feeds: expat (in place of XmlLite).
pkg_check_modules(LAUNCHME_IMAGE_LIBS REQUIRED IMPORTED_TARGET libpng libwebp expat)
list(APPEND LAUNCHME_CORE_LIBS PkgConfig::LAUNCHME_IMAGE_LIBS)
# TurboJPEG 3 (tj3* API): the system's when it is new enough (Arch, Fedora, SteamOS), else the pinned
# release built statically (Ubuntu 24.04 and Debian 12 ship 2.1).
pkg_check_modules(LAUNCHME_TURBOJPEG IMPORTED_TARGET libturbojpeg>=3.0)
if(LAUNCHME_TURBOJPEG_FOUND)
    list(APPEND LAUNCHME_CORE_LIBS PkgConfig::LAUNCHME_TURBOJPEG)
else()
    include(ExternalProject)
    set(LAUNCHME_TURBOJPEG_PREFIX "${CMAKE_BINARY_DIR}/turbojpeg")
    ExternalProject_Add(launchme_turbojpeg_build
        URL https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.2/libjpeg-turbo-3.1.2.tar.gz
        URL_HASH SHA256=8f0012234b464ce50890c490f18194f913a7b1f4e6a03d6644179fa0f867d0cf
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${LAUNCHME_TURBOJPEG_PREFIX} -DCMAKE_INSTALL_LIBDIR=lib
            -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        BUILD_BYPRODUCTS "${LAUNCHME_TURBOJPEG_PREFIX}/lib/libturbojpeg.a")
    file(MAKE_DIRECTORY "${LAUNCHME_TURBOJPEG_PREFIX}/include")
    add_library(launchme_turbojpeg STATIC IMPORTED GLOBAL)
    set_target_properties(launchme_turbojpeg PROPERTIES IMPORTED_LOCATION "${LAUNCHME_TURBOJPEG_PREFIX}/lib/libturbojpeg.a"
        INTERFACE_INCLUDE_DIRECTORIES "${LAUNCHME_TURBOJPEG_PREFIX}/include")
    add_dependencies(launchme_turbojpeg launchme_turbojpeg_build)
    list(APPEND LAUNCHME_CORE_LIBS launchme_turbojpeg)
endif()

# Sockets, folders and files.
set(LAUNCHME_LINUX_FILES_SOURCES
    src/library/drm_free.cpp
    src/library/pe_version.cpp
    src/net/remote_server.cpp
    src/platform/file_listing.cpp
    src/net/companion_backup.cpp
    src/platform/steam_library_sync.cpp
)

# Game store discovery on Linux (Steam, Heroic, Lutris, Prism Launcher, ...).
set(LAUNCHME_LINUX_STORE_SOURCES
    src/library/battlenet.cpp
    src/library/heroic.cpp
    src/library/lutris.cpp
    src/library/multimc.cpp
    src/library/pc_platforms.cpp
)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
list(APPEND LAUNCHME_CORE_LIBS PkgConfig::SQLITE3)

# Companion assets and embeddings, updater.
set(LAUNCHME_LINUX_COMPANION_SOURCES
    src/net/companion_characters.cpp
    src/net/companion_embeddings.cpp
    src/platform/embedded_resources.cpp
    src/platform/updater.cpp
)
# ONNX Runtime (Linux x64), sentencepiece, SQLite and the model, pinned as on Windows.
include(cmake/CompanionEmbeddings.cmake)
# Her characters (generated/companion_manifest.inc) as the Windows build makes them; her pictures
# in launchme_companion_resources (embedded_resources::find), linked only where they are shown.
set(LAUNCHME_RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res")
include(cmake/CompanionManifest.cmake)
include(cmake/CompanionResources.cmake)
add_library(launchme_companion_manifest INTERFACE)
target_include_directories(launchme_companion_manifest INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/generated")
list(APPEND LAUNCHME_CORE_LIBS companion_ort companion_sqlite sentencepiece-static launchme_companion_manifest)

# System services: memory, sensors, power, Discord, downloads.
set(LAUNCHME_LINUX_SERVICE_SOURCES
    src/platform/discord_linux.cpp
    src/platform/memory_usage_linux.cpp
    src/platform/performance.cpp
    src/platform/power_linux.cpp
    src/platform/sensors.cpp
    src/platform/sensors_linux.cpp
    src/platform/steam_downloads_linux.cpp
)
# GDBus for systemd-logind (power_linux.cpp); dlopen for NVML (sensors_linux.cpp).
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
list(APPEND LAUNCHME_CORE_LIBS PkgConfig::GIO ${CMAKE_DL_LIBS})

# Audio and video: playback, FLAC, system volume, trailers.
set(LAUNCHME_LINUX_MEDIA_SOURCES
    src/platform/audio.cpp
    src/platform/audio_sdl.cpp
    src/platform/flac.cpp
    src/platform/flac_ffmpeg.cpp
    src/platform/system_audio_pulse.cpp
    src/platform/video_player_ffmpeg.cpp
    src/platform/volume_monitor.cpp
)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(LIBPULSE REQUIRED IMPORTED_TARGET libpulse)
if(NOT SDL3_INCLUDE_DIR IN_LIST CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES) # -I/usr/include would break #include_next.
    set_property(SOURCE src/platform/audio_sdl.cpp src/platform/video_player_ffmpeg.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}")
endif()
list(APPEND LAUNCHME_CORE_LIBS PkgConfig::FFMPEG PkgConfig::LIBPULSE "${SDL3_LIBRARY}")

# Game controllers: gamepads, Steam Controller, Bluetooth, device service.
set(LAUNCHME_LINUX_CONTROLLER_SOURCES
    src/platform/bluetooth_controllers_linux.cpp
    src/platform/bluez.cpp
    src/platform/device_service.cpp
    src/platform/device_service_linux.cpp
    src/platform/game_controllers.cpp
    src/platform/game_controllers_linux.cpp
    src/platform/linux_hid.cpp
    src/platform/steam_controller.cpp
    src/platform/steam_controller_linux.cpp
)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
if(NOT SDL3_INCLUDE_DIR IN_LIST CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES) # -I/usr/include would break #include_next.
    set_property(SOURCE src/platform/game_controllers_linux.cpp src/platform/steam_controller_linux.cpp
        APPEND PROPERTY INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}")
endif()
list(APPEND LAUNCHME_CORE_LIBS "${SDL3_LIBRARY}" PkgConfig::LIBSYSTEMD)

list(APPEND LAUNCHME_CORE_SOURCES ${LAUNCHME_LINUX_PROCESS_SOURCES} ${LAUNCHME_LINUX_IMAGE_SOURCES}
    ${LAUNCHME_LINUX_FILES_SOURCES} ${LAUNCHME_LINUX_STORE_SOURCES} ${LAUNCHME_LINUX_COMPANION_SOURCES}
    ${LAUNCHME_LINUX_SERVICE_SOURCES} ${LAUNCHME_LINUX_MEDIA_SOURCES} ${LAUNCHME_LINUX_CONTROLLER_SOURCES})

add_library(launchme_core STATIC ${LAUNCHME_CORE_SOURCES})
launchme_target_defaults(launchme_core)
target_include_directories(launchme_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(launchme_core PUBLIC ${LAUNCHME_CORE_LIBS})
get_target_property(miniz_includes miniz INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(miniz PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${miniz_includes}")

# The launcher itself: window, input, renderer and every screen (phase 3).
include(cmake/LinuxApp.cmake)

# Same signature as the Windows build's helper (the WIN32 flag is ignored).
function(launchme_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "WIN32" "TEST" "SOURCES;LIBS;DEFINES")
    set(sources ${ARG_SOURCES})
    list(REMOVE_ITEM sources ${LAUNCHME_CORE_SOURCES})
    add_executable(${name} ${sources})
    launchme_target_defaults(${name})
    # Tests' Win32 helpers (GetCurrentProcessId, Sleep, ...): tests/linux_compat/Windows.h.
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests/linux_compat")
    target_compile_definitions(${name} PRIVATE ${ARG_DEFINES})
    target_link_libraries(${name} PRIVATE launchme_core ${ARG_LIBS})
    if(ARG_TEST)
        add_test(NAME ${ARG_TEST} COMMAND ${name})
    endif()
endfunction()

if(BUILD_TESTING)
    launchme_executable(game_fps_tests TEST game_fps_history_and_overlay_bar SOURCES tests/game_fps_tests.cpp)
    launchme_executable(failure_cache_tests TEST persistent_artwork_failure_cache SOURCES tests/failure_cache_tests.cpp)
    launchme_executable(protondb_tests TEST protondb_and_steam_deck_ratings SOURCES tests/protondb_tests.cpp)
    launchme_executable(ludusavi_tests TEST ludusavi_save_backups SOURCES tests/ludusavi_tests.cpp src/library/ludusavi.cpp src/platform/helper_process.cpp)
    launchme_executable(controller_models_tests TEST controller_picture_classification SOURCES tests/controller_models_tests.cpp)
    launchme_executable(profile_data_tests TEST play_journal_achievement_history_and_playtime SOURCES tests/profile_data_tests.cpp)
    launchme_executable(visibility_tests TEST steam_visibility SOURCES tests/visibility_tests.cpp)
    launchme_executable(ink_bounds_tests TEST logo_ink_bounds SOURCES tests/ink_bounds_tests.cpp)
    launchme_executable(shortcuts_tests TEST steam_nonsteam_shortcuts SOURCES tests/shortcuts_tests.cpp)
    launchme_executable(category_menu_tests TEST long_collection_menu SOURCES tests/category_menu_tests.cpp)
    launchme_executable(search_tests TEST live_library_search SOURCES tests/search_tests.cpp)
    launchme_executable(retry_policy_tests TEST artwork_retry_backoff SOURCES tests/retry_policy_tests.cpp)
    launchme_executable(lru_cache_tests TEST artwork_memory_lru SOURCES tests/lru_cache_tests.cpp)
    launchme_executable(background_work_tests TEST owned_background_workers SOURCES tests/background_work_tests.cpp)
    launchme_executable(screensaver_tests TEST screensaver_idle_playlist_and_input SOURCES tests/screensaver_tests.cpp)
    launchme_executable(input_gesture_tests TEST guide_start_hold_gestures SOURCES tests/input_gesture_tests.cpp)

    # Tests per area, as their sources are ported (same areas as above).
    # Processes:
    launchme_executable(game_session_tests TEST game_session_music_lifecycle
        SOURCES tests/game_session_tests.cpp src/platform/game_activity.cpp src/platform/process_freeze.cpp)
    launchme_executable(process_linux_tests TEST linux_launch_job_and_steam_registry SOURCES tests/process_linux_tests.cpp)

    # Images:
    launchme_executable(text_crop_tests TEST text_aware_crop SOURCES tests/text_crop_tests.cpp)
    launchme_executable(low_memory_tests TEST low_memory_block_compression SOURCES tests/low_memory_tests.cpp)
    launchme_executable(glance_tests TEST glance_sources_and_cache SOURCES tests/glance_tests.cpp)
    launchme_executable(image_validation_tests TEST controller_png_transparency SOURCES tests/image_validation_tests.cpp)
    launchme_executable(background_art_tests TEST background_art_and_trailer_match SOURCES tests/background_art_tests.cpp)
    launchme_executable(steamgriddb_tests TEST steamgriddb_browser_and_selection SOURCES tests/steamgriddb_tests.cpp)
    launchme_executable(identified_artwork_tests TEST rawg_identification_missing_artwork SOURCES tests/identified_artwork_tests.cpp)
    launchme_executable(companion_feed_tests TEST companion_feed_publication_and_archive SOURCES tests/companion_feed_tests.cpp)
    launchme_executable(download_art_tests TEST download_artwork_sources_and_cache SOURCES tests/download_art_tests.cpp)
    launchme_executable(bulk_covers_tests TEST bulk_covers_missing_only_and_limits SOURCES tests/bulk_covers_tests.cpp)
    # GCC's -Wdangling-pointer misreads braced lists passed by value in these tests' conditions.
    set_source_files_properties(tests/glance_tests.cpp tests/identified_artwork_tests.cpp PROPERTIES COMPILE_OPTIONS -Wno-dangling-pointer)

    # Files and sockets:
    launchme_executable(remote_server_tests TEST remote_server_parsing_and_loopback SOURCES tests/remote_server_tests.cpp)
    launchme_executable(file_listing_tests TEST file_browser_listing_and_drives SOURCES tests/file_listing_tests.cpp)
    launchme_executable(change_watch_tests TEST folder_change_notifications SOURCES tests/change_watch_tests.cpp)
    launchme_executable(pe_version_tests TEST pe_version_resources SOURCES tests/pe_version_tests.cpp)
    launchme_executable(steam_library_sync_tests TEST steam_library_sync_outbox SOURCES tests/steam_library_sync_tests.cpp)
    find_program(LAUNCHME_NODE_EXECUTABLE node)
    if(LAUNCHME_NODE_EXECUTABLE)
        add_test(NAME steam_library_sync_bridge COMMAND "${LAUNCHME_NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/steam_library_bridge_tests.mjs")
    endif()

    # Stores:
    launchme_executable(linux_stores_tests TEST linux_heroic_lutris_prism_libraries SOURCES tests/linux_stores_tests.cpp)
    launchme_executable(pc_platforms_tests TEST native_pc_platform_libraries SOURCES tests/pc_platforms_tests.cpp)
    launchme_executable(battlenet_tests TEST battlenet_local_installation_lifecycle SOURCES tests/battlenet_tests.cpp)
    launchme_executable(ubisoft_tests TEST ubisoft_connect_client_library SOURCES tests/ubisoft_tests.cpp)
    launchme_executable(store_import_tests TEST ea_battlenet_website_imports SOURCES tests/store_import_tests.cpp)
    launchme_executable(store_art_tests TEST store_own_artwork SOURCES tests/store_art_tests.cpp)
    launchme_executable(owned_games_tests TEST steam_app_names SOURCES tests/owned_games_tests.cpp)
    launchme_executable(steam_owned_tests TEST steam_client_owned_library SOURCES tests/steam_owned_tests.cpp)

    # Companion and updater:
    launchme_executable(updater_tests TEST updater_release_package_and_swap SOURCES tests/updater_tests.cpp)
    launchme_executable(companion_social_tests TEST companion_social_rules_history_and_plans SOURCES tests/companion_social_tests.cpp)
    launchme_executable(companion_memory_tests TEST companion_weighted_memory_and_capacity SOURCES tests/companion_memory_tests.cpp)
    launchme_executable(companion_semantic_tests TEST companion_semantic_storage_and_privacy SOURCES tests/companion_semantic_tests.cpp)
    companion_embedding_assets(companion_semantic_tests)
    add_test(NAME companion_semantic_real_model COMMAND companion_semantic_tests --model)
    set_tests_properties(companion_semantic_real_model PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME companion_semantic_installed_package COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}" "-DTEST_EXE=$<TARGET_FILE:companion_semantic_tests>" "-DCONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/companion_semantic_install.cmake")
    set_tests_properties(companion_semantic_installed_package PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    launchme_executable(companion_links_tests TEST companion_bounded_link_previews SOURCES tests/companion_links_tests.cpp)
    launchme_executable(companion_feelings_tests TEST companion_named_feelings SOURCES tests/companion_feelings_tests.cpp)
    launchme_executable(companion_reply_style_tests TEST companion_reply_length_and_history SOURCES tests/companion_reply_style_tests.cpp)
    launchme_executable(companion_resources_tests TEST companion_embedded_pictures SOURCES tests/companion_resources_tests.cpp
        LIBS launchme_companion_resources)

    # System services:
    launchme_executable(sensors_tests TEST dashboard_temperature_sensors SOURCES tests/sensors_tests.cpp src/platform/sensors.cpp)
    launchme_executable(system_services_linux_tests TEST linux_system_services SOURCES tests/system_services_linux_tests.cpp)
    launchme_executable(steam_install_live_test SOURCES tests/steam_install_live_test.cpp)

    # Audio and video:
    add_custom_target(launchme_audio ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/res/HorizonsTheme.mp3" "${CMAKE_CURRENT_BINARY_DIR}/assets/HorizonsTheme.mp3"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/HorizonsTheme.mp3" VERBATIM)
    launchme_executable(soundtrack_tests TEST native_flac_decode SOURCES tests/soundtrack_tests.cpp src/platform/flac.cpp)
    add_dependencies(soundtrack_tests launchme_audio)
    launchme_executable(volume_monitor_tests TEST off_thread_system_volume SOURCES tests/volume_monitor_tests.cpp)
    launchme_executable(audio_output_tests TEST audio_cues_music_and_voice SOURCES tests/audio_output_tests.cpp)

    # Controllers:
    launchme_executable(controller_linux_tests TEST controller_linux_sysfs_and_bluez SOURCES tests/controller_linux_tests.cpp)
    launchme_executable(device_live_test SOURCES tests/device_live_test_linux.cpp)

endif()

# Linux areas built on the app (each in its own file).
include(cmake/linux/Stores.cmake)
include(cmake/linux/Wine.cmake)
include(cmake/linux/Browser.cmake)
