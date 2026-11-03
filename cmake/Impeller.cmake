# The renderer: a pinned standalone Impeller SDK with our C ABI extension (tools/linux/build-impeller.sh),
# its kernel in src/linux/impeller, and Clipper2 for path booleans.
set(LAUNCHME_IMPELLER_DIR "$ENV{HOME}/launchme-impeller/sdk" CACHE PATH "SDK produced by tools/linux/build-impeller.sh")
set(_impeller_revision "f6965468e5902c28fb719793f5da3e7e812870fb")
foreach(_file include/impeller.h include/launchme_impeller.h lib/libimpeller.so revision.txt LICENSE.txt)
    if(NOT EXISTS "${LAUNCHME_IMPELLER_DIR}/${_file}")
        message(FATAL_ERROR "Missing Impeller SDK file ${_file}. Run tools/linux/build-impeller.sh and set LAUNCHME_IMPELLER_DIR.")
    endif()
endforeach()
file(STRINGS "${LAUNCHME_IMPELLER_DIR}/revision.txt" _impeller_actual LIMIT_COUNT 1)
if(NOT _impeller_actual STREQUAL _impeller_revision)
    message(FATAL_ERROR "Impeller SDK revision mismatch: expected ${_impeller_revision}, found ${_impeller_actual}")
endif()
add_library(launchme_impeller_sdk SHARED IMPORTED GLOBAL)
set_target_properties(launchme_impeller_sdk PROPERTIES
    IMPORTED_LOCATION "${LAUNCHME_IMPELLER_DIR}/lib/libimpeller.so"
    INTERFACE_INCLUDE_DIRECTORIES "${LAUNCHME_IMPELLER_DIR}/include")

set(CLIPPER2_UTILS OFF CACHE BOOL "" FORCE)
set(CLIPPER2_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLIPPER2_TESTS OFF CACHE BOOL "" FORCE)
set(USE_EXTERNAL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(clipper2 GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git GIT_TAG Clipper2_1.5.4
    SOURCE_SUBDIR CPP)
FetchContent_MakeAvailable(clipper2)

file(GLOB LAUNCHME_IMPELLER_SOURCES CONFIGURE_DEPENDS src/linux/impeller/*.cpp)
add_library(launchme_impeller STATIC ${LAUNCHME_IMPELLER_SOURCES})
launchme_target_defaults(launchme_impeller)
target_include_directories(launchme_impeller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/linux/impeller" PRIVATE "${SDL3_INCLUDE_DIR}")
target_link_libraries(launchme_impeller PUBLIC launchme_impeller_sdk Clipper2 PkgConfig::LAUNCHME_FONT_DEPS PRIVATE launchme_core "${SDL3_LIBRARY}" dl)
target_link_libraries(launchme_gfx PUBLIC launchme_impeller)
target_include_directories(launchme_gfx PRIVATE "${SDL3_INCLUDE_DIR}")
target_link_libraries(launchme_gfx PRIVATE PkgConfig::LAUNCHME_FONT_DEPS)

function(launchme_impeller_assets target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHME_IMPELLER_DIR}/lib/libimpeller.so" "$<TARGET_FILE_DIR:${target}>/libimpeller.so"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHME_IMPELLER_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:${target}>/licenses/impeller.txt"
        VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    install(FILES "${LAUNCHME_IMPELLER_DIR}/lib/libimpeller.so" DESTINATION . COMPONENT Runtime)
    install(FILES "${LAUNCHME_IMPELLER_DIR}/LICENSE.txt" DESTINATION licenses RENAME impeller.txt COMPONENT Runtime)
endfunction()

if(BUILD_TESTING)
    add_executable(impeller_backend_tests tests/impeller_backend_tests.cpp)
    launchme_target_defaults(impeller_backend_tests)
    target_include_directories(impeller_backend_tests PRIVATE "${SDL3_INCLUDE_DIR}")
    target_link_libraries(impeller_backend_tests PRIVATE launchme_window launchme_gfx launchme_impeller launchme_core)
    add_executable(impeller_d2d_tests tests/impeller_d2d_tests.cpp)
    launchme_target_defaults(impeller_d2d_tests)
    target_link_libraries(impeller_d2d_tests PRIVATE launchme_window launchme_gfx)
    find_program(LAUNCHME_XVFB_RUN xvfb-run)
    foreach(_backend gl vulkan)
        foreach(_suite backend d2d)
            if(_suite STREQUAL backend)
                set(_name impeller_${_backend}_rendering)
            else()
                set(_name impeller_d2d_${_backend})
            endif()
            if(LAUNCHME_XVFB_RUN)
                add_test(NAME ${_name} COMMAND "${LAUNCHME_XVFB_RUN}" -a -s "-screen 0 800x600x24"
                    "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
                    "$<TARGET_FILE:impeller_${_suite}_tests>" "${_backend}")
            else()
                add_test(NAME ${_name} COMMAND impeller_${_suite}_tests "${_backend}")
            endif()
            set_tests_properties(${_name} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
endif()
