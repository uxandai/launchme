cmake_minimum_required(VERSION 3.24)
project(launchme_launch_provider_example LANGUAGES C)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This example requires Windows x64")
endif()
add_library(launch SHARED plugin.c)
target_include_directories(launch PRIVATE ../..)
set_property(TARGET launch PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_custom_target(package ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" "$<TARGET_FILE_DIR:launch>/plugin.json"
    COMMAND ${CMAKE_COMMAND} -E tar cf "$<TARGET_FILE_DIR:launch>/example.launch.zip" --format=zip -- plugin.json launch.dll
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:launch>"
    DEPENDS launch "${CMAKE_CURRENT_SOURCE_DIR}/plugin.json" VERBATIM)
