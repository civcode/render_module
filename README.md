# Render Module





## Dependencies
- $ sudo apt-get install libglfw3-dev

## Build and Install

Default install path is ~/.local/.

Do **NOT** use sudo for installation, because this will mess with the installation of the roboto true type fonts.
```
$ git clone ...
$ cd render_module
$ cmake -B builddir
$ cmake --build builddir/ -- -j$(nproc)
$ cmake --install builddir
```


## How to use with CMake
Make sure that CMake can find the RenderModule package.

```
$ export PATH="$PATH:$HOME/.local/bin"
```

```
cmake_minimum_required(VERSION  3.14)
project(RenderModuleTest)

find_Package(RenderModule REQUIRED)

add_executable(RenderModuleTest main.cpp)
target_link_libraries(RenderModuleTest RenderModule::RenderModule)
```
