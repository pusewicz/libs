// Reads all the memory of a loaded sprite and checks its invariants. It
// aborts when an invariant is false. The fuzz and sweep tools use it.

#ifndef WALK_H
#define WALK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aseprite.h"

// Aborts when the condition is false.
static void walk_check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "invariant failed: %s\n", message);
    abort();
  }
}

// Reads all the bytes.
static uint64_t walk_bytes(const uint8_t* data, size_t size) {
  uint64_t sum = 0;
  for (size_t i = 0; i < size; i++) {
    sum += data[i];
  }
  return sum;
}

// Reads all the bytes of a string.
static uint64_t walk_string(const char* text) {
  return text ? walk_bytes((const uint8_t*)text, strlen(text)) : 0;
}

// Reads a property and its items.
static uint64_t walk_property(const aseprite_property* property, int depth) {
  walk_check(depth <= 129, "property depth");
  uint64_t sum = walk_string(property->name) + property->type;
  switch (property->type) {
  case ASEPRITE_PROPERTY_TYPE_STRING:
    sum += walk_string(property->value.string);
    break;
  case ASEPRITE_PROPERTY_TYPE_VECTOR:
  case ASEPRITE_PROPERTY_TYPE_MAP:
    for (uint32_t i = 0; i < property->value.list.count; i++) {
      const aseprite_property* item = &property->value.list.items[i];
      walk_check((item->name != nullptr) ==
                     (property->type == ASEPRITE_PROPERTY_TYPE_MAP),
                 "property names");
      sum += walk_property(item, depth + 1);
    }
    break;
  default:
    sum += walk_bytes(property->value.uuid, sizeof property->value.uuid);
    break;
  }
  return sum;
}

// Reads user data.
static uint64_t walk_user_data(const aseprite_user_data* user_data) {
  uint64_t sum = walk_string(user_data->text);
  for (uint32_t i = 0; i < user_data->map_count; i++) {
    const aseprite_property_map* map = &user_data->maps[i];
    for (uint32_t j = 0; j < map->count; j++) {
      walk_check(map->properties[j].name != nullptr, "map property name");
      sum += walk_property(&map->properties[j], 1);
    }
  }
  return sum;
}

// Reads all the pixels of an image.
static uint64_t walk_image(const aseprite_sprite* sprite,
                           const aseprite_image* image) {
  if (!image->pixels) {
    return 0;
  }
  size_t size = (size_t)image->width * image->height *
                aseprite_bytes_per_pixel(sprite->depth);
  walk_check(size > 0, "image size");
  return walk_bytes(image->pixels, size);
}

// Reads a cel and checks it.
static uint64_t walk_cel(const aseprite_sprite* sprite, uint32_t frame_index,
                         const aseprite_cel* cel) {
  walk_check(cel->layer < sprite->layer_count, "cel layer");
  walk_check(cel->image.pixels || cel->tilemap.tiles, "cel data");
  if (cel->type == ASEPRITE_CEL_TYPE_LINKED) {
    walk_check(cel->linked_frame < frame_index, "linked frame");
  }
  uint64_t sum =
      walk_image(sprite, &cel->image) + walk_user_data(&cel->user_data);
  const aseprite_tilemap* tilemap = &cel->tilemap;
  if (tilemap->tiles) {
    size_t count = (size_t)tilemap->width * tilemap->height;
    walk_check(count > 0, "tilemap size");
    for (size_t i = 0; i < count; i++) {
      sum += aseprite_tile_id(tilemap, tilemap->tiles[i]);
    }
  }
  return sum;
}

// Reads the layers and checks the hierarchy.
static uint64_t walk_layers(const aseprite_sprite* sprite) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < sprite->layer_count; i++) {
    const aseprite_layer* layer = &sprite->layers[i];
    walk_check(layer->parent >= -1 && layer->parent < (int32_t)i,
               "layer parent");
    if (layer->parent >= 0) {
      walk_check(sprite->layers[layer->parent].type ==
                     ASEPRITE_LAYER_TYPE_GROUP,
                 "parent type");
    }
    if (layer->type == ASEPRITE_LAYER_TYPE_TILEMAP) {
      walk_check(layer->tileset < sprite->tileset_count, "layer tileset");
    }
    sum += walk_string(layer->name) + walk_user_data(&layer->user_data);
  }
  return sum;
}

// Reads the frames and their cels.
static uint64_t walk_frames(const aseprite_sprite* sprite) {
  uint64_t sum = 0;
  walk_check(sprite->frame_count > 0, "frame count");
  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    const aseprite_frame* frame = &sprite->frames[i];
    walk_check(frame->palette < sprite->palette_count, "frame palette");
    for (uint32_t j = 0; j < frame->cel_count; j++) {
      sum += walk_cel(sprite, i, &frame->cels[j]);
    }
  }
  return sum;
}

// Reads the palettes.
static uint64_t walk_palettes(const aseprite_sprite* sprite) {
  uint64_t sum = 0;
  walk_check(sprite->palette_count > 0, "palette count");
  for (uint32_t i = 0; i < sprite->palette_count; i++) {
    const aseprite_palette* palette = &sprite->palettes[i];
    walk_check(palette->count <= 65536, "palette size");
    sum += walk_bytes((const uint8_t*)palette->colors,
                      palette->count * sizeof *palette->colors);
    for (uint32_t j = 0; palette->names && j < palette->count; j++) {
      sum += walk_string(palette->names[j]);
    }
  }
  return sum;
}

// Reads the tags, slices, tilesets, external files and color profile.
static uint64_t walk_others(const aseprite_sprite* sprite) {
  uint64_t sum = 0;
  for (uint32_t i = 0; i < sprite->tag_count; i++) {
    const aseprite_tag* tag = &sprite->tags[i];
    walk_check(tag->from <= tag->to && tag->to < sprite->frame_count,
               "tag range");
    sum += walk_string(tag->name) + walk_user_data(&tag->user_data);
  }
  for (uint32_t i = 0; i < sprite->slice_count; i++) {
    const aseprite_slice* slice = &sprite->slices[i];
    sum += walk_string(slice->name) + walk_user_data(&slice->user_data);
    sum += walk_bytes((const uint8_t*)slice->keys,
                      slice->key_count * sizeof *slice->keys);
  }
  for (uint32_t i = 0; i < sprite->tileset_count; i++) {
    const aseprite_tileset* tileset = &sprite->tilesets[i];
    walk_check(tileset->tile_user_data_count <= tileset->tile_count,
               "tile user data");
    sum += walk_string(tileset->name) + walk_image(sprite, &tileset->image);
    sum += walk_user_data(&tileset->user_data);
    for (uint32_t j = 0; j < tileset->tile_user_data_count; j++) {
      sum += walk_user_data(&tileset->tile_user_data[j]);
    }
  }
  for (uint32_t i = 0; i < sprite->external_file_count; i++) {
    sum += walk_string(sprite->external_files[i].name);
  }
  sum += walk_bytes(sprite->color_profile.icc, sprite->color_profile.icc_size);
  return sum + walk_user_data(&sprite->user_data);
}

// Returns a checksum so that the compiler keeps all the reads.
static uint64_t walk_sprite(const aseprite_sprite* sprite) {
  return walk_layers(sprite) + walk_frames(sprite) + walk_palettes(sprite) +
         walk_others(sprite);
}

#endif
