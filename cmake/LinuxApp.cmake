# Linux: the launcher's window, input, renderer and screens (docs/linux-port.md, phase 3), built
# from the same src/app and src/ui sources as Windows. Their Windows surface (src/linux/win32) is
# implemented by real ports: Direct2D and DirectWrite on Impeller (GPU: Vulkan, else OpenGL),
# WIC on image_codec (src/linux/gfx) and the window manager on SDL3 (src/linux/window).
include(cmake/Sources.cmake)

# Impeller (the renderer, text included), Clipper2 (path booleans), fontconfig and FreeType (fonts).
pkg_check_modules(LAUNCHME_FONT_DEPS REQUIRED IMPORTED_TARGET fontconfig freetype2)

set(LAUNCHME_WIN32_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/win32" "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/win32/lower")

# Direct2D, DirectWrite, WIC and COM's basics.
file(GLOB LAUNCHME_GFX_SOURCES CONFIGURE_DEPENDS
    src/linux/gfx/d2d_*.cpp src/linux/gfx/dwrite_*.cpp src/linux/gfx/wic_*.cpp src/linux/gfx/com_*.cpp)
add_library(launchme_gfx STATIC ${LAUNCHME_GFX_SOURCES})
launchme_target_defaults(launchme_gfx)
target_include_directories(launchme_gfx PUBLIC ${LAUNCHME_WIN32_INCLUDES})
target_link_libraries(launchme_gfx PUBLIC launchme_core)
include(cmake/Impeller.cmake)

# The window manager, input, clipboard, timers and the shell calls, on SDL3.
file(GLOB LAUNCHME_WINDOW_SOURCES CONFIGURE_DEPENDS src/linux/window/*.cpp)
add_library(launchme_window STATIC ${LAUNCHME_WINDOW_SOURCES})
launchme_target_defaults(launchme_window)
target_include_directories(launchme_window PUBLIC ${LAUNCHME_WIN32_INCLUDES})
target_include_directories(launchme_window PRIVATE "${SDL3_INCLUDE_DIR}")
target_link_libraries(launchme_window PUBLIC launchme_gfx "${SDL3_LIBRARY}")

# Every app and UI source the Windows build compiles, but the Windows-only ones, which have Linux
# counterparts listed after them.
set(LAUNCHME_UI_SOURCES ${LAUNCHME_APP_SOURCES})
list(FILTER LAUNCHME_UI_SOURCES INCLUDE REGEX "^src/(app|ui)/")
set(LAUNCHME_UI_WINDOWS_ONLY src/ui/browser_window.cpp src/ui/overlay_window.cpp)
list(REMOVE_ITEM LAUNCHME_UI_SOURCES ${LAUNCHME_UI_WINDOWS_ONLY})
list(APPEND LAUNCHME_UI_SOURCES src/ui/browser_window_linux.cpp src/ui/overlay_window_linux.cpp)
# Shared sources the app needs that the core does not build yet.
set(LAUNCHME_UI_SHARED ${LAUNCHME_APP_SOURCES})
list(FILTER LAUNCHME_UI_SHARED EXCLUDE REGEX "^src/(app|ui)/")
list(REMOVE_ITEM LAUNCHME_UI_SHARED ${LAUNCHME_CORE_SOURCES})
list(FILTER LAUNCHME_UI_SHARED EXCLUDE REGEX "_windows\\.cpp$|_wic\\.cpp$|http_winhttp\\.cpp$|/etw_access\\.cpp$")
list(REMOVE_ITEM LAUNCHME_UI_SHARED
    src/platform/memory_usage.cpp        # memory_usage_linux.cpp
    src/platform/frame_clock.cpp         # frame_clock_linux.cpp
    src/platform/layered_surface.cpp     # layered_surface_linux.cpp: no layered windows (overlay_window_linux.cpp)
    src/platform/game_overlay_monitor.cpp # game_overlay_monitor_linux.cpp
    src/platform/frame_limiter.cpp       # frame_limiter_linux.cpp
    src/platform/steam_downloads.cpp     # steam_downloads_linux.cpp
    src/platform/power.cpp               # power_linux.cpp
    src/platform/discord.cpp             # discord_linux.cpp
    src/platform/file_dialog.cpp)        # file_dialog_linux.cpp
list(APPEND LAUNCHME_UI_SHARED src/platform/pdf_pages_linux.cpp src/platform/etw_access_linux.cpp src/platform/frame_limiter_linux.cpp src/platform/frame_clock_linux.cpp src/platform/file_dialog_linux.cpp src/platform/layered_surface_linux.cpp)
message(STATUS "Linux app: shared sources outside the core: ${LAUNCHME_UI_SHARED}")

function(launchme_ui_library name)
    add_library(${name} STATIC ${LAUNCHME_UI_SOURCES} ${LAUNCHME_UI_SHARED} ${ARGN})
    launchme_target_defaults(${name})
    target_include_directories(${name} BEFORE PUBLIC ${LAUNCHME_WIN32_INCLUDES})
    target_include_directories(${name} PRIVATE "${SDL3_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(${name} PUBLIC launchme_window launchme_gfx launchme_core)
endfunction()
launchme_ui_library(launchme_ui)

add_executable(launchme src/main.cpp)
launchme_target_defaults(launchme)
target_link_libraries(launchme PRIVATE launchme_ui)
launchme_impeller_assets(launchme)
# Companion: ONNX Runtime and the model next to it ($ORIGIN), her pictures inside (as companion.rc on Windows).
companion_embedding_assets(launchme)
target_link_libraries(launchme PRIVATE launchme_companion_resources)
# Beside the program, as the Windows build places them: the music themes and the studio logos.
file(GLOB LAUNCHME_STUDIO_LOGOS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/studios/*.png")
file(GLOB LAUNCHME_PLATFORM_LOGOS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/platforms/*.png")
file(GLOB LAUNCHME_SYSTEM_LOGOS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/systems/*.png") # Emulacja's system logos (res/systems/README.txt).
file(GLOB LAUNCHME_CASE_FRONTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/cases/*.png") # Półka's case fronts (MIT, res/cases/LICENSE.txt).
set(LAUNCHME_THEMES "${CMAKE_CURRENT_SOURCE_DIR}/res/HorizonsTheme.mp3" "${CMAKE_CURRENT_SOURCE_DIR}/res/ExploratorySilenceTheme.mp3")
# The icon font standing in for Segoe Fluent Icons (tools/linux/make-icon-font.sh, Fluent UI System Icons, MIT).
file(GLOB LAUNCHME_LEXEND CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Lexend-*.ttf")
set(LAUNCHME_FONTS "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/launchme-icons.ttf" "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/launchme-icons-LICENSE.txt"
    ${LAUNCHME_LEXEND} "${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/Lexend-OFL.txt") # Lexend: Google Fonts, SIL OFL.
add_custom_target(launchme_assets ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/studios" "${CMAKE_CURRENT_BINARY_DIR}/assets/platforms" "${CMAKE_CURRENT_BINARY_DIR}/assets/fonts"
            "${CMAKE_CURRENT_BINARY_DIR}/assets/cases" "${CMAKE_CURRENT_BINARY_DIR}/assets/systems"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_THEMES} "${CMAKE_CURRENT_BINARY_DIR}/assets/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_STUDIO_LOGOS} "${CMAKE_CURRENT_BINARY_DIR}/assets/studios/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_PLATFORM_LOGOS} "${CMAKE_CURRENT_BINARY_DIR}/assets/platforms/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_CASE_FRONTS} "${CMAKE_CURRENT_SOURCE_DIR}/res/cases/LICENSE.txt" "${CMAKE_CURRENT_BINARY_DIR}/assets/cases/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_SYSTEM_LOGOS} "${CMAKE_CURRENT_SOURCE_DIR}/res/systems/README.txt" "${CMAKE_CURRENT_BINARY_DIR}/assets/systems/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LAUNCHME_FONTS} "${CMAKE_CURRENT_BINARY_DIR}/assets/fonts/"
    DEPENDS ${LAUNCHME_THEMES} ${LAUNCHME_STUDIO_LOGOS} ${LAUNCHME_PLATFORM_LOGOS} ${LAUNCHME_CASE_FRONTS} ${LAUNCHME_SYSTEM_LOGOS} ${LAUNCHME_FONTS} VERBATIM)
add_dependencies(launchme launchme_assets)
