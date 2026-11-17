# Pinned, CPU-only local semantic memory. No runtime downloads or external service.
# Fetched from NuGet's immutable flatcontainer (byte-identical win-x64 and linux-x64 1.23.2 binaries).
include(FetchContent)
FetchContent_Declare(companion_ort
    URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/1.23.2/microsoft.ml.onnxruntime.1.23.2.nupkg
    DOWNLOAD_NAME onnxruntime-1.23.2.zip
    URL_HASH SHA256=25fe172f7fcdf34f2b5b02c8b997f6b88abc282956849d4af4335af23d0c4e4a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(companion_ort)
if(WIN32)
add_library(companion_ort SHARED IMPORTED GLOBAL)
set_target_properties(companion_ort PROPERTIES
    IMPORTED_IMPLIB "${companion_ort_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.lib"
    IMPORTED_LOCATION "${companion_ort_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${companion_ort_SOURCE_DIR}/build/native/include")
set(COMPANION_ORT_RUNTIME onnxruntime.dll) # Its name next to the program.
else()
# Linux (cmake/Linux.cmake): the same release's x64 build; programs find it next to them ($ORIGIN).
add_library(companion_ort SHARED IMPORTED GLOBAL)
set_target_properties(companion_ort PROPERTIES
    IMPORTED_LOCATION "${companion_ort_SOURCE_DIR}/runtimes/linux-x64/native/libonnxruntime.so"
    IMPORTED_SONAME "libonnxruntime.so.1"
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${companion_ort_SOURCE_DIR}/build/native/include"
    INTERFACE_INCLUDE_DIRECTORIES "${companion_ort_SOURCE_DIR}/build/native/include")
set(COMPANION_ORT_RUNTIME libonnxruntime.so.1) # Its SONAME: what the loader looks for.
endif()

set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(companion_spm GIT_REPOSITORY https://github.com/google/sentencepiece.git
    GIT_TAG 31646a467d2051eb904e0b45de3a73e91fe1c1e3)
FetchContent_MakeAvailable(companion_spm)
set_property(DIRECTORY "${companion_spm_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)
get_property(companion_spm_targets DIRECTORY "${companion_spm_SOURCE_DIR}/src" PROPERTY BUILDSYSTEM_TARGETS)
foreach(spm_target IN LISTS companion_spm_targets)
    set_target_properties(${spm_target} PROPERTIES MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")
    if(NOT spm_target STREQUAL "sentencepiece-static")
        set_target_properties(${spm_target} PROPERTIES EXCLUDE_FROM_ALL TRUE EXCLUDE_FROM_DEFAULT_BUILD TRUE)
    endif()
endforeach()
set_property(TARGET sentencepiece-static PROPERTY MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")
if(NOT WIN32)
    # Its headers for launchme_core (Windows adds them to launchme_core in CMakeLists.txt); its own warnings stay quiet.
    target_include_directories(sentencepiece-static SYSTEM INTERFACE "${companion_spm_SOURCE_DIR}/src")
    target_compile_options(sentencepiece-static PRIVATE -w)
endif()

FetchContent_Declare(companion_sqlite
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(companion_sqlite)
add_library(companion_sqlite STATIC "${companion_sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(companion_sqlite PUBLIC "${companion_sqlite_SOURCE_DIR}")
target_compile_definitions(companion_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION SQLITE_DQS=0)
set_property(TARGET companion_sqlite PROPERTY MSVC_RUNTIME_LIBRARY "${LAUNCHME_RUNTIME}")
if(NOT WIN32)
    target_link_libraries(companion_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
    target_compile_options(companion_sqlite PRIVATE -w) # The amalgamation as released (newer GCC warns).
endif()

set(COMPANION_MODEL_DIR "${CMAKE_BINARY_DIR}/companion-model")
set(COMPANION_MODEL_URL "https://huggingface.co/Xenova/paraphrase-multilingual-MiniLM-L12-v2/resolve/2c4055b12046f11709e9df2c122e59ffbdc2f900")
file(MAKE_DIRECTORY "${COMPANION_MODEL_DIR}")
function(companion_model_file remote local hash)
    if(EXISTS "${COMPANION_MODEL_DIR}/${local}")
        file(SHA256 "${COMPANION_MODEL_DIR}/${local}" existing)
        if(existing STREQUAL hash)
            return()
        endif()
    endif()
    set(url "${COMPANION_MODEL_URL}/${remote}")
    if(remote STREQUAL "sentencepiece.bpe.model")
        set(url "https://huggingface.co/sentence-transformers/paraphrase-multilingual-MiniLM-L12-v2/resolve/e8f8c211226b894fcb81acc59f3b34ba3efd5f42/sentencepiece.bpe.model")
    endif()
    file(DOWNLOAD "${url}" "${COMPANION_MODEL_DIR}/${local}.part"
         EXPECTED_HASH "SHA256=${hash}" TLS_VERIFY ON STATUS result SHOW_PROGRESS)
    if(NOT result MATCHES "^0;")
        message(FATAL_ERROR "Companion embedding download failed: ${result}")
    endif()
    file(RENAME "${COMPANION_MODEL_DIR}/${local}.part" "${COMPANION_MODEL_DIR}/${local}")
endfunction()
companion_model_file(onnx/model_quantized.onnx model.onnx 66fc00f5f29afcaff34092e1bdd20008ca3918265a82fb9695a551e510cc4ebc)
companion_model_file(sentencepiece.bpe.model sentencepiece.bpe.model cfc8146abe2a0488e9e2a0c56de7952f7c11ab059eca145a0a727afce0db2865)

function(companion_embedding_assets target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:companion_ort>" "$<TARGET_FILE_DIR:${target}>/${COMPANION_ORT_RUNTIME}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/assets/companion-embeddings"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${COMPANION_MODEL_DIR}/model.onnx" "${COMPANION_MODEL_DIR}/sentencepiece.bpe.model"
            "$<TARGET_FILE_DIR:${target}>/assets/companion-embeddings/"
        VERBATIM)
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()
if(WIN32)
    install(FILES "${companion_ort_SOURCE_DIR}/runtimes/win-x64/native/onnxruntime.dll" DESTINATION . COMPONENT Runtime)
else()
    install(FILES "${companion_ort_SOURCE_DIR}/runtimes/linux-x64/native/libonnxruntime.so" DESTINATION . RENAME libonnxruntime.so.1 COMPONENT Runtime)
endif()
install(FILES "${COMPANION_MODEL_DIR}/model.onnx" "${COMPANION_MODEL_DIR}/sentencepiece.bpe.model"
    DESTINATION assets/companion-embeddings COMPONENT Runtime)
install(FILES "${companion_ort_SOURCE_DIR}/LICENSE" DESTINATION assets/companion-embeddings RENAME onnxruntime-license.txt COMPONENT Runtime)
install(FILES "${companion_ort_SOURCE_DIR}/ThirdPartyNotices.txt" DESTINATION assets/companion-embeddings RENAME onnxruntime-third-party-notices.txt COMPONENT Runtime)
install(FILES "${companion_spm_SOURCE_DIR}/LICENSE" DESTINATION assets/companion-embeddings RENAME sentencepiece-license.txt COMPONENT Runtime)
foreach(dependency absl darts_clone protobuf-lite esaxx)
    install(FILES "${companion_spm_SOURCE_DIR}/third_party/${dependency}/LICENSE"
        DESTINATION assets/companion-embeddings RENAME "sentencepiece-${dependency}-license.txt" COMPONENT Runtime)
endforeach()
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/companion-embeddings-NOTICE.txt" DESTINATION assets/companion-embeddings COMPONENT Runtime)
