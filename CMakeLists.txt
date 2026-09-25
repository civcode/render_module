cmake_minimum_required(VERSION 3.16) # Required by GLFW 3.5.1.
project(RenderModule VERSION 0.3.0 LANGUAGES C CXX)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(RENDER_MODULE_BUILD_EXAMPLES "Build RenderModule examples" ON)
option(RENDER_MODULE_BUILD_TESTS "Build RenderModule regression tests" OFF)
option(RENDER_MODULE_ENABLE_DESKTOP "Build the existing Desktop backend" ON)
option(RENDER_MODULE_ENABLE_HEADLESS "Build Linux GLFW Null/EGL and native EGL backends" OFF)
option(RENDER_MODULE_ENABLE_WEB "Build temporary JPEG/WebSocket development backend" OFF)
option(RENDER_MODULE_ENABLE_VIDEO "Build transport-independent CPU video pipeline" OFF)
option(RENDER_MODULE_ENABLE_OPENH264 "Use OpenH264 when video is enabled" ON)
option(RENDER_MODULE_VIDEO_ASAN "Instrument video library/dependencies/tests with ASan" OFF)
option(RENDER_MODULE_BUILD_BROWSER_TESTS "Run installed Playwright Chromium tests" OFF)
if(RENDER_MODULE_ENABLE_WEB AND NOT RENDER_MODULE_ENABLE_HEADLESS)
    message(FATAL_ERROR "Web requires RENDER_MODULE_ENABLE_HEADLESS=ON")
endif()

if(RENDER_MODULE_ENABLE_HEADLESS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "RenderModule: headless EGL backends currently require Linux")
endif()
if(NOT RENDER_MODULE_ENABLE_DESKTOP AND NOT RENDER_MODULE_ENABLE_HEADLESS)
    message(FATAL_ERROR "RenderModule: enable at least one graphics backend")
endif()
if(RENDER_MODULE_ENABLE_HEADLESS)
    find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
endif()

# GLFW 3.5.1 added Mesa surfaceless EGL to the Null platform. Always use this
# pinned version rather than silently accepting an older system GLFW.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
if(NOT RENDER_MODULE_ENABLE_DESKTOP)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.5.1 # d9d6f0f1f967807ffade6598ea9a631ebaf37a56
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)
if(RENDER_MODULE_ENABLE_DESKTOP AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND NOT GLFW_BUILD_X11 AND NOT GLFW_BUILD_WAYLAND)
    message(FATAL_ERROR "Desktop needs a GLFW window-system backend. Enable GLFW_BUILD_X11 or GLFW_BUILD_WAYLAND, or use a fresh build directory.")
endif()
foreach(part MAJOR MINOR REVISION)
    file(STRINGS "${glfw_SOURCE_DIR}/include/GLFW/glfw3.h" _version_line
        REGEX "^#define GLFW_VERSION_${part} +[0-9]+")
    string(REGEX MATCH "[0-9]+$" _glfw_${part} "${_version_line}")
endforeach()
set(RENDER_MODULE_GLFW_VERSION "${_glfw_MAJOR}.${_glfw_MINOR}.${_glfw_REVISION}")
if(RENDER_MODULE_GLFW_VERSION VERSION_LESS "3.5.1")
    message(FATAL_ERROR "RenderModule requires GLFW 3.5.1+, found ${RENDER_MODULE_GLFW_VERSION}")
endif()
message(STATUS "RenderModule GLFW: ${RENDER_MODULE_GLFW_VERSION}")
set_target_properties(glfw PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(RENDER_MODULE_GLFW_TARGET glfw)

if(APPLE)
    set(MAGNUM_WITH_CGLCONTEXT ON CACHE BOOL "" FORCE)
    set(RENDER_MODULE_MAGNUM_CONTEXT_TARGET Magnum::CglContext)
elseif(WIN32)
    set(MAGNUM_WITH_WGLCONTEXT ON CACHE BOOL "" FORCE)
    set(RENDER_MODULE_MAGNUM_CONTEXT_TARGET Magnum::WglContext)
elseif(UNIX)
    set(MAGNUM_WITH_GLXCONTEXT ${RENDER_MODULE_ENABLE_DESKTOP} CACHE BOOL "" FORCE)
    set(MAGNUM_WITH_EGLCONTEXT ${RENDER_MODULE_ENABLE_HEADLESS} CACHE BOOL "" FORCE)
    set(RENDER_MODULE_MAGNUM_CONTEXT_TARGET Magnum::GlxContext)
else()
    message(FATAL_ERROR
        "RenderModule: no Magnum GL context handler configured for this platform")
endif()

# Magnum is used as a rendering library only. RenderModule keeps ownership of
# GLFW, ImGui and input handling and attaches Magnum to the already-current GL
# context through Platform::GLContext.
set(CORRADE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CORRADE_BUILD_STATIC_PIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    Corrade
    GIT_REPOSITORY https://github.com/mosra/corrade.git
    GIT_TAG c028fd6e261a0665f72fbc75c7c034baf47747c6
)

set(MAGNUM_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(MAGNUM_BUILD_STATIC_PIC ON CACHE BOOL "" FORCE)
set(MAGNUM_WITH_GL ON CACHE BOOL "" FORCE)
set(MAGNUM_WITH_MESHTOOLS ON CACHE BOOL "" FORCE)
set(MAGNUM_WITH_PRIMITIVES ON CACHE BOOL "" FORCE)
set(MAGNUM_WITH_SHADERS ON CACHE BOOL "" FORCE)
# Keep the embedded Magnum build focused on the functionality used here.
set(MAGNUM_WITH_AUDIO OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_DEBUGTOOLS OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_MATERIALTOOLS OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_SCENEGRAPH OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_SCENETOOLS OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_SHADERTOOLS OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_TEXT OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_TEXTURETOOLS OFF CACHE BOOL "" FORCE)
set(MAGNUM_WITH_VK OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    Magnum
    GIT_REPOSITORY https://github.com/mosra/magnum.git
    GIT_TAG f239823e9afd27ab0ecf28e1d567831e3c62a25e
)
FetchContent_MakeAvailable(Corrade)
FetchContent_MakeAvailable(Magnum)
if(RENDER_MODULE_ENABLE_HEADLESS)
    include(cmake/RuntimeMagnumContext.cmake)
endif()

FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG v0.1.34
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glad)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.16
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(implot)

FetchContent_Declare(
    nanovg
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG ce3bf745eb2d2dbc14a50bf2446783f691ac4353 # Includes public-domain PNG writer.
)
FetchContent_MakeAvailable(nanovg)

FetchContent_Declare(
    roboto_font
    GIT_REPOSITORY https://github.com/googlefonts/roboto-2.git
    GIT_TAG v2.138
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(roboto_font)

FetchContent_Declare(
    roboto_mono_font
    GIT_REPOSITORY https://github.com/googlefonts/RobotoMono.git
    GIT_TAG v3.001
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(roboto_mono_font)

# Collect fonts into one predictable build-tree directory. This lets examples
# render text before installation and gives the install step a stable source.
file(GLOB_RECURSE ROBOTO_TTF_FILES CONFIGURE_DEPENDS
    "${roboto_font_SOURCE_DIR}/*.ttf"
    "${roboto_mono_font_SOURCE_DIR}/*.ttf"
)
set(RENDER_MODULE_BUILD_FONT_DIR "${CMAKE_CURRENT_BINARY_DIR}/fonts")
file(MAKE_DIRECTORY "${RENDER_MODULE_BUILD_FONT_DIR}")
file(COPY ${ROBOTO_TTF_FILES} DESTINATION "${RENDER_MODULE_BUILD_FONT_DIR}")

add_library(RenderModule SHARED)
add_library(RenderModule::RenderModule ALIAS RenderModule)

target_sources(RenderModule PRIVATE
    src/canvas.cpp
    src/debug_console.cpp
    src/nvg_wrapper.cpp
    src/pose_tool.cpp
    src/render_module.cpp
    src/core/root_framebuffer.cpp
    src/input/remote_input_queue.cpp
    src/input/remote_input_backend.cpp
    src/present/image_presenter.cpp
    src/platform/platform_backend.cpp
    src/view3d.cpp
    src/zoom_view.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${nanovg_SOURCE_DIR}/src/nanovg.c
)

if(RENDER_MODULE_ENABLE_DESKTOP)
    target_sources(RenderModule PRIVATE
        src/platform/glfw_desktop_backend.cpp
        src/input/glfw_imgui_input.cpp
        src/present/desktop_presenter.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
    target_compile_definitions(RenderModule PRIVATE RENDER_MODULE_ENABLE_DESKTOP)
endif()
if(RENDER_MODULE_ENABLE_HEADLESS)
    target_sources(RenderModule PRIVATE
        src/platform/glfw_null_egl_backend.cpp
        src/platform/native_egl_backend.cpp
        src/platform/headless_adapters.cpp)
    target_compile_definitions(RenderModule PRIVATE RENDER_MODULE_ENABLE_HEADLESS)
    target_link_libraries(RenderModule PRIVATE OpenGL::EGL ${CMAKE_DL_LIBS})
endif()

if(RENDER_MODULE_ENABLE_WEB)
    find_package(Boost 1.75 REQUIRED COMPONENTS system json)
    find_package(JPEG REQUIRED)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" WEB_HTML)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/app.mjs" WEB_JS)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/web/style.css" WEB_CSS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        web/index.html web/app.mjs web/style.css)
    configure_file(src/web/web_assets.cpp.in generated/web_assets.cpp @ONLY)
    target_sources(RenderModule PRIVATE src/web/web_protocol.cpp src/web/web_server.cpp
        src/web/jpeg_encode.c src/present/web_presenter.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/web_assets.cpp")
    target_link_libraries(RenderModule PRIVATE Boost::system Boost::json JPEG::JPEG)
    execute_process(COMMAND git describe --always --dirty --exclude "*"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE WEB_BUILD_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT WEB_BUILD_ID)
        set(WEB_BUILD_ID "source-archive")
    endif()
    target_compile_definitions(RenderModule PRIVATE RENDER_MODULE_ENABLE_WEB
        RENDER_MODULE_WEB_BUILD_ID="${WEB_BUILD_ID}" RENDER_MODULE_WEB_VERSION="${PROJECT_VERSION}")
endif()

find_package(Threads REQUIRED)
if(RENDER_MODULE_ENABLE_VIDEO)
    include(cmake/Video.cmake)
    target_link_libraries(RenderModule PRIVATE RenderModuleVideo)
    target_compile_definitions(RenderModule PRIVATE RENDER_MODULE_ENABLE_VIDEO)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET RenderModule APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
endif()
target_link_libraries(RenderModule PRIVATE Threads::Threads)
# Committed UTF-8 includes supplementary planes; clients must share ImGui's ABI.
target_compile_definitions(RenderModule PUBLIC IMGUI_USE_WCHAR32)
target_compile_features(RenderModule PUBLIC cxx_std_17)
target_include_directories(RenderModule
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${glad_BINARY_DIR}/include>
        $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>
        $<BUILD_INTERFACE:${implot_SOURCE_DIR}>
        $<BUILD_INTERFACE:${nanovg_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${nanovg_SOURCE_DIR}/example # stb_image_write.h, private diagnostic PNG writer.
)
target_link_libraries(RenderModule PRIVATE
    ${RENDER_MODULE_GLFW_TARGET}
    glad
    Magnum::Magnum
    Magnum::GL
    ${RENDER_MODULE_MAGNUM_CONTEXT_TARGET}
    Magnum::MeshTools
    Magnum::Primitives
    Magnum::Shaders
)
target_compile_definitions(RenderModule PRIVATE
    RENDER_MODULE_BUILD_FONT_DIR="${RENDER_MODULE_BUILD_FONT_DIR}"
    RENDER_MODULE_DEFAULT_FONT_DIR="${CMAKE_INSTALL_FULL_DATADIR}/render-module/fonts"
)
set_target_properties(RenderModule PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(
    TARGETS RenderModule
    EXPORT RenderModuleTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES third_party/PNG_NOTICE.md third_party/WEB_NOTICE.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/render-module/licenses)

install(
    FILES ${glad_BINARY_DIR}/include/glad/glad.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/glad
)
install(
    FILES ${glad_BINARY_DIR}/include/KHR/khrplatform.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/KHR
)
install(
    FILES
        ${imgui_SOURCE_DIR}/imgui.h
        ${imgui_SOURCE_DIR}/imconfig.h
        ${implot_SOURCE_DIR}/implot.h
        ${nanovg_SOURCE_DIR}/src/nanovg.h
        ${nanovg_SOURCE_DIR}/src/nanovg_gl.h
        ${nanovg_SOURCE_DIR}/src/nanovg_gl_utils.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    DIRECTORY "${RENDER_MODULE_BUILD_FONT_DIR}/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/render-module/fonts
)

configure_package_config_file(
    cmake/RenderModuleConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    EXPORT RenderModuleTargets
    FILE RenderModuleTargets.cmake
    NAMESPACE RenderModule::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)

if(RENDER_MODULE_BUILD_EXAMPLES)
    function(render_module_example target source)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE RenderModule::RenderModule)
    endfunction()

    render_module_example(RenderModuleDemo examples/render_module_demo.cpp)
    render_module_example(TextOutputDemo examples/text_output_demo.cpp)
    render_module_example(FrameBufferDemo examples/frame_buffer_demo.cpp)
    render_module_example(DebugConsoleDemo examples/debug_console_demo.cpp)
    render_module_example(PathPlanningDemo examples/path_planning_demo.cpp)
    render_module_example(RenderModule3DDemo examples/view3d_demo.cpp)
endif()

if(RENDER_MODULE_BUILD_TESTS)
    enable_testing()
    if(RENDER_MODULE_ENABLE_VIDEO)
        add_executable(RenderModuleVideoTests tests/video_tests.cpp)
        target_link_libraries(RenderModuleVideoTests PRIVATE RenderModule::Video)
        if(RENDER_MODULE_VIDEO_ASAN)
            target_compile_options(RenderModuleVideoTests PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
        endif()
        set(_video_tests config color pool)
        if(RENDER_MODULE_ENABLE_OPENH264)
            list(APPEND _video_tests codec queue)
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(VIDEO_TEST_AV QUIET IMPORTED_TARGET libavcodec libavutil)
                if(VIDEO_TEST_AV_FOUND)
                    target_compile_definitions(RenderModuleVideoTests PRIVATE VIDEO_TEST_DECODER)
                    target_link_libraries(RenderModuleVideoTests PRIVATE PkgConfig::VIDEO_TEST_AV)
                    list(APPEND _video_tests roundtrip)
                endif()
            endif()
        endif()
        if(RENDER_MODULE_ENABLE_OPENH264 AND RENDER_MODULE_ENABLE_HEADLESS)
            add_executable(RenderModuleVideoRootTests tests/video_root_tests.cpp)
            target_include_directories(RenderModuleVideoRootTests PRIVATE src)
            target_link_libraries(RenderModuleVideoRootTests PRIVATE RenderModule::RenderModule RenderModule::Video)
            if(VIDEO_TEST_AV_FOUND)
                target_compile_definitions(RenderModuleVideoRootTests PRIVATE VIDEO_TEST_DECODER)
                target_link_libraries(RenderModuleVideoRootTests PRIVATE PkgConfig::VIDEO_TEST_AV)
            endif()
            foreach(mode benchmark resize)
                add_test(NAME video_root_${mode} COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    $<TARGET_FILE:RenderModuleVideoRootTests> ${mode} ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/video)
                set_tests_properties(video_root_${mode} PROPERTIES TIMEOUT 120 LABELS "video;headless")
            endforeach()
        endif()
        foreach(test IN LISTS _video_tests)
            add_test(NAME video_${test} COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                $<TARGET_FILE:RenderModuleVideoTests> ${test})
            set_tests_properties(video_${test} PROPERTIES TIMEOUT 90 LABELS "video;headless")
        endforeach()
    endif()
    if(RENDER_MODULE_ENABLE_WEB)
        add_executable(RenderModuleWebTests tests/web_tests.cpp)
        target_include_directories(RenderModuleWebTests PRIVATE src)
        target_link_libraries(RenderModuleWebTests PRIVATE RenderModule::RenderModule Boost::system Boost::json Threads::Threads)
        foreach(test protocol server jpeg)
            add_test(NAME web_${test} COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                $<TARGET_FILE:RenderModuleWebTests> ${test})
            set_tests_properties(web_${test} PROPERTIES TIMEOUT 45 LABELS "web;headless")
        endforeach()
        add_executable(RenderModuleWebFixture tests/web_fixture.cpp)
        target_link_libraries(RenderModuleWebFixture PRIVATE RenderModule::RenderModule Boost::json)
        if(RENDER_MODULE_ENABLE_VIDEO AND RENDER_MODULE_ENABLE_OPENH264)
            target_link_libraries(RenderModuleWebFixture PRIVATE RenderModule::Video)
            target_compile_definitions(RenderModuleWebFixture PRIVATE VIDEO_COEXISTENCE_TEST)
        endif()
        find_program(RENDER_MODULE_NODE node)
        if(RENDER_MODULE_NODE)
            add_test(NAME web_js_helpers COMMAND ${RENDER_MODULE_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/helpers_test.mjs)
            set_tests_properties(web_js_helpers PROPERTIES TIMEOUT 30 LABELS web)
        endif()
        if(RENDER_MODULE_BUILD_BROWSER_TESTS)
            if(NOT RENDER_MODULE_NODE)
                message(FATAL_ERROR "Browser tests need Node.js; normal Web builds do not")
            endif()
            set(RENDER_MODULE_BROWSER_CACHE "${CMAKE_CURRENT_BINARY_DIR}/browser-cache" CACHE PATH "Playwright browser cache")
            add_test(NAME web_chromium COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                "PLAYWRIGHT_BROWSERS_PATH=${RENDER_MODULE_BROWSER_CACHE}" ${RENDER_MODULE_NODE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/browser_test.mjs $<TARGET_FILE:RenderModuleWebFixture>
                ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/web)
            set_tests_properties(web_chromium PROPERTIES TIMEOUT 120 LABELS "web;headless")
            if(RENDER_MODULE_ENABLE_VIDEO AND RENDER_MODULE_ENABLE_OPENH264)
                add_test(NAME web_chromium_video COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    "RENDER_MODULE_TEST_VIDEO=1" "PLAYWRIGHT_BROWSERS_PATH=${RENDER_MODULE_BROWSER_CACHE}" ${RENDER_MODULE_NODE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/web/browser_test.mjs $<TARGET_FILE:RenderModuleWebFixture>
                    ${CMAKE_CURRENT_BINARY_DIR}/test-artifacts/web-video)
                set_tests_properties(web_chromium_video PROPERTIES TIMEOUT 120 LABELS "web;video;headless")
            endif()
        endif()
    endif()
    add_executable(RenderModuleInputTests tests/input_tests.cpp)
    target_include_directories(RenderModuleInputTests PRIVATE src)
    target_link_libraries(RenderModuleInputTests PRIVATE RenderModule::RenderModule Threads::Threads)
    foreach(test queue events)
        add_test(NAME input_${test} COMMAND RenderModuleInputTests ${test})
        set_tests_properties(input_${test} PROPERTIES TIMEOUT 30 LABELS "input;headless")
    endforeach()
    add_executable(RenderModuleInputIntegrationTests tests/input_integration_tests.cpp)
    target_include_directories(RenderModuleInputIntegrationTests PRIVATE src)
    target_link_libraries(RenderModuleInputIntegrationTests PRIVATE RenderModule::RenderModule Threads::Threads)
    add_executable(RenderModuleOutputTests tests/output_tests.cpp)
    target_include_directories(RenderModuleOutputTests PRIVATE src)
    target_link_libraries(RenderModuleOutputTests PRIVATE RenderModule::RenderModule)
    target_compile_definitions(RenderModuleOutputTests PRIVATE
        RENDER_MODULE_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/root-ui.png"
        RENDER_MODULE_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-artifacts")
    if(RENDER_MODULE_ENABLE_DESKTOP)
        add_executable(RenderModuleDesktopTests tests/desktop_tests.cpp)
        target_include_directories(RenderModuleDesktopTests PRIVATE src)
        target_link_libraries(RenderModuleDesktopTests PRIVATE
            RenderModule::RenderModule ${RENDER_MODULE_GLFW_TARGET})
        if(RENDER_MODULE_ENABLE_HEADLESS)
            target_compile_definitions(RenderModuleDesktopTests PRIVATE RENDER_MODULE_TEST_HEADLESS)
        endif()

        target_compile_definitions(RenderModuleInputIntegrationTests PRIVATE RENDER_MODULE_TEST_DESKTOP)
        target_link_libraries(RenderModuleInputIntegrationTests PRIVATE ${RENDER_MODULE_GLFW_TARGET})
        add_test(NAME desktop_input COMMAND RenderModuleInputIntegrationTests desktop)
        set_tests_properties(desktop_input PROPERTIES TIMEOUT 60)
        add_test(NAME root_framebuffer_desktop COMMAND RenderModuleOutputTests root desktop)
        set_tests_properties(root_framebuffer_desktop PROPERTIES TIMEOUT 30)
        add_test(NAME desktop_backend COMMAND RenderModuleDesktopTests --backend)
        add_test(NAME desktop_render COMMAND RenderModuleDesktopTests --render)
        set_tests_properties(desktop_backend desktop_render PROPERTIES TIMEOUT 30)
        if(UNIX AND NOT APPLE)
            add_test(NAME desktop_no_display
                COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    $<TARGET_FILE:RenderModuleDesktopTests> --no-display)
            set_tests_properties(desktop_no_display PROPERTIES TIMEOUT 30)
        endif()
    endif()
    if(RENDER_MODULE_ENABLE_HEADLESS)
        foreach(provider native glfw)
            add_test(NAME headless_input_${provider}
                COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    $<TARGET_FILE:RenderModuleInputIntegrationTests> ${provider})
            set_tests_properties(headless_input_${provider} PROPERTIES TIMEOUT 60 LABELS headless)
        endforeach()
        foreach(test root resize visual)
            add_test(NAME headless_output_${test}
                COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    $<TARGET_FILE:RenderModuleOutputTests> ${test} headless)
            set_tests_properties(headless_output_${test} PROPERTIES TIMEOUT 60 LABELS headless)
        endforeach()
        add_executable(RenderModuleHeadlessTests tests/headless_tests.cpp)
        target_include_directories(RenderModuleHeadlessTests PRIVATE src)
        target_link_libraries(RenderModuleHeadlessTests PRIVATE RenderModule::RenderModule OpenGL::EGL)
        foreach(provider glfw native pbuffer)
            add_test(NAME headless_${provider}
                COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                    $<TARGET_FILE:RenderModuleHeadlessTests> ${provider})
            set_tests_properties(headless_${provider} PROPERTIES TIMEOUT 60 LABELS headless)
        endforeach()
        if(RENDER_MODULE_BUILD_EXAMPLES)
            foreach(context glfw-null-egl native-egl)
                add_test(NAME headless_demo_${context}
                    COMMAND ${CMAKE_COMMAND} -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY
                        $<TARGET_FILE:RenderModule3DDemo> --render-backend headless
                        --headless-context ${context} --frames 3)
                set_tests_properties(headless_demo_${context} PROPERTIES TIMEOUT 60 LABELS headless)
            endforeach()
        endif()
    endif()
endif()
