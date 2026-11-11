# Linux: helpers Aurelia, Nile (Amazon), launchme-xbox and their tests.
# Included at the end of cmake/Linux.cmake: add sources with target_sources(launchme_ui PRIVATE ...),
# tests with launchme_executable(...).
#
# The store sources themselves (src/store/aurelia*.cpp, amazon.cpp, xbox_account.cpp and
# src/platform/helper_process.cpp) are shared with Windows and reach launchme_ui through
# LAUNCHME_APP_SOURCES (cmake/LinuxApp.cmake); their Linux code is in #else branches. The tests
# compile them in until they move into the core's list.

if(BUILD_TESTING)
    set(LAUNCHME_AURELIA_SOURCES src/store/aurelia.cpp src/store/aurelia_process.cpp src/store/aurelia_lifecycle.cpp src/store/aurelia_install.cpp)
    # A fake Aurelia: the test executable itself (its --json branch); Unix socket daemon endpoint.
    launchme_executable(aurelia_tests TEST aurelia_protocol_install_and_process
        SOURCES tests/aurelia_tests.cpp ${LAUNCHME_AURELIA_SOURCES})
    # Fake Nile and helper_process fixtures as shell scripts.
    launchme_executable(amazon_tests TEST amazon_nile_contracts
        SOURCES tests/amazon_tests.cpp src/store/amazon.cpp src/platform/helper_process.cpp)
    # A fake launchme-xbox as a shell script (LAUNCHME_XBOX_HELPER).
    launchme_executable(xbox_account_tests TEST xbox_account_contracts
        SOURCES tests/xbox_account_tests.cpp src/store/xbox_account.cpp src/platform/helper_process.cpp)
    # The Stores' built-in tabs: every outside effect of an installation is the test's own backend.
    launchme_executable(store_hub_tests TEST store_hub_launchers_promotions_and_grid
        SOURCES tests/store_hub_tests.cpp src/store/launcher_install.cpp src/platform/helper_process.cpp)
endif()

# launchme-xbox (helpers/xbox, Rust) next to the launcher: not part of `all`, it needs cargo, protoc
# and the WebKitGTK 6.0 development files (tools/build-xbox-helper.sh). `cmake --build <dir> --target
# launchme_xbox_helper` puts it next to the launchme executable, where src/store/xbox_account.cpp looks.
find_program(LAUNCHME_BASH bash)
if(LAUNCHME_BASH)
    add_custom_target(launchme_xbox_helper
        COMMAND "${LAUNCHME_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/build-xbox-helper.sh" --out "$<TARGET_FILE_DIR:launchme>"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        USES_TERMINAL VERBATIM)
endif()
