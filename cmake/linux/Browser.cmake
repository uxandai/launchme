# Linux: the WebKitGTK reader window helper (phase 4).
# Included at the end of cmake/Linux.cmake: add sources with target_sources(launchme_ui PRIVATE ...),
# tests with launchme_executable(...).
#
# launchme-browser is a GTK 4 + WebKitGTK 6.0 program of its own that ships next to launchme;
# src/ui/browser_window_linux.cpp (already in launchme_ui) starts and drives it over a socket
# (src/linux/browser/browser_protocol.hpp). Without it the reader window opens pages in the user's
# browser. Run time: libwebkitgtk-6.0-4 and libgtk-4-1 (Debian/Ubuntu), webkitgtk-6.0 and gtk4 (Arch).
pkg_check_modules(LAUNCHME_WEBKIT IMPORTED_TARGET webkitgtk-6.0 gtk4 libsoup-3.0)
if(LAUNCHME_WEBKIT_FOUND)
    add_executable(launchme-browser src/linux/browser/browser_main.cpp)
    target_compile_features(launchme-browser PRIVATE cxx_std_20)
    target_compile_options(launchme-browser PRIVATE -Wall -Wextra -Wno-missing-field-initializers -finput-charset=UTF-8)
    target_link_libraries(launchme-browser PRIVATE PkgConfig::LAUNCHME_WEBKIT)
    # On X11 the window is made the launcher's transient window (Xlib's XSetTransientForHint).
    pkg_check_modules(LAUNCHME_X11 IMPORTED_TARGET x11)
    if(LAUNCHME_X11_FOUND)
        target_compile_definitions(launchme-browser PRIVATE LAUNCHME_BROWSER_X11)
        target_link_libraries(launchme-browser PRIVATE PkgConfig::LAUNCHME_X11)
    endif()
    # Built with the launcher, into the same folder (the launcher looks for it next to itself).
    add_dependencies(launchme launchme-browser)
else()
    message(STATUS "WebKitGTK 6.0 / GTK 4 not found: launchme-browser is not built; the reader window opens pages in the user's browser")
endif()

if(BUILD_TESTING)
    # The reader window against the real helper (skipped, 77, without a display or the helper).
    launchme_executable(browser_window_linux_tests
        SOURCES tests/browser_window_linux_tests.cpp src/ui/browser_window_linux.cpp
        LIBS launchme_window DEFINES LAUNCHME_UI_TEST)
    # The Win32 surface (src/linux/win32) before the tests' own small Windows.h stand-in.
    target_include_directories(browser_window_linux_tests BEFORE PRIVATE ${LAUNCHME_WIN32_INCLUDES})
    if(NOT SDL3_INCLUDE_DIR IN_LIST CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES) # -I/usr/include would break #include_next.
        target_include_directories(browser_window_linux_tests PRIVATE "${SDL3_INCLUDE_DIR}")
    endif()
    if(TARGET launchme-browser)
        add_dependencies(browser_window_linux_tests launchme-browser)
    endif()
    add_test(NAME webkit_browser_lifecycle COMMAND browser_window_linux_tests)
    set_tests_properties(webkit_browser_lifecycle PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
    # Not run by default: WebKitGTK needs a display with EGL, which WSL and build machines lack (the
    # program is still built). -DLAUNCHME_WEBKIT_TEST=ON runs it on a real desktop.
    option(LAUNCHME_WEBKIT_TEST "Run webkit_browser_lifecycle (needs a display with EGL)" OFF)
    if(NOT LAUNCHME_WEBKIT_TEST)
        set_tests_properties(webkit_browser_lifecycle PROPERTIES DISABLED TRUE)
    endif()
endif()
