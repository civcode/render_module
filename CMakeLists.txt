cmake_minimum_required(VERSION 3.14)
project(RenderModule VERSION 0.2.0 LANGUAGES C CXX)

include(FetchContent)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(RENDER_MODULE_BUILD_EXAMPLES "Build RenderModule examples" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED IMPORTED_TARGET glfw3)

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
    GIT_TAG master 
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
    src/zoom_view.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${nanovg_SOURCE_DIR}/src/nanovg.c
)

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
)
target_link_libraries(RenderModule PRIVATE PkgConfig::GLFW glad)
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
endif()
