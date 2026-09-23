# One runtime-dispatched loader for Desktop GLX and headless EGL in one binary.
# Both stock context libraries export the same flextGLInit/loader symbols, so
# linking them together would select one at link time, NOT per native context.
# Reuse the pinned upstream generated loader and its complete function list.
set(_flext_dir "${magnum_SOURCE_DIR}/src/MagnumExternal/OpenGL/GL")
file(READ "${_flext_dir}/flextGLPlatform.cpp" _flext_source)
set(_egl_guard [=[        EGLDisplay display = eglGetCurrentDisplay();
        const Corrade::Containers::StringView vendor = eglQueryString(display, EGL_VENDOR);
        if(vendor == "NVIDIA"_s && !context.isDriverWorkaroundDisabled("nv-egl-incorrect-gl11-function-pointers"_s)) {]=])
string(FIND "${_flext_source}" "${_egl_guard}" _guard_offset)
if(_guard_offset EQUAL -1)
    message(FATAL_ERROR "RenderModule: upstream Magnum loader changed; review the runtime loader adapter")
endif()
# Reload core GL pointers for ALL providers, avoiding stale pointers when changing
# EGL/GLX contexts. This also retains the intent of Magnum's old NVIDIA workaround
# without querying EGL on the Desktop GLX path. No fetched source is modified.
string(REPLACE "${_egl_guard}" "        static_cast<void>(context);\n        {"
    _flext_source "${_flext_source}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/runtime_flextGLPlatform.cpp" "${_flext_source}")
add_library(RenderModuleMagnumContext STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/runtime_flextGLPlatform.cpp"
    src/platform/magnum_function_loader.cpp)
set_target_properties(RenderModuleMagnumContext PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(RenderModuleMagnumContext PRIVATE cxx_std_17)
target_compile_definitions(RenderModuleMagnumContext PRIVATE MAGNUM_PLATFORM_USE_EGL)
target_include_directories(RenderModuleMagnumContext PRIVATE
    "${_flext_dir}" "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(RenderModuleMagnumContext PRIVATE Magnum::GL OpenGL::EGL)
set(RENDER_MODULE_MAGNUM_CONTEXT_TARGET RenderModuleMagnumContext)
