# Immutable revisions: libyuv 1972, OpenH264 2.6.0. Gitiles archive gzip
# metadata varies between requests; pin the Git object instead of its tar hash.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(libyuv
    GIT_REPOSITORY https://chromium.googlesource.com/libyuv/libyuv
    GIT_TAG 41a6e684a68950f07912b7e6f57ba3fa2e10fb65
    GIT_SUBMODULES "")
FetchContent_GetProperties(libyuv)
if(NOT libyuv_POPULATED)
    FetchContent_Populate(libyuv)
endif()
set(LIBYUV_DISABLE_JPEG ON CACHE BOOL "" FORCE)
set(UNIT_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${libyuv_SOURCE_DIR}" "${libyuv_BINARY_DIR}" EXCLUDE_FROM_ALL)
add_library(RenderModuleVideo SHARED src/video/video_frame.cpp src/video/video_pipeline.cpp)
add_library(RenderModule::Video ALIAS RenderModuleVideo)
set_target_properties(RenderModuleVideo PROPERTIES EXPORT_NAME Video VERSION ${PROJECT_VERSION} SOVERSION 0)
target_compile_features(RenderModuleVideo PUBLIC cxx_std_17)
target_include_directories(RenderModuleVideo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "${libyuv_SOURCE_DIR}/include")
target_link_libraries(RenderModuleVideo PRIVATE yuv Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(RenderModuleVideo PRIVATE "-Wl,--exclude-libs,ALL")
endif()
if(RENDER_MODULE_ENABLE_OPENH264)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Phase 6 OpenH264 build adapter currently supports native Linux builds")
    endif()
    FetchContent_Declare(openh264
        URL https://codeload.github.com/cisco/openh264/tar.gz/652bdb7719f30b52b08e506645a7322ff1b2cc6f
        URL_HASH SHA256=7a060916a9fcb63ba51d83a4f2388660c0b58797be547c4fa5c52e8d65660a85)
    FetchContent_GetProperties(openh264)
    if(NOT openh264_POPULATED)
        FetchContent_Populate(openh264)
    endif()
    find_program(RENDER_MODULE_MAKE NAMES gmake make)
    if(NOT RENDER_MODULE_MAKE)
        message(FATAL_ERROR "OpenH264 source build needs GNU make")
    endif()
    find_program(RENDER_MODULE_NASM nasm)
    set(_asm USE_ASM=No)
    if(RENDER_MODULE_NASM)
        set(_asm USE_ASM=Yes "ASM=${RENDER_MODULE_NASM}")
    endif()
    set(_asan USE_ASAN=No)
    if(RENDER_MODULE_VIDEO_ASAN)
        set(_asan USE_ASAN=Yes)
        set(_asm USE_ASM=No) # Instrument the scalar codec, not opaque assembly.
    endif()
    include(ExternalProject)
    ExternalProject_Add(RenderModuleOpenH264Build
        SOURCE_DIR "${openh264_SOURCE_DIR}" BINARY_DIR "${openh264_BINARY_DIR}"
        DOWNLOAD_COMMAND "" CONFIGURE_COMMAND "" INSTALL_COMMAND ""
        BUILD_COMMAND "${RENDER_MODULE_MAKE}" -f "${openh264_SOURCE_DIR}/Makefile" -j2
            libopenh264.a BUILDTYPE=Release ${_asm} ${_asan}
            "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}"
        BUILD_BYPRODUCTS "${openh264_BINARY_DIR}/libopenh264.a")
    add_library(RenderModuleOpenH264 STATIC IMPORTED)
    set_target_properties(RenderModuleOpenH264 PROPERTIES IMPORTED_LOCATION "${openh264_BINARY_DIR}/libopenh264.a")
    add_dependencies(RenderModuleOpenH264 RenderModuleOpenH264Build)
    target_sources(RenderModuleVideo PRIVATE src/video/openh264_encoder.cpp)
    target_include_directories(RenderModuleVideo PRIVATE "${openh264_SOURCE_DIR}/codec/api")
    target_compile_definitions(RenderModuleVideo PRIVATE RENDER_MODULE_HAS_OPENH264)
    target_link_libraries(RenderModuleVideo PRIVATE RenderModuleOpenH264)
    install(FILES "${openh264_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DATADIR}/render-module/licenses RENAME OpenH264-LICENSE)
endif()
if(RENDER_MODULE_VIDEO_ASAN)
    target_compile_options(RenderModuleVideo PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(RenderModuleVideo PUBLIC -fsanitize=address)
    target_compile_options(yuv_common_objects PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_compile_definitions(yuv_common_objects PRIVATE LIBYUV_DISABLE_X86)
endif()
install(TARGETS RenderModuleVideo EXPORT RenderModuleTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VIDEO_NOTICE.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/render-module/licenses)
install(FILES "${libyuv_SOURCE_DIR}/LICENSE" "${libyuv_SOURCE_DIR}/PATENTS"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/render-module/licenses/libyuv)
