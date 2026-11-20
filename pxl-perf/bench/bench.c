// Benchmark for pxl.h. Draws scenes into an offscreen canvas and times the
// CPU side: recording the draws, and pxl_end_frame (upload + encode + submit).
// The GPU work is waited for outside of the timed regions.
//
// Usage: bench [scene ...] [-f frames]

#define PXL_IMPLEMENTATION
#include "pxl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <valgrind/callgrind.h>

enum { CANVAS_W = 320, CANVAS_H = 180, MAX_ITEMS = 100000 };

typedef struct item {
  float x, y, x2, y2, r, rot, s;
  int frame;
  pxl_color color;
} item;

static struct {
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_texture* sheet;
  pxl_texture* other;
  pxl_texture* stream;
  item items[MAX_ITEMS];
  pxl_vertex tris[30000];
  uint8_t stream_pixels[32 * 32 * 4];
} b;

static uint32_t rng = 0x12345678u;
static float frand(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (float)(rng >> 8) / 16777216.0f;
}

static pxl_texture* make_texture(int w, int h, uint32_t seed) {
  uint8_t* px = malloc((size_t)w * (size_t)h * 4);
  for (int i = 0; i < w * h; ++i) {
    px[(i * 4) + 0] = (uint8_t)(i * 7 + seed);
    px[(i * 4) + 1] = (uint8_t)(i * 13 + seed);
    px[(i * 4) + 2] = (uint8_t)(i * 29 + seed);
    px[(i * 4) + 3] = (uint8_t)((i % 3) ? 255 : 128);
  }
  pxl_texture* t = pxl_create_texture(
      b.pxl, &(pxl_texture_desc){.width = w, .height = h, .pixels = px});
  free(px);
  return t;
}

// ---------------------------------------------------------------------------
// Scenes. Each draws one frame.

static void scene_sprites(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_sprite(b.pxl, b.sheet,
                    &(pxl_sprite){
                        .x   = it->x,
                        .y   = it->y,
                        .src = {.x = (float)(it->frame * 16), .w = 16, .h = 16},
                    });
  }
}

static void scene_sprites_rot(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_sprite(b.pxl, b.sheet,
                    &(pxl_sprite){
                        .x        = it->x,
                        .y        = it->y,
                        .src      = {.x = (float)(it->frame * 16), .w = 16, .h = 16},
                        .origin   = {8, 8},
                        .scale    = {it->s, it->s},
                        .rotation = it->rot,
                        .color    = it->color,
                    });
  }
}

static void scene_sprites_2tex(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_sprite(b.pxl, (i & 1) ? b.other : b.sheet,
                    &(pxl_sprite){
                        .x   = it->x,
                        .y   = it->y,
                        .src = {.w = 16, .h = 16},
                    });
  }
}

static void scene_rects(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_rect(b.pxl, it->x, it->y, 8, 6, it->color);
  }
}

static void scene_text(size_t n) {
  // Each string has 40 glyphs.
  for (size_t i = 0; i < n / 40; ++i) {
    const item* it = &b.items[i];
    pxl_draw_text(b.pxl, it->x, it->y, it->color,
                  "Score: %06d  Lives: %d  Level %02d-%d!!", (int)i * 37,
                  (int)i % 9, (int)i % 99, (int)i % 4);
  }
}

static void scene_circles(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_circle(b.pxl, it->x, it->y, it->r, it->color);
  }
}

static void scene_circle_lines(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_circle_lines(b.pxl, it->x, it->y, it->r, it->color);
  }
}

static void scene_lines(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_line(b.pxl, it->x, it->y, it->x2, it->y2, it->color);
  }
}

static void scene_rect_lines(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_rect_lines(b.pxl, it->x, it->y, 20, 12, it->color);
  }
}

static void scene_triangles(size_t n) {
  size_t count = n * 3 < SDL_arraysize(b.tris) ? n * 3 : SDL_arraysize(b.tris);
  pxl_draw_triangles(b.pxl, b.sheet, b.tris, count);
}

static void scene_update_texture(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    b.stream_pixels[i % sizeof b.stream_pixels] ^= 1;
    pxl_update_texture(b.pxl, b.stream, 0, 0, 32, 32, b.stream_pixels);
    pxl_draw_sprite(b.pxl, b.stream, &(pxl_sprite){.x = b.items[i].x});
  }
}

/** A game-like frame: tiles, sprites, a light target, shapes and text. */
static void scene_mixed(size_t n) {
  pxl_clear(b.pxl, pxl_black);
  // Background tiles.
  for (int y = 0; y < CANVAS_H; y += 16) {
    for (int x = 0; x < CANVAS_W; x += 16) {
      pxl_draw_sprite(b.pxl, b.sheet,
                      &(pxl_sprite){.x = (float)x, .y = (float)y,
                                    .src = {.x = 48, .w = 16, .h = 16}});
    }
  }
  for (size_t i = 0; i < n; ++i) {
    const item* it = &b.items[i];
    pxl_draw_sprite(b.pxl, b.sheet,
                    &(pxl_sprite){.x = it->x, .y = it->y,
                                  .src = {.x = (float)(it->frame * 16), .w = 16, .h = 16},
                                  .flip_x = (i & 1) != 0});
    if (i % 16 == 0) {
      pxl_draw_rect(b.pxl, it->x, it->y - 3, 12, 2, pxl_rgb(0xff0000));
    }
  }
  pxl_push(b.pxl);
  pxl_set_blend(b.pxl, PXL_BLEND_ADD);
  for (int i = 0; i < 16; ++i) {
    pxl_draw_circle(b.pxl, b.items[i].x, b.items[i].y, 10, pxl_rgb(0x202010));
  }
  pxl_pop(b.pxl);
  for (int i = 0; i < 10; ++i) {
    pxl_draw_text(b.pxl, 4, (float)(4 + (i * 10)), pxl_white, "HP %d  MP %d  XP %d",
                  i * 3, i * 5, i * 7);
  }
}

typedef struct scene {
  const char* name;
  void (*draw)(size_t);
  size_t n;
} scene;

static const scene scenes[] = {
    {"sprites", scene_sprites, 20000},
    {"sprites_rot", scene_sprites_rot, 20000},
    {"sprites_2tex", scene_sprites_2tex, 2000},
    {"rects", scene_rects, 20000},
    {"text", scene_text, 20000},
    {"circles", scene_circles, 1000},
    {"circle_lines", scene_circle_lines, 1000},
    {"lines", scene_lines, 2000},
    {"rect_lines", scene_rect_lines, 5000},
    {"triangles", scene_triangles, 10000},
    {"update_texture", scene_update_texture, 50},
    {"mixed", scene_mixed, 2000},
};

static double seconds(Uint64 ticks) {
  return (double)ticks / (double)SDL_GetPerformanceFrequency();
}

static int compare(const void* a, const void* c) {
  double x = *(const double*)a;
  double y = *(const double*)c;
  return (x > y) - (x < y);
}

static double median(double* v, size_t n) {
  qsort(v, n, sizeof *v, compare);
  return v[n / 2];
}

static const char* dump_dir = nullptr;

/** Draws one more frame of the scene and writes the canvas pixels to a file. */
static void dump_scene(const scene* s) {
  static uint8_t pixels[CANVAS_W * CANVAS_H * 4];
  pxl_begin_frame(b.pxl);
  pxl_clear(b.pxl, pxl_rgb(0x102030));
  s->draw(s->n);
  if (!pxl_end_frame(b.pxl) ||
      !pxl_read_texture(b.pxl, pxl_get_canvas(b.pxl), pixels)) {
    fprintf(stderr, "%s: dump: %s\n", s->name, SDL_GetError());
    exit(1);
  }
  char path[512];
  snprintf(path, sizeof path, "%s/%s.rgba", dump_dir, s->name);
  FILE* f = fopen(path, "wb");
  fwrite(pixels, 1, sizeof pixels, f);
  fclose(f);
}

[[gnu::noinline]] static void run_scene(const scene* s, int frames) {
  double record[512];
  double end[512];
  double gpu[512];
  pxl_stats stats = {};
  for (int f = -3; f < frames; ++f) {
    Uint64 t0 = SDL_GetPerformanceCounter();
    pxl_begin_frame(b.pxl);
    s->draw(s->n);
    Uint64 t1 = SDL_GetPerformanceCounter();
    if (!pxl_end_frame(b.pxl)) {
      fprintf(stderr, "%s: pxl_end_frame: %s\n", s->name, SDL_GetError());
      exit(1);
    }
    Uint64 t2 = SDL_GetPerformanceCounter();
    SDL_WaitForGPUIdle(b.device);
    Uint64 t3 = SDL_GetPerformanceCounter();
    if (f >= 0) {
      record[f] = seconds(t1 - t0) * 1e6;
      end[f]    = seconds(t2 - t1) * 1e6;
      gpu[f]    = seconds(t3 - t2) * 1e6;
    }
    stats = pxl_get_stats(b.pxl);
    if (f == -1) {
      CALLGRIND_ZERO_STATS;
    }
  }
  CALLGRIND_DUMP_STATS_AT(s->name);
  if (dump_dir) {
    dump_scene(s);
  }
  double r = median(record, (size_t)frames);
  double e = median(end, (size_t)frames);
  double g = median(gpu, (size_t)frames);
  printf("%-15s %7zu %10.1f %10.1f %10.1f %9.1f %7zu %8zu %8zu %9.1f\n", s->name,
         s->n, r, e, r + e, (r + e) * 1000.0 / (double)s->n, stats.draw_calls,
         stats.vertices, stats.indices, g);
}

int main(int argc, char** argv) {
  int frames = 60;
  const char* only[32];
  size_t only_count = 0;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
      frames = atoi(argv[++i]);
    } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      dump_dir = argv[++i];
    } else if (only_count < 32) {
      only[only_count++] = argv[i];
    }
  }
  if (frames > 512) {
    frames = 512;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  b.device = SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
  if (!b.device) {
    fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError());
    return 1;
  }
  b.pxl = pxl_create(&(pxl_desc){.device = b.device, .width = CANVAS_W, .height = CANVAS_H});
  if (!b.pxl) {
    fprintf(stderr, "pxl_create: %s\n", SDL_GetError());
    return 1;
  }
  b.sheet  = make_texture(64, 16, 1);
  b.other  = make_texture(16, 16, 2);
  b.stream = make_texture(32, 32, 3);
  for (size_t i = 0; i < MAX_ITEMS; ++i) {
    b.items[i] = (item){
        .x     = frand() * CANVAS_W,
        .y     = frand() * CANVAS_H,
        .x2    = frand() * CANVAS_W,
        .y2    = frand() * CANVAS_H,
        .r     = 1.0f + (frand() * 29.0f),
        .rot   = frand() * 6.28f,
        .s     = 0.5f + frand(),
        .frame = (int)(frand() * 4.0f) & 3,
        .color = {(uint8_t)(frand() * 255), (uint8_t)(frand() * 255),
                  (uint8_t)(frand() * 255), 255},
    };
  }
  // Small triangles: each corner within 8 pixels of a random center.
  for (size_t i = 0; i < SDL_arraysize(b.tris); ++i) {
    if (i % 3 == 0) {
      b.items[MAX_ITEMS - 1].x = frand() * CANVAS_W;
      b.items[MAX_ITEMS - 1].y = frand() * CANVAS_H;
    }
    b.tris[i] = (pxl_vertex){
        .x = b.items[MAX_ITEMS - 1].x + (frand() * 16.0f) - 8.0f,
        .y = b.items[MAX_ITEMS - 1].y + (frand() * 16.0f) - 8.0f,
        .u = frand(), .v = frand(), .color = pxl_white};
  }
  // Create the pipelines before timing: the first use compiles them.
  pxl_begin_frame(b.pxl);
  pxl_draw_sprite(b.pxl, b.sheet, &(pxl_sprite){});
  pxl_set_blend(b.pxl, PXL_BLEND_ADD);
  pxl_draw_sprite(b.pxl, b.sheet, &(pxl_sprite){});
  pxl_end_frame(b.pxl);
  SDL_WaitForGPUIdle(b.device);
  printf("%-15s %7s %10s %10s %10s %9s %7s %8s %8s %9s\n", "scene", "items",
         "record_us", "end_us", "total_us", "ns/item", "draws", "verts",
         "indices", "gpu_us");
  for (size_t i = 0; i < SDL_arraysize(scenes); ++i) {
    bool run = only_count == 0;
    for (size_t j = 0; j < only_count; ++j) {
      run = run || strcmp(only[j], scenes[i].name) == 0;
    }
    if (run) {
      run_scene(&scenes[i], frames);
    }
  }
  pxl_destroy_texture(b.pxl, b.sheet);
  pxl_destroy_texture(b.pxl, b.other);
  pxl_destroy_texture(b.pxl, b.stream);
  pxl_destroy(b.pxl);
  SDL_DestroyGPUDevice(b.device);
  SDL_Quit();
  return 0;
}
