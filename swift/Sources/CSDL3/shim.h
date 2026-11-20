#include <SDL3/SDL.h>

// Swift does not import macros that use SDL_UINT64_C, so the window flags
// are also constants here.
static const SDL_WindowFlags CSDL3_WINDOW_RESIZABLE = SDL_WINDOW_RESIZABLE;
static const SDL_WindowFlags CSDL3_WINDOW_HIGH_PIXEL_DENSITY =
    SDL_WINDOW_HIGH_PIXEL_DENSITY;
