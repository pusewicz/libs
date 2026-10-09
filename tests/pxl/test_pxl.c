// Tests for pxl.h. The GPU tests draw into an offscreen canvas and read the
// pixels back. They need a GPU device; without one they are skipped. Set
// PXL_TEST_REQUIRE_GPU to make them fail instead.

#define PXL_IMPLEMENTATION
#include "pxl.h"
// A second include must not define the implementation again.
#include "pxl.h" // NOLINT(readability-duplicate-include)
#include "test_shaders.h"

#define PICO_UNIT_IMPLEMENTATION
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico_unit.h"

static constexpr int canvas_size = 16;

static bool near(float a, float b) {
  return fabsf(a - b) < 1e-4f;
}

static bool same_color(pxl_color a, pxl_color b, int tolerance) {
  return abs(a.r - b.r) <= tolerance && abs(a.g - b.g) <= tolerance &&
         abs(a.b - b.b) <= tolerance && abs(a.a - b.a) <= tolerance;
}

static constexpr pxl_color red   = {255, 0, 0, 255};
static constexpr pxl_color green = {0, 255, 0, 255};
static constexpr pxl_color blue  = {0, 0, 255, 255};

// ---------------------------------------------------------------------------
// CPU tests

TEST_CASE(test_colors) {
  pxl_color c = pxl_rgb(0x123456);
  REQUIRE(c.r == 0x12 && c.g == 0x34 && c.b == 0x56 && c.a == 255);
  c = pxl_rgba(0x12345678);
  REQUIRE(c.r == 0x12 && c.g == 0x34 && c.b == 0x56 && c.a == 0x78);
  REQUIRE(pxl__premultiply(255, 128) == 128);
  REQUIRE(pxl__premultiply(100, 255) == 100);
  REQUIRE(pxl__premultiply(100, 0) == 0);
  return true;
}

TEST_CASE(test_transforms) {
  pxl_transform m = {
      .a  = 2.0f,
      .b  = 1.0f,
      .c  = -1.0f,
      .d  = 3.0f,
      .tx = 5.0f,
      .ty = -7.0f,
  };
  pxl_vec2 p = pxl_transform_point(m, (pxl_vec2){.x = 1.0f, .y = 2.0f});
  REQUIRE(near(p.x, 5.0f) && near(p.y, 0.0f));
  pxl_vec2 back = pxl_transform_point(pxl_transform_inverse(m), p);
  REQUIRE(near(back.x, 1.0f) && near(back.y, 2.0f));
  pxl_transform zero = {};
  pxl_transform inv  = pxl_transform_inverse(zero);
  REQUIRE(inv.a == 1.0f && inv.d == 1.0f);
  return true;
}

TEST_CASE(test_fit) {
  bool exact = false;
  pxl_rect r = pxl__fit(320, 180, 1280, 720, PXL_SCALE_INTEGER, &exact);
  REQUIRE(r.x == 0.0f && r.y == 0.0f && r.w == 1280.0f && r.h == 720.0f);
  REQUIRE(exact);

  r = pxl__fit(320, 180, 1000, 700, PXL_SCALE_INTEGER, &exact);
  REQUIRE(r.x == 20.0f && r.y == 80.0f && r.w == 960.0f && r.h == 540.0f);
  REQUIRE(exact);

  r = pxl__fit(320, 180, 1000, 700, PXL_SCALE_FIT, &exact);
  REQUIRE(r.x == 0.0f && r.y == 68.0f && r.w == 1000.0f && r.h == 563.0f);
  REQUIRE(!exact);

  r = pxl__fit(320, 180, 1000, 700, PXL_SCALE_STRETCH, &exact);
  REQUIRE(r.x == 0.0f && r.y == 0.0f && r.w == 1000.0f && r.h == 700.0f);
  REQUIRE(!exact);

  r = pxl__fit(320, 180, 200, 100, PXL_SCALE_INTEGER, &exact);
  REQUIRE(r.w < 200.5f && r.h <= 100.0f);
  REQUIRE(!exact);
  return true;
}

TEST_CASE(test_circle_spans) {
  REQUIRE(pxl__circle_span(0, 0) == 0);
  REQUIRE(pxl__circle_span(0, 1) == -1);
  REQUIRE(pxl__circle_span(2, 0) == 2);
  REQUIRE(pxl__circle_span(2, 1) == 2);
  REQUIRE(pxl__circle_span(2, -2) == 1);
  REQUIRE(pxl__circle_span(2, 3) == -1);
  for (int r = 0; r < 200; ++r) {
    for (int dy = -r; dy <= r; ++dy) {
      int half = pxl__circle_span(r, dy);
      REQUIRE((half * half) + (dy * dy) <= r * (r + 1));
      REQUIRE(((half + 1) * (half + 1)) + (dy * dy) > r * (r + 1));
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// GPU tests

/** The GPU state of the tests: a context with a canvas of canvas_size. */
static struct {
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_color pixels[canvas_size * canvas_size];
} gpu;

static pxl_color pixel_at(int x, int y) {
  return gpu.pixels[(y * canvas_size) + x];
}

/** Ends the frame and reads the canvas into gpu.pixels. */
static bool finish() {
  REQUIRE(pxl_end_frame(gpu.pxl));
  REQUIRE(pxl_read_texture(gpu.pxl, pxl_get_canvas(gpu.pxl), gpu.pixels));
  return true;
}

/** Checks that the canvas has `color` where `mask` has '#', else `other`. */
static bool matches_mask(const char* const mask[], pxl_color color,
                         pxl_color other) {
  for (int y = 0; y < canvas_size && mask[y]; ++y) {
    for (int x = 0; x < canvas_size && mask[y][x]; ++x) {
      pxl_color want = mask[y][x] == '#' ? color : other;
      if (!same_color(pixel_at(x, y), want, 0)) {
        fprintf(stderr, "the pixel at (%d, %d) does not match the mask\n", x,
                y);
        return false;
      }
    }
  }
  return true;
}

/** The resources of the current test. The teardown destroys them. */
static struct {
  pxl_texture* textures[8];
  size_t texture_count;
  pxl_font* fonts[4];
  size_t font_count;
  pxl_shader* shaders[4];
  size_t shader_count;
  SDL_Surface* surface;
} owned;

/** Stops the tests if a test owns more resources than `owned` holds. */
static void check_room(size_t count, size_t capacity) {
  if (count >= capacity) {
    fprintf(stderr, "a test owns too many resources\n");
    abort();
  }
}

static pxl_texture* own_texture(pxl_texture* texture) {
  check_room(owned.texture_count, SDL_arraysize(owned.textures));
  owned.textures[owned.texture_count++] = texture;
  return texture;
}

static pxl_font* own_font(pxl_font* font) {
  check_room(owned.font_count, SDL_arraysize(owned.fonts));
  owned.fonts[owned.font_count++] = font;
  return font;
}

static pxl_shader* own_shader(pxl_shader* shader) {
  check_room(owned.shader_count, SDL_arraysize(owned.shaders));
  owned.shaders[owned.shader_count++] = shader;
  return shader;
}

static SDL_Surface* own_surface(SDL_Surface* surface) {
  owned.surface = surface;
  return surface;
}

/** Destroys the resources of the test that ended. */
static void destroy_owned() {
  for (size_t i = 0; i < owned.font_count; ++i) {
    pxl_destroy_font(gpu.pxl, owned.fonts[i]);
  }
  for (size_t i = 0; i < owned.shader_count; ++i) {
    pxl_destroy_shader(gpu.pxl, owned.shaders[i]);
  }
  for (size_t i = 0; i < owned.texture_count; ++i) {
    pxl_destroy_texture(gpu.pxl, owned.textures[i]);
  }
  SDL_DestroySurface(owned.surface);
  memset(&owned, 0, sizeof owned);
}

static pxl_texture* quad_texture(pxl_context* pxl) {
  const pxl_color pixels[] = {red, green, blue, pxl_white};
  return pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 2, .height = 2, .pixels = pixels});
}

TEST_CASE(test_clear) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, red);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), red, 0));
  REQUIRE(same_color(pixel_at(canvas_size - 1, canvas_size - 1), red, 0));
  return true;
}

TEST_CASE(test_coordinates) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_pixel(gpu.pxl, 0, 0, pxl_white);
  pxl_draw_rect(gpu.pxl, 2, 1, 3, 2, pxl_white);
  pxl_draw_pixel(gpu.pxl, 15, 15, pxl_white);
  REQUIRE(finish());
  const char* const mask[] = {
      "#...............",
      "..###...........",
      "..###...........",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "................",
      "...............#",
      nullptr,
  };
  REQUIRE(matches_mask(mask, pxl_white, pxl_black));
  return true;
}

TEST_CASE(test_sprite_orientation) {
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  REQUIRE(texture != nullptr);
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){.x = 4, .y = 4});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 4, .y = 8, .flip_x = true});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 10, .y = 2, .scale = {2, 2}});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 12, .y = 12, .rotation = SDL_PI_F / 2});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(4, 4), red, 0));
  REQUIRE(same_color(pixel_at(5, 4), green, 0));
  REQUIRE(same_color(pixel_at(4, 5), blue, 0));
  REQUIRE(same_color(pixel_at(5, 5), pxl_white, 0));
  REQUIRE(same_color(pixel_at(3, 8), red, 0));
  REQUIRE(same_color(pixel_at(2, 8), green, 0));
  REQUIRE(same_color(pixel_at(4, 8), pxl_black, 0));
  REQUIRE(same_color(pixel_at(10, 2), red, 0));
  REQUIRE(same_color(pixel_at(11, 3), red, 0));
  REQUIRE(same_color(pixel_at(12, 2), green, 0));
  REQUIRE(same_color(pixel_at(13, 5), pxl_white, 0));
  REQUIRE(same_color(pixel_at(11, 12), red, 0));
  REQUIRE(same_color(pixel_at(11, 13), green, 0));
  REQUIRE(same_color(pixel_at(10, 12), blue, 0));
  return true;
}

TEST_CASE(test_sprite_options) {
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 1, .y = 1, .src = {1, 0, 1, 2}});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 6, .y = 6, .origin = {1, 1}});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 10, .y = 10, .color = {255, 0, 0, 255}});
  pxl_draw_sprite(gpu.pxl, texture,
                  &(pxl_sprite){.x = 13, .y = 1, .overlay = {0, 0, 255, 255}});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(1, 1), green, 0));
  REQUIRE(same_color(pixel_at(1, 2), pxl_white, 0));
  REQUIRE(same_color(pixel_at(2, 1), pxl_black, 0));
  REQUIRE(same_color(pixel_at(5, 5), red, 0));
  REQUIRE(same_color(pixel_at(6, 6), pxl_white, 0));
  REQUIRE(same_color(pixel_at(11, 10), pxl_black, 0));
  REQUIRE(same_color(pixel_at(11, 11), red, 0));
  REQUIRE(same_color(pixel_at(13, 1), blue, 0));
  REQUIRE(same_color(pixel_at(14, 2), blue, 0));
  return true;
}

TEST_CASE(test_snap) {
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){.x = 2.4f, .y = 2.6f});
  pxl_translate(gpu.pxl, 0.5f, 0.5f);
  pxl_draw_rect(gpu.pxl, 8, 8, 2, 2, pxl_white);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(2, 3), red, 0));
  REQUIRE(same_color(pixel_at(3, 4), pxl_white, 0));
  REQUIRE(same_color(pixel_at(8, 8), pxl_black, 0));
  REQUIRE(same_color(pixel_at(9, 9), pxl_white, 0));
  REQUIRE(same_color(pixel_at(10, 10), pxl_white, 0));
  return true;
}

TEST_CASE(test_lines) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_line(gpu.pxl, 0, 0, 7, 3, pxl_white);
  pxl_draw_line(gpu.pxl, 15, 5, 15, 9, pxl_white);
  pxl_draw_line(gpu.pxl, 3, 12, 0, 15, pxl_white);
  pxl_draw_line(gpu.pxl, -100, 6, 1000, 6, pxl_white);
  REQUIRE(finish());
  const char* const mask[] = {
      "##..............",
      "..##............",
      "....##..........",
      "......##........",
      "................",
      "...............#",
      "################",
      "...............#",
      "...............#",
      "...............#",
      "................",
      "................",
      "...#............",
      "..#.............",
      ".#..............",
      "#...............",
      nullptr,
  };
  REQUIRE(matches_mask(mask, pxl_white, pxl_black));
  return true;
}

TEST_CASE(test_rect_lines) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_rect_lines(gpu.pxl, 1, 1, 5, 4, pxl_white);
  pxl_draw_rect_lines(gpu.pxl, 8, 1, 2, 2, pxl_white);
  REQUIRE(finish());
  const char* const mask[] = {
      "................",
      ".#####..##......",
      ".#...#..##......",
      ".#...#..........",
      ".#####..........",
      "................",
      nullptr,
  };
  REQUIRE(matches_mask(mask, pxl_white, pxl_black));
  return true;
}

TEST_CASE(test_circles) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_circle(gpu.pxl, 3, 3, 2, pxl_white);
  pxl_draw_circle_lines(gpu.pxl, 10, 3, 2, pxl_white);
  pxl_draw_circle(gpu.pxl, 3, 10, 0, pxl_white);
  pxl_draw_circle_lines(gpu.pxl, 10, 11, 3, pxl_white);
  pxl_draw_circle(gpu.pxl, 1, 14, -1, pxl_white);
  REQUIRE(finish());
  const char* const mask[] = {
      "................",
      "..###....###....",
      ".#####..#...#...",
      ".#####..#...#...",
      ".#####..#...#...",
      "..###....###....",
      "................",
      "................",
      ".........###....",
      "........#...#...",
      "...#...#.....#..",
      ".......#.....#..",
      ".......#.....#..",
      "........#...#...",
      ".........###....",
      "................",
      nullptr,
  };
  REQUIRE(matches_mask(mask, pxl_white, pxl_black));
  return true;
}

TEST_CASE(test_triangles) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_triangle(gpu.pxl, 0, 0, 4, 0, 0, 4, pxl_white);
  const pxl_vertex vertices[] = {
      {.x = 8, .y = 8, .color = red},
      {.x = 16, .y = 8, .color = red},
      {.x = 8, .y = 16, .color = red},
  };
  pxl_draw_triangles(gpu.pxl, nullptr, vertices, 3);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), pxl_white, 0));
  REQUIRE(same_color(pixel_at(1, 1), pxl_white, 0));
  REQUIRE(same_color(pixel_at(3, 3), pxl_black, 0));
  REQUIRE(same_color(pixel_at(8, 8), red, 0));
  REQUIRE(same_color(pixel_at(15, 15), pxl_black, 0));
  return true;
}

TEST_CASE(test_blend) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, blue);
  pxl_draw_rect(gpu.pxl, 0, 0, 4, 4, (pxl_color){255, 0, 0, 128});
  pxl_set_blend(gpu.pxl, PXL_BLEND_ADD);
  pxl_draw_rect(gpu.pxl, 4, 0, 4, 4, (pxl_color){50, 60, 0, 255});
  pxl_set_blend(gpu.pxl, PXL_BLEND_MULTIPLY);
  pxl_draw_rect(gpu.pxl, 8, 0, 4, 4, (pxl_color){128, 0, 0, 255});
  pxl_set_blend(gpu.pxl, PXL_BLEND_NONE);
  pxl_draw_rect(gpu.pxl, 12, 0, 4, 4, (pxl_color){255, 0, 0, 128});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), (pxl_color){128, 0, 127, 255}, 1));
  REQUIRE(same_color(pixel_at(4, 0), (pxl_color){50, 60, 255, 255}, 1));
  REQUIRE(same_color(pixel_at(8, 0), (pxl_color){0, 0, 0, 255}, 1));
  REQUIRE(same_color(pixel_at(12, 0), (pxl_color){128, 0, 0, 128}, 1));
  return true;
}

TEST_CASE(test_push_pop) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_push(gpu.pxl);
  pxl_translate(gpu.pxl, 4, 4);
  pxl_scale(gpu.pxl, 2, 2);
  pxl_draw_rect(gpu.pxl, 0, 0, 1, 1, pxl_white);
  pxl_pop(gpu.pxl);
  pxl_draw_pixel(gpu.pxl, 0, 0, red);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(4, 4), pxl_white, 0));
  REQUIRE(same_color(pixel_at(5, 5), pxl_white, 0));
  REQUIRE(same_color(pixel_at(6, 6), pxl_black, 0));
  REQUIRE(same_color(pixel_at(0, 0), red, 0));
  return true;
}

TEST_CASE(test_clip) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_set_clip(gpu.pxl, 2, 2, 3, 3);
  pxl_draw_rect(gpu.pxl, 0, 0, 16, 16, pxl_white);
  pxl_set_clip(gpu.pxl, 14, 14, 10, 10);
  pxl_draw_rect(gpu.pxl, 0, 0, 16, 16, red);
  pxl_set_clip(gpu.pxl, 0, 0, 0, 0);
  pxl_draw_rect(gpu.pxl, 0, 0, 16, 16, red);
  pxl_reset_clip(gpu.pxl);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(1, 1), pxl_black, 0));
  REQUIRE(same_color(pixel_at(2, 2), pxl_white, 0));
  REQUIRE(same_color(pixel_at(4, 4), pxl_white, 0));
  REQUIRE(same_color(pixel_at(5, 5), pxl_black, 0));
  REQUIRE(same_color(pixel_at(14, 14), red, 0));
  REQUIRE(same_color(pixel_at(13, 13), pxl_black, 0));
  return true;
}

TEST_CASE(test_render_target) {
  pxl_texture* target = own_texture(pxl_create_texture(
      gpu.pxl,
      &(pxl_texture_desc){.width = 4, .height = 4, .render_target = true}));
  REQUIRE(target != nullptr);
  pxl_begin_frame(gpu.pxl);
  pxl_set_target(gpu.pxl, target);
  REQUIRE(pxl_get_width(gpu.pxl) == 4);
  pxl_clear(gpu.pxl, green);
  pxl_draw_pixel(gpu.pxl, 1, 1, red);
  pxl_set_target(gpu.pxl, nullptr);
  REQUIRE(pxl_get_width(gpu.pxl) == canvas_size);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, target, &(pxl_sprite){.x = 2, .y = 2});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(1, 1), pxl_black, 0));
  REQUIRE(same_color(pixel_at(2, 2), green, 0));
  REQUIRE(same_color(pixel_at(3, 3), red, 0));
  REQUIRE(same_color(pixel_at(5, 5), green, 0));
  return true;
}

TEST_CASE(test_nine_slice) {
  pxl_color pixels[9] = {};
  for (size_t i = 0; i < 9; ++i) {
    pixels[i] = i == 4 ? blue : red;
  }
  pxl_texture* texture = own_texture(pxl_create_texture(
      gpu.pxl, &(pxl_texture_desc){.width = 3, .height = 3, .pixels = pixels}));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_nine_slice(gpu.pxl, texture,
                      &(pxl_nine_slice){
                          .left   = 1,
                          .top    = 1,
                          .right  = 1,
                          .bottom = 1,
                          .x      = 2,
                          .y      = 2,
                          .w      = 8,
                          .h      = 6,
                      });
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(2, 2), red, 0));
  REQUIRE(same_color(pixel_at(9, 7), red, 0));
  REQUIRE(same_color(pixel_at(5, 2), red, 0));
  REQUIRE(same_color(pixel_at(3, 3), blue, 0));
  REQUIRE(same_color(pixel_at(8, 6), blue, 0));
  REQUIRE(same_color(pixel_at(10, 8), pxl_black, 0));
  return true;
}

TEST_CASE(test_update_texture) {
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  REQUIRE(pxl_update_texture(gpu.pxl, texture, 1, 1, 1, 1, &blue));
  REQUIRE(!pxl_update_texture(gpu.pxl, texture, 1, 1, 2, 1, &blue));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), red, 0));
  REQUIRE(same_color(pixel_at(1, 1), blue, 0));
  return true;
}

TEST_CASE(test_batching) {
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  pxl_texture* other   = own_texture(quad_texture(gpu.pxl));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  for (int i = 0; i < 100; ++i) {
    pxl_draw_sprite(gpu.pxl, texture,
                    &(pxl_sprite){.x = (float)(i % 8), .y = (float)(i % 5)});
    pxl_draw_rect(gpu.pxl, 0, 0, 1, 1, red);
    pxl_draw_circle(gpu.pxl, 8, 8, 2, red);
  }
  REQUIRE(pxl_end_frame(gpu.pxl));
  pxl_stats stats = pxl_get_stats(gpu.pxl);
  REQUIRE(stats.draw_calls == 1);
  REQUIRE(stats.passes == 1);

  pxl_begin_frame(gpu.pxl);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){});
  pxl_draw_sprite(gpu.pxl, other, &(pxl_sprite){});
  pxl_set_blend(gpu.pxl, PXL_BLEND_ADD);
  pxl_draw_sprite(gpu.pxl, other, &(pxl_sprite){});
  REQUIRE(pxl_end_frame(gpu.pxl));
  stats = pxl_get_stats(gpu.pxl);
  REQUIRE(stats.draw_calls == 3);
  return true;
}

TEST_CASE(test_destroy_during_frame) {
  pxl_texture* texture = quad_texture(gpu.pxl);
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){});
  pxl_destroy_texture(gpu.pxl, texture);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), red, 0));
  return true;
}

TEST_CASE(test_measure_text) {
  pxl_vec2 size = pxl_measure_text(gpu.pxl, "Hi");
  REQUIRE(size.x == 8.0f && size.y == 10.0f);
  size = pxl_measure_text(gpu.pxl, "a\nbc");
  REQUIRE(size.x == 11.0f && size.y == 20.0f);
  size = pxl_measure_text(gpu.pxl, "%s", "");
  REQUIRE(size.x == 0.0f && size.y == 0.0f);
  size = pxl_measure_text(gpu.pxl, "%d", 42);
  REQUIRE(size.x == 12.0f);
  size = pxl_measure_text(gpu.pxl, "%300s", "a");
  REQUIRE(size.x == (299.0f * 4.0f) + 6.0f);
  return true;
}

TEST_CASE(test_draw_text) {
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_text(gpu.pxl, 1, 1, pxl_white, "!");
  pxl_draw_text(gpu.pxl, 3, 1, pxl_white, "\xc3\xa9");
  pxl_draw_text(gpu.pxl, 9, 1, pxl_white, "T\nT");
  REQUIRE(finish());
  const char* const mask[] = {
      "................",
      ".#..###..#####..",
      ".#.#...#...#....",
      ".#.....#...#....",
      ".#....#....#....",
      ".#...#.....#....",
      "...........#....",
      ".#...#.....#....",
      "................",
      "................",
      "................",
      ".........#####..",
      "...........#....",
      "...........#....",
      "...........#....",
      "...........#....",
      nullptr,
  };
  REQUIRE(matches_mask(mask, pxl_white, pxl_black));
  return true;
}

TEST_CASE(test_custom_font) {
  const pxl_color pixels[] = {
      red, pxl_transparent, green, green, red, pxl_transparent, green, green,
  };
  pxl_texture* texture            = own_texture(pxl_create_texture(
      gpu.pxl, &(pxl_texture_desc){.width = 4, .height = 2, .pixels = pixels}));
  static const uint8_t advances[] = {1, 3};
  pxl_font* font = own_font(pxl_create_font(gpu.pxl, &(pxl_font_desc){
                                                         .texture     = texture,
                                                         .glyph_width = 2,
                                                         .glyph_height = 2,
                                                         .first        = 'A',
                                                         .advances = advances,
                                                     }));
  REQUIRE(font != nullptr);
  REQUIRE(!own_font(
      pxl_create_font(gpu.pxl, &(pxl_font_desc){.texture = texture})));
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_set_font(gpu.pxl, font);
  pxl_draw_text(gpu.pxl, 0, 0, pxl_white, "ABAz");
  REQUIRE(pxl_measure_text(gpu.pxl, "AB").x == 4.0f);
  pxl_set_font(gpu.pxl, nullptr);
  REQUIRE(pxl_measure_text(gpu.pxl, "AB").x == 12.0f);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), red, 0));
  REQUIRE(same_color(pixel_at(1, 1), green, 0));
  REQUIRE(same_color(pixel_at(2, 0), green, 0));
  REQUIRE(same_color(pixel_at(3, 0), pxl_black, 0));
  REQUIRE(same_color(pixel_at(4, 0), red, 0));
  REQUIRE(same_color(pixel_at(5, 0), pxl_black, 0));
  REQUIRE(same_color(pixel_at(6, 0), pxl_black, 0));
  return true;
}

TEST_CASE(test_custom_shader) {
  REQUIRE(!own_shader(pxl_create_shader(gpu.pxl, &(pxl_shader_desc){})));
  pxl_shader* shader = own_shader(pxl_create_shader(gpu.pxl, &slots_frag));
  REQUIRE(shader != nullptr);
  pxl_texture* extra     = own_texture(pxl_create_texture(
      gpu.pxl, &(pxl_texture_desc){.width = 1, .height = 1, .pixels = &green}));
  const float add_red[]  = {1.0f, 0.0f, 0.0f, 0.0f};
  const float add_blue[] = {0.0f, 0.0f, 1.0f, 0.0f};
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_set_shader(gpu.pxl, shader);
  pxl_set_shader_texture(gpu.pxl, 1, extra);
  REQUIRE(pxl_set_uniforms(gpu.pxl, add_red, sizeof add_red));
  pxl_draw_rect(gpu.pxl, 0, 0, 4, 4, pxl_white);
  REQUIRE(pxl_set_uniforms(gpu.pxl, add_blue, sizeof add_blue));
  pxl_draw_rect(gpu.pxl, 4, 0, 4, 4, pxl_white);
  pxl_push(gpu.pxl);
  pxl_set_shader(gpu.pxl, nullptr);
  pxl_draw_rect(gpu.pxl, 8, 0, 4, 4, red);
  pxl_pop(gpu.pxl);
  pxl_draw_rect(gpu.pxl, 12, 0, 4, 4, pxl_white);
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(0, 0), (pxl_color){255, 255, 0, 255}, 0));
  REQUIRE(same_color(pixel_at(4, 0), (pxl_color){0, 255, 255, 255}, 0));
  REQUIRE(same_color(pixel_at(8, 0), red, 0));
  REQUIRE(same_color(pixel_at(12, 0), (pxl_color){0, 255, 255, 255}, 0));
  REQUIRE(pxl_get_stats(gpu.pxl).draw_calls == 4);
  return true;
}

/** Ends the frame into a screen texture and reads the screen. */
static bool present(pxl_texture* screen, pxl_color* pixels) {
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
  REQUIRE(cmd != nullptr);
  bool ended =
      pxl_end_frame_into(gpu.pxl, cmd, pxl_texture_handle(screen),
                         SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                         pxl_texture_width(screen), pxl_texture_height(screen));
  REQUIRE(SDL_SubmitGPUCommandBuffer(cmd));
  REQUIRE(ended);
  REQUIRE(pxl_read_texture(gpu.pxl, screen, pixels));
  return true;
}

TEST_CASE(test_present) {
  constexpr int w     = 40;
  constexpr int h     = 24;
  pxl_texture* screen = own_texture(pxl_create_texture(
      gpu.pxl,
      &(pxl_texture_desc){.width = w, .height = h, .render_target = true}));
  static pxl_color pixels[(size_t)w * h];
  REQUIRE(screen != nullptr);

  // Scale 1, in the middle.
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, red);
  pxl_draw_pixel(gpu.pxl, 0, 0, pxl_white);
  REQUIRE(present(screen, pixels));
  pxl_rect v = pxl_get_viewport(gpu.pxl);
  REQUIRE(v.x == 12 && v.y == 4 && v.w == 16 && v.h == 16);
  REQUIRE(same_color(pixels[(4 * w) + 12], pxl_white, 0));
  REQUIRE(same_color(pixels[(4 * w) + 13], red, 0));
  REQUIRE(same_color(pixels[(4 * w) + 11], pxl_black, 0));
  REQUIRE(same_color(pixels[(19 * w) + 27], red, 0));
  REQUIRE(same_color(pixels[(20 * w) + 28], pxl_black, 0));
  pxl_vec2 p = pxl_window_to_canvas(gpu.pxl, 12.5f, 4.5f);
  REQUIRE(near(p.x, 0.5f) && near(p.y, 0.5f));

  // Scale 1.5: sharp filtering blends only the texel edges.
  pxl_set_scale_mode(gpu.pxl, PXL_SCALE_FIT);
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, red);
  pxl_draw_pixel(gpu.pxl, 0, 0, pxl_white);
  REQUIRE(present(screen, pixels));
  v = pxl_get_viewport(gpu.pxl);
  REQUIRE(v.x == 8 && v.y == 0 && v.w == 24 && v.h == 24);
  REQUIRE(same_color(pixels[8], pxl_white, 0));
  REQUIRE(same_color(pixels[9], (pxl_color){255, 128, 128, 255}, 3));
  REQUIRE(same_color(pixels[10], red, 0));
  REQUIRE(same_color(pixels[7], pxl_black, 0));
  p = pxl_window_to_canvas(gpu.pxl, 20.0f, 12.0f);
  REQUIRE(near(p.x, 8.0f) && near(p.y, 8.0f));
  pxl_set_scale_mode(gpu.pxl, PXL_SCALE_INTEGER);

  return true;
}

/** Saves a surface as BMP and PNG, and loads both with pxl. */
static bool check_load(SDL_Surface* surface) {
  char bmp[1024];
  char png[1024];
  snprintf(bmp, sizeof bmp, "%spxl_load_test.bmp", SDL_GetBasePath());
  snprintf(png, sizeof png, "%spxl_load_test.png", SDL_GetBasePath());
  REQUIRE(SDL_SaveBMP(surface, bmp));
  pxl_texture* textures[2] = {own_texture(pxl_load_texture(gpu.pxl, bmp))};
#if SDL_VERSION_ATLEAST(3, 4, 0)
  REQUIRE(SDL_SavePNG(surface, png));
  textures[1] = own_texture(pxl_load_texture(gpu.pxl, png));
  REQUIRE(textures[1] != nullptr);
#endif
  REQUIRE(textures[0] != nullptr);
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  for (size_t i = 0; i < 2; ++i) {
    if (textures[i]) {
      pxl_draw_sprite(gpu.pxl, textures[i], &(pxl_sprite){.y = (float)i * 2});
    }
  }
  REQUIRE(finish());
  for (int i = 0; i < 2; ++i) {
    if (textures[i]) {
      REQUIRE(pxl_texture_width(textures[i]) == 2);
      REQUIRE(same_color(pixel_at(0, i * 2), red, 0));
      REQUIRE(same_color(pixel_at(1, i * 2), (pxl_color){0, 0, 128, 255}, 1));
    }
  }
  SDL_RemovePath(bmp);
  SDL_RemovePath(png);
  return true;
}

TEST_CASE(test_load_texture) {
  REQUIRE(!own_texture(pxl_load_texture(gpu.pxl, "no such file.png")));
  SDL_Surface* surface =
      own_surface(SDL_CreateSurface(2, 1, SDL_PIXELFORMAT_ARGB8888));
  REQUIRE(surface != nullptr);
  REQUIRE(SDL_WriteSurfacePixel(surface, 0, 0, 255, 0, 0, 255));
  REQUIRE(SDL_WriteSurfacePixel(surface, 1, 0, 0, 0, 255, 128));
  REQUIRE(check_load(surface));

  pxl_texture* texture =
      own_texture(pxl_create_texture_from_surface(gpu.pxl, surface));
  REQUIRE(texture != nullptr);
  pxl_begin_frame(gpu.pxl);
  pxl_clear(gpu.pxl, pxl_black);
  pxl_draw_sprite(gpu.pxl, texture, &(pxl_sprite){});
  REQUIRE(finish());
  REQUIRE(same_color(pixel_at(1, 0), (pxl_color){0, 0, 128, 255}, 1));
  return true;
}

/** Returns true if pxl_create() fails. */
static bool create_fails(const pxl_desc* desc) {
  pxl_context* pxl = pxl_create(desc);
  pxl_destroy(pxl);
  return pxl == nullptr;
}

TEST_CASE(test_create_errors) {
  REQUIRE(create_fails(&(pxl_desc){}));
  REQUIRE(create_fails(&(pxl_desc){.device = gpu.device}));
  REQUIRE(create_fails(&(pxl_desc){.device = gpu.device, .width = 8}));
  return true;
}

TEST_CASE(test_resource_errors) {
  REQUIRE(!own_texture(pxl_create_texture(gpu.pxl, &(pxl_texture_desc){})));
  REQUIRE(!own_texture(pxl_create_texture(
      gpu.pxl, &(pxl_texture_desc){.width = 4, .height = -1})));
  pxl_texture* texture = own_texture(quad_texture(gpu.pxl));
  REQUIRE(texture != nullptr);
  REQUIRE(!pxl_update_texture(gpu.pxl, texture, -1, 0, 1, 1, &red));
  REQUIRE(!pxl_update_texture(gpu.pxl, texture, 0, 0, 0, 1, &red));
  REQUIRE(!own_font(pxl_create_font(gpu.pxl, &(pxl_font_desc){})));
  REQUIRE(!own_font(pxl_create_font(gpu.pxl, &(pxl_font_desc){
                                                 .texture      = texture,
                                                 .glyph_width  = 4,
                                                 .glyph_height = 4,
                                             })));
  REQUIRE(!own_font(pxl_create_font(gpu.pxl, &(pxl_font_desc){
                                                 .texture      = texture,
                                                 .glyph_width  = 1,
                                                 .glyph_height = 1,
                                                 .count        = 5,
                                             })));
  pxl_shader_desc too_many = slots_frag;
  too_many.num_textures    = pxl_max_textures + 1;
  REQUIRE(!own_shader(pxl_create_shader(gpu.pxl, &too_many)));
  return true;
}

// ---------------------------------------------------------------------------
// Memory

static constexpr size_t block_size = 1 << 16;

/**
 * The block of the contexts in these tests. They have small limits. It is
 * aligned by hand, because the Windows C runtime has no aligned_alloc, and it
 * is not a static array, because the analyzer reads a static array as zero.
 */
static unsigned char* block;

static constexpr size_t canary_size   = 64;
static constexpr unsigned char canary = 0xA5;

/** Gets a desc for the block, with limits that a test lowers. */
static pxl_desc limited_desc() {
  return (pxl_desc){
      .device            = gpu.device,
      .width             = 8,
      .height            = 8,
      .max_vertices      = 64,
      .max_indices       = 96,
      .max_commands      = 8,
      .max_uniform_bytes = 64,
      .max_pipelines     = 8,
  };
}

TEST_CASE(test_create_in) {
  pxl_desc desc = limited_desc();
  size_t size   = pxl_memory_size(&desc);
  REQUIRE(size > 0 && size + canary_size <= block_size);
  memset(block + size, canary, canary_size);

  REQUIRE(pxl_create_in(&desc, block, size - 1) == nullptr);
  REQUIRE(pxl_create_in(&desc, nullptr, size) == nullptr);

  // The block is free again after pxl_destroy().
  for (int round = 0; round < 2; ++round) {
    pxl_context* pxl = pxl_create_in(&desc, block, size);
    REQUIRE(pxl != nullptr);
    pxl_begin_frame(pxl);
    pxl_clear(pxl, pxl_black);
    pxl_draw_rect(pxl, 0, 0, 2, 2, red);
    pxl_color pixels[64];
    bool ok         = pxl_end_frame(pxl) &&
                      pxl_read_texture(pxl, pxl_get_canvas(pxl), pixels) &&
                      same_color(pixels[0], red, 0) &&
                      same_color(pixels[63], pxl_black, 0);
    pxl_stats stats = pxl_get_stats(pxl);
    ok = ok && stats.commands == 2 && stats.vertices == 4 && stats.indices == 6;
    pxl_destroy(pxl);
    REQUIRE(ok);
  }
  for (size_t i = 0; i < canary_size; ++i) {
    REQUIRE(block[size + i] == canary);
  }
  return true;
}

/** Draws too much for the limits of limited_desc(), as the test lowered. */
typedef void overdraw_fn(pxl_context* pxl);

/**
 * Checks that a frame that goes over a limit fails, and that the next frame
 * works.
 */
static bool check_limit(const pxl_desc* desc, overdraw_fn* overdraw) {
  size_t size      = pxl_memory_size(desc);
  pxl_context* pxl = pxl_create_in(desc, block, size);
  if (!pxl) {
    return false;
  }
  pxl_begin_frame(pxl);
  overdraw(pxl);
  bool failed = !pxl_end_frame(pxl);
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  pxl_draw_rect(pxl, 0, 0, 1, 1, red);
  bool recovers = pxl_end_frame(pxl);
  pxl_destroy(pxl);
  return failed && recovers;
}

static void draw_three_rects(pxl_context* pxl) {
  for (int i = 0; i < 3; ++i) {
    pxl_draw_rect(pxl, (float)i, 0, 1, 1, red);
  }
}

static void clear_three_times(pxl_context* pxl) {
  for (int i = 0; i < 3; ++i) {
    pxl_clear(pxl, pxl_black);
  }
}

static void set_too_many_uniforms(pxl_context* pxl) {
  static const unsigned char data[64] = {};
  (void)pxl_set_uniforms(pxl, data, 16);
  (void)pxl_set_uniforms(pxl, data, sizeof data);
}

static void draw_long_text(pxl_context* pxl) {
  pxl_draw_text(pxl, 0, 0, red, "%s", "123456789");
}

static void draw_with_two_blends(pxl_context* pxl) {
  pxl_draw_rect(pxl, 0, 0, 1, 1, red);
  pxl_set_blend(pxl, PXL_BLEND_ADD);
  pxl_draw_rect(pxl, 1, 0, 1, 1, red);
}

TEST_CASE(test_limits) {
  pxl_desc desc     = limited_desc();
  desc.max_vertices = 8;
  REQUIRE(check_limit(&desc, draw_three_rects));

  desc             = limited_desc();
  desc.max_indices = 12;
  REQUIRE(check_limit(&desc, draw_three_rects));

  desc              = limited_desc();
  desc.max_commands = 2;
  REQUIRE(check_limit(&desc, clear_three_times));

  desc                   = limited_desc();
  desc.max_uniform_bytes = 32;
  REQUIRE(check_limit(&desc, set_too_many_uniforms));

  desc               = limited_desc();
  desc.max_pipelines = 1;
  REQUIRE(check_limit(&desc, draw_with_two_blends));

  desc          = limited_desc();
  desc.max_text = 8;
  REQUIRE(check_limit(&desc, draw_long_text));
  return true;
}

TEST_CASE(test_text_limit) {
  pxl_desc desc    = limited_desc();
  desc.max_text    = 8;
  pxl_context* pxl = pxl_create_in(&desc, block, pxl_memory_size(&desc));
  REQUIRE(pxl != nullptr);
  pxl_vec2 fits = pxl_measure_text(pxl, "%s", "12345678");
  SDL_ClearError();
  pxl_vec2 too_big = pxl_measure_text(pxl, "%s", "123456789");
  bool names_limit = strstr(SDL_GetError(), "max_text") != nullptr;
  pxl_begin_frame(pxl);
  pxl_draw_text(pxl, 0, 0, red, "%s", "12345678");
  bool draws = pxl_end_frame(pxl);
  pxl_destroy(pxl);
  REQUIRE(fits.x > 0.0f && fits.y > 0.0f);
  REQUIRE(too_big.x == 0.0f && too_big.y == 0.0f);
  REQUIRE(names_limit);
  REQUIRE(draws);
  return true;
}

TEST_CASE(test_present_with_full_geometry) {
  pxl_desc desc     = limited_desc();
  desc.max_vertices = 8;
  desc.max_indices  = 12;
  size_t size       = pxl_memory_size(&desc);
  pxl_context* pxl  = pxl_create_in(&desc, block, size);
  REQUIRE(pxl != nullptr);
  pxl_texture* screen = pxl_create_texture(
      pxl,
      &(pxl_texture_desc){.width = 16, .height = 16, .render_target = true});
  pxl_color pixels[16 * 16];
  bool ok = screen != nullptr;
  if (ok) {
    pxl_begin_frame(pxl);
    pxl_clear(pxl, pxl_black);
    pxl_draw_rect(pxl, 0, 0, 2, 2, red);
    pxl_draw_rect(pxl, 2, 2, 2, 2, red);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
    ok                        = cmd != nullptr;
    if (ok) {
      ok = pxl_end_frame_into(pxl, cmd, pxl_texture_handle(screen),
                              SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 16, 16);
      ok = SDL_SubmitGPUCommandBuffer(cmd) && ok;
    }
    ok = ok && pxl_read_texture(pxl, screen, pixels) &&
         same_color(pixels[0], red, 0) &&
         same_color(pixels[(15 * 16) + 15], pxl_black, 0);
  }
  pxl_destroy_texture(pxl, screen);
  pxl_destroy(pxl);
  REQUIRE(ok);
  return true;
}

TEST_CASE(test_pools) {
  pxl_desc desc     = limited_desc();
  desc.max_textures = 2;
  desc.max_shaders  = 1;
  desc.max_fonts    = 1;
  pxl_context* pxl  = pxl_create_in(&desc, block, pxl_memory_size(&desc));
  REQUIRE(pxl != nullptr);

  const pxl_color pixels[]       = {red, red, red, red, red, red, red, red};
  const pxl_texture_desc texture = {.width = 4, .height = 2, .pixels = pixels};
  pxl_texture* a                 = pxl_create_texture(pxl, &texture);
  pxl_texture* b                 = pxl_create_texture(pxl, &texture);
  bool ok                        = a && b && !pxl_create_texture(pxl, &texture);

  // A slot is free again after pxl_destroy_texture(). In a frame, the slot
  // is free after the frame.
  pxl_destroy_texture(pxl, a);
  a  = pxl_create_texture(pxl, &texture);
  ok = ok && a;
  pxl_begin_frame(pxl);
  pxl_destroy_texture(pxl, a);
  ok = ok && !pxl_create_texture(pxl, &texture);
  ok = pxl_end_frame(pxl) && ok;
  a  = pxl_create_texture(pxl, &texture);
  ok = ok && a;

  pxl_shader* shader = pxl_create_shader(pxl, &slots_frag);
  ok                 = ok && shader && !pxl_create_shader(pxl, &slots_frag);
  pxl_destroy_shader(pxl, shader);
  shader = pxl_create_shader(pxl, &slots_frag);
  ok     = ok && shader;

  const pxl_font_desc font_desc = {
      .texture      = b,
      .glyph_width  = 2,
      .glyph_height = 2,
  };
  pxl_font* font = pxl_create_font(pxl, &font_desc);
  ok             = ok && font && !pxl_create_font(pxl, &font_desc);
  pxl_destroy_font(pxl, font);
  font = pxl_create_font(pxl, &font_desc);
  ok   = ok && font;

  pxl_destroy_font(pxl, font);
  pxl_destroy_shader(pxl, shader);
  pxl_destroy_texture(pxl, a);
  pxl_destroy_texture(pxl, b);
  pxl_destroy(pxl);
  REQUIRE(ok);
  return true;
}

TEST_CASE(test_memory_size) {
  pxl_desc desc = {};
  size_t normal = pxl_memory_size(&desc);
  REQUIRE(normal > 0);
  desc.max_vertices = 128;
  REQUIRE(pxl_memory_size(&desc) < normal);
  desc.max_vertices = SIZE_MAX / 2;
  REQUIRE(pxl_memory_size(&desc) == 0);
  desc = (pxl_desc){.max_uniform_bytes = (size_t)UINT32_MAX + 1};
  REQUIRE(pxl_memory_size(&desc) == 0);
  desc = (pxl_desc){.max_commands = 2};
  REQUIRE(pxl_memory_size(&desc) < normal);
  return true;
}

// ---------------------------------------------------------------------------
// Main

static TEST_SUITE(suite_cpu) {
  RUN_TEST_CASE(test_colors);
  RUN_TEST_CASE(test_transforms);
  RUN_TEST_CASE(test_fit);
  RUN_TEST_CASE(test_circle_spans);
  RUN_TEST_CASE(test_memory_size);
}

static TEST_SUITE(suite_gpu) {
  RUN_TEST_CASE(test_clear);
  RUN_TEST_CASE(test_coordinates);
  RUN_TEST_CASE(test_sprite_orientation);
  RUN_TEST_CASE(test_sprite_options);
  RUN_TEST_CASE(test_snap);
  RUN_TEST_CASE(test_lines);
  RUN_TEST_CASE(test_rect_lines);
  RUN_TEST_CASE(test_circles);
  RUN_TEST_CASE(test_triangles);
  RUN_TEST_CASE(test_blend);
  RUN_TEST_CASE(test_push_pop);
  RUN_TEST_CASE(test_clip);
  RUN_TEST_CASE(test_render_target);
  RUN_TEST_CASE(test_nine_slice);
  RUN_TEST_CASE(test_update_texture);
  RUN_TEST_CASE(test_batching);
  RUN_TEST_CASE(test_destroy_during_frame);
  RUN_TEST_CASE(test_measure_text);
  RUN_TEST_CASE(test_draw_text);
  RUN_TEST_CASE(test_custom_font);
  RUN_TEST_CASE(test_custom_shader);
  RUN_TEST_CASE(test_present);
  RUN_TEST_CASE(test_load_texture);
  RUN_TEST_CASE(test_create_errors);
  RUN_TEST_CASE(test_resource_errors);
  RUN_TEST_CASE(test_create_in);
  RUN_TEST_CASE(test_limits);
  RUN_TEST_CASE(test_text_limit);
  RUN_TEST_CASE(test_present_with_full_geometry);
  RUN_TEST_CASE(test_pools);
}

/**
 * Reports that the GPU tests cannot run, because the SDL function `failed`
 * failed. Returns false if PXL_TEST_REQUIRE_GPU is set: then they must run.
 */
static bool skip_gpu_suite(const char* failed) {
  if (getenv("PXL_TEST_REQUIRE_GPU") != nullptr) {
    fprintf(stderr, "%s: %s: PXL_TEST_REQUIRE_GPU is set\n", failed,
            SDL_GetError());
    return false;
  }
  printf("%s: %s: GPU tests skipped\n", failed, SDL_GetError());
  return true;
}

/** Runs the GPU tests. Returns false if pxl fails on a GPU that works. */
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
  printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(gpu.device));
  gpu.pxl       = pxl_create(&(pxl_desc){
      .device = gpu.device,
      .width  = canvas_size,
      .height = canvas_size,
  });
  void* storage = malloc(block_size + alignof(max_align_t));
  if (!storage) {
    fprintf(stderr, "no memory for the test block\n");
    pxl_destroy(gpu.pxl);
    SDL_DestroyGPUDevice(gpu.device);
    SDL_Quit();
    return false;
  }
  size_t padding = (size_t)(-(uintptr_t)storage) & (alignof(max_align_t) - 1);
  block          = (unsigned char*)storage + padding;
  if (gpu.pxl) {
    pu_setup(nullptr, destroy_owned);
    RUN_TEST_SUITE(suite_gpu);
    pu_clear_setup();
  } else {
    fprintf(stderr, "pxl_create: %s\n", SDL_GetError());
  }
  free(storage);
  bool created = gpu.pxl != nullptr;
  pxl_destroy(gpu.pxl);
  SDL_DestroyGPUDevice(gpu.device);
  SDL_Quit();
  return created;
}

int main() {
  RUN_TEST_SUITE(suite_cpu);
  bool ok = run_gpu_suite();
  pu_print_stats();
  return ok && !pu_test_failed() ? 0 : 1;
}
