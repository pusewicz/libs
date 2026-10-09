// Tests the pxl functions of pxl_anim.h. Usage: test_draw FIXTURE_DIR
//
// The draw tests need a GPU device; without one they are skipped. Set
// PXL_TEST_REQUIRE_GPU to make them fail instead.

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"
#define PXL_IMPLEMENTATION
#include "pxl.h"
#define PXL_ANIM_IMPLEMENTATION
#include "pxl_anim.h"

#define PICO_UNIT_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>

#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/pxl_anim";

static constexpr int canvas_size = 16;

/* ---- Sprites ----------------------------------------------------------- */

TEST_CASE(test_sprite) {
  pxl_anim_image images[] = {
      {.x = 3, .y = 4, .width = 5, .height = 6, .offset_x = 7, .offset_y = 8},
      {},
  };
  pxl_anim_sheet sheet = {.frame_count = 2, .part_count = 1, .images = images};
  pxl_sprite sprite    = {.x = 1, .origin = {10, 20}};
  REQUIRE(pxl_anim_sprite(&sheet, 0, 0, &sprite));
  REQUIRE(sprite.src.x == 3 && sprite.src.y == 4);
  REQUIRE(sprite.src.w == 5 && sprite.src.h == 6);
  REQUIRE(sprite.origin.x == 3 && sprite.origin.y == 12);
  REQUIRE(sprite.x == 1);

  // An empty image must not draw: pxl draws all the texture for it.
  sprite = (pxl_sprite){.origin = {10, 20}};
  REQUIRE(!pxl_anim_sprite(&sheet, 1, 0, &sprite));
  REQUIRE(sprite.src.w == 0 && sprite.origin.x == 10);
  return true;
}

/* ---- Draws ------------------------------------------------------------- */

// The GPU state of the tests: a context, the character sheet and its atlas.
static struct {
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_anim_sheet sheet;
  pxl_texture* atlas;
  pxl_color pixels[canvas_size * canvas_size];
} gpu;

// Draws a part of frame 0 at (8, 8) with an origin and reads the canvas.
static bool draw(uint32_t part, float origin_x, bool flip_x) {
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_play(&player, &gpu.sheet,
                        pxl_anim_find_clip(&gpu.sheet, "idle")));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_transparent);
  pxl_anim_draw(gpu.pxl, gpu.atlas, &player, part,
                &(pxl_sprite){
                    .x      = 8,
                    .y      = 8,
                    .origin = {origin_x, 8},
                    .flip_x = flip_x,
                });
  REQUIRE(pxl_end_frame(gpu.pxl));
  REQUIRE(pxl_read_texture(gpu.pxl, pxl_get_canvas(gpu.pxl), gpu.pixels));
  return true;
}

// Tells if a pixel of the canvas has the body color of frame 0.
static bool is_body(int x, int y) {
  pxl_color c = gpu.pixels[(y * canvas_size) + x];
  return c.r == 0 && c.g == 200 && c.b == 30 && c.a == 255;
}

// Counts the pixels of the canvas that are not transparent.
static int count_pixels() {
  int count = 0;
  for (int i = 0; i < canvas_size * canvas_size; i++) {
    count += gpu.pixels[i].a != 0;
  }
  return count;
}

TEST_CASE(test_draw_body) {
  // The body is at x 6 to 9 and y 8 to 11 of the frame.
  uint32_t body = pxl_anim_find_part(&gpu.sheet, "body");
  REQUIRE(draw(body, 8, false));
  REQUIRE(count_pixels() == 16);
  REQUIRE(is_body(6, 8) && is_body(9, 11));

  // With the origin at x 7, the body moves 1 pixel to the right.
  REQUIRE(draw(body, 7, false));
  REQUIRE(count_pixels() == 16);
  REQUIRE(is_body(7, 8) && is_body(10, 11));

  // A flip mirrors the frame around the origin: x 6 to 9 become 5 to 8.
  REQUIRE(draw(body, 7, true));
  REQUIRE(count_pixels() == 16);
  REQUIRE(is_body(5, 8) && is_body(8, 11));
  return true;
}

TEST_CASE(test_draw_empty_part) {
  // The effects are empty in frame 0.
  REQUIRE(draw(pxl_anim_find_part(&gpu.sheet, "fx"), 8, false));
  REQUIRE(count_pixels() == 0);
  return true;
}

/* ---- Main -------------------------------------------------------------- */

static TEST_SUITE(suite_cpu) {
  RUN_TEST_CASE(test_sprite);
}

static TEST_SUITE(suite_gpu) {
  RUN_TEST_CASE(test_draw_body);
  RUN_TEST_CASE(test_draw_empty_part);
}

// Makes the character sheet and its atlas texture.
static bool load_character() {
  char path[1024];
  int length =
      snprintf(path, sizeof path, "%s/character.aseprite", fixture_dir);
  aseprite_sprite sprite;
  if (length < 0 || (size_t)length >= sizeof path ||
      aseprite_load_file(path, &sprite) != ASEPRITE_OK) {
    fprintf(stderr, "cannot load %s\n", path);
    return false;
  }
  static const char* const parts[] = {"body", "fx"};
  pxl_anim_result result           = pxl_anim_load_aseprite(
      &gpu.sheet, &sprite,
      &(pxl_anim_aseprite_desc){.parts = parts, .part_count = 2});
  aseprite_free(&sprite);
  if (result != PXL_ANIM_OK) {
    fprintf(stderr, "%s\n", pxl_anim_result_string(result));
    return false;
  }
  gpu.atlas =
      pxl_create_texture(gpu.pxl, &(pxl_texture_desc){
                                      .width  = (int)gpu.sheet.atlas_width,
                                      .height = (int)gpu.sheet.atlas_height,
                                      .pixels = gpu.sheet.atlas,
                                  });
  return gpu.atlas != nullptr;
}

// Reports that the GPU tests cannot run, because the SDL function `failed`
// failed. Returns false if PXL_TEST_REQUIRE_GPU is set: then they must run.
static bool skip_gpu_suite(const char* failed) {
  if (getenv("PXL_TEST_REQUIRE_GPU") != nullptr) {
    fprintf(stderr, "%s: %s: PXL_TEST_REQUIRE_GPU is set\n", failed,
            SDL_GetError());
    return false;
  }
  printf("%s: %s: GPU tests skipped\n", failed, SDL_GetError());
  return true;
}

// Runs the GPU tests. Returns false if a step fails on a GPU that works.
static bool run_gpu_suite() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return skip_gpu_suite("SDL_Init");
  }
  gpu.device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  if (!gpu.device) {
    bool ok = skip_gpu_suite("SDL_CreateGPUDevice");
    SDL_Quit();
    return ok;
  }
  gpu.pxl = pxl_create(&(pxl_desc){
      .device = gpu.device,
      .width  = canvas_size,
      .height = canvas_size,
  });
  bool ok = gpu.pxl && load_character();
  if (ok) {
    RUN_TEST_SUITE(suite_gpu);
  }
  if (gpu.pxl) {
    pxl_destroy_texture(gpu.pxl, gpu.atlas);
    pxl_destroy(gpu.pxl);
  }
  pxl_anim_free(&gpu.sheet);
  SDL_DestroyGPUDevice(gpu.device);
  SDL_Quit();
  return ok;
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_cpu);
  bool ok = run_gpu_suite();
  pu_print_stats();
  return ok && !pu_test_failed() ? 0 : 1;
}
