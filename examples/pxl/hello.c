// The smallest pxl program: a 320 x 180 canvas with a sprite and text.

#define SDL_MAIN_USE_CALLBACKS
#define PXL_IMPLEMENTATION
#include <SDL3/SDL_main.h>
#include <math.h>

#include "art.h"
#include "pxl.h"

typedef struct app {
  SDL_Window* window;
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_texture* smiley;
} app;

SDL_AppResult SDL_AppInit(void** state, [[maybe_unused]] int argc,
                          [[maybe_unused]] char* argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return SDL_APP_FAILURE;
  }
  app* a = SDL_calloc(1, sizeof *a);
  *state = a;
  if (!a) {
    return SDL_APP_FAILURE;
  }
  a->window =
      SDL_CreateWindow("pxl: hello", 960, 540,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  a->device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  if (!a->window || !a->device) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->pxl = pxl_create(&(pxl_desc){
      .device = a->device,
      .window = a->window,
      .width  = 320,
      .height = 180,
  });
  if (!a->pxl) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  const art_palette palette = {
      ['#'] = pxl_rgb(0xffec27),
      ['o'] = pxl_rgb(0x000000),
  };
  a->smiley = art_texture(a->pxl, 8, 8,
                          "..####.."
                          ".######."
                          "##o##o##"
                          "########"
                          "#o####o#"
                          "##oooo##"
                          ".######."
                          "..####..",
                          palette);
  return a->smiley ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppEvent([[maybe_unused]] void* state, SDL_Event* event) {
  return event->type == SDL_EVENT_QUIT ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* state) {
  app* a  = state;
  float t = (float)SDL_GetTicks() / 1000.0f;

  pxl_begin_frame(a->pxl);
  pxl_clear(a->pxl, pxl_rgb(0x1d2b53));
  pxl_draw_sprite(a->pxl, a->smiley,
                  &(pxl_sprite){
                      .x      = 160,
                      .y      = 90 + (10 * sinf(t * 3)),
                      .origin = {4, 4},
                      .scale  = {4, 4},
                  });
  pxl_draw_text(a->pxl, 4, 4, pxl_white, "Hello, pixels!");
  pxl_end_frame(a->pxl);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, [[maybe_unused]] SDL_AppResult result) {
  app* a = state;
  if (a) {
    if (a->pxl) {
      pxl_destroy_texture(a->pxl, a->smiley);
      pxl_destroy(a->pxl);
    }
    SDL_DestroyGPUDevice(a->device);
    SDL_DestroyWindow(a->window);
    SDL_free(a);
  }
}
