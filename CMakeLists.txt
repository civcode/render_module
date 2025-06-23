cmake_minimum_required(VERSION 3.14)
project(RenderModule VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_BUILD_TYPE Release)
add_compile_options(-fmax-errors=3)


# === Find dependencies ===
# Assume GLFW is installed on the system
find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
# === ===


# === Fetch dependencies using FetchContent ===
include(FetchContent)

# Fetch glad
FetchContent_Declare(
    glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.34
)
FetchContent_MakeAvailable(glad)
set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Fetch ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Fetch ImPlot
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG v0.16
)
FetchContent_MakeAvailable(implot)

# Fetch NanoVG
FetchContent_Declare(
    nanovg
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG master
)
FetchContent_MakeAvailable(nanovg)

# Fetch Roboto font
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
# === ===



# === RenderModule libraruy ===
file(GLOB SRC_FILES
    ${CMAKE_SOURCE_DIR}/src/render_module.cpp
    ${CMAKE_SOURCE_DIR}/src/nvg_wrapper.cpp
    ${CMAKE_SOURCE_DIR}/src/zoom_view.cpp
    ${imgui_SOURCE_DIR}/*.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${implot_SOURCE_DIR}/*.cpp
    ${nanovg_SOURCE_DIR}/src/nanovg.c
)

# === Create the library ===
add_library(RenderModule SHARED src/render_module.cpp)

target_include_directories(RenderModule 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        # $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE 
        ${GLFW_INCLUDE_DIRS} 
        ${glad_BINARY_DIR}/include
        ${imgui_SOURCE_DIR}
        ${implot_SOURCE_DIR}
        ${nanovg_SOURCE_DIR}/src
)
target_sources(RenderModule PRIVATE ${SRC_FILES})
target_link_libraries(RenderModule PRIVATE ${GLFW_LIBRARIES} glad)
target_compile_definitions(RenderModule 
    PUBLIC 
        NANOVG_GL3_IMPLEMENTATION 
        # IMGUI_ENABLE_VIEWPORTS
        # IMGUI_HAS_DOCK
    # PRIVATE RENDERMODULE_INTERNAL_BUILD
)



# === Install the library ===
include(GNUInstallDirs)
# set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install")
set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local")

install(TARGETS RenderModule
    EXPORT RenderModuleTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})



# === Install public Library Headers ===
set(IMGUI_HEADERS
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imconfig.h
    ${imgui_SOURCE_DIR}/imgui_internal.h
)
set(IMPLOT_HEADERS
    ${implot_SOURCE_DIR}/implot.h
    ${implot_SOURCE_DIR}/implot_internal.h
)
set(NANOVG_HEADERS
    ${nanovg_SOURCE_DIR}/src/nanovg.h
    ${nanovg_SOURCE_DIR}/src/nanovg_gl.h
    ${nanovg_SOURCE_DIR}/src/nanovg_gl_utils.h
)
install(
    FILES 
        ${IMGUI_HEADERS} 
        ${IMPLOT_HEADERS}
        ${NANOVG_HEADERS}
    DESTINATION 
        ${CMAKE_INSTALL_INCLUDEDIR}/
)



# Install the roboto fonts to ~/.local/share/render-module/fonts/roboto
file(GLOB_RECURSE ROBOTO_TTF_FILES
     "${roboto_font_SOURCE_DIR}/*.ttf"
     "${roboto_mono_font_SOURCE_DIR}/*.ttf"
)

install(
    FILES 
        ${ROBOTO_TTF_FILES} 
    DESTINATION 
        "$ENV{HOME}/.local/share/render-module/fonts/roboto"
)



# === Create the cmake config files ===
install(EXPORT RenderModuleTargets
    FILE RenderModuleTargets.cmake
    NAMESPACE RenderModule::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RenderModuleConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/RenderModuleConfig.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RenderModule
)
# === ===



# === Add an example executable ===
set(EXAMPLE_INLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${GLFW_INCLUDE_DIRS}
    ${glad_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${implot_SOURCE_DIR}
    ${nanovg_SOURCE_DIR}/src
)
add_executable(RenderModuleDemo examples/render_module_demo.cpp)
target_include_directories(RenderModuleDemo PRIVATE ${EXAMPLE_INLUDES})
target_link_libraries(RenderModuleDemo PRIVATE RenderModule)

add_executable(TextOutputDemo examples/text_output_demo.cpp)
target_include_directories(TextOutputDemo PRIVATE ${EXAMPLE_INLUDES})
target_link_libraries(TextOutputDemo PRIVATE RenderModule)
# === ===
