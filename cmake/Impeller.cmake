# The optional Linux backend uses a pinned standalone SDK with our C ABI extension.
option(LAUNCHME_WITH_IMPELLER "Build the experimental Impeller renderer alongside Skia" OFF)
set(LAUNCHME_IMPELLER_DIR "$ENV{HOME}/launchme-impeller/sdk" CACHE PATH "SDK produced by tools/linux/build-impeller.sh")
if(LAUNCHME_WITH_IMPELLER)
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
    target_sources(launchme_gfx PRIVATE src/linux/impeller/backend.cpp src/linux/impeller/canvas.cpp
        src/linux/impeller/images.cpp src/linux/impeller/paint.cpp)
    target_include_directories(launchme_gfx PRIVATE "${SDL3_INCLUDE_DIR}")
    target_link_libraries(launchme_gfx PRIVATE launchme_impeller_sdk "${SDL3_LIBRARY}")
    set_property(TARGET launchme_gfx PROPERTY LAUNCHME_IMPELLER_ENABLED TRUE)
    if(BUILD_TESTING)
        add_executable(impeller_backend_tests tests/impeller_backend_tests.cpp)
        launchme_target_defaults(impeller_backend_tests)
        target_include_directories(impeller_backend_tests PRIVATE "${SDL3_INCLUDE_DIR}")
        target_link_libraries(impeller_backend_tests PRIVATE launchme_window launchme_gfx launchme_skia launchme_impeller_sdk)
        add_executable(impeller_d2d_tests tests/impeller_d2d_tests.cpp)
        launchme_target_defaults(impeller_d2d_tests)
        target_link_libraries(impeller_d2d_tests PRIVATE launchme_window launchme_gfx launchme_skia)
        find_program(LAUNCHME_XVFB_RUN xvfb-run)
        foreach(_backend gl vulkan)
            if(LAUNCHME_XVFB_RUN)
                add_test(NAME impeller_${_backend}_rendering COMMAND "${LAUNCHME_XVFB_RUN}" -a -s "-screen 0 800x600x24"
                    "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
                    "$<TARGET_FILE:impeller_backend_tests>" "${_backend}")
            else()
                add_test(NAME impeller_${_backend}_rendering COMMAND impeller_backend_tests "${_backend}")
            endif()
            set_tests_properties(impeller_${_backend}_rendering PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
            if(LAUNCHME_XVFB_RUN)
                add_test(NAME impeller_d2d_${_backend} COMMAND "${LAUNCHME_XVFB_RUN}" -a -s "-screen 0 800x600x24"
                    "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
                    "$<TARGET_FILE:impeller_d2d_tests>" "${_backend}")
            else()
                add_test(NAME impeller_d2d_${_backend} COMMAND impeller_d2d_tests "${_backend}")
            endif()
            set_tests_properties(impeller_d2d_${_backend} PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
        endforeach()
    endif()
else()
    target_sources(launchme_gfx PRIVATE src/linux/impeller/backend_stub.cpp)
endif()

function(launchme_impeller_assets target)
    if(LAUNCHME_WITH_IMPELLER)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHME_IMPELLER_DIR}/lib/libimpeller.so" "$<TARGET_FILE_DIR:${target}>/libimpeller.so"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/licenses"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAUNCHME_IMPELLER_DIR}/LICENSE.txt" "$<TARGET_FILE_DIR:${target}>/licenses/impeller.txt"
            VERBATIM)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        install(FILES "${LAUNCHME_IMPELLER_DIR}/lib/libimpeller.so" DESTINATION . COMPONENT Runtime)
        install(FILES "${LAUNCHME_IMPELLER_DIR}/LICENSE.txt" DESTINATION licenses RENAME impeller.txt COMPONENT Runtime)
    endif()
endfunction()
