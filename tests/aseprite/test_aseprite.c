// Tests for aseprite.h. Usage: test_aseprite FIXTURE_DIR
//
// tests/aseprite/fixtures.rb writes the fixture files.

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"
// A second include must not define the implementation again.
#include "aseprite.h" // NOLINT(readability-duplicate-include)
#include "consumer.h"
#include "walk.h"

#define PICO_UNIT_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/aseprite";

// Makes the path of a fixture file.
static void fixture_path(char* path, size_t size, const char* name,
                         const char* extension) {
  int length = snprintf(path, size, "%s/%s%s", fixture_dir, name, extension);
  if (length < 0 || (size_t)length >= size) {
    fprintf(stderr, "path is too long: %s\n", name);
    exit(1);
  }
}

// Loads a fixture file.
static aseprite_result load(const char* name, aseprite_sprite* sprite) {
  char path[1024];
  fixture_path(path, sizeof path, name, ".aseprite");
  return aseprite_load_file(path, nullptr, sprite);
}

// Reads a whole file. The caller frees the data.
static uint8_t* read_file(const char* name, const char* extension,
                          size_t* size) {
  char path[1024];
  fixture_path(path, sizeof path, name, extension);
  FILE* file = fopen(path, "rb");
  if (!file) {
    return nullptr;
  }
  uint8_t* data = nullptr;
  long length   = -1;
  if (fseek(file, 0, SEEK_END) == 0) {
    length = ftell(file);
  }
  if (length >= 0 && fseek(file, 0, SEEK_SET) == 0) {
    data = malloc((size_t)length + 1);
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
      free(data);
      data = nullptr;
    }
  }
  (void)fclose(file);
  *size = (size_t)length;
  return data;
}

// Tells if a string is not nullptr and equals the expected string.
static bool same_string(const char* actual, const char* expected) {
  return actual && strcmp(actual, expected) == 0;
}

// Tells if a color has the expected channels.
static bool same_color(aseprite_color color, uint8_t r, uint8_t g, uint8_t b,
                       uint8_t a) {
  return color.r == r && color.g == g && color.b == b && color.a == a;
}

// The pixels that fixtures.rb makes with rgba_pixels(count, seed).
static bool same_rgba_pixels(const aseprite_image* image, uint32_t seed) {
  if (!image->pixels) {
    return false;
  }
  uint32_t count = image->width * image->height;
  for (uint32_t i = 0; i < count; i++) {
    const uint8_t* pixel = image->pixels + ((size_t)i * 4);
    if (pixel[0] != ((seed * 7) + (i * 13)) % 256 ||
        pixel[1] != (i * 3) % 256 || pixel[2] != seed || pixel[3] != 255) {
      return false;
    }
  }
  return true;
}

/* ---- RGBA -------------------------------------------------------------- */

TEST_CASE(test_rgba_header) {
  aseprite_sprite sprite;
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.width == 4);
  REQUIRE(sprite.height == 3);
  REQUIRE(sprite.depth == ASEPRITE_DEPTH_RGBA);
  REQUIRE(aseprite_bytes_per_pixel(sprite.depth) == 4);
  REQUIRE(sprite.flags == ASEPRITE_SPRITE_FLAG_LAYER_OPACITY);
  REQUIRE(sprite.speed == 120);
  REQUIRE(sprite.transparent_index == 0);
  REQUIRE(sprite.color_count == 256);
  REQUIRE(sprite.pixel_width == 2);
  REQUIRE(sprite.pixel_height == 1);
  REQUIRE(sprite.grid_x == -3);
  REQUIRE(sprite.grid_y == 4);
  REQUIRE(sprite.grid_width == 8);
  REQUIRE(sprite.grid_height == 6);
  REQUIRE(sprite.color_profile.type == ASEPRITE_COLOR_PROFILE_SRGB);
  REQUIRE(sprite.user_data.flags ==
          (ASEPRITE_USER_DATA_FLAG_TEXT | ASEPRITE_USER_DATA_FLAG_COLOR));
  REQUIRE(same_string(sprite.user_data.text, "sprite"));
  REQUIRE(same_color(sprite.user_data.color, 1, 2, 3, 4));
  aseprite_free(&sprite);
  REQUIRE(sprite.frames == nullptr);
  return true;
}

TEST_CASE(test_rgba_palette) {
  aseprite_sprite sprite;
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.palette_count == 1);
  const aseprite_palette* palette = &sprite.palettes[0];
  REQUIRE(palette->count == 3);
  REQUIRE(same_color(palette->colors[0], 255, 0, 0, 255));
  REQUIRE(same_color(palette->colors[1], 0, 255, 0, 128));
  REQUIRE(same_color(palette->colors[2], 0, 0, 255, 255));
  REQUIRE(palette->names && palette->names[0] == nullptr);
  REQUIRE(palette->names && same_string(palette->names[1], "green"));
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_rgba_layers) {
  aseprite_sprite sprite;
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.layer_count == 3);

  const aseprite_layer* group = &sprite.layers[0];
  REQUIRE(same_string(group->name, "group"));
  REQUIRE(group->type == ASEPRITE_LAYER_TYPE_GROUP);
  REQUIRE(group->parent == -1);
  REQUIRE(group->blend_mode == ASEPRITE_BLEND_NORMAL);
  REQUIRE(group->opacity == 255);

  const aseprite_layer* body = &sprite.layers[1];
  REQUIRE(same_string(body->name, "body"));
  REQUIRE(body->type == ASEPRITE_LAYER_TYPE_IMAGE);
  REQUIRE(body->child_level == 1);
  REQUIRE(body->parent == 0);
  REQUIRE(body->blend_mode == ASEPRITE_BLEND_MULTIPLY);
  REQUIRE(body->opacity == 128);
  REQUIRE(same_string(body->user_data.text, "body layer"));

  const aseprite_layer* background = &sprite.layers[2];
  REQUIRE(background->parent == -1);
  REQUIRE(background->flags ==
          (ASEPRITE_LAYER_FLAG_VISIBLE | ASEPRITE_LAYER_FLAG_EDITABLE |
           ASEPRITE_LAYER_FLAG_BACKGROUND));
  REQUIRE(background->blend_mode == ASEPRITE_BLEND_NORMAL);
  REQUIRE(background->opacity == 255);
  REQUIRE(background->user_data.text == nullptr);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_rgba_cels) {
  aseprite_sprite sprite;
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.frame_count == 3);
  REQUIRE(sprite.frames[0].duration == 50);
  REQUIRE(sprite.frames[1].duration == 120);
  REQUIRE(sprite.frames[2].duration == 100);

  const aseprite_frame* frame = &sprite.frames[0];
  REQUIRE(frame->cel_count == 2);
  REQUIRE(aseprite_frame_cel(frame, 0) == nullptr);
  const aseprite_cel* body = aseprite_frame_cel(frame, 1);
  REQUIRE(body == &frame->cels[0]);
  REQUIRE(body->type == ASEPRITE_CEL_TYPE_COMPRESSED_IMAGE);
  REQUIRE(body->x == 1);
  REQUIRE(body->y == -1);
  REQUIRE(body->opacity == 200);
  REQUIRE(body->z_index == -2);
  REQUIRE(body->image.width == 2);
  REQUIRE(body->image.height == 2);
  REQUIRE(same_rgba_pixels(&body->image, 1));
  REQUIRE(body->tilemap.tiles == nullptr);
  REQUIRE(same_string(body->user_data.text, "body cel"));
  REQUIRE(body->has_precise_bounds);
  REQUIRE(aseprite_fixed_to_double(body->precise_x) == 1.5);
  REQUIRE(aseprite_fixed_to_double(body->precise_y) == 0.5);
  REQUIRE(aseprite_fixed_to_double(body->precise_width) == 2.0);
  REQUIRE(aseprite_fixed_to_double(body->precise_height) == 2.0);

  const aseprite_cel* raw = aseprite_frame_cel(frame, 2);
  REQUIRE(raw->type == ASEPRITE_CEL_TYPE_RAW);
  REQUIRE(raw->image.width == 4);
  REQUIRE(raw->image.height == 3);
  REQUIRE(same_rgba_pixels(&raw->image, 2));
  REQUIRE(!raw->has_precise_bounds);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_rgba_linked_and_compressed_cels) {
  aseprite_sprite sprite;
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  const aseprite_cel* source = aseprite_frame_cel(&sprite.frames[0], 1);

  const aseprite_cel* linked = aseprite_frame_cel(&sprite.frames[1], 1);
  REQUIRE(linked->type == ASEPRITE_CEL_TYPE_LINKED);
  REQUIRE(linked->linked_frame == 0);
  REQUIRE(linked->x == 3);
  REQUIRE(linked->y == 2);
  REQUIRE(linked->image.pixels == source->image.pixels);
  REQUIRE(linked->image.width == 2);

  REQUIRE(linked->has_precise_bounds);
  REQUIRE(aseprite_fixed_to_double(linked->precise_x) == 3.0);
  REQUIRE(aseprite_fixed_to_double(source->precise_x) == 1.5);

  const aseprite_cel* stored = aseprite_frame_cel(&sprite.frames[1], 2);
  REQUIRE(same_rgba_pixels(&stored->image, 3));
  const aseprite_cel* fixed = aseprite_frame_cel(&sprite.frames[2], 1);
  REQUIRE(fixed->image.width == 3);
  REQUIRE(same_rgba_pixels(&fixed->image, 4));
  const aseprite_cel* huffman = aseprite_frame_cel(&sprite.frames[2], 2);
  REQUIRE(same_rgba_pixels(&huffman->image, 5));
  aseprite_free(&sprite);
  return true;
}

/* ---- Other color depths ------------------------------------------------ */

TEST_CASE(test_grayscale) {
  aseprite_sprite sprite;
  REQUIRE(load("grayscale", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.depth == ASEPRITE_DEPTH_GRAYSCALE);
  REQUIRE(aseprite_bytes_per_pixel(sprite.depth) == 2);
  REQUIRE(sprite.palettes[0].count == 4);
  const aseprite_cel* cel = &sprite.frames[0].cels[0];
  REQUIRE(cel->image.width == 2);
  REQUIRE(cel->image.height == 1);
  REQUIRE(memcmp(cel->image.pixels, (uint8_t[]){10, 255, 20, 128}, 4) == 0);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_indexed) {
  aseprite_sprite sprite;
  REQUIRE(load("indexed", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.depth == ASEPRITE_DEPTH_INDEXED);
  REQUIRE(aseprite_bytes_per_pixel(sprite.depth) == 1);
  REQUIRE(sprite.transparent_index == 5);
  REQUIRE(sprite.color_count == 2);
  const aseprite_cel* cel = &sprite.frames[0].cels[0];
  REQUIRE(memcmp(cel->image.pixels, (uint8_t[]){0, 1, 3, 5}, 4) == 0);

  REQUIRE(sprite.palette_count == 3);
  REQUIRE(sprite.frames[0].palette == 0);
  REQUIRE(sprite.frames[1].palette == 1);
  REQUIRE(sprite.frames[2].palette == 1);
  REQUIRE(sprite.frames[3].palette == 2);

  const aseprite_palette* first = &sprite.palettes[0];
  REQUIRE(first->count == 4);
  REQUIRE(same_color(first->colors[0], 1, 2, 3, 255));
  REQUIRE(same_color(first->colors[1], 4, 5, 6, 255));
  REQUIRE(same_color(first->colors[2], 0, 0, 0, 255));
  REQUIRE(same_color(first->colors[3], 7, 8, 9, 255));
  REQUIRE(first->names == nullptr);
  REQUIRE(same_string(sprite.user_data.text, "indexed sprite"));

  const aseprite_palette* second = &sprite.palettes[1];
  REQUIRE(second->count == 4);
  REQUIRE(same_color(second->colors[0], 255, 130, 0, 255));
  REQUIRE(same_color(second->colors[3], 7, 8, 9, 255));

  const aseprite_palette* third = &sprite.palettes[2];
  REQUIRE(third->count == 6);
  REQUIRE(same_color(third->colors[0], 255, 130, 0, 255));
  REQUIRE(same_color(third->colors[4], 0, 0, 0, 255));
  REQUIRE(same_color(third->colors[5], 10, 20, 30, 40));
  REQUIRE(third->names && third->names[0] == nullptr);
  REQUIRE(third->names && same_string(third->names[5], "last"));
  aseprite_free(&sprite);
  return true;
}

/* ---- Tilemaps ---------------------------------------------------------- */

TEST_CASE(test_tilesets) {
  aseprite_sprite sprite;
  REQUIRE(load("tilemap", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.external_file_count == 2);
  REQUIRE(sprite.external_files[0].id == 7);
  REQUIRE(sprite.external_files[0].type == ASEPRITE_EXTERNAL_FILE_TILESET);
  REQUIRE(same_string(sprite.external_files[0].name, "tiles.aseprite"));
  REQUIRE(sprite.external_files[1].type ==
          ASEPRITE_EXTERNAL_FILE_PROPERTIES_EXTENSION);
  REQUIRE(same_string(sprite.external_files[1].name, "pub/ext"));

  REQUIRE(sprite.tileset_count == 2);
  const aseprite_tileset* ground = &sprite.tilesets[0];
  REQUIRE(ground->id == 4);
  REQUIRE(ground->flags == (ASEPRITE_TILESET_FLAG_EMBEDDED |
                            ASEPRITE_TILESET_FLAG_ZERO_IS_EMPTY));
  REQUIRE(ground->tile_count == 3);
  REQUIRE(ground->tile_width == 2);
  REQUIRE(ground->tile_height == 2);
  REQUIRE(ground->base_index == 1);
  REQUIRE(same_string(ground->name, "ground"));
  REQUIRE(ground->image.width == 2);
  REQUIRE(ground->image.height == 6);
  REQUIRE(same_rgba_pixels(&ground->image, 6));
  REQUIRE(same_string(ground->user_data.text, "tileset"));
  REQUIRE(ground->tile_user_data_count == 3);
  REQUIRE(ground->tile_user_data[0].flags == 0);
  REQUIRE(ground->tile_user_data[0].text == nullptr);
  REQUIRE(same_string(ground->tile_user_data[1].text, "tile 1"));
  REQUIRE(same_string(ground->tile_user_data[2].text, "tile 2"));

  const aseprite_tileset* far = &sprite.tilesets[1];
  REQUIRE(far->id == 5);
  REQUIRE(far->flags == ASEPRITE_TILESET_FLAG_EXTERNAL);
  REQUIRE(far->tile_count == 10);
  REQUIRE(far->base_index == 0);
  REQUIRE(far->external_file == 7);
  REQUIRE(far->external_tileset == 2);
  REQUIRE(far->image.pixels == nullptr);
  REQUIRE(far->user_data.flags == 0);
  REQUIRE(far->tile_user_data_count == 0);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_tilemap_cels) {
  aseprite_sprite sprite;
  REQUIRE(load("tilemap", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.layer_count == 1);
  REQUIRE(sprite.layers[0].type == ASEPRITE_LAYER_TYPE_TILEMAP);
  REQUIRE(sprite.layers[0].tileset == 0);

  const aseprite_tilemap* wide = &sprite.frames[0].cels[0].tilemap;
  REQUIRE(sprite.frames[0].cels[0].image.pixels == nullptr);
  REQUIRE(wide->width == 2);
  REQUIRE(wide->height == 1);
  REQUIRE(wide->bits_per_tile == 32);
  REQUIRE(aseprite_tile_id(wide, wide->tiles[0]) == 1);
  REQUIRE(!aseprite_tile_x_flip(wide, wide->tiles[0]));
  REQUIRE(aseprite_tile_id(wide, wide->tiles[1]) == 2);
  REQUIRE(aseprite_tile_x_flip(wide, wide->tiles[1]));
  REQUIRE(!aseprite_tile_y_flip(wide, wide->tiles[1]));

  const aseprite_tilemap* tall = &sprite.frames[1].cels[0].tilemap;
  REQUIRE(tall->bits_per_tile == 16);
  REQUIRE(tall->tiles[0] == 2);
  REQUIRE(tall->tiles[1] == 0x4001);
  REQUIRE(aseprite_tile_id(tall, tall->tiles[1]) == 1);
  REQUIRE(aseprite_tile_diagonal_flip(tall, tall->tiles[1]));
  REQUIRE(!aseprite_tile_x_flip(tall, tall->tiles[1]));

  const aseprite_tilemap* small = &sprite.frames[2].cels[0].tilemap;
  REQUIRE(small->bits_per_tile == 8);
  REQUIRE(small->tiles[0] == 0x83);
  REQUIRE(aseprite_tile_id(small, small->tiles[0]) == 3);
  REQUIRE(aseprite_tile_diagonal_flip(small, small->tiles[0]));
  REQUIRE(!aseprite_tile_y_flip(small, small->tiles[0]));

  const aseprite_cel* linked = &sprite.frames[3].cels[0];
  REQUIRE(linked->type == ASEPRITE_CEL_TYPE_LINKED);
  REQUIRE(linked->tilemap.tiles == wide->tiles);
  REQUIRE(linked->tilemap.width == 2);

  const aseprite_tilemap empty_masks = {};
  REQUIRE(aseprite_tile_id(&empty_masks, 0xFFFFFFFF) == 0);
  REQUIRE(!aseprite_tile_x_flip(&empty_masks, 0xFFFFFFFF));
  aseprite_free(&sprite);
  return true;
}

/* ---- Tags and slices --------------------------------------------------- */

TEST_CASE(test_tags) {
  aseprite_sprite sprite;
  REQUIRE(load("tags", &sprite) == ASEPRITE_OK);
  const aseprite_tag* walk = &sprite.tags[0];
  REQUIRE(same_string(walk->name, "walk"));
  REQUIRE(walk->from == 0);
  REQUIRE(walk->to == 1);
  REQUIRE(walk->direction == ASEPRITE_DIRECTION_FORWARD);
  REQUIRE(walk->repeat == 0);
  REQUIRE(same_color(walk->color, 1, 2, 3, 255));
  REQUIRE(walk->user_data.flags == ASEPRITE_USER_DATA_FLAG_COLOR);
  REQUIRE(same_color(walk->user_data.color, 10, 20, 30, 255));

  const aseprite_tag* run = &sprite.tags[1];
  REQUIRE(same_string(run->name, "run"));
  REQUIRE(run->direction == ASEPRITE_DIRECTION_FORWARD);
  REQUIRE(run->repeat == 3);
  REQUIRE(same_string(run->user_data.text, "run tag"));

  REQUIRE(sprite.tag_count == 3);
  const aseprite_tag* idle = &sprite.tags[2];
  REQUIRE(same_string(idle->name, "idle"));
  REQUIRE(idle->direction == ASEPRITE_DIRECTION_REVERSE);
  REQUIRE(same_string(idle->user_data.text, "idle tag"));

  REQUIRE(sprite.layers[0].user_data.text == nullptr);
  REQUIRE(sprite.user_data.text == nullptr);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_slices) {
  aseprite_sprite sprite;
  REQUIRE(load("slices", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.slice_count == 2);

  const aseprite_slice* plain = &sprite.slices[0];
  REQUIRE(same_string(plain->name, "plain"));
  REQUIRE(plain->flags == 0);
  REQUIRE(plain->key_count == 1);
  REQUIRE(plain->keys[0].frame == 0);
  REQUIRE(plain->keys[0].x == -1);
  REQUIRE(plain->keys[0].y == -2);
  REQUIRE(plain->keys[0].width == 3);
  REQUIRE(plain->keys[0].height == 4);
  REQUIRE(same_string(plain->user_data.text, "plain slice"));

  const aseprite_slice* patch = &sprite.slices[1];
  REQUIRE(patch->flags ==
          (ASEPRITE_SLICE_FLAG_NINE_PATCH | ASEPRITE_SLICE_FLAG_PIVOT));
  REQUIRE(patch->key_count == 2);
  const aseprite_slice_key* key = &patch->keys[1];
  REQUIRE(key->frame == 1);
  REQUIRE(key->x == 2);
  REQUIRE(key->y == 3);
  REQUIRE(key->width == 11);
  REQUIRE(key->height == 21);
  REQUIRE(key->center_x == 3);
  REQUIRE(key->center_y == 4);
  REQUIRE(key->center_width == 5);
  REQUIRE(key->center_height == 6);
  REQUIRE(key->pivot_x == 8);
  REQUIRE(key->pivot_y == 9);
  REQUIRE(patch->user_data.flags == 0);
  aseprite_free(&sprite);
  return true;
}

/* ---- User data properties ---------------------------------------------- */

// Finds a property in a map by its name.
static const aseprite_property* find_property(const aseprite_property_map* map,
                                              const char* name) {
  for (uint32_t i = 0; i < map->count; i++) {
    if (same_string(map->properties[i].name, name)) {
      return &map->properties[i];
    }
  }
  return nullptr;
}

TEST_CASE(test_property_numbers) {
  aseprite_sprite sprite;
  REQUIRE(load("properties", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.user_data.map_count == 2);
  const aseprite_property_map* map = &sprite.user_data.maps[0];
  REQUIRE(map->key == 0);
  REQUIRE(map->count == 20);

  REQUIRE(find_property(map, "bool")->value.boolean);
  REQUIRE(find_property(map, "int8")->value.signed_int == -8);
  REQUIRE(find_property(map, "uint8")->value.unsigned_int == 200);
  REQUIRE(find_property(map, "int16")->value.signed_int == -1600);
  REQUIRE(find_property(map, "uint16")->value.unsigned_int == 60000);
  REQUIRE(find_property(map, "int32")->value.signed_int == -320000);
  REQUIRE(find_property(map, "uint32")->value.unsigned_int == 4000000000);
  REQUIRE(find_property(map, "int64")->value.signed_int == -64000000000);
  REQUIRE(find_property(map, "uint64")->value.unsigned_int ==
          18000000000000000000U);
  REQUIRE(find_property(map, "fixed")->value.fixed == 0x18000);
  REQUIRE(find_property(map, "float")->value.float32 == 1.5F);
  REQUIRE(find_property(map, "double")->value.float64 == -2.25);
  REQUIRE(find_property(map, "int64")->type == ASEPRITE_PROPERTY_TYPE_INT64);
  REQUIRE(find_property(map, "float")->type == ASEPRITE_PROPERTY_TYPE_FLOAT);

  const aseprite_property_map* extension = &sprite.user_data.maps[1];
  REQUIRE(extension->key == 3);
  REQUIRE(extension->count == 1);
  REQUIRE(find_property(extension, "x")->value.signed_int == 1);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_property_structures) {
  aseprite_sprite sprite;
  REQUIRE(load("properties", &sprite) == ASEPRITE_OK);
  const aseprite_property_map* map = &sprite.user_data.maps[0];

  REQUIRE(same_string(find_property(map, "string")->value.string, "text"));
  const aseprite_property* point = find_property(map, "point");
  REQUIRE(point->value.point.x == 1 && point->value.point.y == -2);
  const aseprite_property* size = find_property(map, "size");
  REQUIRE(size->value.size.width == 3 && size->value.size.height == 4);
  const aseprite_property* rect = find_property(map, "rect");
  REQUIRE(rect->value.rect.x == 5 && rect->value.rect.height == 8);
  const aseprite_property* uuid = find_property(map, "uuid");
  REQUIRE(uuid->value.uuid[0] == 1 && uuid->value.uuid[15] == 16);

  const aseprite_property* ints = find_property(map, "ints");
  REQUIRE(ints->type == ASEPRITE_PROPERTY_TYPE_VECTOR);
  REQUIRE(ints->value.list.count == 3);
  REQUIRE(ints->value.list.items[0].name == nullptr);
  REQUIRE(ints->value.list.items[2].type == ASEPRITE_PROPERTY_TYPE_INT32);
  REQUIRE(ints->value.list.items[2].value.signed_int == 3);

  const aseprite_property* mixed = find_property(map, "mixed");
  REQUIRE(mixed->value.list.count == 2);
  REQUIRE(mixed->value.list.items[0].type == ASEPRITE_PROPERTY_TYPE_STRING);
  REQUIRE(same_string(mixed->value.list.items[0].value.string, "a"));
  REQUIRE(mixed->value.list.items[1].type == ASEPRITE_PROPERTY_TYPE_BOOL);
  REQUIRE(!mixed->value.list.items[1].value.boolean);

  const aseprite_property* nested = find_property(map, "nested");
  REQUIRE(nested->type == ASEPRITE_PROPERTY_TYPE_MAP);
  REQUIRE(nested->value.list.count == 1);
  REQUIRE(same_string(nested->value.list.items[0].name, "inner"));
  REQUIRE(nested->value.list.items[0].value.unsigned_int == 9);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_property_errors) {
  aseprite_sprite sprite;
  REQUIRE(load("properties", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.layer_count == 3);

  const aseprite_user_data* unknown = &sprite.layers[0].user_data;
  REQUIRE(same_string(unknown->text, "kept"));
  REQUIRE((unknown->flags & ASEPRITE_USER_DATA_FLAG_PROPERTIES) != 0);
  REQUIRE(unknown->map_count == 0);
  REQUIRE(sprite.layers[1].user_data.map_count == 0);

  REQUIRE(sprite.layers[2].user_data.map_count == 1);
  const aseprite_property* deep =
      &sprite.layers[2].user_data.maps[0].properties[0];
  int levels = 0;
  while (deep->type == ASEPRITE_PROPERTY_TYPE_VECTOR) {
    deep = &deep->value.list.items[0];
    levels++;
  }
  REQUIRE(levels == 127);
  REQUIRE(deep->type == ASEPRITE_PROPERTY_TYPE_INT8);
  REQUIRE(deep->value.signed_int == 1);
  aseprite_free(&sprite);
  return true;
}

/* ---- Other chunks ------------------------------------------------------ */

TEST_CASE(test_uuid_and_icc_profile) {
  aseprite_sprite sprite;
  REQUIRE(load("uuid", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.flags == 7);
  REQUIRE(sprite.layers[0].uuid[0] == 100);
  REQUIRE(sprite.layers[0].uuid[15] == 115);
  REQUIRE(sprite.layers[1].uuid[0] == 115);
  REQUIRE(sprite.layers[1].blend_mode == ASEPRITE_BLEND_OVERLAY);
  REQUIRE(sprite.layers[1].opacity == 77);

  const aseprite_color_profile* profile = &sprite.color_profile;
  REQUIRE(profile->type == ASEPRITE_COLOR_PROFILE_ICC);
  REQUIRE(profile->flags == ASEPRITE_COLOR_PROFILE_FLAG_FIXED_GAMMA);
  REQUIRE(aseprite_fixed_to_double(profile->gamma) == 1.0);
  REQUIRE(profile->icc_size == 7);
  REQUIRE(memcmp(profile->icc, "ICCDATA", 7) == 0);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_skipped_chunks) {
  aseprite_sprite sprite;
  REQUIRE(load("skipped", &sprite) == ASEPRITE_OK);
  REQUIRE(sprite.layers[0].user_data.text == nullptr);
  REQUIRE(sprite.frames[0].cel_count == 1);
  const aseprite_cel* cel = &sprite.frames[0].cels[0];
  REQUIRE(cel->z_index == 5);
  REQUIRE(cel->user_data.text == nullptr);
  REQUIRE(memcmp(cel->image.pixels, (uint8_t[]){1, 2, 3, 4}, 4) == 0);
  REQUIRE(sprite.frames[1].cel_count == 1);
  REQUIRE(memcmp(sprite.frames[1].cels[0].image.pixels, (uint8_t[]){5, 6, 7, 8},
                 4) == 0);
  aseprite_free(&sprite);
  return true;
}

/* ---- Errors ------------------------------------------------------------ */

TEST_CASE(test_invalid_files) {
  static const struct {
    const char* name;
    aseprite_result result;
  } cases[] = {
      {"not_aseprite", ASEPRITE_ERROR_NOT_ASEPRITE},
      {"short", ASEPRITE_ERROR_TRUNCATED},
      {"truncated_header", ASEPRITE_ERROR_TRUNCATED},
      {"truncated_frame", ASEPRITE_ERROR_TRUNCATED},
      {"bad_depth", ASEPRITE_ERROR_MALFORMED},
      {"zero_width", ASEPRITE_ERROR_MALFORMED},
      {"zero_frames", ASEPRITE_ERROR_MALFORMED},
      {"many_frames", ASEPRITE_ERROR_TRUNCATED},
      {"bad_frame_magic", ASEPRITE_ERROR_MALFORMED},
      {"small_chunk", ASEPRITE_ERROR_MALFORMED},
      {"big_chunk", ASEPRITE_ERROR_TRUNCATED},
      {"bad_checksum", ASEPRITE_ERROR_COMPRESSION},
      {"truncated_zlib", ASEPRITE_ERROR_COMPRESSION},
      {"bad_layer", ASEPRITE_ERROR_MALFORMED},
      {"link_forward", ASEPRITE_ERROR_MALFORMED},
      {"link_missing", ASEPRITE_ERROR_MALFORMED},
      {"tilemap_on_image", ASEPRITE_ERROR_MALFORMED},
      {"image_on_group", ASEPRITE_ERROR_MALFORMED},
      {"child_of_image", ASEPRITE_ERROR_MALFORMED},
      {"missing_tileset", ASEPRITE_ERROR_MALFORMED},
      {"tag_range", ASEPRITE_ERROR_MALFORMED},
      {"big_palette", ASEPRITE_ERROR_MALFORMED},
      {"palette_range", ASEPRITE_ERROR_MALFORMED},
      {"old_palette_range", ASEPRITE_ERROR_MALFORMED},
      {"bomb", ASEPRITE_ERROR_COMPRESSION},
      {"many_tags", ASEPRITE_ERROR_TRUNCATED},
      {"many_keys", ASEPRITE_ERROR_TRUNCATED},
      {"missing_file", ASEPRITE_ERROR_IO},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    aseprite_sprite sprite;
    aseprite_result result = load(cases[i].name, &sprite);
    if (result != cases[i].result) {
      fprintf(stderr, "%s: %s\n", cases[i].name,
              aseprite_result_string(result));
    }
    REQUIRE(result == cases[i].result);
    REQUIRE(sprite.frames == nullptr && sprite.memory == nullptr);
  }
  return true;
}

// Loads every prefix of a fixture. Only the whole file may load.
static bool check_prefixes(const char* name) {
  size_t size   = 0;
  uint8_t* data = read_file(name, ".aseprite", &size);
  if (!data) {
    return false;
  }
  bool ok = true;
  for (size_t length = 0; ok && length <= size; length++) {
    aseprite_sprite sprite;
    aseprite_result result =
        aseprite_load_memory(data, length, nullptr, &sprite);
    ok = (result == ASEPRITE_OK) == (length == size);
    aseprite_free(&sprite);
  }
  free(data);
  if (!ok) {
    fprintf(stderr, "prefix test failed for %s\n", name);
  }
  return ok;
}

TEST_CASE(test_every_prefix_fails) {
  static const char* const names[] = {
      "rgba", "indexed", "tilemap", "properties", "skipped",
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    REQUIRE(check_prefixes(names[i]));
  }
  return true;
}

TEST_CASE(test_header_without_implementation) {
  REQUIRE(consumer_result_string(ASEPRITE_ERROR_IO) ==
          aseprite_result_string(ASEPRITE_ERROR_IO));
  REQUIRE(consumer_bytes_per_pixel(ASEPRITE_DEPTH_RGBA) == 4);
  return true;
}

TEST_CASE(test_free_is_safe) {
  aseprite_sprite sprite = {};
  aseprite_free(&sprite);
  REQUIRE(load("rgba", &sprite) == ASEPRITE_OK);
  aseprite_free(&sprite);
  aseprite_free(&sprite);
  REQUIRE(sprite.memory == nullptr);
  return true;
}

TEST_CASE(test_result_strings) {
  for (int i = ASEPRITE_OK; i <= ASEPRITE_ERROR_COMPRESSION; i++) {
    const char* text = aseprite_result_string((aseprite_result)i);
    REQUIRE(text != nullptr);
    REQUIRE(strcmp(text, "unknown result") != 0);
  }
  REQUIRE(same_string(aseprite_result_string((aseprite_result)200),
                      "unknown result"));
  return true;
}

/* ---- Memory ------------------------------------------------------------ */

static constexpr size_t max_live_blocks = 32;
static constexpr size_t canary_size     = 64;
static constexpr unsigned char canary   = 0xA5;

// An allocator that records its blocks and can fail one allocation.
typedef struct tracking_allocator {
  size_t allocations;
  size_t fail_at; // The allocation to fail, counted from 1. 0: none.
  size_t live_count;
  struct {
    void* pointer;
    size_t size;
  } live[max_live_blocks];
  bool misuse; // A bad alignment, a bad release or a wrong size.
} tracking_allocator;

static void* tracking_alloc(void* user, size_t size, size_t alignment) {
  tracking_allocator* tracker = user;
  tracker->allocations++;
  if (tracker->allocations == tracker->fail_at) {
    return nullptr;
  }
  void* pointer = malloc(size);
  if (!pointer || tracker->live_count == max_live_blocks || alignment == 0 ||
      alignment > alignof(max_align_t) || (alignment & (alignment - 1)) != 0 ||
      (uintptr_t)pointer % alignment != 0) {
    tracker->misuse = true;
    free(pointer);
    return nullptr;
  }
  tracker->live[tracker->live_count].pointer = pointer;
  tracker->live[tracker->live_count].size    = size;
  tracker->live_count++;
  return pointer;
}

static void tracking_release(void* user, void* pointer, size_t size) {
  tracking_allocator* tracker = user;
  for (size_t i = 0; i < tracker->live_count; i++) {
    if (tracker->live[i].pointer == pointer) {
      tracker->misuse  = tracker->misuse || tracker->live[i].size != size;
      tracker->live[i] = tracker->live[--tracker->live_count];
      free(pointer);
      return;
    }
  }
  tracker->misuse = true;
}

// Makes options that use a tracking allocator.
static aseprite_options tracked(tracking_allocator* tracker) {
  return (aseprite_options){
      .allocator =
          {
              .alloc   = tracking_alloc,
              .release = tracking_release,
              .user    = tracker,
          },
  };
}

// Loads a fixture from memory, or from the disk.
static aseprite_result load_fixture(const char* name, bool from_file,
                                    const aseprite_options* options,
                                    aseprite_sprite* sprite) {
  if (from_file) {
    char path[1024];
    fixture_path(path, sizeof path, name, ".aseprite");
    return aseprite_load_file(path, options, sprite);
  }
  size_t size   = 0;
  uint8_t* data = read_file(name, ".aseprite", &size);
  if (!data) {
    *sprite = (aseprite_sprite){};
    return ASEPRITE_ERROR_IO;
  }
  aseprite_result result = aseprite_load_memory(data, size, options, sprite);
  free(data);
  return result;
}

static const char* const memory_fixtures[] = {
    "rgba", "indexed", "grayscale", "tilemap", "properties",
    "tags", "slices",  "uuid",      "skipped",
};

// Loads a fixture with each allocation failing in turn. A failed load must
// leave nothing allocated, and the sprite must release with its allocator.
static bool check_allocator_failures(const char* name, bool from_file) {
  tracking_allocator tracker = {};
  aseprite_options options   = tracked(&tracker);
  aseprite_sprite sprite;
  bool ok = load_fixture(name, from_file, &options, &sprite) == ASEPRITE_OK;
  size_t needed = tracker.allocations;
  ok            = ok && tracker.live_count > 0;
  aseprite_free(&sprite);
  ok = ok && needed > 0 && tracker.live_count == 0 && !tracker.misuse;

  for (size_t i = 1; ok && i <= needed; i++) {
    tracker = (tracking_allocator){.fail_at = i};
    ok      = load_fixture(name, from_file, &options, &sprite) ==
                  ASEPRITE_ERROR_NO_MEMORY &&
              sprite.memory == nullptr && sprite.frames == nullptr &&
              tracker.live_count == 0 && !tracker.misuse;
  }
  if (!ok) {
    fprintf(stderr, "allocator test failed for %s (file: %d)\n", name,
            from_file);
  }
  return ok;
}

TEST_CASE(test_allocator_failures) {
  for (size_t i = 0; i < sizeof memory_fixtures / sizeof memory_fixtures[0];
       i++) {
    REQUIRE(check_allocator_failures(memory_fixtures[i], false));
    REQUIRE(check_allocator_failures(memory_fixtures[i], true));
  }
  return true;
}

// Checks that the bytes after the usable part of a block are unchanged.
static bool canary_intact(const unsigned char* block, size_t size,
                          size_t total) {
  for (size_t i = size; i < total; i++) {
    if (block[i] != canary) {
      return false;
    }
  }
  return true;
}

// Loads a fixture into blocks of different sizes. memory_used is the least
// size that works, and a load never writes outside of its block.
static bool check_caller_block(const char* name, bool from_file) {
  aseprite_sprite reference;
  if (load_fixture(name, from_file, nullptr, &reference) != ASEPRITE_OK) {
    return false;
  }
  size_t used       = reference.memory_used;
  uint64_t checksum = walk_sprite(&reference);
  aseprite_free(&reference);
  bool ok = used > 0 && used % alignof(max_align_t) == 0;

  const size_t sizes[] = {
      used, used + 3, used - 1, used - alignof(max_align_t), 0,
  };
  for (size_t i = 0; ok && i < sizeof sizes / sizeof sizes[0]; i++) {
    size_t size          = sizes[i];
    size_t total         = (size + canary_size + alignof(max_align_t) - 1) &
                           ~(alignof(max_align_t) - 1);
    unsigned char* block = aligned_alloc(alignof(max_align_t), total);
    if (!block) {
      return false;
    }
    memset(block, 0, size);
    memset(block + size, canary, total - size);

    aseprite_options options = {.memory = block, .memory_size = size};
    aseprite_sprite sprite;
    aseprite_result result = load_fixture(name, from_file, &options, &sprite);
    if (size >= used) {
      const unsigned char* frames = (const unsigned char*)sprite.frames;
      ok = result == ASEPRITE_OK && sprite.memory == nullptr &&
           sprite.memory_used == used && frames >= block &&
           frames < block + size && walk_sprite(&sprite) == checksum;
      aseprite_free(&sprite);
      aseprite_free(&sprite);
      if (ok) {
        // The block is free again after aseprite_free().
        ok = load_fixture(name, from_file, &options, &sprite) == ASEPRITE_OK &&
             walk_sprite(&sprite) == checksum;
        aseprite_free(&sprite);
      }
    } else {
      ok = result == ASEPRITE_ERROR_NO_MEMORY && sprite.frames == nullptr &&
           sprite.memory_used == 0;
    }
    ok = ok && canary_intact(block, size, total);
    free(block);
  }
  if (!ok) {
    fprintf(stderr, "caller block test failed for %s (file: %d)\n", name,
            from_file);
  }
  return ok;
}

TEST_CASE(test_caller_block) {
  for (size_t i = 0; i < sizeof memory_fixtures / sizeof memory_fixtures[0];
       i++) {
    REQUIRE(check_caller_block(memory_fixtures[i], false));
    REQUIRE(check_caller_block(memory_fixtures[i], true));
  }
  return true;
}

TEST_CASE(test_memory_used_does_not_depend_on_the_allocator) {
  for (size_t i = 0; i < sizeof memory_fixtures / sizeof memory_fixtures[0];
       i++) {
    for (int from_file = 0; from_file <= 1; from_file++) {
      tracking_allocator tracker = {};
      aseprite_options options   = tracked(&tracker);
      aseprite_sprite plain;
      aseprite_sprite custom;
      REQUIRE(load_fixture(memory_fixtures[i], from_file, nullptr, &plain) ==
              ASEPRITE_OK);
      REQUIRE(load_fixture(memory_fixtures[i], from_file, &options, &custom) ==
              ASEPRITE_OK);
      REQUIRE(plain.memory_used == custom.memory_used);
      aseprite_free(&plain);
      aseprite_free(&custom);
    }
  }
  return true;
}

/* ---- Inflate ----------------------------------------------------------- */

// Checks the inflate function with a pair of fixture files.
static bool check_inflate(const char* name) {
  size_t raw_size        = 0;
  size_t compressed_size = 0;
  uint8_t* raw           = read_file(name, ".raw", &raw_size);
  uint8_t* compressed    = read_file(name, ".z", &compressed_size);
  uint8_t* out           = malloc(raw_size + 1);
  bool ok                = raw && compressed && out;
  if (ok) {
    ok = aseprite_inflate(compressed, compressed_size, out, raw_size) ==
             ASEPRITE_OK &&
         memcmp(out, raw, raw_size) == 0;
  }
  if (ok) {
    ok = aseprite_inflate(compressed, compressed_size, out, raw_size + 1) ==
         ASEPRITE_ERROR_COMPRESSION;
  }
  if (ok && raw_size > 0) {
    ok = aseprite_inflate(compressed, compressed_size, out, raw_size - 1) ==
         ASEPRITE_ERROR_COMPRESSION;
  }
  if (ok) {
    compressed[compressed_size / 2] ^= 0x10;
    ok = aseprite_inflate(compressed, compressed_size, out, raw_size) !=
         ASEPRITE_OK;
    compressed[compressed_size / 2] ^= 0x10;
  }
  size_t step = (compressed_size / 64) + 1;
  for (size_t length = 0; ok && length < compressed_size; length += step) {
    ok = aseprite_inflate(compressed, length, out, raw_size) != ASEPRITE_OK;
  }
  if (!ok) {
    fprintf(stderr, "inflate %s failed\n", name);
  }
  free(raw);
  free(compressed);
  free(out);
  return ok;
}

TEST_CASE(test_inflate) {
  static const char* const names[] = {
      "inflate/empty", "inflate/stored", "inflate/fast",
      "inflate/best",  "inflate/fixed",  "inflate/huffman",
      "inflate/rle",   "inflate/window", "inflate/mixed",
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    REQUIRE(check_inflate(names[i]));
  }
  return true;
}

TEST_CASE(test_inflate_bad_headers) {
  uint8_t out[4];
  static const uint8_t bad_method[]     = {0x79, 0x9C, 0x03, 0x00};
  static const uint8_t bad_check[]      = {0x78, 0x9D, 0x03, 0x00};
  static const uint8_t dictionary[]     = {0x78, 0xBB, 0x03, 0x00};
  static const uint8_t reserved_block[] = {0x78, 0x9C, 0x07, 0x00};
  REQUIRE(aseprite_inflate(bad_method, 4, out, 0) != ASEPRITE_OK);
  REQUIRE(aseprite_inflate(bad_check, 4, out, 0) != ASEPRITE_OK);
  REQUIRE(aseprite_inflate(dictionary, 4, out, 0) != ASEPRITE_OK);
  REQUIRE(aseprite_inflate(reserved_block, 4, out, 0) != ASEPRITE_OK);
  REQUIRE(aseprite_inflate(bad_method, 1, out, 0) != ASEPRITE_OK);
  return true;
}

/* ---- Main -------------------------------------------------------------- */

// Runs all the tests.
static TEST_SUITE(suite_aseprite) {
  RUN_TEST_CASE(test_rgba_header);
  RUN_TEST_CASE(test_rgba_palette);
  RUN_TEST_CASE(test_rgba_layers);
  RUN_TEST_CASE(test_rgba_cels);
  RUN_TEST_CASE(test_rgba_linked_and_compressed_cels);
  RUN_TEST_CASE(test_grayscale);
  RUN_TEST_CASE(test_indexed);
  RUN_TEST_CASE(test_tilesets);
  RUN_TEST_CASE(test_tilemap_cels);
  RUN_TEST_CASE(test_tags);
  RUN_TEST_CASE(test_slices);
  RUN_TEST_CASE(test_property_numbers);
  RUN_TEST_CASE(test_property_structures);
  RUN_TEST_CASE(test_property_errors);
  RUN_TEST_CASE(test_uuid_and_icc_profile);
  RUN_TEST_CASE(test_skipped_chunks);
  RUN_TEST_CASE(test_invalid_files);
  RUN_TEST_CASE(test_every_prefix_fails);
  RUN_TEST_CASE(test_header_without_implementation);
  RUN_TEST_CASE(test_free_is_safe);
  RUN_TEST_CASE(test_allocator_failures);
  RUN_TEST_CASE(test_caller_block);
  RUN_TEST_CASE(test_memory_used_does_not_depend_on_the_allocator);
  RUN_TEST_CASE(test_result_strings);
  RUN_TEST_CASE(test_inflate);
  RUN_TEST_CASE(test_inflate_bad_headers);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_aseprite);
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
