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