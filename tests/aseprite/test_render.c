// Tests for aseprite_render_frame(). Usage: test_render FIXTURE_DIR
//
// fixtures.rb writes the fixture files. `rake compare` checks that
// Aseprite renders them with the same pixels as the hashes here.

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"

#define PICO_UNIT_IMPLEMENTATION
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/aseprite";

static constexpr aseprite_color red   = {255, 0, 0, 255};
static constexpr aseprite_color green = {0, 255, 0, 255};
static constexpr aseprite_color blue  = {0, 0, 255, 255};
static constexpr aseprite_color clear = {};

// Tells if two colors are the same.
static bool same(aseprite_color a, aseprite_color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/* ---- Fixtures ---------------------------------------------------------- */

// Renders all the frames of a fixture and hashes the pixels with FNV-1a.
// Transparent pixels count as 0: Aseprite does not keep their color.
static uint64_t hash_fixture(const char* name) {
  char path[1024];
  int length =
      snprintf(path, sizeof path, "%s/render/%s.aseprite", fixture_dir, name);
  if (length < 0 || (size_t)length >= sizeof path) {
    return 0;
  }
  aseprite_sprite sprite;
  if (aseprite_load_file(path, &sprite) != ASEPRITE_OK) {
    fprintf(stderr, "cannot load %s\n", path);
    return 0;
  }
  size_t count           = (size_t)sprite.width * sprite.height;
  aseprite_color* pixels = malloc(count * sizeof *pixels);
  uint64_t hash          = 14695981039346656037U;
  for (uint32_t frame = 0; pixels && frame < sprite.frame_count; frame++) {
    if (aseprite_render_frame(&sprite, frame, nullptr, pixels) != ASEPRITE_OK) {
      hash = 0;
      break;
    }
    for (size_t i = 0; i < count; i++) {
      aseprite_color color   = pixels[i].a == 0 ? clear : pixels[i];
      const uint8_t bytes[4] = {color.r, color.g, color.b, color.a};
      for (size_t k = 0; k < 4; k++) {
        hash = (hash ^ bytes[k]) * 1099511628211U;
      }
    }
  }
  free(pixels);
  aseprite_free(&sprite);
  return hash;
}

TEST_CASE(test_fixtures_match_aseprite) {
  static const struct {
    const char* name;
    uint64_t hash;
  } fixtures[] = {
      {"blend_normal", 0xe9c2fdf4d2d2c50d},
      {"blend_multiply", 0x2fb6867327757284},
      {"blend_screen", 0x3c7922e4e2d6ef50},
      {"blend_overlay", 0x3537a696ee029b52},
      {"blend_darken", 0xee3d822d87d965f6},
      {"blend_lighten", 0x3c50537da3efd460},
      {"blend_color_dodge", 0xb9df7a7118c9a7b7},
      {"blend_color_burn", 0xba9c7d4283b4a2a8},
      {"blend_hard_light", 0xa69cc2b21bddf7c0},
      {"blend_soft_light", 0x68a5f9b00edf4950},
      {"blend_difference", 0x02937cc0c7fbd7f9},
      {"blend_exclusion", 0xe9828fc58a3ae99d},
      {"blend_hue", 0xbc9de422aef0cd44},
      {"blend_saturation", 0xe61f8ba79505b7a2},
      {"blend_color", 0xf9540d6db1eafd2b},
      {"blend_luminosity", 0xbf425cf8db6386f1},
      {"blend_addition", 0x50f7930d857e3d92},
      {"blend_subtract", 0x96c4b094c2bfaa48},
      {"blend_divide", 0x4e8312557920c74a},
      {"order", 0xdaea37c262f63cbb},
      {"tiles", 0x3ed218e702026902},
      {"indexed", 0x8362b8d1370c9fd5},
      {"grayscale", 0x4a38e3ebdcc9efc5},
  };
  bool ok = true;
  for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; i++) {
    uint64_t hash = hash_fixture(fixtures[i].name);
    if (hash != fixtures[i].hash) {
      fprintf(stderr, "%s: hash 0x%016" PRIx64 "\n", fixtures[i].name, hash);
      ok = false;
    }
  }
  REQUIRE(ok);
  return true;
}

/* ---- Sprites in memory ------------------------------------------------- */

static constexpr size_t max_layers = 72;

// A sprite in memory with one frame. Each cel has one pixel of its own.
typedef struct test_sprite {
  aseprite_sprite sprite;
  aseprite_frame frame;
  aseprite_palette palette;
  aseprite_layer layers[max_layers];
  aseprite_cel cels[max_layers];
  uint8_t pixels[max_layers][4];
} test_sprite;

// Makes an empty RGBA sprite of 1 x 1 pixels.
static void make_sprite(test_sprite* t, uint32_t flags) {
  *t                      = (test_sprite){};
  t->sprite.width         = 1;
  t->sprite.height        = 1;
  t->sprite.depth         = ASEPRITE_DEPTH_RGBA;
  t->sprite.flags         = flags;
  t->sprite.frame_count   = 1;
  t->sprite.frames        = &t->frame;
  t->sprite.layers        = t->layers;
  t->sprite.palette_count = 1;
  t->sprite.palettes      = &t->palette;
  t->frame.cels           = t->cels;
}

// Adds a visible layer. Returns its index.
static uint32_t add_layer(test_sprite* t, aseprite_layer_type type,
                          int32_t parent) {
  uint32_t index   = t->sprite.layer_count++;
  t->layers[index] = (aseprite_layer){
      .flags       = ASEPRITE_LAYER_FLAG_VISIBLE,
      .type        = type,
      .child_level = parent < 0 ? 0 : t->layers[parent].child_level + 1,
      .parent      = parent,
      .opacity     = 255,
  };
  return index;
}

// Adds a cel of one pixel to a layer. Returns the cel.
static aseprite_cel* add_cel(test_sprite* t, uint32_t layer,
                             aseprite_color color) {
  uint32_t index = t->frame.cel_count++;
  memcpy(t->pixels[index], &color, sizeof color);
  t->cels[index] = (aseprite_cel){
      .layer   = layer,
      .opacity = 255,
      .image   = {.width = 1, .height = 1, .pixels = t->pixels[index]},
  };
  return &t->cels[index];
}

// Renders the pixel of a sprite.
static aseprite_color render(const test_sprite* t, const bool* layers) {
  aseprite_color pixel = {1, 2, 3, 4};
  if (aseprite_render_frame(&t->sprite, 0, layers, &pixel) != ASEPRITE_OK) {
    return (aseprite_color){1, 2, 3, 4};
  }
  return pixel;
}

TEST_CASE(test_normal_blend) {
  test_sprite t;
  make_sprite(&t, ASEPRITE_SPRITE_FLAG_LAYER_OPACITY);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1),
          (aseprite_color){200, 100, 50, 128});
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1),
          (aseprite_color){10, 20, 30, 100});
  // Ra = Sa + Ba - Ba * Sa, Rc = Bc + (Sc - Bc) * Sa / Ra.
  REQUIRE(same(render(&t, nullptr), (aseprite_color){94, 56, 39, 178}));

  // Opacity is cel opacity times layer opacity.
  t.cels[1].opacity   = 128;
  t.layers[1].opacity = 128;
  REQUIRE(same(render(&t, nullptr), (aseprite_color){167, 86, 47, 140}));
  return true;
}

TEST_CASE(test_layer_selection) {
  test_sprite t;
  make_sprite(&t, 0);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1), red);
  uint32_t hidden = add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1);
  add_cel(&t, hidden, green);
  t.layers[hidden].flags = 0;
  REQUIRE(same(render(&t, nullptr), red));
  REQUIRE(same(render(&t, (const bool[]){false, true}), green));
  REQUIRE(same(render(&t, (const bool[]){false, false}), clear));

  t.layers[hidden].flags = ASEPRITE_LAYER_FLAG_REFERENCE;
  REQUIRE(same(render(&t, (const bool[]){false, true}), clear));
  return true;
}

TEST_CASE(test_hidden_group) {
  test_sprite t;
  make_sprite(&t, 0);
  uint32_t group = add_layer(&t, ASEPRITE_LAYER_TYPE_GROUP, -1);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, (int32_t)group), red);
  t.layers[group].flags = 0;
  REQUIRE(!aseprite_layer_visible(&t.sprite, 1));
  REQUIRE(same(render(&t, nullptr), clear));
  REQUIRE(same(render(&t, (const bool[]){false, true}), red));

  t.sprite.flags = ASEPRITE_SPRITE_FLAG_GROUP_BLEND;
  REQUIRE(same(render(&t, nullptr), clear));
  REQUIRE(same(render(&t, (const bool[]){false, true}), red));
  return true;
}

TEST_CASE(test_group_composition) {
  test_sprite t;
  make_sprite(&t, ASEPRITE_SPRITE_FLAG_LAYER_OPACITY |
                      ASEPRITE_SPRITE_FLAG_GROUP_BLEND);
  uint32_t group = add_layer(&t, ASEPRITE_LAYER_TYPE_GROUP, -1);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, (int32_t)group), red);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, (int32_t)group), blue);
  t.layers[group].opacity = 128;
  // The group blends its layers first, then blends blue with opacity 128.
  REQUIRE(same(render(&t, nullptr), (aseprite_color){0, 0, 255, 128}));

  // Without composition, a group has no opacity.
  t.sprite.flags = ASEPRITE_SPRITE_FLAG_LAYER_OPACITY;
  REQUIRE(same(render(&t, nullptr), blue));
  return true;
}

TEST_CASE(test_group_depth) {
  test_sprite t;
  make_sprite(&t, ASEPRITE_SPRITE_FLAG_GROUP_BLEND);
  int32_t parent = -1;
  for (int i = 0; i < 64; i++) {
    parent = (int32_t)add_layer(&t, ASEPRITE_LAYER_TYPE_GROUP, parent);
  }
  uint32_t image = add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, parent);
  add_cel(&t, image, red);
  aseprite_color pixel = {};
  REQUIRE(aseprite_render_frame(&t.sprite, 0, nullptr, &pixel) == ASEPRITE_OK);
  REQUIRE(same(pixel, red));

  // One more group is too deep.
  make_sprite(&t, ASEPRITE_SPRITE_FLAG_GROUP_BLEND);
  parent = -1;
  for (int i = 0; i < 65; i++) {
    parent = (int32_t)add_layer(&t, ASEPRITE_LAYER_TYPE_GROUP, parent);
  }
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, parent), red);
  REQUIRE(aseprite_render_frame(&t.sprite, 0, nullptr, &pixel) ==
          ASEPRITE_ERROR_MALFORMED);

  // Without composition, the depth does not matter.
  t.sprite.flags = 0;
  REQUIRE(aseprite_render_frame(&t.sprite, 0, nullptr, &pixel) == ASEPRITE_OK);
  REQUIRE(same(pixel, red));
  return true;
}

TEST_CASE(test_z_index) {
  test_sprite t;
  make_sprite(&t, 0);
  aseprite_cel* bottom =
      add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1), red);
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1), green);
  aseprite_cel* top =
      add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1), blue);
  REQUIRE(same(render(&t, nullptr), blue));

  // Order 0 + 2 is the order of the top layer. The higher z-index wins.
  bottom->z_index = 2;
  REQUIRE(same(render(&t, nullptr), red));

  // Order 2 - 2 is the order of the bottom layer. The lower z-index loses.
  bottom->z_index = 0;
  top->z_index    = -2;
  REQUIRE(same(render(&t, nullptr), green));
  return true;
}

TEST_CASE(test_background_first) {
  test_sprite t;
  make_sprite(&t, 0);
  uint32_t background = add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1);
  t.layers[background].flags |= ASEPRITE_LAYER_FLAG_BACKGROUND;
  add_cel(&t, background, red)->z_index = 5;
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1),
          (aseprite_color){0, 0, 255, 128});
  REQUIRE(same(render(&t, nullptr), (aseprite_color){127, 0, 128, 255}));
  return true;
}

TEST_CASE(test_indexed) {
  test_sprite t;
  make_sprite(&t, 0);
  static aseprite_color colors[] = {
      {10, 20, 30, 255},
      {40, 50, 60, 255},
      {70, 80, 90, 128},
  };
  t.sprite.depth             = ASEPRITE_DEPTH_INDEXED;
  t.sprite.transparent_index = 1;
  t.palette                  = (aseprite_palette){.count = 3, .colors = colors};
  uint32_t layer             = add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1);
  add_cel(&t, layer, (aseprite_color){.r = 2});
  REQUIRE(same(render(&t, nullptr), colors[2]));

  // Aseprite does not draw the transparent index.
  t.pixels[0][0] = 1;
  REQUIRE(same(render(&t, nullptr), clear));

  // An index outside the palette has no color.
  t.pixels[0][0] = 9;
  REQUIRE(same(render(&t, nullptr), clear));

  // The background layer shows the color of the transparent index.
  t.layers[layer].flags |= ASEPRITE_LAYER_FLAG_BACKGROUND;
  t.pixels[0][0] = 1;
  REQUIRE(same(render(&t, nullptr), colors[1]));
  REQUIRE(same(render(&t, (const bool[]){false}), clear));
  return true;
}

TEST_CASE(test_grayscale) {
  test_sprite t;
  make_sprite(&t, 0);
  t.sprite.depth = ASEPRITE_DEPTH_GRAYSCALE;
  add_cel(&t, add_layer(&t, ASEPRITE_LAYER_TYPE_IMAGE, -1),
          (aseprite_color){.r = 90, .g = 200});
  REQUIRE(same(render(&t, nullptr), (aseprite_color){90, 90, 90, 200}));
  return true;
}

// A tilemap of one 2 x 3 tile on a 3 x 3 sprite. Tile 1 has the pixels
// 1 to 6, row by row. The red channel of each pixel tells the pixel.
typedef struct test_tilemap {
  test_sprite t;
  aseprite_tileset tileset;
  uint8_t tiles[2 * 6 * 4];
  uint32_t tile;
} test_tilemap;

static void make_tilemap(test_tilemap* m, uint32_t tile) {
  make_sprite(&m->t, 0);
  m->t.sprite.width  = 3;
  m->t.sprite.height = 3;
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t* pixel = &m->tiles[(size_t)(6 + i) * 4];
    pixel[0]       = (uint8_t)(i + 1);
    pixel[3]       = 255;
  }
  m->tileset = (aseprite_tileset){
      .tile_count  = 2,
      .tile_width  = 2,
      .tile_height = 3,
      .image       = {.width = 2, .height = 6, .pixels = m->tiles},
  };
  m->t.sprite.tileset_count = 1;
  m->t.sprite.tilesets      = &m->tileset;
  m->tile                   = tile;
  add_layer(&m->t, ASEPRITE_LAYER_TYPE_TILEMAP, -1);
  m->t.cels[0] = (aseprite_cel){
      .opacity = 255,
      .tilemap =
          {
              .width              = 1,
              .height             = 1,
              .bits_per_tile      = 32,
              .id_mask            = 0x1fffffff,
              .x_flip_mask        = 0x20000000,
              .y_flip_mask        = 0x40000000,
              .diagonal_flip_mask = 0x80000000,
              .tiles              = &m->tile,
          },
  };
  m->t.frame.cel_count = 1;
}

// Renders a tilemap and writes the red channel of each pixel, row by row.
static bool render_tiles(const test_tilemap* m, const char* expected) {
  aseprite_color pixels[9];
  if (aseprite_render_frame(&m->t.sprite, 0, nullptr, pixels) != ASEPRITE_OK) {
    return false;
  }
  char actual[10] = {};
  for (size_t i = 0; i < 9; i++) {
    actual[i] = pixels[i].a ? (char)('0' + pixels[i].r) : '.';
  }
  if (strcmp(actual, expected) != 0) {
    fprintf(stderr, "tiles: %s, expected %s\n", actual, expected);
    return false;
  }
  return true;
}

TEST_CASE(test_tile_flips) {
  test_tilemap m;
  make_tilemap(&m, 1);
  REQUIRE(render_tiles(&m, "12.34.56."));
  make_tilemap(&m, 1 | 0x20000000);
  REQUIRE(render_tiles(&m, "21.43.65."));
  make_tilemap(&m, 1 | 0x40000000);
  REQUIRE(render_tiles(&m, "56.34.12."));
  make_tilemap(&m, 1 | 0x60000000);
  REQUIRE(render_tiles(&m, "65.43.21."));
  // A diagonal flip of a 2 x 3 tile uses its 2 x 2 part.
  make_tilemap(&m, 1 | 0x80000000);
  REQUIRE(render_tiles(&m, "13.24...."));
  return true;
}

TEST_CASE(test_tiles_not_drawn) {
  test_tilemap m;
  make_tilemap(&m, 0);
  REQUIRE(render_tiles(&m, "........."));
  make_tilemap(&m, 2);
  REQUIRE(render_tiles(&m, "........."));
  make_tilemap(&m, 1);
  m.t.cels[0].x = -1;
  m.t.cels[0].y = 1;
  REQUIRE(render_tiles(&m, "...2..4.."));
  m.tileset.image.pixels = nullptr;
  REQUIRE(render_tiles(&m, "........."));
  return true;
}

TEST_CASE(test_empty_sprite) {
  test_sprite t;
  make_sprite(&t, 0);
  REQUIRE(same(render(&t, nullptr), clear));
  return true;
}

/* ---- Main -------------------------------------------------------------- */

// Runs all the tests.
static TEST_SUITE(suite_render) {
  RUN_TEST_CASE(test_fixtures_match_aseprite);
  RUN_TEST_CASE(test_normal_blend);
  RUN_TEST_CASE(test_layer_selection);
  RUN_TEST_CASE(test_hidden_group);
  RUN_TEST_CASE(test_group_composition);
  RUN_TEST_CASE(test_group_depth);
  RUN_TEST_CASE(test_z_index);
  RUN_TEST_CASE(test_background_first);
  RUN_TEST_CASE(test_indexed);
  RUN_TEST_CASE(test_grayscale);
  RUN_TEST_CASE(test_tile_flips);
  RUN_TEST_CASE(test_tiles_not_drawn);
  RUN_TEST_CASE(test_empty_sprite);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_render);
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
