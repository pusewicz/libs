// Prints the content of Aseprite files.
//
// Usage: dump FILE...

#define ASEPRITE_IMPLEMENTATION
#include <inttypes.h>
#include <stdio.h>

#include "aseprite.h"

// Prints two spaces for each level of depth.
static void print_indent(int depth) {
  for (int i = 0; i < depth; i++) {
    fputs("  ", stdout);
  }
}

static void print_property(const aseprite_property* property, int depth);

// Prints the items of a vector or a map.
static void print_list(const aseprite_property* property, int depth) {
  printf("%s (%" PRIu32 ")\n",
         property->type == ASEPRITE_PROPERTY_TYPE_MAP ? "map" : "vector",
         property->value.list.count);
  for (uint32_t i = 0; i < property->value.list.count; i++) {
    print_property(&property->value.list.items[i], depth + 1);
  }
}

// Prints a property and its items.
static void print_property(const aseprite_property* property, int depth) {
  print_indent(depth);
  if (property->name) {
    printf("%s = ", property->name);
  }
  switch (property->type) {
  case ASEPRITE_PROPERTY_TYPE_BOOL:
    printf("%s\n", property->value.boolean ? "true" : "false");
    break;
  case ASEPRITE_PROPERTY_TYPE_INT8:
  case ASEPRITE_PROPERTY_TYPE_INT16:
  case ASEPRITE_PROPERTY_TYPE_INT32:
  case ASEPRITE_PROPERTY_TYPE_INT64:
    printf("%" PRId64 "\n", property->value.signed_int);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT8:
  case ASEPRITE_PROPERTY_TYPE_UINT16:
  case ASEPRITE_PROPERTY_TYPE_UINT32:
  case ASEPRITE_PROPERTY_TYPE_UINT64:
    printf("%" PRIu64 "\n", property->value.unsigned_int);
    break;
  case ASEPRITE_PROPERTY_TYPE_FIXED:
    printf("%g (fixed)\n", aseprite_fixed_to_double(property->value.fixed));
    break;
  case ASEPRITE_PROPERTY_TYPE_FLOAT:
    printf("%g (float)\n", (double)property->value.float32);
    break;
  case ASEPRITE_PROPERTY_TYPE_DOUBLE:
    printf("%g (double)\n", property->value.float64);
    break;
  case ASEPRITE_PROPERTY_TYPE_STRING:
    printf("\"%s\"\n", property->value.string);
    break;
  case ASEPRITE_PROPERTY_TYPE_POINT:
    printf("point %" PRId32 ",%" PRId32 "\n", property->value.point.x,
           property->value.point.y);
    break;
  case ASEPRITE_PROPERTY_TYPE_SIZE:
    printf("size %" PRId32 "x%" PRId32 "\n", property->value.size.width,
           property->value.size.height);
    break;
  case ASEPRITE_PROPERTY_TYPE_RECT:
    printf("rect %" PRId32 ",%" PRId32 " %" PRId32 "x%" PRId32 "\n",
           property->value.rect.x, property->value.rect.y,
           property->value.rect.width, property->value.rect.height);
    break;
  case ASEPRITE_PROPERTY_TYPE_VECTOR:
  case ASEPRITE_PROPERTY_TYPE_MAP:
    print_list(property, depth);
    break;
  case ASEPRITE_PROPERTY_TYPE_UUID:
    printf("uuid %02x%02x%02x%02x-...\n", property->value.uuid[0],
           property->value.uuid[1], property->value.uuid[2],
           property->value.uuid[3]);
    break;
  default:
    printf("type %u\n", (unsigned)property->type);
    break;
  }
}

// Prints user data.
static void print_user_data(const aseprite_user_data* user_data, int depth) {
  if (user_data->text) {
    print_indent(depth);
    printf("text: \"%s\"\n", user_data->text);
  }
  if (user_data->flags & ASEPRITE_USER_DATA_FLAG_COLOR) {
    print_indent(depth);
    printf("color: #%02x%02x%02x%02x\n", user_data->color.r, user_data->color.g,
           user_data->color.b, user_data->color.a);
  }
  for (uint32_t i = 0; i < user_data->map_count; i++) {
    const aseprite_property_map* map = &user_data->maps[i];
    print_indent(depth);
    printf("properties (key %" PRIu32 "):\n", map->key);
    for (uint32_t j = 0; j < map->count; j++) {
      print_property(&map->properties[j], depth + 1);
    }
  }
}

// Gets the number of groups that contain a layer.
static int layer_depth(const aseprite_sprite* sprite, uint32_t index) {
  int depth = 0;
  for (int32_t parent = sprite->layers[index].parent; parent >= 0;
       parent         = sprite->layers[parent].parent) {
    depth++;
  }
  return depth;
}

// Prints the layers as a tree.
static void print_layers(const aseprite_sprite* sprite) {
  static const char* const types[] = {"image", "group", "tilemap"};
  printf("layers: %" PRIu32 "\n", sprite->layer_count);
  for (uint32_t i = 0; i < sprite->layer_count; i++) {
    const aseprite_layer* layer = &sprite->layers[i];
    int depth                   = layer_depth(sprite, i) + 1;
    print_indent(depth);
    printf("%" PRIu32 " \"%s\" %s, flags %u, blend %u, opacity %u\n", i,
           layer->name,
           layer->type <= ASEPRITE_LAYER_TYPE_TILEMAP ? types[layer->type]
                                                      : "unknown",
           (unsigned)layer->flags, (unsigned)layer->blend_mode,
           (unsigned)layer->opacity);
    print_user_data(&layer->user_data, depth + 1);
  }
}

// Prints a cel.
static void print_cel(const aseprite_cel* cel) {
  printf("    layer %" PRIu32 " at %d,%d, opacity %u, z %d", cel->layer, cel->x,
         cel->y, (unsigned)cel->opacity, cel->z_index);
  if (cel->type == ASEPRITE_CEL_TYPE_LINKED) {
    printf(", linked to frame %u", (unsigned)cel->linked_frame);
  }
  if (cel->image.pixels) {
    printf(", image %" PRIu32 "x%" PRIu32, cel->image.width, cel->image.height);
  }
  if (cel->tilemap.tiles) {
    printf(", tilemap %" PRIu32 "x%" PRIu32 " tiles", cel->tilemap.width,
           cel->tilemap.height);
  }
  putchar('\n');
  print_user_data(&cel->user_data, 3);
}

// Prints the frames and their cels.
static void print_frames(const aseprite_sprite* sprite) {
  printf("frames: %" PRIu32 "\n", sprite->frame_count);
  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    const aseprite_frame* frame = &sprite->frames[i];
    printf("  %" PRIu32 ": %u ms, palette %" PRIu32 ", %" PRIu32 " cels\n", i,
           (unsigned)frame->duration, frame->palette, frame->cel_count);
    for (uint32_t j = 0; j < frame->cel_count; j++) {
      print_cel(&frame->cels[j]);
    }
  }
}

// Prints the tags and the slices.
static void print_tags_and_slices(const aseprite_sprite* sprite) {
  printf("tags: %" PRIu32 "\n", sprite->tag_count);
  for (uint32_t i = 0; i < sprite->tag_count; i++) {
    const aseprite_tag* tag = &sprite->tags[i];
    printf("  \"%s\" frames %u-%u, direction %u, repeat %u\n", tag->name,
           (unsigned)tag->from, (unsigned)tag->to, (unsigned)tag->direction,
           (unsigned)tag->repeat);
    print_user_data(&tag->user_data, 2);
  }
  printf("slices: %" PRIu32 "\n", sprite->slice_count);
  for (uint32_t i = 0; i < sprite->slice_count; i++) {
    const aseprite_slice* slice = &sprite->slices[i];
    printf("  \"%s\" flags %" PRIu32 ", %" PRIu32 " keys\n", slice->name,
           slice->flags, slice->key_count);
    for (uint32_t j = 0; j < slice->key_count; j++) {
      const aseprite_slice_key* key = &slice->keys[j];
      printf("    frame %" PRIu32 ": %" PRId32 ",%" PRId32 " %" PRIu32
             "x%" PRIu32 "\n",
             key->frame, key->x, key->y, key->width, key->height);
    }
    print_user_data(&slice->user_data, 2);
  }
}

// Prints the tilesets.
static void print_tilesets(const aseprite_sprite* sprite) {
  printf("tilesets: %" PRIu32 "\n", sprite->tileset_count);
  for (uint32_t i = 0; i < sprite->tileset_count; i++) {
    const aseprite_tileset* tileset = &sprite->tilesets[i];
    printf("  %" PRIu32 " \"%s\" %" PRIu32 " tiles of %ux%u, flags %" PRIu32
           "\n",
           tileset->id, tileset->name, tileset->tile_count,
           (unsigned)tileset->tile_width, (unsigned)tileset->tile_height,
           tileset->flags);
    print_user_data(&tileset->user_data, 2);
    for (uint32_t j = 0; j < tileset->tile_user_data_count; j++) {
      printf("    tile %" PRIu32 ":\n", j);
      print_user_data(&tileset->tile_user_data[j], 3);
    }
  }
}

// Prints all the content of a sprite.
static void print_sprite(const aseprite_sprite* sprite) {
  printf("size: %ux%u, depth %u, flags %" PRIu32 "\n", (unsigned)sprite->width,
         (unsigned)sprite->height, (unsigned)sprite->depth, sprite->flags);
  printf("memory: %zu bytes\n", sprite->memory_used);
  printf("palettes: %" PRIu32 ", first has %" PRIu32 " colors\n",
         sprite->palette_count, sprite->palettes[0].count);
  printf("color profile: type %u\n", (unsigned)sprite->color_profile.type);
  for (uint32_t i = 0; i < sprite->external_file_count; i++) {
    const aseprite_external_file* file = &sprite->external_files[i];
    printf("external file %" PRIu32 ": type %u, \"%s\"\n", file->id,
           (unsigned)file->type, file->name);
  }
  print_user_data(&sprite->user_data, 0);
  print_layers(sprite);
  print_frames(sprite);
  print_tags_and_slices(sprite);
  print_tilesets(sprite);
}

int main(int argc, char** argv) {
  int status = 0;
  for (int i = 1; i < argc; i++) {
    aseprite_sprite sprite;
    aseprite_result result = aseprite_load_file(argv[i], nullptr, &sprite);
    if (result != ASEPRITE_OK) {
      fprintf(stderr, "%s: %s\n", argv[i], aseprite_result_string(result));
      status = 1;
      continue;
    }
    printf("== %s\n", argv[i]);
    print_sprite(&sprite);
    aseprite_free(&sprite);
  }
  return status;
}
