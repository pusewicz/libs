// Draws a scene in shades of gray into a texture, then shows the texture
// through a custom shader: four colors from a palette, and rows of pixels
// that wave. See shaders/retro.frag.
//
// Keys: Space changes the palette. W turns the wave on and off.

#define SDL_MAIN_USE_CALLBACKS
#define PXL_IMPLEMENTATION
#include <SDL3/SDL_main.h>
#include <math.h>

#include "art.h"
#include "example_shaders.h"
#include "pxl.h"

static constexpr int canvas_width  = 160;
static constexpr int canvas_height = 144;
static constexpr int palette_count = 3;

/** The uniforms of retro.frag, in std140 layout. */
typedef struct retro_uniforms {
  float palette;
  float time;
  float wave;
  float unused;
} retro_uniforms;

typedef struct app {
  SDL_Window* window;
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_shader* retro;
  pxl_texture* palettes;
  pxl_texture* scene;
  pxl_texture* smiley;
  int palette;
  bool wave;
} app;

static bool create_resources(app* a) {
  a->retro = pxl_create_shader(a->pxl, &retro_frag);

  // One palette in each row, from dark to light.
  const pxl_color palettes[palette_count * 4] = {
      pxl_rgb(0x0f380f), pxl_rgb(0x306230), pxl_rgb(0x8bac0f),
      pxl_rgb(0x9bbc0f), pxl_rgb(0x1b1b3a), pxl_rgb(0x3d4b8f),
      pxl_rgb(0x7aa5d8), pxl_rgb(0xe0f4ff), pxl_rgb(0x2b0f0e),
      pxl_rgb(0x7a2214), pxl_rgb(0xe0611e), pxl_rgb(0xffd27a),
  };
  a->palettes = pxl_create_texture(a->pxl, &(pxl_texture_desc){
                                               .width  = 4,
                                               .height = palette_count,
                                               .pixels = palettes,
                                           });
  a->scene    = pxl_create_texture(a->pxl, &(pxl_texture_desc){
                                               .width         = canvas_width,
                                               .height        = canvas_height,
                                               .render_target = true,
                                           });
  const art_palette gray = {
      ['#'] = pxl_rgb(0xffffff),
      ['+'] = pxl_rgb(0xaaaaaa),
      ['o'] = pxl_rgb(0x000000),
  };
  a->smiley = art_texture(a->pxl, 8, 8,
                          "..####.."
                          ".######."
                          "##o##o##"
                          "########"
                          "#o####o#"
                          "##oooo##"
                          ".+####+."
                          "..++++..",
                          gray);
  return a->retro && a->palettes && a->scene && a->smiley;
}

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
      SDL_CreateWindow("pxl: shader", 4 * canvas_width, 4 * canvas_height,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  a->device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  if (!a->window || !a->device) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->pxl = pxl_create(&(pxl_desc){
      .device = a->device,
      .window = a->window,
      .width  = canvas_width,
      .height = canvas_height,
  });
  if (!a->pxl || !create_resources(a)) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->wave = true;
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
  app* a = state;
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  if (event->type == SDL_EVENT_KEY_DOWN) {
    if (event->key.key == SDLK_SPACE) {
      a->palette = (a->palette + 1) % palette_count;
    } else if (event->key.key == SDLK_W) {
      a->wave = !a->wave;
    } else if (event->key.key == SDLK_ESCAPE) {
      return SDL_APP_SUCCESS;
    }
  }
  return SDL_APP_CONTINUE;
}

/** Draws the scene in shades of gray. */
static void draw_scene(app* a, float t) {
  pxl_context* pxl = a->pxl;
  pxl_clear(pxl, pxl_black);
  for (int i = 0; i < 4; ++i) {
    uint8_t v = (uint8_t)(i * 85);
    pxl_draw_rect(pxl, (float)i * 40, 104, 40, 40, (pxl_color){v, v, v, 255});
  }
  pxl_draw_circle(pxl, 80 + (50 * cosf(t)), 70, 14, pxl_rgb(0xaaaaaa));
  pxl_draw_circle_lines(pxl, 80 - (50 * cosf(t)), 70, 10, pxl_white);
  pxl_draw_sprite(pxl, a->smiley,
                  &(pxl_sprite){
                      .x      = 80,
                      .y      = 40 + (6 * sinf(t * 3)),
                      .origin = {4, 4},
                      .scale  = {3, 3},
                  });
  pxl_draw_text(pxl, 4, 4, pxl_white, "SPACE: palette");
  pxl_draw_text(pxl, 4, 14, pxl_rgb(0xaaaaaa), "W: wave (%s)",
                a->wave ? "on" : "off");
}

SDL_AppResult SDL_AppIterate(void* state) {
  app* a           = state;
  pxl_context* pxl = a->pxl;
  float t          = (float)SDL_GetTicks() / 1000.0f;

  pxl_begin_frame(pxl);
  pxl_push(pxl);
  pxl_set_target(pxl, a->scene);
  draw_scene(a, t);
  pxl_pop(pxl);

  pxl_push(pxl);
  pxl_set_shader(pxl, a->retro);
  pxl_set_shader_texture(pxl, 1, a->palettes);
  pxl_set_uniforms(pxl,
                   &(retro_uniforms){
                       .palette = (float)a->palette,
                       .time    = t,
                       .wave    = a->wave ? 2.0f : 0.0f,
                   },
                   sizeof(retro_uniforms));
  pxl_draw_sprite(pxl, a->scene, &(pxl_sprite){});
  pxl_pop(pxl);
  if (!pxl_end_frame(pxl)) {
    SDL_Log("pxl: %s", SDL_GetError());
  }
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, [[maybe_unused]] SDL_AppResult result) {
  app* a = state;
  if (!a) {
    return;
  }
  if (a->pxl) {
    pxl_destroy_shader(a->pxl, a->retro);
    pxl_destroy_texture(a->pxl, a->palettes);
    pxl_destroy_texture(a->pxl, a->scene);
    pxl_destroy_texture(a->pxl, a->smiley);
    pxl_destroy(a->pxl);
  }
  SDL_DestroyGPUDevice(a->device);
  SDL_DestroyWindow(a->window);
  SDL_free(a);
}
