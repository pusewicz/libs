// Measures the CPU time of pxl.h for three workloads. The Swift port runs the
// same workloads in swift/Sources/pxl-bench/main.swift. Both print the same
// table.

#define PXL_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>

#include "pxl.h"

static constexpr int warmup_frames = 30;
static constexpr int max_frames    = 10000;
static constexpr int sprite_count  = 20000;
static constexpr int shape_count   = 2000;
static constexpr int text_count    = 500;

/** Draws the sprites of a frame. Each sprite is a quad. */
static void draw_sprites(pxl_context* pxl, pxl_texture* sprite, int frame) {
  for (int i = 0; i < sprite_count; ++i) {
    float scale = 1.0f + ((float)(i % 3) * 0.5f);
    pxl_draw_sprite(
        pxl, sprite,
        &(pxl_sprite){
            .x        = (float)(i % 320),
            .y        = (float)(i / 320 % 180),
            .origin   = {8, 8},
            .scale    = {scale, scale},
            .rotation = i % 4 == 0 ? (float)(i + frame) * 0.01f : 0.0f,
            .flip_x   = i % 2 == 0,
        });
  }
}

/** Draws the shapes of a frame: circles, lines and rectangle edges. */
static void draw_shapes(pxl_context* pxl, [[maybe_unused]] pxl_texture* sprite,
                        int frame) {
  for (int i = 0; i < shape_count; ++i) {
    float x         = (float)(((i * 7) + frame) % 320);
    float y         = (float)(i * 13 % 180);
    pxl_color color = pxl_rgb((uint32_t)i * 2654435761u);
    pxl_draw_circle(pxl, x, y, (float)(4 + (i % 8)), color);
    pxl_draw_line(pxl, x, y, x + 40.0f, y + (float)(i % 30), color);
    pxl_draw_rect_lines(pxl, x, y, 24.0f, 16.0f, color);
  }
}

/** Draws the text of a frame. Each line is formatted. */
static void draw_text(pxl_context* pxl, [[maybe_unused]] pxl_texture* sprite,
                      int frame) {
  for (int i = 0; i < text_count; ++i) {
    pxl_draw_text(pxl, (float)(i * 3 % 300), (float)(i * 5 % 170), pxl_white,
                  "frame %d score %d", frame, i * 10);
  }
}

typedef struct workload {
  const char* name;
  void (*draw)(pxl_context* pxl, pxl_texture* sprite, int frame);
} workload;

static int compare_doubles(const void* a, const void* b) {
  double x = *(const double*)a;
  double y = *(const double*)b;
  return (x > y) - (x < y);
}

static double median(double values[], size_t count) {
  qsort(values, count, sizeof *values, compare_doubles);
  return values[count / 2];
}

static double milliseconds(uint64_t start, uint64_t end) {
  return (double)(end - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

/** Returns the number of timed frames: PXL_BENCH_FRAMES, or 300. */
static int timed_frames() {
  const char* value = SDL_getenv("PXL_BENCH_FRAMES");
  int frames        = value ? SDL_atoi(value) : 300;
  return SDL_clamp(frames, 1, max_frames);
}

/** Runs a workload. Prints the median times and the work of the last frame. */
static bool run(SDL_GPUDevice* device, pxl_context* pxl, pxl_texture* sprite,
                const workload* w, int frames) {
  static double record[max_frames];
  static double end[max_frames];
  for (int frame = 0; frame < warmup_frames + frames; ++frame) {
    uint64_t t0 = SDL_GetPerformanceCounter();
    pxl_begin_frame(pxl);
    pxl_clear(pxl, pxl_black);
    w->draw(pxl, sprite, frame);
    uint64_t t1 = SDL_GetPerformanceCounter();
    bool ended  = pxl_end_frame(pxl);
    uint64_t t2 = SDL_GetPerformanceCounter();
    SDL_WaitForGPUIdle(device);
    if (!ended) {
      fprintf(stderr, "%s: %s\n", w->name, SDL_GetError());
      return false;
    }
    if (frame >= warmup_frames) {
      record[frame - warmup_frames] = milliseconds(t0, t1);
      end[frame - warmup_frames]    = milliseconds(t1, t2);
    }
  }
  pxl_stats stats = pxl_get_stats(pxl);
  printf("%-8s %10.3f %10.3f %6zu %9zu %9zu\n", w->name,
         median(record, (size_t)frames), median(end, (size_t)frames),
         stats.draw_calls, stats.vertices, stats.indices);
  return true;
}

/** Makes a 16 x 16 checkerboard sprite. */
static pxl_texture* create_sprite(pxl_context* pxl) {
  pxl_color pixels[16 * 16];
  for (int i = 0; i < 16 * 16; ++i) {
    pixels[i] =
        ((i % 16) + (i / 16)) % 2 ? pxl_rgb(0xff004d) : pxl_rgb(0x29adff);
  }
  return pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 16, .height = 16, .pixels = pixels});
}

/** Returns true if the arguments name a workload, or name none. */
static bool selected(const char* name, int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (SDL_strcmp(argv[i], name) == 0) {
      return true;
    }
  }
  return argc <= 1;
}

int main(int argc, char* argv[]) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GPUDevice* device =
      SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
  pxl_context* pxl    = device ? pxl_create(&(pxl_desc){
                                     .device = device,
                                     .width  = 320,
                                     .height = 180,
                                 })
                               : nullptr;
  pxl_texture* sprite = pxl ? create_sprite(pxl) : nullptr;
  bool ok             = sprite != nullptr;
  if (ok) {
    printf("C, %s, median of %d frames in ms\n", SDL_GetGPUDeviceDriver(device),
           timed_frames());
    printf("%-8s %10s %10s %6s %9s %9s\n", "workload", "record", "end_frame",
           "draws", "vertices", "indices");
    const workload workloads[] = {
        {"sprites", draw_sprites},
        {"shapes", draw_shapes},
        {"text", draw_text},
    };
    for (size_t i = 0; ok && i < SDL_arraysize(workloads); ++i) {
      if (selected(workloads[i].name, argc, argv)) {
        ok = run(device, pxl, sprite, &workloads[i], timed_frames());
      }
    }
  } else {
    fprintf(stderr, "setup: %s\n", SDL_GetError());
  }
  if (pxl) {
    pxl_destroy_texture(pxl, sprite);
    pxl_destroy(pxl);
  }
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
  return ok ? 0 : 1;
}
