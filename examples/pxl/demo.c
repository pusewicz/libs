// A tour of pxl: animated sprites, a scrolling tile background, light in a
// render target, shapes, a nine-slice panel, text and the scale modes.
//
// Keys: 1, 2 and 3 select the scale mode. S turns snap on and off.
// Click to flash the slimes near the mouse.

#define SDL_MAIN_USE_CALLBACKS
#define PXL_IMPLEMENTATION
#include <SDL3/SDL_main.h>
#include <math.h>

#include "art.h"
#include "pxl.h"

static constexpr int canvas_width   = 320;
static constexpr int canvas_height  = 180;
static constexpr int slime_size     = 16;
static constexpr size_t slime_count = 48;
static constexpr float flash_time   = 0.25f;

typedef struct slime {
  float x, y;
  float vx, vy;
  float phase;
  float flash;
} slime;

typedef struct app {
  SDL_Window* window;
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_texture* slime_sheet;
  pxl_texture* tile;
  pxl_texture* panel;
  pxl_texture* light;
  slime slimes[slime_count];
  uint64_t ticks;
  float time;
  float fps;
  uint32_t random;
  bool snap;
} app;

/** Returns a random number in [0, 1). */
static float random_float(app* a) {
  a->random ^= a->random << 13;
  a->random ^= a->random >> 17;
  a->random ^= a->random << 5;
  return (float)(a->random >> 8) / 16777216.0f;
}

static bool create_textures(app* a) {
  const art_palette slime_palette = {
      ['o'] = pxl_rgb(0x1d2b53), ['g'] = pxl_rgb(0x00e436),
      ['l'] = pxl_rgb(0xb4ffb4), ['w'] = pxl_rgb(0xfff1e8),
      ['k'] = pxl_rgb(0x000000),
  };
  // Two frames side by side: rest and squash.
  a->slime_sheet = art_texture(a->pxl, 2 * slime_size, slime_size,
                               "................................"
                               "................................"
                               "................................"
                               "................................"
                               "......oooo......................"
                               "....oogggloo...................."
                               "...ogggggglgo........oooooo....."
                               "..oggggggggggo.....oogggggloo..."
                               "..ogwkggggwkgo....oggggggggglgo."
                               ".oggwkggggwkggo..oggwkggggwkgggo"
                               ".oggggggggggggo.ogggwkggggwkgggo"
                               ".ogggggoogggggo.oggggggggggggggo"
                               ".oggggggggggggo.oggggggooggggggo"
                               "..oggggggggggo..oggggggggggggggo"
                               "...oooooooooo....oooooooooooooo."
                               "................................",
                               slime_palette);

  const art_palette tile_palette = {
      ['a'] = pxl_rgb(0x1d2b53),
      ['b'] = pxl_rgb(0x222f5c),
      ['c'] = pxl_rgb(0x2a3a6e),
      ['e'] = pxl_rgb(0x2c3770),
  };
  a->tile = art_texture(a->pxl, 16, 16,
                        "cccccccceeeeeeee"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "caaaaaaaebbbbbbb"
                        "eeeeeeeecccccccc"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa"
                        "ebbbbbbbcaaaaaaa",
                        tile_palette);

  const art_palette panel_palette = {
      ['o'] = pxl_rgb(0x000000),
      ['w'] = pxl_rgb(0xc2c3c7),
      ['b'] = pxl_rgb(0x5f574f),
  };
  a->panel = art_texture(a->pxl, 8, 8,
                         ".oooooo."
                         "owwwwwwo"
                         "owbbbbwo"
                         "owbbbbwo"
                         "owbbbbwo"
                         "owbbbbwo"
                         "owwwwwwo"
                         ".oooooo.",
                         panel_palette);

  a->light = pxl_create_texture(a->pxl, &(pxl_texture_desc){
                                            .width         = canvas_width,
                                            .height        = canvas_height,
                                            .render_target = true,
                                        });
  if (!a->slime_sheet || !a->tile || !a->panel || !a->light) {
    return false;
  }
  pxl_set_texture_wrap(a->tile, PXL_WRAP_REPEAT);
  return true;
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
      SDL_CreateWindow("pxl: demo", 3 * canvas_width, 3 * canvas_height,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  a->device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  if (!a->window || !a->device) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->pxl = pxl_create(&(pxl_desc){
      .device    = a->device,
      .window    = a->window,
      .width     = canvas_width,
      .height    = canvas_height,
      .letterbox = pxl_rgb(0x000000),
  });
  if (!a->pxl || !create_textures(a)) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->random = 0x9e3779b9u;
  a->snap   = true;
  for (size_t i = 0; i < slime_count; ++i) {
    slime* s = &a->slimes[i];
    s->x     = random_float(a) * (canvas_width - slime_size);
    s->y     = random_float(a) * (canvas_height - 60);
    s->vx    = (random_float(a) - 0.5f) * 60.0f;
    s->vy    = (random_float(a) - 0.5f) * 60.0f;
    s->phase = random_float(a);
  }
  a->ticks = SDL_GetTicksNS();
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
  app* a = state;
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  if (event->type == SDL_EVENT_KEY_DOWN) {
    switch (event->key.key) {
    case SDLK_1:
      pxl_set_scale_mode(a->pxl, PXL_SCALE_INTEGER);
      break;
    case SDLK_2:
      pxl_set_scale_mode(a->pxl, PXL_SCALE_FIT);
      break;
    case SDLK_3:
      pxl_set_scale_mode(a->pxl, PXL_SCALE_STRETCH);
      break;
    case SDLK_S:
      a->snap = !a->snap;
      break;
    case SDLK_ESCAPE:
      return SDL_APP_SUCCESS;
    default:
      break;
    }
  }
  if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    pxl_vec2 mouse =
        pxl_window_to_canvas(a->pxl, event->button.x, event->button.y);
    for (size_t i = 0; i < slime_count; ++i) {
      slime* s = &a->slimes[i];
      float dx = s->x + (slime_size / 2.0f) - mouse.x;
      float dy = s->y + (slime_size / 2.0f) - mouse.y;
      if ((dx * dx) + (dy * dy) < 32.0f * 32.0f) {
        s->flash = flash_time;
      }
    }
  }
  return SDL_APP_CONTINUE;
}

static void update(app* a, float dt) {
  float bottom = canvas_height - 40.0f - slime_size;
  for (size_t i = 0; i < slime_count; ++i) {
    slime* s = &a->slimes[i];
    s->x += s->vx * dt;
    s->y += s->vy * dt;
    if (s->x < 0 || s->x > canvas_width - slime_size) {
      s->vx = -s->vx;
      s->x  = SDL_clamp(s->x, 0.0f, (float)(canvas_width - slime_size));
    }
    if (s->y < 12 || s->y > bottom) {
      s->vy = -s->vy;
      s->y  = SDL_clamp(s->y, 12.0f, bottom);
    }
    s->flash = fmaxf(s->flash - dt, 0.0f);
  }
}

static void draw_world(app* a) {
  pxl_context* pxl = a->pxl;
  pxl_set_snap(pxl, a->snap);

  // A source area larger than the texture repeats it.
  pxl_draw_sprite(
      pxl, a->tile,
      &(pxl_sprite){
          .src = {a->time * 12, a->time * 6, canvas_width, canvas_height},
      });

  for (size_t i = 0; i < slime_count; ++i) {
    const slime* s = &a->slimes[i];
    float frame    = fmodf((a->time * 3) + s->phase, 1.0f) < 0.5f ? 0 : 1;
    uint8_t flash  = (uint8_t)(255.0f * s->flash / flash_time);
    pxl_draw_sprite(pxl, a->slime_sheet,
                    &(pxl_sprite){
                        .x   = s->x,
                        .y   = s->y,
                        .src = {frame * slime_size, 0, slime_size, slime_size},
                        .flip_x  = s->vx < 0,
                        .origin  = {slime_size / 2.0f, 0},
                        .overlay = {255, 255, 255, flash},
                    });
  }

  // A turning square and a pulsing ring, drawn with exact pixels.
  pxl_push(pxl);
  pxl_translate(pxl, 270, 40);
  pxl_rotate(pxl, a->time);
  pxl_draw_rect_lines(pxl, -12, -12, 24, 24, pxl_rgb(0xffa300));
  pxl_pop(pxl);
  pxl_draw_circle_lines(pxl, 270, 40, 20 + (4 * sinf(a->time * 2)),
                        pxl_rgb(0xff77a8));
  pxl_draw_triangle(pxl, 30, 30, 50, 60, 10, 60, pxl_rgba(0x29adff80));
}

static void draw_light(app* a, pxl_vec2 mouse) {
  pxl_context* pxl = a->pxl;
  pxl_push(pxl);
  pxl_set_target(pxl, a->light);
  pxl_clear(pxl, pxl_rgb(0x50506e));
  pxl_set_blend(pxl, PXL_BLEND_ADD);
  for (int ring = 4; ring > 0; --ring) {
    pxl_draw_circle(pxl, mouse.x, mouse.y, (float)ring * 14, pxl_rgb(0x2a2214));
    pxl_draw_circle(pxl, 160 + (100 * cosf(a->time)), 70, (float)ring * 10,
                    pxl_rgb(0x101a2a));
  }
  pxl_pop(pxl);

  pxl_push(pxl);
  pxl_set_blend(pxl, PXL_BLEND_MULTIPLY);
  pxl_draw_sprite(pxl, a->light, &(pxl_sprite){});
  pxl_pop(pxl);
}

static void draw_ui(app* a, pxl_vec2 mouse) {
  pxl_context* pxl = a->pxl;
  pxl_stats stats  = pxl_get_stats(pxl);
  pxl_draw_nine_slice(pxl, a->panel,
                      &(pxl_nine_slice){
                          .left   = 2,
                          .top    = 2,
                          .right  = 2,
                          .bottom = 2,
                          .x      = 4,
                          .y      = canvas_height - 36,
                          .w      = canvas_width - 8,
                          .h      = 32,
                      });
  const char* modes[] = {"integer", "fit", "stretch"};
  pxl_draw_text(pxl, 10, canvas_height - 31, pxl_white,
                "%zu slimes   %zu draw calls   %.0f fps", slime_count,
                stats.draw_calls, (double)a->fps);
  pxl_draw_text(pxl, 10, canvas_height - 19, pxl_rgb(0xc2c3c7),
                "1-3: scale (%s)   S: snap (%s)   click: flash",
                modes[pxl_get_scale_mode(pxl)], a->snap ? "on" : "off");
  pxl_draw_text(pxl, 4, 2, pxl_rgb(0xffec27), "pxl demo");

  // A crosshair on the mouse.
  pxl_color red = pxl_rgb(0xff004d);
  pxl_draw_line(pxl, mouse.x - 6, mouse.y, mouse.x - 2, mouse.y, red);
  pxl_draw_line(pxl, mouse.x + 2, mouse.y, mouse.x + 6, mouse.y, red);
  pxl_draw_line(pxl, mouse.x, mouse.y - 6, mouse.x, mouse.y - 2, red);
  pxl_draw_line(pxl, mouse.x, mouse.y + 2, mouse.x, mouse.y + 6, red);
}

SDL_AppResult SDL_AppIterate(void* state) {
  app* a       = state;
  uint64_t now = SDL_GetTicksNS();
  float dt     = (float)(now - a->ticks) / 1e9f;
  a->ticks     = now;
  a->time += dt;
  a->fps = dt > 0 ? (0.95f * a->fps) + (0.05f / dt) : a->fps;
  update(a, fminf(dt, 0.1f));

  float x = 0;
  float y = 0;
  SDL_GetMouseState(&x, &y);
  pxl_vec2 mouse = pxl_window_to_canvas(a->pxl, x, y);

  pxl_begin_frame(a->pxl);
  pxl_clear(a->pxl, pxl_black);
  draw_world(a);
  draw_light(a, mouse);
  pxl_set_snap(a->pxl, true);
  draw_ui(a, mouse);
  if (!pxl_end_frame(a->pxl)) {
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
    pxl_destroy_texture(a->pxl, a->slime_sheet);
    pxl_destroy_texture(a->pxl, a->tile);
    pxl_destroy_texture(a->pxl, a->panel);
    pxl_destroy_texture(a->pxl, a->light);
    pxl_destroy(a->pxl);
  }
  SDL_DestroyGPUDevice(a->device);
  SDL_DestroyWindow(a->window);
  SDL_free(a);
}
