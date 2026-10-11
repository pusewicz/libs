# Set SP to a work directory and PREFIX to an SDL 3.2+ install, then source this file.
: "${SP:?set SP to the work directory}"
: "${PREFIX:?set PREFIX to the SDL3 install prefix}"
export LD_LIBRARY_PATH=$PREFIX/lib
export SDL_VIDEO_DRIVER=offscreen
export VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1
