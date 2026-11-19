/*
 * aseprite.h - read Aseprite files (.ase, .aseprite).
 *
 * Version: 0.1.0
 * SPDX-License-Identifier: Zlib
 * Copyright (c) 2026 Piotr Usewicz
 *
 * This library reads all the chunks of an Aseprite file into an
 * aseprite_sprite. It decompresses the cel and tileset images. The pixels
 * keep the color depth of the sprite. The library does not blend layers.
 *
 * Usage:
 *
 *   Define ASEPRITE_IMPLEMENTATION in one C file, before you include
 *   aseprite.h:
 *
 *     #define ASEPRITE_IMPLEMENTATION
 *     #include "aseprite.h"
 *
 *   Load a sprite, then free it:
 *
 *     aseprite_sprite sprite;
 *     aseprite_result result = aseprite_load_file("hero.aseprite", &sprite);
 *     if (result != ASEPRITE_OK) {
 *       fprintf(stderr, "%s\n", aseprite_result_string(result));
 *     }
 *     ...
 *     aseprite_free(&sprite);
 *
 * Options:
 *
 *   Define them before the implementation:
 *
 *     ASEPRITE_MALLOC(size)   Replace malloc.
 *     ASEPRITE_FREE(pointer)  Replace free.
 *     ASEPRITE_ASSERT(expr)   Replace assert.
 *     ASEPRITE_NO_STDIO       Remove aseprite_load_file.
 *
 *   Define all or none of ASEPRITE_MALLOC and ASEPRITE_FREE. Define
 *   ASEPRITE_NO_STDIO in all files.
 *
 * File format:
 *
 *   https://github.com/aseprite/aseprite/blob/c9baf298a43c1e31ede7812392f80de155d5f6a2/docs/ase-file-specs.md
 */

#ifndef ASEPRITE_H
#define ASEPRITE_H

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "aseprite.h needs C23"
#endif

#include <stddef.h>
#include <stdint.h>

/** The result of a load. */
typedef enum aseprite_result : uint8_t {
  ASEPRITE_OK,                 /**< The load is successful. */
  ASEPRITE_ERROR_IO,           /**< The file cannot be opened or read. */
  ASEPRITE_ERROR_NO_MEMORY,    /**< An allocation failed. */
  ASEPRITE_ERROR_NOT_ASEPRITE, /**< The data is not an Aseprite file. */
  ASEPRITE_ERROR_TRUNCATED,    /**< The data ends too soon. */
  ASEPRITE_ERROR_MALFORMED,    /**< A value in the file is not valid. */
  ASEPRITE_ERROR_COMPRESSION,  /**< The compressed data is not valid. */
} aseprite_result;

/** The color depth of a sprite, in bits per pixel. */
typedef enum aseprite_color_depth : uint16_t {
  ASEPRITE_DEPTH_INDEXED   = 8,  /**< 1 byte: the palette index. */
  ASEPRITE_DEPTH_GRAYSCALE = 16, /**< 2 bytes: value, alpha. */
  ASEPRITE_DEPTH_RGBA      = 32, /**< 4 bytes: red, green, blue, alpha. */
} aseprite_color_depth;

/** The bits of aseprite_sprite.flags. */
enum : uint32_t {
  ASEPRITE_SPRITE_FLAG_LAYER_OPACITY = 1, /**< Layer opacity is valid. */
  ASEPRITE_SPRITE_FLAG_GROUP_BLEND   = 2, /**< Groups blend separately. */
  ASEPRITE_SPRITE_FLAG_LAYER_UUID    = 4, /**< Layers have a UUID. */
};

/** The bits of aseprite_layer.flags. */
enum : uint32_t {
  ASEPRITE_LAYER_FLAG_VISIBLE            = 1,
  ASEPRITE_LAYER_FLAG_EDITABLE           = 2,
  ASEPRITE_LAYER_FLAG_LOCK_MOVEMENT      = 4,
  ASEPRITE_LAYER_FLAG_BACKGROUND         = 8,
  ASEPRITE_LAYER_FLAG_PREFER_LINKED_CELS = 16,
  ASEPRITE_LAYER_FLAG_COLLAPSED          = 32,
  ASEPRITE_LAYER_FLAG_REFERENCE          = 64,
};

/** The type of a layer. Other values are possible in newer files. */
typedef enum aseprite_layer_type : uint16_t {
  ASEPRITE_LAYER_TYPE_IMAGE   = 0,
  ASEPRITE_LAYER_TYPE_GROUP   = 1,
  ASEPRITE_LAYER_TYPE_TILEMAP = 2,
} aseprite_layer_type;

/** The blend mode of a layer. */
typedef enum aseprite_blend_mode : uint16_t {
  ASEPRITE_BLEND_NORMAL      = 0,
  ASEPRITE_BLEND_MULTIPLY    = 1,
  ASEPRITE_BLEND_SCREEN      = 2,
  ASEPRITE_BLEND_OVERLAY     = 3,
  ASEPRITE_BLEND_DARKEN      = 4,
  ASEPRITE_BLEND_LIGHTEN     = 5,
  ASEPRITE_BLEND_COLOR_DODGE = 6,
  ASEPRITE_BLEND_COLOR_BURN  = 7,
  ASEPRITE_BLEND_HARD_LIGHT  = 8,
  ASEPRITE_BLEND_SOFT_LIGHT  = 9,
  ASEPRITE_BLEND_DIFFERENCE  = 10,
  ASEPRITE_BLEND_EXCLUSION   = 11,
  ASEPRITE_BLEND_HUE         = 12,
  ASEPRITE_BLEND_SATURATION  = 13,
  ASEPRITE_BLEND_COLOR       = 14,
  ASEPRITE_BLEND_LUMINOSITY  = 15,
  ASEPRITE_BLEND_ADDITION    = 16,
  ASEPRITE_BLEND_SUBTRACT    = 17,
  ASEPRITE_BLEND_DIVIDE      = 18,
} aseprite_blend_mode;

/** How a cel is stored in the file. */
typedef enum aseprite_cel_type : uint16_t {
  ASEPRITE_CEL_TYPE_RAW                = 0, /**< Uncompressed image. */
  ASEPRITE_CEL_TYPE_LINKED             = 1, /**< Data of an other frame. */
  ASEPRITE_CEL_TYPE_COMPRESSED_IMAGE   = 2, /**< Compressed image. */
  ASEPRITE_CEL_TYPE_COMPRESSED_TILEMAP = 3, /**< Compressed tilemap. */
} aseprite_cel_type;

/** The direction of an animation tag. */
typedef enum aseprite_direction : uint8_t {
  ASEPRITE_DIRECTION_FORWARD           = 0,
  ASEPRITE_DIRECTION_REVERSE           = 1,
  ASEPRITE_DIRECTION_PING_PONG         = 2,
  ASEPRITE_DIRECTION_PING_PONG_REVERSE = 3,
} aseprite_direction;

/** The bits of aseprite_user_data.flags. */
enum : uint32_t {
  ASEPRITE_USER_DATA_FLAG_TEXT       = 1,
  ASEPRITE_USER_DATA_FLAG_COLOR      = 2,
  ASEPRITE_USER_DATA_FLAG_PROPERTIES = 4,
};

/** The type of a property value. */
typedef enum aseprite_property_type : uint16_t {
  ASEPRITE_PROPERTY_TYPE_BOOL   = 1,  /**< value.boolean */
  ASEPRITE_PROPERTY_TYPE_INT8   = 2,  /**< value.signed_int */
  ASEPRITE_PROPERTY_TYPE_UINT8  = 3,  /**< value.unsigned_int */
  ASEPRITE_PROPERTY_TYPE_INT16  = 4,  /**< value.signed_int */
  ASEPRITE_PROPERTY_TYPE_UINT16 = 5,  /**< value.unsigned_int */
  ASEPRITE_PROPERTY_TYPE_INT32  = 6,  /**< value.signed_int */
  ASEPRITE_PROPERTY_TYPE_UINT32 = 7,  /**< value.unsigned_int */
  ASEPRITE_PROPERTY_TYPE_INT64  = 8,  /**< value.signed_int */
  ASEPRITE_PROPERTY_TYPE_UINT64 = 9,  /**< value.unsigned_int */
  ASEPRITE_PROPERTY_TYPE_FIXED  = 10, /**< value.fixed */
  ASEPRITE_PROPERTY_TYPE_FLOAT  = 11, /**< value.float32 */
  ASEPRITE_PROPERTY_TYPE_DOUBLE = 12, /**< value.float64 */
  ASEPRITE_PROPERTY_TYPE_STRING = 13, /**< value.string */
  ASEPRITE_PROPERTY_TYPE_POINT  = 14, /**< value.point */
  ASEPRITE_PROPERTY_TYPE_SIZE   = 15, /**< value.size */
  ASEPRITE_PROPERTY_TYPE_RECT   = 16, /**< value.rect */
  ASEPRITE_PROPERTY_TYPE_VECTOR = 17, /**< value.list, items have no name */
  ASEPRITE_PROPERTY_TYPE_MAP    = 18, /**< value.list, items have a name */
  ASEPRITE_PROPERTY_TYPE_UUID   = 19, /**< value.uuid */
} aseprite_property_type;

/** The bits of aseprite_slice.flags. */
enum : uint32_t {
  ASEPRITE_SLICE_FLAG_NINE_PATCH = 1, /**< The keys have a center. */
  ASEPRITE_SLICE_FLAG_PIVOT      = 2, /**< The keys have a pivot. */
};

/** The bits of aseprite_tileset.flags. */
enum : uint32_t {
  ASEPRITE_TILESET_FLAG_EXTERNAL = 1, /**< The tiles are in an other file. */
  ASEPRITE_TILESET_FLAG_EMBEDDED = 2, /**< The tiles are in this file. */
  ASEPRITE_TILESET_FLAG_ZERO_IS_EMPTY = 4, /**< Tile 0 is the empty tile. */
  ASEPRITE_TILESET_FLAG_MATCH_X_FLIP  = 8,
  ASEPRITE_TILESET_FLAG_MATCH_Y_FLIP  = 16,
  ASEPRITE_TILESET_FLAG_MATCH_DIAGONAL_FLIP = 32,
};

/** The type of a color profile. */
typedef enum aseprite_color_profile_type : uint16_t {
  ASEPRITE_COLOR_PROFILE_NONE = 0,
  ASEPRITE_COLOR_PROFILE_SRGB = 1,
  ASEPRITE_COLOR_PROFILE_ICC  = 2,
} aseprite_color_profile_type;

/** The bits of aseprite_color_profile.flags. */
enum : uint32_t {
  ASEPRITE_COLOR_PROFILE_FLAG_FIXED_GAMMA = 1,
};

/** The type of an external file. */
typedef enum aseprite_external_file_type : uint8_t {
  ASEPRITE_EXTERNAL_FILE_PALETTE                   = 0,
  ASEPRITE_EXTERNAL_FILE_TILESET                   = 1,
  ASEPRITE_EXTERNAL_FILE_PROPERTIES_EXTENSION      = 2,
  ASEPRITE_EXTERNAL_FILE_TILE_MANAGEMENT_EXTENSION = 3,
} aseprite_external_file_type;

/** A 16.16 fixed point number. */
typedef int32_t aseprite_fixed;

/** An RGBA color. */
typedef struct aseprite_color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} aseprite_color;

/** A point. */
typedef struct aseprite_point {
  int32_t x;
  int32_t y;
} aseprite_point;

/** A size. */
typedef struct aseprite_size {
  int32_t width;
  int32_t height;
} aseprite_size;

/** A rectangle. */
typedef struct aseprite_rect {
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} aseprite_rect;

/**
 * An image in the color depth of the sprite. The pixels are row by row,
 * from top to bottom. Use aseprite_bytes_per_pixel() to get the pixel size.
 */
typedef struct aseprite_image {
  uint32_t width;
  uint32_t height;
  uint8_t* pixels;
} aseprite_image;

typedef struct aseprite_property aseprite_property;

/** A user data property. It is a map entry or a vector item. */
struct aseprite_property {
  char* name; /**< The key in a map. Vector items have no name. */
  aseprite_property_type type;
  union {
    bool boolean;
    int64_t signed_int;
    uint64_t unsigned_int;
    aseprite_fixed fixed;
    float float32;
    double float64;
    char* string;
    aseprite_point point;
    aseprite_size size;
    aseprite_rect rect;
    struct {
      uint32_t count;
      aseprite_property* items;
    } list;
    uint8_t uuid[16];
  } value;
};

/** A set of properties. */
typedef struct aseprite_property_map {
  uint32_t key; /**< 0 for user properties, else an external file ID. */
  uint32_t count;
  aseprite_property* properties;
} aseprite_property_map;

/** User data: text, color and properties. */
typedef struct aseprite_user_data {
  uint32_t flags; /**< ASEPRITE_USER_DATA_FLAG_* bits. */
  char* text;     /**< nullptr when there is no text. */
  aseprite_color color;
  uint32_t map_count;
  aseprite_property_map* maps;
} aseprite_user_data;

/** A layer. The layers are in the file order, from bottom to top. */
typedef struct aseprite_layer {
  char* name;
  uint16_t flags; /**< ASEPRITE_LAYER_FLAG_* bits. */
  aseprite_layer_type type;
  uint16_t child_level;
  int32_t parent; /**< The index of the parent group, or -1. */
  /** The blend mode that applies. It is normal when the file has none. */
  aseprite_blend_mode blend_mode;
  /** The opacity that applies. It is 255 when the file has none. */
  uint8_t opacity;
  uint32_t tileset; /**< Tilemap layers: index in aseprite_sprite.tilesets. */
  uint8_t uuid[16]; /**< Set when the sprite has a layer UUID flag. */
  aseprite_user_data user_data;
} aseprite_layer;

/**
 * A tilemap. Each tile is a tile index with flip bits. Use
 * aseprite_tile_id() and the aseprite_tile_*_flip() functions to read it.
 */
typedef struct aseprite_tilemap {
  uint32_t width;  /**< In tiles. */
  uint32_t height; /**< In tiles. */
  uint16_t bits_per_tile;
  uint32_t id_mask;
  uint32_t x_flip_mask;
  uint32_t y_flip_mask;
  uint32_t diagonal_flip_mask;
  uint32_t* tiles;
} aseprite_tilemap;

/**
 * The content of a layer in a frame. Image cels have image.pixels. Tilemap
 * cels have tilemap.tiles. A linked cel shares the data of the cel in
 * linked_frame.
 */
typedef struct aseprite_cel {
  uint32_t layer; /**< The index in aseprite_sprite.layers. */
  int16_t x;
  int16_t y;
  uint8_t opacity;
  int16_t z_index;
  aseprite_cel_type type;
  uint16_t linked_frame; /**< Linked cels only. */
  aseprite_image image;
  aseprite_tilemap tilemap;
  bool has_precise_bounds;
  aseprite_fixed precise_x;
  aseprite_fixed precise_y;
  aseprite_fixed precise_width;
  aseprite_fixed precise_height;
  aseprite_user_data user_data;
} aseprite_cel;

/** A frame. */
typedef struct aseprite_frame {
  uint16_t duration; /**< In milliseconds. */
  uint32_t palette;  /**< The index in aseprite_sprite.palettes. */
  uint32_t cel_count;
  aseprite_cel* cels; /**< In the file order. */
} aseprite_frame;

/** A palette. */
typedef struct aseprite_palette {
  uint32_t count;
  aseprite_color* colors;
  char** names; /**< nullptr, or count names. A name can be nullptr. */
} aseprite_palette;

/** An animation tag. */
typedef struct aseprite_tag {
  char* name;
  uint16_t from; /**< The first frame. */
  uint16_t to;   /**< The last frame. */
  aseprite_direction direction;
  uint16_t repeat; /**< 0 means no limit. */
  /** Old files only. Newer files have the color in user_data. */
  aseprite_color color;
  aseprite_user_data user_data;
} aseprite_tag;

/** The bounds of a slice, from a frame to the next key. */
typedef struct aseprite_slice_key {
  uint32_t frame;
  int32_t x;
  int32_t y;
  uint32_t width;
  uint32_t height;
  int32_t center_x; /**< Nine patch slices only. */
  int32_t center_y;
  uint32_t center_width;
  uint32_t center_height;
  int32_t pivot_x; /**< Slices with a pivot only. */
  int32_t pivot_y;
} aseprite_slice_key;

/** A slice. */
typedef struct aseprite_slice {
  char* name;
  uint32_t flags; /**< ASEPRITE_SLICE_FLAG_* bits. */
  uint32_t key_count;
  aseprite_slice_key* keys;
  aseprite_user_data user_data;
} aseprite_slice;

/** A tileset. */
typedef struct aseprite_tileset {
  uint32_t id;    /**< The ID that tilemap layers use in the file. */
  uint32_t flags; /**< ASEPRITE_TILESET_FLAG_* bits. */
  uint32_t tile_count;
  uint16_t tile_width;
  uint16_t tile_height;
  int16_t base_index; /**< For display only. */
  char* name;
  uint32_t external_file;    /**< External tilesets: an external file ID. */
  uint32_t external_tileset; /**< External tilesets: the ID in that file. */
  /** Embedded tilesets: tile_width x (tile_height * tile_count) pixels. */
  aseprite_image image;
  aseprite_user_data user_data;
  uint32_t tile_user_data_count; /**< Tiles from 0 that have user data. */
  aseprite_user_data* tile_user_data;
} aseprite_tileset;

/** A reference to an other file or an extension. */
typedef struct aseprite_external_file {
  uint32_t id;
  aseprite_external_file_type type;
  char* name; /**< A file name or an extension ID. */
} aseprite_external_file;

/** The color profile of the sprite. */
typedef struct aseprite_color_profile {
  aseprite_color_profile_type type;
  uint16_t flags; /**< ASEPRITE_COLOR_PROFILE_FLAG_* bits. */
  aseprite_fixed gamma;
  uint32_t icc_size;
  uint8_t* icc; /**< ICC profiles only. */
} aseprite_color_profile;

/**
 * A loaded Aseprite file. The library owns all the memory. Release it with
 * aseprite_free().
 */
typedef struct aseprite_sprite {
  uint16_t width;
  uint16_t height;
  aseprite_color_depth depth;
  uint32_t flags; /**< ASEPRITE_SPRITE_FLAG_* bits. */
  uint16_t speed; /**< Deprecated. Use aseprite_frame.duration. */
  /** The transparent index of indexed sprites. 0 for other depths. */
  uint8_t transparent_index;
  uint16_t color_count; /**< The header color count. 0 in the file is 256. */
  uint8_t pixel_width;  /**< The pixel ratio. 0 in the file is 1:1. */
  uint8_t pixel_height;
  int16_t grid_x;
  int16_t grid_y;
  uint16_t grid_width; /**< 0 when there is no grid. */
  uint16_t grid_height;
  uint32_t frame_count;
  aseprite_frame* frames;
  uint32_t layer_count;
  aseprite_layer* layers;
  uint32_t palette_count; /**< 1 or more. Palette 0 is for frame 0. */
  aseprite_palette* palettes;
  uint32_t tag_count;
  aseprite_tag* tags;
  uint32_t slice_count;
  aseprite_slice* slices;
  uint32_t tileset_count;
  aseprite_tileset* tilesets;
  uint32_t external_file_count;
  aseprite_external_file* external_files;
  aseprite_color_profile color_profile;
  aseprite_user_data user_data;
  void* memory; /**< Internal. Do not change. */
} aseprite_sprite;

/**
 * Loads an Aseprite file from memory.
 *
 * @param data   The file data. The sprite does not use it after the load.
 * @param size   The size of the data in bytes.
 * @param sprite Receives the sprite. On error it is empty.
 * @return ASEPRITE_OK, or the error.
 */
[[__nodiscard__]] aseprite_result
aseprite_load_memory(const void* data, size_t size, aseprite_sprite* sprite);

#ifndef ASEPRITE_NO_STDIO
/**
 * Loads an Aseprite file from the disk.
 *
 * @param path   The path of the file.
 * @param sprite Receives the sprite. On error it is empty.
 * @return ASEPRITE_OK, or the error.
 */
[[__nodiscard__]] aseprite_result aseprite_load_file(const char* path,
                                                     aseprite_sprite* sprite);
#endif

/**
 * Releases the memory of a sprite and makes it empty. It is safe to call it
 * on an empty sprite.
 *
 * @param sprite The sprite.
 */
void aseprite_free(aseprite_sprite* sprite);

/**
 * Gets a description of a result.
 *
 * @param result The result.
 * @return A static string.
 */
const char* aseprite_result_string(aseprite_result result);

/**
 * Gets the size of a pixel.
 *
 * @param depth The color depth.
 * @return The number of bytes in one pixel.
 */
uint32_t aseprite_bytes_per_pixel(aseprite_color_depth depth);

/**
 * Finds the cel of a layer in a frame.
 *
 * @param frame The frame.
 * @param layer The layer index.
 * @return The cel, or nullptr when the layer has no cel in the frame.
 */
const aseprite_cel* aseprite_frame_cel(const aseprite_frame* frame,
                                       uint32_t layer);

/**
 * Gets the tile index of a tile.
 *
 * @param tilemap The tilemap that contains the tile.
 * @param tile    The tile.
 * @return The index in the tileset.
 */
uint32_t aseprite_tile_id(const aseprite_tilemap* tilemap, uint32_t tile);

/**
 * Tells if a tile is flipped horizontally.
 *
 * @param tilemap The tilemap that contains the tile.
 * @param tile    The tile.
 * @return true when the tile is flipped.
 */
bool aseprite_tile_x_flip(const aseprite_tilemap* tilemap, uint32_t tile);

/**
 * Tells if a tile is flipped vertically.
 *
 * @param tilemap The tilemap that contains the tile.
 * @param tile    The tile.
 * @return true when the tile is flipped.
 */
bool aseprite_tile_y_flip(const aseprite_tilemap* tilemap, uint32_t tile);

/**
 * Tells if a tile is flipped diagonally (the X and Y axes swap).
 *
 * @param tilemap The tilemap that contains the tile.
 * @param tile    The tile.
 * @return true when the tile is flipped.
 */
bool aseprite_tile_diagonal_flip(const aseprite_tilemap* tilemap,
                                 uint32_t tile);

/**
 * Converts a fixed point number.
 *
 * @param value The 16.16 fixed point number.
 * @return The value as a double.
 */
double aseprite_fixed_to_double(aseprite_fixed value);

#endif

#ifdef ASEPRITE_IMPLEMENTATION
#ifndef ASEPRITE_IMPLEMENTATION_INCLUDED
#define ASEPRITE_IMPLEMENTATION_INCLUDED

#include <stdckdint.h>
#include <string.h>

#if defined(ASEPRITE_MALLOC) != defined(ASEPRITE_FREE)
#error "Define both ASEPRITE_MALLOC and ASEPRITE_FREE, or none of them"
#endif

#ifndef ASEPRITE_MALLOC
#include <stdlib.h>
#define ASEPRITE_MALLOC(size) malloc(size)
#define ASEPRITE_FREE(pointer) free(pointer)
#endif

#ifndef ASEPRITE_ASSERT
#include <assert.h>
#define ASEPRITE_ASSERT(expr) assert(expr)
#endif

#ifndef ASEPRITE_NO_STDIO
#include <stdio.h>
#endif

static_assert(sizeof(float) == 4 && sizeof(double) == 8);

enum : uint16_t {
  ASEPRITE__CHUNK_OLD_PALETTE = 0x0004,
  // The specification says 0x0011. Aseprite and the FLI format use 11.
  ASEPRITE__CHUNK_OLD_PALETTE_64 = 0x000B,
  ASEPRITE__CHUNK_LAYER          = 0x2004,
  ASEPRITE__CHUNK_CEL            = 0x2005,
  ASEPRITE__CHUNK_CEL_EXTRA      = 0x2006,
  ASEPRITE__CHUNK_COLOR_PROFILE  = 0x2007,
  ASEPRITE__CHUNK_EXTERNAL_FILES = 0x2008,
  ASEPRITE__CHUNK_TAGS           = 0x2018,
  ASEPRITE__CHUNK_PALETTE        = 0x2019,
  ASEPRITE__CHUNK_USER_DATA      = 0x2020,
  ASEPRITE__CHUNK_SLICE          = 0x2022,
  ASEPRITE__CHUNK_TILESET        = 0x2023,
};

constexpr uint16_t aseprite__file_magic         = 0xA5E0;
constexpr uint16_t aseprite__frame_magic        = 0xF1FA;
constexpr size_t aseprite__header_size          = 128;
constexpr size_t aseprite__frame_header_size    = 16;
constexpr size_t aseprite__chunk_header_size    = 6;
constexpr uint32_t aseprite__max_palette_size   = 65536;
constexpr uint32_t aseprite__max_property_depth = 128;
// Deflate gives at most 1032 bytes for each input byte.
constexpr size_t aseprite__max_inflate_ratio = 1032;
constexpr size_t aseprite__block_size        = (size_t)64 * 1024;
constexpr size_t aseprite__alignment         = alignof(max_align_t);

// Minimum sizes of records. They limit counts before an allocation.
constexpr size_t aseprite__min_tag_size           = 19;
constexpr size_t aseprite__min_external_file_size = 14;
constexpr size_t aseprite__min_slice_key_size     = 20;
constexpr size_t aseprite__min_property_size      = 5;
constexpr size_t aseprite__min_mixed_item_size    = 3;
constexpr size_t aseprite__min_map_size           = 8;

/* ---- Memory ------------------------------------------------------------ */

typedef struct aseprite__block aseprite__block;

struct aseprite__block {
  aseprite__block* next;
  size_t used;
  size_t size;
  alignas(max_align_t) unsigned char data[];
};

typedef struct aseprite__arena {
  aseprite__block* head;
} aseprite__arena;

// Releases a list of blocks.
static void aseprite__arena_free(aseprite__block* block) {
  while (block) {
    aseprite__block* next = block->next;
    ASEPRITE_FREE(block);
    block = next;
  }
}

// Allocates a block with room for size bytes.
static aseprite__block* aseprite__new_block(size_t size) {
  size_t total = 0;
  if (ckd_add(&total, sizeof(aseprite__block), size)) {
    return nullptr;
  }
  aseprite__block* block = ASEPRITE_MALLOC(total);
  if (block) {
    block->next = nullptr;
    block->used = 0;
    block->size = size;
  }
  return block;
}

// Returns aligned memory that lives until aseprite__arena_free().
static void* aseprite__alloc(aseprite__arena* arena, size_t size) {
  size_t rounded = 0;
  if (ckd_add(&rounded, size, aseprite__alignment - 1)) {
    return nullptr;
  }
  rounded &= ~(aseprite__alignment - 1);

  aseprite__block* head = arena->head;
  if (head && head->size - head->used >= rounded) {
    void* memory = head->data + head->used;
    head->used += rounded;
    return memory;
  }

  if (head && rounded > aseprite__block_size / 4) {
    aseprite__block* block = aseprite__new_block(rounded);
    if (!block) {
      return nullptr;
    }
    block->used = rounded;
    block->next = head->next;
    head->next  = block;
    return block->data;
  }

  size_t size_of_block =
      rounded > aseprite__block_size ? rounded : aseprite__block_size;
  aseprite__block* block = aseprite__new_block(size_of_block);
  if (!block) {
    return nullptr;
  }
  block->used = rounded;
  block->next = head;
  arena->head = block;
  return block->data;
}

// Returns zeroed memory for count items, or nullptr on failure.
static void* aseprite__alloc_array(aseprite__arena* arena, size_t count,
                                   size_t size) {
  size_t total = 0;
  if (ckd_mul(&total, count, size)) {
    return nullptr;
  }
  void* memory = aseprite__alloc(arena, total);
  if (memory) {
    memset(memory, 0, total);
  }
  return memory;
}

/* ---- Reader ------------------------------------------------------------ */

// Reads little endian data. A read past the end sets truncated and gives 0.
typedef struct aseprite__reader {
  const uint8_t* at;
  const uint8_t* end;
  bool truncated;
} aseprite__reader;

// Makes a reader for size bytes of data.
static aseprite__reader aseprite__reader_make(const uint8_t* data,
                                              size_t size) {
  return (aseprite__reader){.at = data, .end = data + size};
}

// Gets the number of bytes that the reader has not read.
static size_t aseprite__remaining(const aseprite__reader* reader) {
  return (size_t)(reader->end - reader->at);
}

// Reads size bytes. Gives nullptr when the data ends too soon.
static const uint8_t* aseprite__take(aseprite__reader* reader, size_t size) {
  if (size > aseprite__remaining(reader)) {
    reader->at        = reader->end;
    reader->truncated = true;
    return nullptr;
  }
  const uint8_t* data = reader->at;
  reader->at += size;
  return data;
}

// Skips size bytes.
static void aseprite__skip(aseprite__reader* reader, size_t size) {
  (void)aseprite__take(reader, size);
}

// Reads size bytes into a new reader.
static aseprite__reader aseprite__sub_reader(aseprite__reader* reader,
                                             size_t size) {
  const uint8_t* data = aseprite__take(reader, size);
  if (!data) {
    return (aseprite__reader){
        .at        = reader->end,
        .end       = reader->end,
        .truncated = true,
    };
  }
  return aseprite__reader_make(data, size);
}

// Reads a BYTE.
static uint8_t aseprite__u8(aseprite__reader* reader) {
  const uint8_t* data = aseprite__take(reader, 1);
  return data ? data[0] : 0;
}

// Reads a WORD.
static uint16_t aseprite__u16(aseprite__reader* reader) {
  const uint8_t* data = aseprite__take(reader, 2);
  if (!data) {
    return 0;
  }
  return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

// Reads a DWORD.
static uint32_t aseprite__u32(aseprite__reader* reader) {
  const uint8_t* data = aseprite__take(reader, 4);
  if (!data) {
    return 0;
  }
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

// Reads a QWORD.
static uint64_t aseprite__u64(aseprite__reader* reader) {
  uint64_t low  = aseprite__u32(reader);
  uint64_t high = aseprite__u32(reader);
  return low | (high << 32);
}

// Reads a signed byte.
static int32_t aseprite__i8(aseprite__reader* reader) {
  int32_t value = aseprite__u8(reader);
  return value < 128 ? value : value - 256;
}

// Reads a SHORT.
static int16_t aseprite__i16(aseprite__reader* reader) {
  uint16_t bits = aseprite__u16(reader);
  int16_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a LONG.
static int32_t aseprite__i32(aseprite__reader* reader) {
  uint32_t bits = aseprite__u32(reader);
  int32_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a LONG64.
static int64_t aseprite__i64(aseprite__reader* reader) {
  uint64_t bits = aseprite__u64(reader);
  int64_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a FLOAT.
static float aseprite__f32(aseprite__reader* reader) {
  uint32_t bits = aseprite__u32(reader);
  float value   = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a DOUBLE.
static double aseprite__f64(aseprite__reader* reader) {
  uint64_t bits = aseprite__u64(reader);
  double value  = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* ---- Inflate (RFC 1950, RFC 1951) -------------------------------------- */

constexpr uint32_t aseprite__fast_bits = 9;

typedef struct aseprite__huffman {
  uint16_t fast[1U << aseprite__fast_bits]; // (length << 9) | symbol, or 0.
  uint16_t first_code[16];
  uint16_t first_symbol[16];
  uint32_t max_code[17];
  uint8_t lengths[288];
  uint16_t symbols[288];
} aseprite__huffman;

typedef struct aseprite__inflater {
  const uint8_t* in;
  const uint8_t* in_end;
  uint64_t bits;
  uint32_t bit_count;
  uint32_t padding; // Zero bytes added after the end of the input.
  uint8_t* out;
  size_t out_size;
  size_t out_pos;
} aseprite__inflater;

constexpr uint16_t aseprite__length_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23,  27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
constexpr uint8_t aseprite__length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
constexpr uint16_t aseprite__distance_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
constexpr uint8_t aseprite__distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};
constexpr uint8_t aseprite__code_length_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
};

// Reverses the order of the low count bits.
static uint32_t aseprite__reverse_bits(uint32_t value, uint32_t count) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < count; i++) {
    result = (result << 1) | (value & 1);
    value >>= 1;
  }
  return result;
}

// Builds a canonical Huffman decoder. Returns false for bad lengths.
static bool aseprite__build_huffman(aseprite__huffman* huffman,
                                    const uint8_t* lengths, uint32_t count) {
  uint32_t sizes[16]     = {};
  uint32_t next_code[16] = {};
  *huffman               = (aseprite__huffman){};
  for (uint32_t i = 0; i < count; i++) {
    sizes[lengths[i]]++;
  }
  sizes[0] = 0;

  uint32_t code   = 0;
  uint32_t symbol = 0;
  for (uint32_t length = 1; length < 16; length++) {
    next_code[length]             = code;
    huffman->first_code[length]   = (uint16_t)code;
    huffman->first_symbol[length] = (uint16_t)symbol;
    code += sizes[length];
    if (sizes[length] > 0 && code - 1 >= (1U << length)) {
      return false;
    }
    huffman->max_code[length] = code << (16 - length);
    code <<= 1;
    symbol += sizes[length];
  }
  huffman->max_code[16] = 0x10000;

  for (uint32_t i = 0; i < count; i++) {
    uint32_t length = lengths[i];
    if (length == 0) {
      continue;
    }
    uint32_t index          = next_code[length] - huffman->first_code[length] +
                              huffman->first_symbol[length];
    huffman->lengths[index] = (uint8_t)length;
    huffman->symbols[index] = (uint16_t)i;
    if (length <= aseprite__fast_bits) {
      uint16_t entry = (uint16_t)((length << aseprite__fast_bits) | i);
      for (uint32_t j = aseprite__reverse_bits(next_code[length], length);
           j < (1U << aseprite__fast_bits); j += 1U << length) {
        huffman->fast[j] = entry;
      }
    }
    next_code[length]++;
  }
  return true;
}

// Fills the bit buffer. It adds zero bytes after the end of the input.
static void aseprite__fill_bits(aseprite__inflater* inflater) {
  while (inflater->bit_count <= 56) {
    uint64_t byte = 0;
    if (inflater->in < inflater->in_end) {
      byte = *inflater->in++;
    } else {
      inflater->padding++;
    }
    inflater->bits |= byte << inflater->bit_count;
    inflater->bit_count += 8;
  }
}

// Reads count bits, from the least significant bit.
static uint32_t aseprite__bits(aseprite__inflater* inflater, uint32_t count) {
  if (inflater->bit_count < count) {
    aseprite__fill_bits(inflater);
  }
  uint32_t value = (uint32_t)(inflater->bits & ((1ULL << count) - 1));
  inflater->bits >>= count;
  inflater->bit_count -= count;
  return value;
}

// Returns the next symbol, or -1 when the code is not valid.
static int32_t aseprite__decode(aseprite__inflater* inflater,
                                const aseprite__huffman* huffman) {
  if (inflater->bit_count < 16) {
    aseprite__fill_bits(inflater);
  }
  uint32_t entry =
      huffman->fast[inflater->bits & ((1U << aseprite__fast_bits) - 1)];
  if (entry != 0) {
    uint32_t length = entry >> aseprite__fast_bits;
    inflater->bits >>= length;
    inflater->bit_count -= length;
    return (int32_t)(entry & ((1U << aseprite__fast_bits) - 1));
  }

  uint32_t code =
      aseprite__reverse_bits((uint32_t)(inflater->bits & 0xFFFF), 16);
  uint32_t length = aseprite__fast_bits + 1;
  while (length < 16 && code >= huffman->max_code[length]) {
    length++;
  }
  if (length >= 16) {
    return -1;
  }
  uint32_t index = (code >> (16 - length)) - huffman->first_code[length] +
                   huffman->first_symbol[length];
  if (index >= 288 || huffman->lengths[index] != length) {
    return -1;
  }
  inflater->bits >>= length;
  inflater->bit_count -= length;
  return huffman->symbols[index];
}

// Decodes the symbols of a Huffman block. Returns false for bad data.
static bool aseprite__inflate_codes(aseprite__inflater* inflater,
                                    const aseprite__huffman* literals,
                                    const aseprite__huffman* distances) {
  for (;;) {
    int32_t symbol = aseprite__decode(inflater, literals);
    if (symbol < 0) {
      return false;
    }
    if (symbol < 256) {
      if (inflater->out_pos == inflater->out_size) {
        return false;
      }
      inflater->out[inflater->out_pos++] = (uint8_t)symbol;
      continue;
    }
    if (symbol == 256) {
      return true;
    }

    uint32_t length_code = (uint32_t)symbol - 257;
    if (length_code >= 29) {
      return false;
    }
    size_t length =
        aseprite__length_base[length_code] +
        aseprite__bits(inflater, aseprite__length_extra[length_code]);

    int32_t distance_code = aseprite__decode(inflater, distances);
    if (distance_code < 0 || distance_code >= 30) {
      return false;
    }
    size_t distance =
        aseprite__distance_base[distance_code] +
        aseprite__bits(inflater, aseprite__distance_extra[distance_code]);

    if (distance > inflater->out_pos ||
        length > inflater->out_size - inflater->out_pos) {
      return false;
    }
    uint8_t* to         = inflater->out + inflater->out_pos;
    const uint8_t* from = to - distance;
    if (distance >= length) {
      memcpy(to, from, length);
    } else {
      for (size_t i = 0; i < length; i++) {
        to[i] = from[i];
      }
    }
    inflater->out_pos += length;
  }
}

// Copies a stored block. Returns false for bad data.
static bool aseprite__inflate_stored(aseprite__inflater* inflater) {
  (void)aseprite__bits(inflater, inflater->bit_count & 7);
  uint32_t length = aseprite__bits(inflater, 16);
  uint32_t check  = aseprite__bits(inflater, 16);
  if ((length ^ 0xFFFF) != check ||
      length > inflater->out_size - inflater->out_pos) {
    return false;
  }
  while (length > 0 && inflater->bit_count >= 8) {
    inflater->out[inflater->out_pos++] = (uint8_t)aseprite__bits(inflater, 8);
    length--;
  }
  if (length > 0) {
    if ((size_t)(inflater->in_end - inflater->in) < length) {
      return false;
    }
    memcpy(inflater->out + inflater->out_pos, inflater->in, length);
    inflater->in += length;
    inflater->out_pos += length;
  }
  return inflater->padding * 8 <= inflater->bit_count;
}

// Decodes a block with the fixed Huffman codes.
static bool aseprite__inflate_fixed(aseprite__inflater* inflater) {
  uint8_t lengths[288];
  memset(lengths, 8, 144);
  memset(lengths + 144, 9, 112);
  memset(lengths + 256, 7, 24);
  memset(lengths + 280, 8, 8);
  uint8_t distance_lengths[30];
  memset(distance_lengths, 5, sizeof distance_lengths);

  aseprite__huffman literals;
  aseprite__huffman distances;
  if (!aseprite__build_huffman(&literals, lengths, 288) ||
      !aseprite__build_huffman(&distances, distance_lengths, 30)) {
    return false;
  }
  return aseprite__inflate_codes(inflater, &literals, &distances);
}

// Decodes a block with dynamic Huffman codes.
static bool aseprite__inflate_dynamic(aseprite__inflater* inflater) {
  uint32_t literal_count     = aseprite__bits(inflater, 5) + 257;
  uint32_t distance_count    = aseprite__bits(inflater, 5) + 1;
  uint32_t code_length_count = aseprite__bits(inflater, 4) + 4;
  if (literal_count > 286) {
    return false;
  }

  uint8_t code_lengths[19] = {};
  for (uint32_t i = 0; i < code_length_count; i++) {
    code_lengths[aseprite__code_length_order[i]] =
        (uint8_t)aseprite__bits(inflater, 3);
  }
  aseprite__huffman code_huffman;
  if (!aseprite__build_huffman(&code_huffman, code_lengths, 19)) {
    return false;
  }

  uint8_t lengths[286 + 32] = {};
  uint32_t total            = literal_count + distance_count;
  uint32_t count            = 0;
  while (count < total) {
    int32_t symbol = aseprite__decode(inflater, &code_huffman);
    if (symbol < 0) {
      return false;
    }
    if (symbol < 16) {
      lengths[count++] = (uint8_t)symbol;
      continue;
    }
    uint8_t value   = 0;
    uint32_t repeat = 0;
    if (symbol == 16) {
      if (count == 0) {
        return false;
      }
      value  = lengths[count - 1];
      repeat = 3 + aseprite__bits(inflater, 2);
    } else if (symbol == 17) {
      repeat = 3 + aseprite__bits(inflater, 3);
    } else {
      repeat = 11 + aseprite__bits(inflater, 7);
    }
    if (repeat > total - count) {
      return false;
    }
    memset(lengths + count, value, repeat);
    count += repeat;
  }

  aseprite__huffman literals;
  aseprite__huffman distances;
  if (!aseprite__build_huffman(&literals, lengths, literal_count) ||
      !aseprite__build_huffman(&distances, lengths + literal_count,
                               distance_count)) {
    return false;
  }
  return aseprite__inflate_codes(inflater, &literals, &distances);
}

// Calculates the Adler-32 checksum of the data.
static uint32_t aseprite__adler32(const uint8_t* data, size_t size) {
  uint32_t a = 1;
  uint32_t b = 0;
  while (size > 0) {
    size_t block = size < 5552 ? size : 5552;
    size -= block;
    for (size_t i = 0; i < block; i++) {
      a += data[i];
      b += a;
    }
    data += block;
    a %= 65521;
    b %= 65521;
  }
  return (b << 16) | a;
}

// Decompresses a zlib stream. The output must fill out_size exactly.
static aseprite_result aseprite__inflate(const uint8_t* in, size_t in_size,
                                         uint8_t* out, size_t out_size) {
  if (in_size < 2) {
    return ASEPRITE_ERROR_COMPRESSION;
  }
  uint32_t method = in[0];
  uint32_t flags  = in[1];
  if ((method & 15) != 8 || (method >> 4) > 7 ||
      ((method << 8) | flags) % 31 != 0 || (flags & 32) != 0) {
    return ASEPRITE_ERROR_COMPRESSION;
  }

  aseprite__inflater inflater = {
      .in       = in + 2,
      .in_end   = in + in_size,
      .out      = out,
      .out_size = out_size,
  };
  bool last = false;
  while (!last) {
    last    = aseprite__bits(&inflater, 1) != 0;
    bool ok = false;
    switch (aseprite__bits(&inflater, 2)) {
    case 0:
      ok = aseprite__inflate_stored(&inflater);
      break;
    case 1:
      ok = aseprite__inflate_fixed(&inflater);
      break;
    case 2:
      ok = aseprite__inflate_dynamic(&inflater);
      break;
    default:
      break;
    }
    if (!ok) {
      return ASEPRITE_ERROR_COMPRESSION;
    }
  }
  if (inflater.out_pos != out_size) {
    return ASEPRITE_ERROR_COMPRESSION;
  }

  (void)aseprite__bits(&inflater, inflater.bit_count & 7);
  uint32_t checksum = 0;
  for (int i = 0; i < 4; i++) {
    checksum = (checksum << 8) | aseprite__bits(&inflater, 8);
  }
  if (inflater.padding * 8 > inflater.bit_count ||
      checksum != aseprite__adler32(out, out_size)) {
    return ASEPRITE_ERROR_COMPRESSION;
  }
  return ASEPRITE_OK;
}

/* ---- Frames and chunks ------------------------------------------------- */

typedef struct aseprite__frame_header {
  aseprite__reader chunks;
  uint32_t chunk_count;
  uint16_t duration;
} aseprite__frame_header;

typedef struct aseprite__chunk {
  uint16_t type;
  aseprite__reader data;
} aseprite__chunk;

// Reads a frame header and makes a reader for its chunks.
static aseprite_result
aseprite__read_frame_header(aseprite__reader* file,
                            aseprite__frame_header* out) {
  if (aseprite__remaining(file) < aseprite__frame_header_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  uint32_t size            = aseprite__u32(file);
  uint16_t magic           = aseprite__u16(file);
  uint16_t old_chunk_count = aseprite__u16(file);
  uint16_t duration        = aseprite__u16(file);
  aseprite__skip(file, 2);
  uint32_t chunk_count = aseprite__u32(file);
  if (magic != aseprite__frame_magic || size < aseprite__frame_header_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (size - aseprite__frame_header_size > aseprite__remaining(file)) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  *out = (aseprite__frame_header){
      .chunks = aseprite__sub_reader(file, size - aseprite__frame_header_size),
      .chunk_count = chunk_count != 0 ? chunk_count : old_chunk_count,
      .duration    = duration,
  };
  return ASEPRITE_OK;
}

// Reads a chunk header and makes a reader for its data.
static aseprite_result aseprite__read_chunk(aseprite__reader* frame,
                                            aseprite__chunk* out) {
  if (aseprite__remaining(frame) < aseprite__chunk_header_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  uint32_t size = aseprite__u32(frame);
  uint16_t type = aseprite__u16(frame);
  if (size < aseprite__chunk_header_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (size - aseprite__chunk_header_size > aseprite__remaining(frame)) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  *out = (aseprite__chunk){
      .type = type,
      .data = aseprite__sub_reader(frame, size - aseprite__chunk_header_size),
  };
  return ASEPRITE_OK;
}

/* ---- Parser ------------------------------------------------------------ */

// The object that gets the next user data chunk.
typedef enum aseprite__target : uint8_t {
  ASEPRITE__TARGET_NONE,
  ASEPRITE__TARGET_OBJECT,
  ASEPRITE__TARGET_TAGS,
  ASEPRITE__TARGET_TILESET,
  ASEPRITE__TARGET_TILES,
} aseprite__target;

typedef struct aseprite__parser {
  aseprite_sprite* sprite;
  aseprite__arena arena;
  aseprite__target target;
  aseprite_user_data* target_user_data;
  uint32_t next_tag;
  uint32_t end_tag;
  aseprite_tileset* tileset;
  uint32_t next_tile;
  aseprite_cel* last_cel;
  bool palette_chunk_seen;
  uint32_t palette_frame;
  uint32_t cel_capacity;
  uint32_t layer_capacity;
  uint32_t tag_capacity;
  uint32_t slice_capacity;
  uint32_t tileset_capacity;
  uint32_t external_file_capacity;
  uint32_t palette_capacity;
} aseprite__parser;

// Returns zeroed memory for count items, or nullptr on failure.
static void* aseprite__parser_alloc(aseprite__parser* parser, size_t count,
                                    size_t size) {
  return aseprite__alloc_array(&parser->arena, count, size);
}

// Reads a STRING into a new string that ends with a zero byte.
static aseprite_result aseprite__read_string(aseprite__parser* parser,
                                             aseprite__reader* reader,
                                             char** out) {
  uint16_t length     = aseprite__u16(reader);
  const uint8_t* data = aseprite__take(reader, length);
  if (!data) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  char* string = aseprite__alloc(&parser->arena, (size_t)length + 1);
  if (!string) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  memcpy(string, data, length);
  string[length] = '\0';
  *out           = string;
  return ASEPRITE_OK;
}

// Gets the size of a tile, or 0 when the size is not supported.
static uint32_t aseprite__bytes_per_tile(uint16_t bits) {
  switch (bits) {
  case 8:
  case 16:
  case 32:
    return bits / 8U;
  default:
    return 0;
  }
}

// Decompresses the rest of the reader into a new buffer of out_size bytes.
// The buffer has room for capacity bytes.
static aseprite_result aseprite__inflate_to(aseprite__parser* parser,
                                            aseprite__reader* reader,
                                            size_t out_size, size_t capacity,
                                            uint8_t** out) {
  size_t in_size = aseprite__remaining(reader);
  if (out_size / aseprite__max_inflate_ratio > in_size) {
    return ASEPRITE_ERROR_COMPRESSION;
  }
  uint8_t* buffer = aseprite__alloc(&parser->arena, capacity);
  if (!buffer) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  const uint8_t* in      = aseprite__take(reader, in_size);
  aseprite_result result = aseprite__inflate(in, in_size, buffer, out_size);
  if (result == ASEPRITE_OK) {
    *out = buffer;
  }
  return result;
}

// Calculates the size of an image in bytes.
static aseprite_result aseprite__image_size(const aseprite_sprite* sprite,
                                            uint32_t width, uint32_t height,
                                            size_t* out) {
  size_t size = 0;
  if (ckd_mul(&size, (size_t)width, (size_t)height) ||
      ckd_mul(&size, size, (size_t)aseprite_bytes_per_pixel(sprite->depth))) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  *out = size;
  return ASEPRITE_OK;
}

/* ---- Header ------------------------------------------------------------ */

// Reads and checks the 128-byte file header.
static aseprite_result aseprite__read_header(aseprite__reader* file,
                                             aseprite_sprite* sprite) {
  if (aseprite__remaining(file) >= 6 &&
      ((uint32_t)file->at[4] | ((uint32_t)file->at[5] << 8)) !=
          aseprite__file_magic) {
    return ASEPRITE_ERROR_NOT_ASEPRITE;
  }
  aseprite__reader header = aseprite__sub_reader(file, aseprite__header_size);
  if (header.truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }

  aseprite__skip(&header, 6);
  sprite->frame_count = aseprite__u16(&header);
  sprite->width       = aseprite__u16(&header);
  sprite->height      = aseprite__u16(&header);
  sprite->depth       = (aseprite_color_depth)aseprite__u16(&header);
  sprite->flags       = aseprite__u32(&header);
  sprite->speed       = aseprite__u16(&header);
  aseprite__skip(&header, 8);
  uint8_t transparent_index = aseprite__u8(&header);
  aseprite__skip(&header, 3);
  uint16_t color_count = aseprite__u16(&header);
  uint8_t pixel_width  = aseprite__u8(&header);
  uint8_t pixel_height = aseprite__u8(&header);
  sprite->grid_x       = aseprite__i16(&header);
  sprite->grid_y       = aseprite__i16(&header);
  sprite->grid_width   = aseprite__u16(&header);
  sprite->grid_height  = aseprite__u16(&header);

  if (sprite->depth != ASEPRITE_DEPTH_INDEXED &&
      sprite->depth != ASEPRITE_DEPTH_GRAYSCALE &&
      sprite->depth != ASEPRITE_DEPTH_RGBA) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (sprite->width == 0 || sprite->height == 0 || sprite->frame_count == 0) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  sprite->transparent_index =
      sprite->depth == ASEPRITE_DEPTH_INDEXED ? transparent_index : 0;
  sprite->color_count = color_count != 0 ? color_count : 256;
  if (pixel_width == 0 || pixel_height == 0) {
    pixel_width  = 1;
    pixel_height = 1;
  }
  sprite->pixel_width  = pixel_width;
  sprite->pixel_height = pixel_height;
  return ASEPRITE_OK;
}

/* ---- First pass: count the objects ------------------------------------- */

// Adds value to a count. Fails when the count overflows.
static aseprite_result aseprite__add_count(uint32_t* count, size_t value) {
  if (value > UINT32_MAX || ckd_add(count, *count, (uint32_t)value)) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  return ASEPRITE_OK;
}

// Counts the objects in one chunk.
static aseprite_result aseprite__count_chunk(aseprite__parser* parser,
                                             aseprite_frame* frame,
                                             aseprite__chunk* chunk,
                                             bool* palette_changes) {
  aseprite__reader* data = &chunk->data;
  switch (chunk->type) {
  case ASEPRITE__CHUNK_OLD_PALETTE:
  case ASEPRITE__CHUNK_OLD_PALETTE_64:
    *palette_changes = *palette_changes || !parser->palette_chunk_seen;
    return ASEPRITE_OK;
  case ASEPRITE__CHUNK_PALETTE:
    parser->palette_chunk_seen = true;
    *palette_changes           = true;
    return ASEPRITE_OK;
  case ASEPRITE__CHUNK_LAYER:
    return aseprite__add_count(&parser->layer_capacity, 1);
  case ASEPRITE__CHUNK_CEL:
    return aseprite__add_count(&frame->cel_count, 1);
  case ASEPRITE__CHUNK_SLICE:
    return aseprite__add_count(&parser->slice_capacity, 1);
  case ASEPRITE__CHUNK_TILESET:
    return aseprite__add_count(&parser->tileset_capacity, 1);
  case ASEPRITE__CHUNK_TAGS: {
    uint16_t count = aseprite__u16(data);
    aseprite__skip(data, 8);
    if (count > aseprite__remaining(data) / aseprite__min_tag_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    return aseprite__add_count(&parser->tag_capacity, count);
  }
  case ASEPRITE__CHUNK_EXTERNAL_FILES: {
    uint32_t count = aseprite__u32(data);
    aseprite__skip(data, 8);
    if (count > aseprite__remaining(data) / aseprite__min_external_file_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    return aseprite__add_count(&parser->external_file_capacity, count);
  }
  default:
    return ASEPRITE_OK;
  }
}

// Checks the frame and chunk sizes and counts the objects.
static aseprite_result aseprite__count(aseprite__parser* parser,
                                       aseprite__reader file) {
  aseprite_sprite* sprite  = parser->sprite;
  parser->palette_capacity = 1;
  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    aseprite__frame_header header;
    aseprite_result result = aseprite__read_frame_header(&file, &header);
    if (result != ASEPRITE_OK) {
      return result;
    }
    bool palette_changes = false;
    for (uint32_t j = 0; j < header.chunk_count; j++) {
      aseprite__chunk chunk;
      result = aseprite__read_chunk(&header.chunks, &chunk);
      if (result == ASEPRITE_OK) {
        result = aseprite__count_chunk(parser, &sprite->frames[i], &chunk,
                                       &palette_changes);
      }
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
    if (i > 0 && palette_changes) {
      parser->palette_capacity++;
    }
  }
  parser->palette_chunk_seen = false;
  return ASEPRITE_OK;
}

/* ---- Second pass: read the chunks -------------------------------------- */

// Copies a palette to new arrays of count colors. New colors are black.
static aseprite_result aseprite__resize_palette(aseprite__parser* parser,
                                                aseprite_palette* palette,
                                                uint32_t count) {
  aseprite_color* colors =
      aseprite__parser_alloc(parser, count, sizeof *colors);
  if (!colors) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  uint32_t kept = palette->count < count ? palette->count : count;
  if (kept > 0) {
    memcpy(colors, palette->colors, kept * sizeof *colors);
  }
  for (uint32_t i = kept; i < count; i++) {
    colors[i] = (aseprite_color){.a = 255};
  }

  char** names = nullptr;
  if (palette->names) {
    names = aseprite__parser_alloc(parser, count, sizeof *names);
    if (!names) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    if (kept > 0) {
      memcpy(names, palette->names, kept * sizeof *names);
    }
  }

  *palette =
      (aseprite_palette){.count = count, .colors = colors, .names = names};
  return ASEPRITE_OK;
}

// Gets the palette that a palette chunk in this frame changes.
static aseprite_result aseprite__frame_palette(aseprite__parser* parser,
                                               uint32_t frame_index,
                                               aseprite_palette** out) {
  aseprite_sprite* sprite   = parser->sprite;
  aseprite_palette* palette = &sprite->palettes[sprite->palette_count - 1];
  if (frame_index != parser->palette_frame) {
    ASEPRITE_ASSERT(sprite->palette_count < parser->palette_capacity);
    aseprite_palette* copy = &sprite->palettes[sprite->palette_count];
    *copy                  = *palette;
    aseprite_result result =
        aseprite__resize_palette(parser, copy, palette->count);
    if (result != ASEPRITE_OK) {
      return result;
    }
    sprite->frames[frame_index].palette = sprite->palette_count;
    sprite->palette_count++;
    parser->palette_frame = frame_index;
    palette               = copy;
  }
  *out = palette;
  return ASEPRITE_OK;
}

// Reads an old palette chunk (0x0004 or 0x000B).
static aseprite_result aseprite__read_old_palette(aseprite__parser* parser,
                                                  uint32_t frame_index,
                                                  aseprite__reader* reader,
                                                  bool six_bit) {
  if (parser->palette_chunk_seen) {
    return ASEPRITE_OK;
  }
  aseprite_palette* palette = nullptr;
  aseprite_result result =
      aseprite__frame_palette(parser, frame_index, &palette);
  if (result != ASEPRITE_OK) {
    return result;
  }

  uint16_t packet_count = aseprite__u16(reader);
  uint32_t index        = 0;
  for (uint16_t i = 0; i < packet_count; i++) {
    index += aseprite__u8(reader);
    uint32_t count = aseprite__u8(reader);
    if (count == 0) {
      count = 256;
    }
    if (reader->truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    if (index + count > 256) {
      return ASEPRITE_ERROR_MALFORMED;
    }
    if (index + count > palette->count) {
      result = aseprite__resize_palette(parser, palette, index + count);
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
    for (uint32_t j = 0; j < count; j++) {
      uint8_t rgb[3] = {};
      for (int k = 0; k < 3; k++) {
        uint32_t value = aseprite__u8(reader);
        if (six_bit) {
          value &= 63U;
          value = (value << 2U) | (value >> 4U);
        }
        rgb[k] = (uint8_t)value;
      }
      palette->colors[index + j] =
          (aseprite_color){.r = rgb[0], .g = rgb[1], .b = rgb[2], .a = 255};
    }
    index += count;
  }
  return ASEPRITE_OK;
}

// Reads a palette chunk (0x2019).
static aseprite_result aseprite__read_palette(aseprite__parser* parser,
                                              uint32_t frame_index,
                                              aseprite__reader* reader) {
  parser->palette_chunk_seen = true;
  uint32_t size              = aseprite__u32(reader);
  uint32_t first             = aseprite__u32(reader);
  uint32_t last              = aseprite__u32(reader);
  aseprite__skip(reader, 8);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (size > aseprite__max_palette_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  aseprite_palette* palette = nullptr;
  aseprite_result result =
      aseprite__frame_palette(parser, frame_index, &palette);
  if (result == ASEPRITE_OK && size > 0) {
    result = aseprite__resize_palette(parser, palette, size);
  }
  if (result != ASEPRITE_OK || first > last) {
    return result;
  }
  if (last >= palette->count) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  for (uint32_t i = first; i <= last; i++) {
    uint16_t flags        = aseprite__u16(reader);
    aseprite_color* color = &palette->colors[i];
    color->r              = aseprite__u8(reader);
    color->g              = aseprite__u8(reader);
    color->b              = aseprite__u8(reader);
    color->a              = aseprite__u8(reader);
    if (reader->truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    if ((flags & 1) == 0) {
      continue;
    }
    if (!palette->names) {
      palette->names = aseprite__parser_alloc(parser, palette->count,
                                              sizeof *palette->names);
      if (!palette->names) {
        return ASEPRITE_ERROR_NO_MEMORY;
      }
    }
    result = aseprite__read_string(parser, reader, &palette->names[i]);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Sets the user data that the next user data chunk fills.
static void aseprite__set_target(aseprite__parser* parser,
                                 aseprite_user_data* user_data) {
  parser->target = user_data ? ASEPRITE__TARGET_OBJECT : ASEPRITE__TARGET_NONE;
  parser->target_user_data = user_data;
}

// Finds the index of the tileset with an ID.
static aseprite_result aseprite__find_tileset(const aseprite_sprite* sprite,
                                              uint32_t id, uint32_t* out) {
  for (uint32_t i = 0; i < sprite->tileset_count; i++) {
    if (sprite->tilesets[i].id == id) {
      *out = i;
      return ASEPRITE_OK;
    }
  }
  return ASEPRITE_ERROR_MALFORMED;
}

// Gets the parent group of a new layer from its child level.
static int32_t aseprite__layer_parent(const aseprite_sprite* sprite,
                                      uint16_t child_level) {
  if (sprite->layer_count == 0) {
    return -1;
  }
  uint32_t previous_index        = sprite->layer_count - 1;
  const aseprite_layer* previous = &sprite->layers[previous_index];
  if (child_level > previous->child_level) {
    return (int32_t)previous_index;
  }
  int32_t parent = previous->parent;
  for (uint32_t up = previous->child_level - child_level; up > 0 && parent >= 0;
       up--) {
    parent = sprite->layers[parent].parent;
  }
  return parent;
}

// Reads a layer chunk.
static aseprite_result aseprite__read_layer(aseprite__parser* parser,
                                            aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->layer_count < parser->layer_capacity);
  aseprite_layer* layer = &sprite->layers[sprite->layer_count];

  layer->flags       = aseprite__u16(reader);
  layer->type        = (aseprite_layer_type)aseprite__u16(reader);
  layer->child_level = aseprite__u16(reader);
  aseprite__skip(reader, 4);
  uint16_t blend_mode = aseprite__u16(reader);
  uint8_t opacity     = aseprite__u8(reader);
  aseprite__skip(reader, 3);
  aseprite_result result = aseprite__read_string(parser, reader, &layer->name);
  if (result != ASEPRITE_OK) {
    return result;
  }

  if (layer->type == ASEPRITE_LAYER_TYPE_TILEMAP) {
    uint32_t id = aseprite__u32(reader);
    if (reader->truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    result = aseprite__find_tileset(sprite, id, &layer->tileset);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  if (layer->type <= ASEPRITE_LAYER_TYPE_TILEMAP &&
      (sprite->flags & ASEPRITE_SPRITE_FLAG_LAYER_UUID) != 0) {
    const uint8_t* uuid = aseprite__take(reader, sizeof layer->uuid);
    if (!uuid) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    memcpy(layer->uuid, uuid, sizeof layer->uuid);
  }

  bool is_group = layer->type == ASEPRITE_LAYER_TYPE_GROUP;
  bool blends =
      (layer->flags & ASEPRITE_LAYER_FLAG_BACKGROUND) == 0 &&
      (!is_group || (sprite->flags & ASEPRITE_SPRITE_FLAG_GROUP_BLEND) != 0);
  bool has_opacity =
      blends && (sprite->flags & ASEPRITE_SPRITE_FLAG_LAYER_OPACITY) != 0;
  layer->blend_mode =
      blends ? (aseprite_blend_mode)blend_mode : ASEPRITE_BLEND_NORMAL;
  layer->opacity = has_opacity ? opacity : 255;

  if (sprite->layer_count > 0) {
    const aseprite_layer* previous = &sprite->layers[sprite->layer_count - 1];
    if (layer->child_level > previous->child_level &&
        previous->type != ASEPRITE_LAYER_TYPE_GROUP) {
      return ASEPRITE_ERROR_MALFORMED;
    }
  }
  layer->parent = aseprite__layer_parent(sprite, layer->child_level);
  sprite->layer_count++;
  aseprite__set_target(parser, &layer->user_data);
  return ASEPRITE_OK;
}

// Reads the image of a raw or compressed cel.
static aseprite_result aseprite__read_image_cel(aseprite__parser* parser,
                                                aseprite__reader* reader,
                                                aseprite_cel* cel) {
  aseprite_sprite* sprite = parser->sprite;
  uint16_t width          = aseprite__u16(reader);
  uint16_t height         = aseprite__u16(reader);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (sprite->layers[cel->layer].type != ASEPRITE_LAYER_TYPE_IMAGE) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (width == 0 || height == 0) {
    return ASEPRITE_OK;
  }

  size_t size            = 0;
  aseprite_result result = aseprite__image_size(sprite, width, height, &size);
  if (result != ASEPRITE_OK) {
    return result;
  }
  uint8_t* pixels = nullptr;
  if (cel->type == ASEPRITE_CEL_TYPE_RAW) {
    const uint8_t* data = aseprite__take(reader, size);
    if (!data) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    pixels = aseprite__alloc(&parser->arena, size);
    if (!pixels) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    memcpy(pixels, data, size);
  } else {
    result = aseprite__inflate_to(parser, reader, size, size, &pixels);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  cel->image =
      (aseprite_image){.width = width, .height = height, .pixels = pixels};
  return ASEPRITE_OK;
}

// Reads the tiles of a compressed tilemap cel.
static aseprite_result aseprite__read_tilemap_cel(aseprite__parser* parser,
                                                  aseprite__reader* reader,
                                                  aseprite_cel* cel) {
  aseprite_tilemap* tilemap   = &cel->tilemap;
  tilemap->width              = aseprite__u16(reader);
  tilemap->height             = aseprite__u16(reader);
  tilemap->bits_per_tile      = aseprite__u16(reader);
  tilemap->id_mask            = aseprite__u32(reader);
  tilemap->x_flip_mask        = aseprite__u32(reader);
  tilemap->y_flip_mask        = aseprite__u32(reader);
  tilemap->diagonal_flip_mask = aseprite__u32(reader);
  aseprite__skip(reader, 10);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (parser->sprite->layers[cel->layer].type != ASEPRITE_LAYER_TYPE_TILEMAP) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  uint32_t tile_size = aseprite__bytes_per_tile(tilemap->bits_per_tile);
  if (tilemap->width == 0 || tilemap->height == 0 || tile_size == 0) {
    return ASEPRITE_OK;
  }

  size_t count    = 0;
  size_t out_size = 0;
  size_t capacity = 0;
  if (ckd_mul(&count, (size_t)tilemap->width, (size_t)tilemap->height) ||
      ckd_mul(&out_size, count, (size_t)tile_size) ||
      ckd_mul(&capacity, count, sizeof *tilemap->tiles)) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  uint8_t* bytes = nullptr;
  aseprite_result result =
      aseprite__inflate_to(parser, reader, out_size, capacity, &bytes);
  if (result != ASEPRITE_OK) {
    return result;
  }

  // Widen the tiles in place. Go back to front so no tile is lost.
  uint32_t* tiles = (uint32_t*)(void*)bytes;
  for (size_t i = count; i-- > 0;) {
    const uint8_t* tile = bytes + (i * tile_size);
    uint32_t value      = 0;
    for (uint32_t k = tile_size; k-- > 0;) {
      value = (value << 8U) | (uint32_t)tile[k];
    }
    tiles[i] = value;
  }
  tilemap->tiles = tiles;
  return ASEPRITE_OK;
}

// Reads a linked cel and shares the data of its source.
static aseprite_result aseprite__read_linked_cel(aseprite__parser* parser,
                                                 uint32_t frame_index,
                                                 aseprite__reader* reader,
                                                 aseprite_cel* cel) {
  cel->linked_frame = aseprite__u16(reader);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (cel->linked_frame >= frame_index) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  const aseprite_cel* source = aseprite_frame_cel(
      &parser->sprite->frames[cel->linked_frame], cel->layer);
  if (!source) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  cel->image   = source->image;
  cel->tilemap = source->tilemap;
  return ASEPRITE_OK;
}

// Reads a cel chunk. It skips empty cels and unknown cel types.
static aseprite_result aseprite__read_cel(aseprite__parser* parser,
                                          uint32_t frame_index,
                                          aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  aseprite_cel cel        = {};
  cel.layer               = aseprite__u16(reader);
  cel.x                   = aseprite__i16(reader);
  cel.y                   = aseprite__i16(reader);
  cel.opacity             = aseprite__u8(reader);
  cel.type                = (aseprite_cel_type)aseprite__u16(reader);
  cel.z_index             = aseprite__i16(reader);
  aseprite__skip(reader, 5);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (cel.layer >= sprite->layer_count) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  aseprite_result result = ASEPRITE_OK;
  switch (cel.type) {
  case ASEPRITE_CEL_TYPE_RAW:
  case ASEPRITE_CEL_TYPE_COMPRESSED_IMAGE:
    result = aseprite__read_image_cel(parser, reader, &cel);
    break;
  case ASEPRITE_CEL_TYPE_LINKED:
    result = aseprite__read_linked_cel(parser, frame_index, reader, &cel);
    break;
  case ASEPRITE_CEL_TYPE_COMPRESSED_TILEMAP:
    result = aseprite__read_tilemap_cel(parser, reader, &cel);
    break;
  default:
    break;
  }
  if (result != ASEPRITE_OK) {
    return result;
  }
  if (!cel.image.pixels && !cel.tilemap.tiles) {
    aseprite__set_target(parser, nullptr);
    return ASEPRITE_OK;
  }

  aseprite_frame* frame = &sprite->frames[frame_index];
  ASEPRITE_ASSERT(frame->cel_count < parser->cel_capacity);
  aseprite_cel* stored = &frame->cels[frame->cel_count++];
  *stored              = cel;
  parser->last_cel     = stored;
  aseprite__set_target(parser, &stored->user_data);
  return ASEPRITE_OK;
}

// Reads a cel extra chunk into the last cel.
static aseprite_result aseprite__read_cel_extra(aseprite__parser* parser,
                                                aseprite__reader* reader) {
  uint32_t flags = aseprite__u32(reader);
  if ((flags & 1) == 0) {
    return ASEPRITE_OK;
  }
  aseprite_fixed x      = aseprite__i32(reader);
  aseprite_fixed y      = aseprite__i32(reader);
  aseprite_fixed width  = aseprite__i32(reader);
  aseprite_fixed height = aseprite__i32(reader);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  aseprite_cel* cel = parser->last_cel;
  if (cel && width != 0 && height != 0) {
    cel->has_precise_bounds = true;
    cel->precise_x          = x;
    cel->precise_y          = y;
    cel->precise_width      = width;
    cel->precise_height     = height;
  }
  return ASEPRITE_OK;
}

// Reads a color profile chunk.
static aseprite_result aseprite__read_color_profile(aseprite__parser* parser,
                                                    aseprite__reader* reader) {
  aseprite_color_profile* profile = &parser->sprite->color_profile;
  *profile                        = (aseprite_color_profile){};
  profile->type  = (aseprite_color_profile_type)aseprite__u16(reader);
  profile->flags = aseprite__u16(reader);
  profile->gamma = aseprite__i32(reader);
  if (profile->type == ASEPRITE_COLOR_PROFILE_ICC) {
    aseprite__skip(reader, 8);
    uint32_t size       = aseprite__u32(reader);
    const uint8_t* data = aseprite__take(reader, size);
    if (!data) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    profile->icc = aseprite__alloc(&parser->arena, size);
    if (!profile->icc) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    memcpy(profile->icc, data, size);
    profile->icc_size = size;
  }
  return reader->truncated ? ASEPRITE_ERROR_TRUNCATED : ASEPRITE_OK;
}

// Reads an external files chunk.
static aseprite_result aseprite__read_external_files(aseprite__parser* parser,
                                                     aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  uint32_t count          = aseprite__u32(reader);
  aseprite__skip(reader, 8);
  ASEPRITE_ASSERT(count <=
                  parser->external_file_capacity - sprite->external_file_count);
  for (uint32_t i = 0; i < count; i++) {
    aseprite_external_file* file =
        &sprite->external_files[sprite->external_file_count++];
    file->id   = aseprite__u32(reader);
    file->type = (aseprite_external_file_type)aseprite__u8(reader);
    aseprite__skip(reader, 7);
    aseprite_result result = aseprite__read_string(parser, reader, &file->name);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Reads a tags chunk.
static aseprite_result aseprite__read_tags(aseprite__parser* parser,
                                           aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  uint16_t count          = aseprite__u16(reader);
  aseprite__skip(reader, 8);
  ASEPRITE_ASSERT(count <= parser->tag_capacity - sprite->tag_count);
  uint32_t first = sprite->tag_count;
  for (uint16_t i = 0; i < count; i++) {
    aseprite_tag* tag = &sprite->tags[sprite->tag_count++];
    tag->from         = aseprite__u16(reader);
    tag->to           = aseprite__u16(reader);
    uint8_t direction = aseprite__u8(reader);
    tag->direction    = direction <= ASEPRITE_DIRECTION_PING_PONG_REVERSE
                            ? (aseprite_direction)direction
                            : ASEPRITE_DIRECTION_FORWARD;
    tag->repeat       = aseprite__u16(reader);
    aseprite__skip(reader, 6);
    tag->color.r = aseprite__u8(reader);
    tag->color.g = aseprite__u8(reader);
    tag->color.b = aseprite__u8(reader);
    tag->color.a = 255;
    aseprite__skip(reader, 1);
    aseprite_result result = aseprite__read_string(parser, reader, &tag->name);
    if (result != ASEPRITE_OK) {
      return result;
    }
    if (tag->from > tag->to || tag->to >= sprite->frame_count) {
      return ASEPRITE_ERROR_MALFORMED;
    }
  }
  if (count == 0) {
    aseprite__set_target(parser, nullptr);
  } else {
    parser->target   = ASEPRITE__TARGET_TAGS;
    parser->next_tag = first;
    parser->end_tag  = sprite->tag_count;
  }
  return ASEPRITE_OK;
}

// Reads a slice chunk.
static aseprite_result aseprite__read_slice(aseprite__parser* parser,
                                            aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->slice_count < parser->slice_capacity);
  aseprite_slice* slice = &sprite->slices[sprite->slice_count];
  slice->key_count      = aseprite__u32(reader);
  slice->flags          = aseprite__u32(reader);
  aseprite__skip(reader, 4);
  aseprite_result result = aseprite__read_string(parser, reader, &slice->name);
  if (result != ASEPRITE_OK) {
    return result;
  }

  bool nine_patch = (slice->flags & ASEPRITE_SLICE_FLAG_NINE_PATCH) != 0;
  bool pivot      = (slice->flags & ASEPRITE_SLICE_FLAG_PIVOT) != 0;
  size_t key_size =
      aseprite__min_slice_key_size + (nine_patch ? 16 : 0) + (pivot ? 8 : 0);
  if (slice->key_count > aseprite__remaining(reader) / key_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  slice->keys =
      aseprite__parser_alloc(parser, slice->key_count, sizeof *slice->keys);
  if (!slice->keys) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < slice->key_count; i++) {
    aseprite_slice_key* key = &slice->keys[i];
    key->frame              = aseprite__u32(reader);
    key->x                  = aseprite__i32(reader);
    key->y                  = aseprite__i32(reader);
    key->width              = aseprite__u32(reader);
    key->height             = aseprite__u32(reader);
    if (nine_patch) {
      key->center_x      = aseprite__i32(reader);
      key->center_y      = aseprite__i32(reader);
      key->center_width  = aseprite__u32(reader);
      key->center_height = aseprite__u32(reader);
    }
    if (pivot) {
      key->pivot_x = aseprite__i32(reader);
      key->pivot_y = aseprite__i32(reader);
    }
  }
  sprite->slice_count++;
  aseprite__set_target(parser, &slice->user_data);
  return ASEPRITE_OK;
}

// Reads a tileset chunk.
static aseprite_result aseprite__read_tileset(aseprite__parser* parser,
                                              aseprite__reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->tileset_count < parser->tileset_capacity);
  aseprite_tileset* tileset = &sprite->tilesets[sprite->tileset_count];
  tileset->id               = aseprite__u32(reader);
  tileset->flags            = aseprite__u32(reader);
  tileset->tile_count       = aseprite__u32(reader);
  tileset->tile_width       = aseprite__u16(reader);
  tileset->tile_height      = aseprite__u16(reader);
  tileset->base_index       = aseprite__i16(reader);
  aseprite__skip(reader, 14);
  aseprite_result result =
      aseprite__read_string(parser, reader, &tileset->name);
  if (result != ASEPRITE_OK) {
    return result;
  }
  if (tileset->tile_width == 0 || tileset->tile_height == 0) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  if ((tileset->flags & ASEPRITE_TILESET_FLAG_EXTERNAL) != 0) {
    tileset->external_file    = aseprite__u32(reader);
    tileset->external_tileset = aseprite__u32(reader);
  }
  if ((tileset->flags & ASEPRITE_TILESET_FLAG_EMBEDDED) != 0 &&
      tileset->tile_count > 0) {
    uint32_t length       = aseprite__u32(reader);
    aseprite__reader data = aseprite__sub_reader(reader, length);
    if (data.truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    uint32_t height = 0;
    size_t size     = 0;
    if (ckd_mul(&height, (uint32_t)tileset->tile_height, tileset->tile_count)) {
      return ASEPRITE_ERROR_MALFORMED;
    }
    result = aseprite__image_size(sprite, tileset->tile_width, height, &size);
    if (result == ASEPRITE_OK) {
      result = aseprite__inflate_to(parser, &data, size, size,
                                    &tileset->image.pixels);
    }
    if (result != ASEPRITE_OK) {
      return result;
    }
    tileset->image.width  = tileset->tile_width;
    tileset->image.height = height;
  }
  sprite->tileset_count++;
  parser->target  = ASEPRITE__TARGET_TILESET;
  parser->tileset = tileset;
  return ASEPRITE_OK;
}

// Gets the smallest size of a property value, or 0 for an unknown type.
static size_t aseprite__property_min_size(uint16_t type) {
  switch (type) {
  case ASEPRITE_PROPERTY_TYPE_BOOL:
  case ASEPRITE_PROPERTY_TYPE_INT8:
  case ASEPRITE_PROPERTY_TYPE_UINT8:
    return 1;
  case ASEPRITE_PROPERTY_TYPE_INT16:
  case ASEPRITE_PROPERTY_TYPE_UINT16:
  case ASEPRITE_PROPERTY_TYPE_STRING:
    return 2;
  case ASEPRITE_PROPERTY_TYPE_INT32:
  case ASEPRITE_PROPERTY_TYPE_UINT32:
  case ASEPRITE_PROPERTY_TYPE_FIXED:
  case ASEPRITE_PROPERTY_TYPE_FLOAT:
  case ASEPRITE_PROPERTY_TYPE_MAP:
    return 4;
  case ASEPRITE_PROPERTY_TYPE_VECTOR:
    return 6;
  case ASEPRITE_PROPERTY_TYPE_INT64:
  case ASEPRITE_PROPERTY_TYPE_UINT64:
  case ASEPRITE_PROPERTY_TYPE_DOUBLE:
  case ASEPRITE_PROPERTY_TYPE_POINT:
  case ASEPRITE_PROPERTY_TYPE_SIZE:
    return 8;
  case ASEPRITE_PROPERTY_TYPE_RECT:
  case ASEPRITE_PROPERTY_TYPE_UUID:
    return 16;
  default:
    return 0;
  }
}

static aseprite_result aseprite__read_properties(aseprite__parser* parser,
                                                 aseprite__reader* reader,
                                                 uint32_t depth,
                                                 aseprite_property* out);

// Reads a property value of a type.
static aseprite_result aseprite__read_value(aseprite__parser* parser,
                                            aseprite__reader* reader,
                                            uint16_t type, uint32_t depth,
                                            aseprite_property* out) {
  if (depth > aseprite__max_property_depth) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  out->type = (aseprite_property_type)type;
  switch (type) {
  case ASEPRITE_PROPERTY_TYPE_BOOL:
    out->value.boolean = aseprite__u8(reader) != 0;
    break;
  case ASEPRITE_PROPERTY_TYPE_INT8:
    out->value.signed_int = aseprite__i8(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT8:
    out->value.unsigned_int = aseprite__u8(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT16:
    out->value.signed_int = aseprite__i16(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT16:
    out->value.unsigned_int = aseprite__u16(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT32:
    out->value.signed_int = aseprite__i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT32:
    out->value.unsigned_int = aseprite__u32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT64:
    out->value.signed_int = aseprite__i64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT64:
    out->value.unsigned_int = aseprite__u64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_FIXED:
    out->value.fixed = aseprite__i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_FLOAT:
    out->value.float32 = aseprite__f32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_DOUBLE:
    out->value.float64 = aseprite__f64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_STRING:
    return aseprite__read_string(parser, reader, &out->value.string);
  case ASEPRITE_PROPERTY_TYPE_POINT:
    out->value.point.x = aseprite__i32(reader);
    out->value.point.y = aseprite__i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_SIZE:
    out->value.size.width  = aseprite__i32(reader);
    out->value.size.height = aseprite__i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_RECT:
    out->value.rect.x      = aseprite__i32(reader);
    out->value.rect.y      = aseprite__i32(reader);
    out->value.rect.width  = aseprite__i32(reader);
    out->value.rect.height = aseprite__i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_VECTOR: {
    uint32_t count     = aseprite__u32(reader);
    uint16_t item_type = aseprite__u16(reader);
    size_t item_size = item_type == 0 ? aseprite__min_mixed_item_size
                                      : aseprite__property_min_size(item_type);
    if (item_size == 0) {
      return ASEPRITE_ERROR_MALFORMED;
    }
    if (count > aseprite__remaining(reader) / item_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    aseprite_property* items =
        aseprite__parser_alloc(parser, count, sizeof *items);
    if (!items) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    out->value.list.count = count;
    out->value.list.items = items;
    for (uint32_t i = 0; i < count; i++) {
      uint16_t type_of_item =
          item_type == 0 ? aseprite__u16(reader) : item_type;
      aseprite_result result = aseprite__read_value(
          parser, reader, type_of_item, depth + 1, &items[i]);
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
    break;
  }
  case ASEPRITE_PROPERTY_TYPE_MAP:
    return aseprite__read_properties(parser, reader, depth, out);
  case ASEPRITE_PROPERTY_TYPE_UUID: {
    const uint8_t* uuid = aseprite__take(reader, sizeof out->value.uuid);
    if (uuid) {
      memcpy(out->value.uuid, uuid, sizeof out->value.uuid);
    }
    break;
  }
  default:
    return ASEPRITE_ERROR_MALFORMED;
  }
  return reader->truncated ? ASEPRITE_ERROR_TRUNCATED : ASEPRITE_OK;
}

// Reads a count and the named properties of a map into out->value.list.
static aseprite_result aseprite__read_properties(aseprite__parser* parser,
                                                 aseprite__reader* reader,
                                                 uint32_t depth,
                                                 aseprite_property* out) {
  uint32_t count = aseprite__u32(reader);
  if (count > aseprite__remaining(reader) / aseprite__min_property_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  aseprite_property* properties =
      aseprite__parser_alloc(parser, count, sizeof *properties);
  if (!properties) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  out->value.list.count = count;
  out->value.list.items = properties;
  for (uint32_t i = 0; i < count; i++) {
    aseprite_result result =
        aseprite__read_string(parser, reader, &properties[i].name);
    if (result != ASEPRITE_OK) {
      return result;
    }
    uint16_t type = aseprite__u16(reader);
    result =
        aseprite__read_value(parser, reader, type, depth + 1, &properties[i]);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Reads the property maps of a user data chunk.
static aseprite_result aseprite__read_property_maps(aseprite__parser* parser,
                                                    aseprite__reader* reader,
                                                    aseprite_user_data* out) {
  uint32_t count = aseprite__u32(reader);
  if (count > aseprite__remaining(reader) / aseprite__min_map_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  aseprite_property_map* maps =
      aseprite__parser_alloc(parser, count, sizeof *maps);
  if (!maps) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < count; i++) {
    maps[i].key            = aseprite__u32(reader);
    aseprite_property map  = {};
    aseprite_result result = aseprite__read_properties(parser, reader, 0, &map);
    if (result != ASEPRITE_OK) {
      return result;
    }
    maps[i].count      = map.value.list.count;
    maps[i].properties = map.value.list.items;
  }
  out->map_count = count;
  out->maps      = maps;
  return ASEPRITE_OK;
}

// Reads user data. It drops properties that are not valid.
static aseprite_result aseprite__read_user_data(aseprite__parser* parser,
                                                aseprite__reader* reader,
                                                aseprite_user_data* out) {
  *out = (aseprite_user_data){.flags = aseprite__u32(reader)};
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_TEXT) != 0) {
    aseprite_result result = aseprite__read_string(parser, reader, &out->text);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_COLOR) != 0) {
    out->color.r = aseprite__u8(reader);
    out->color.g = aseprite__u8(reader);
    out->color.b = aseprite__u8(reader);
    out->color.a = aseprite__u8(reader);
  }
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_PROPERTIES) == 0) {
    return ASEPRITE_OK;
  }

  // Aseprite keeps the sprite when the properties are not valid. So does
  // this reader: it drops the properties and continues.
  uint32_t size = aseprite__u32(reader);
  if (size < 4) {
    return ASEPRITE_OK;
  }
  size_t available            = aseprite__remaining(reader);
  aseprite__reader properties = aseprite__reader_make(
      reader->at, size - 4 < available ? size - 4 : available);
  aseprite_result result =
      aseprite__read_property_maps(parser, &properties, out);
  if (result == ASEPRITE_ERROR_NO_MEMORY) {
    return result;
  }
  if (result != ASEPRITE_OK) {
    out->map_count = 0;
    out->maps      = nullptr;
  }
  return ASEPRITE_OK;
}

// Counts the user data chunks that come next in the frame.
static uint32_t aseprite__count_user_data(aseprite__reader frame,
                                          uint32_t chunk_count) {
  uint32_t count = 0;
  for (; count < chunk_count; count++) {
    aseprite__chunk chunk;
    if (aseprite__read_chunk(&frame, &chunk) != ASEPRITE_OK ||
        chunk.type != ASEPRITE__CHUNK_USER_DATA) {
      break;
    }
  }
  return count;
}

// Reads a user data chunk into the object that gets it.
static aseprite_result aseprite__read_user_data_chunk(aseprite__parser* parser,
                                                      aseprite__reader* reader,
                                                      aseprite__reader frame,
                                                      uint32_t chunks_left) {
  aseprite_sprite* sprite       = parser->sprite;
  aseprite_tileset* tileset     = parser->tileset;
  aseprite_user_data* user_data = nullptr;
  switch (parser->target) {
  case ASEPRITE__TARGET_NONE:
    return ASEPRITE_OK;
  case ASEPRITE__TARGET_OBJECT:
    user_data = parser->target_user_data;
    break;
  case ASEPRITE__TARGET_TAGS:
    user_data = &sprite->tags[parser->next_tag++].user_data;
    if (parser->next_tag == parser->end_tag) {
      aseprite__set_target(parser, nullptr);
    }
    break;
  case ASEPRITE__TARGET_TILESET: {
    // The tileset gets this chunk. Its tiles get the next ones, in order.
    user_data      = &tileset->user_data;
    uint32_t count = aseprite__count_user_data(frame, chunks_left);
    if (count > tileset->tile_count) {
      count = tileset->tile_count;
    }
    aseprite__set_target(parser, nullptr);
    if (count > 0) {
      tileset->tile_user_data = aseprite__parser_alloc(
          parser, count, sizeof *tileset->tile_user_data);
      if (!tileset->tile_user_data) {
        return ASEPRITE_ERROR_NO_MEMORY;
      }
      tileset->tile_user_data_count = count;
      parser->target                = ASEPRITE__TARGET_TILES;
      parser->next_tile             = 0;
    }
    break;
  }
  case ASEPRITE__TARGET_TILES:
    user_data = &tileset->tile_user_data[parser->next_tile++];
    if (parser->next_tile == tileset->tile_user_data_count) {
      aseprite__set_target(parser, nullptr);
    }
    break;
  }
  if (!user_data) {
    return ASEPRITE_OK;
  }
  return aseprite__read_user_data(parser, reader, user_data);
}

// Reads one chunk.
static aseprite_result aseprite__read_chunk_data(aseprite__parser* parser,
                                                 uint32_t frame_index,
                                                 aseprite__chunk* chunk,
                                                 const aseprite__reader* frame,
                                                 uint32_t chunks_left) {
  aseprite__reader* data = &chunk->data;
  if (parser->target == ASEPRITE__TARGET_TILES &&
      chunk->type != ASEPRITE__CHUNK_USER_DATA) {
    aseprite__set_target(parser, nullptr);
  }
  switch (chunk->type) {
  case ASEPRITE__CHUNK_OLD_PALETTE:
    return aseprite__read_old_palette(parser, frame_index, data, false);
  case ASEPRITE__CHUNK_OLD_PALETTE_64:
    return aseprite__read_old_palette(parser, frame_index, data, true);
  case ASEPRITE__CHUNK_PALETTE:
    return aseprite__read_palette(parser, frame_index, data);
  case ASEPRITE__CHUNK_LAYER:
    return aseprite__read_layer(parser, data);
  case ASEPRITE__CHUNK_CEL:
    return aseprite__read_cel(parser, frame_index, data);
  case ASEPRITE__CHUNK_CEL_EXTRA:
    return aseprite__read_cel_extra(parser, data);
  case ASEPRITE__CHUNK_COLOR_PROFILE:
    return aseprite__read_color_profile(parser, data);
  case ASEPRITE__CHUNK_EXTERNAL_FILES:
    return aseprite__read_external_files(parser, data);
  case ASEPRITE__CHUNK_TAGS:
    return aseprite__read_tags(parser, data);
  case ASEPRITE__CHUNK_SLICE:
    return aseprite__read_slice(parser, data);
  case ASEPRITE__CHUNK_TILESET:
    return aseprite__read_tileset(parser, data);
  case ASEPRITE__CHUNK_USER_DATA:
    return aseprite__read_user_data_chunk(parser, data, *frame, chunks_left);
  default:
    return ASEPRITE_OK;
  }
}

// Allocates the arrays that the first pass counted.
static aseprite_result aseprite__alloc_objects(aseprite__parser* parser) {
  aseprite_sprite* sprite = parser->sprite;
  sprite->layers   = aseprite__parser_alloc(parser, parser->layer_capacity,
                                            sizeof *sprite->layers);
  sprite->tags     = aseprite__parser_alloc(parser, parser->tag_capacity,
                                            sizeof *sprite->tags);
  sprite->slices   = aseprite__parser_alloc(parser, parser->slice_capacity,
                                            sizeof *sprite->slices);
  sprite->tilesets = aseprite__parser_alloc(parser, parser->tileset_capacity,
                                            sizeof *sprite->tilesets);
  sprite->external_files = aseprite__parser_alloc(
      parser, parser->external_file_capacity, sizeof *sprite->external_files);
  sprite->palettes = aseprite__parser_alloc(parser, parser->palette_capacity,
                                            sizeof *sprite->palettes);
  if (!sprite->layers || !sprite->tags || !sprite->slices ||
      !sprite->tilesets || !sprite->external_files || !sprite->palettes) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  sprite->palette_count = 1;
  return aseprite__resize_palette(parser, &sprite->palettes[0],
                                  sprite->color_count);
}

// Reads all the frames and their chunks.
static aseprite_result aseprite__read_frames(aseprite__parser* parser,
                                             aseprite__reader file) {
  aseprite_sprite* sprite = parser->sprite;
  aseprite_result result  = aseprite__alloc_objects(parser);
  if (result != ASEPRITE_OK) {
    return result;
  }

  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    aseprite__frame_header header;
    result = aseprite__read_frame_header(&file, &header);
    if (result != ASEPRITE_OK) {
      return result;
    }
    aseprite_frame* frame = &sprite->frames[i];
    frame->duration = header.duration != 0 ? header.duration : sprite->speed;
    frame->palette  = sprite->palette_count - 1;
    parser->cel_capacity = frame->cel_count;
    frame->cel_count     = 0;
    frame->cels          = aseprite__parser_alloc(parser, parser->cel_capacity,
                                                  sizeof *frame->cels);
    if (!frame->cels) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }

    for (uint32_t j = 0; j < header.chunk_count; j++) {
      aseprite__chunk chunk;
      result = aseprite__read_chunk(&header.chunks, &chunk);
      if (result == ASEPRITE_OK) {
        result = aseprite__read_chunk_data(parser, i, &chunk, &header.chunks,
                                           header.chunk_count - j - 1);
      }
      if (result == ASEPRITE_OK && chunk.data.truncated) {
        result = ASEPRITE_ERROR_TRUNCATED;
      }
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
  }
  return ASEPRITE_OK;
}

/* ---- Public functions -------------------------------------------------- */

aseprite_result aseprite_load_memory(const void* data, size_t size,
                                     aseprite_sprite* sprite) {
  ASEPRITE_ASSERT(sprite);
  ASEPRITE_ASSERT(data || size == 0);
  *sprite = (aseprite_sprite){};
  if (size == 0) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  // The file size field has 32 bits. Bigger data cannot be a valid file.
  if (size > UINT32_MAX) {
    size = UINT32_MAX;
  }

  aseprite__parser parser = {
      .sprite           = sprite,
      .target           = ASEPRITE__TARGET_OBJECT,
      .target_user_data = &sprite->user_data,
  };
  aseprite__reader file  = aseprite__reader_make(data, size);
  aseprite_result result = aseprite__read_header(&file, sprite);
  if (result == ASEPRITE_OK &&
      sprite->frame_count >
          aseprite__remaining(&file) / aseprite__frame_header_size) {
    result = ASEPRITE_ERROR_TRUNCATED;
  }
  if (result == ASEPRITE_OK) {
    sprite->frames = aseprite__parser_alloc(&parser, sprite->frame_count,
                                            sizeof *sprite->frames);
    if (!sprite->frames) {
      result = ASEPRITE_ERROR_NO_MEMORY;
    }
  }
  if (result == ASEPRITE_OK) {
    result = aseprite__count(&parser, file);
  }
  if (result == ASEPRITE_OK) {
    result = aseprite__read_frames(&parser, file);
  }
  if (result != ASEPRITE_OK) {
    aseprite__arena_free(parser.arena.head);
    *sprite = (aseprite_sprite){};
    return result;
  }
  sprite->memory = parser.arena.head;
  return ASEPRITE_OK;
}

#ifndef ASEPRITE_NO_STDIO
aseprite_result aseprite_load_file(const char* path, aseprite_sprite* sprite) {
  ASEPRITE_ASSERT(path);
  ASEPRITE_ASSERT(sprite);
  *sprite    = (aseprite_sprite){};
  FILE* file = fopen(path, "rb");
  if (!file) {
    return ASEPRITE_ERROR_IO;
  }

  aseprite_result result = ASEPRITE_ERROR_IO;
  long size              = -1;
  if (fseek(file, 0, SEEK_END) == 0) {
    size = ftell(file);
  }
  if (size >= 0 && fseek(file, 0, SEEK_SET) == 0) {
    uint8_t* data = ASEPRITE_MALLOC(size > 0 ? (size_t)size : 1);
    if (!data) {
      result = ASEPRITE_ERROR_NO_MEMORY;
    } else {
      if (fread(data, 1, (size_t)size, file) == (size_t)size) {
        result = aseprite_load_memory(data, (size_t)size, sprite);
      }
      ASEPRITE_FREE(data);
    }
  }
  (void)fclose(file);
  return result;
}
#endif

void aseprite_free(aseprite_sprite* sprite) {
  ASEPRITE_ASSERT(sprite);
  aseprite__arena_free(sprite->memory);
  *sprite = (aseprite_sprite){};
}

const char* aseprite_result_string(aseprite_result result) {
  switch (result) {
  case ASEPRITE_OK:
    return "success";
  case ASEPRITE_ERROR_IO:
    return "cannot read the file";
  case ASEPRITE_ERROR_NO_MEMORY:
    return "out of memory";
  case ASEPRITE_ERROR_NOT_ASEPRITE:
    return "not an Aseprite file";
  case ASEPRITE_ERROR_TRUNCATED:
    return "the data ends too soon";
  case ASEPRITE_ERROR_MALFORMED:
    return "the file has a value that is not valid";
  case ASEPRITE_ERROR_COMPRESSION:
    return "the compressed data is not valid";
  default:
    return "unknown result";
  }
}

uint32_t aseprite_bytes_per_pixel(aseprite_color_depth depth) {
  return (uint32_t)depth / 8;
}

const aseprite_cel* aseprite_frame_cel(const aseprite_frame* frame,
                                       uint32_t layer) {
  ASEPRITE_ASSERT(frame);
  for (uint32_t i = 0; i < frame->cel_count; i++) {
    if (frame->cels[i].layer == layer) {
      return &frame->cels[i];
    }
  }
  return nullptr;
}

uint32_t aseprite_tile_id(const aseprite_tilemap* tilemap, uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  uint32_t mask = tilemap->id_mask;
  if (mask == 0) {
    return 0;
  }
  uint32_t value = tile & mask;
  while ((mask & 1) == 0) {
    mask >>= 1;
    value >>= 1;
  }
  return value;
}

// Tells if all the bits of a mask are set.
static bool aseprite__has_bits(uint32_t tile, uint32_t mask) {
  return mask != 0 && (tile & mask) == mask;
}

bool aseprite_tile_x_flip(const aseprite_tilemap* tilemap, uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite__has_bits(tile, tilemap->x_flip_mask);
}

bool aseprite_tile_y_flip(const aseprite_tilemap* tilemap, uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite__has_bits(tile, tilemap->y_flip_mask);
}

bool aseprite_tile_diagonal_flip(const aseprite_tilemap* tilemap,
                                 uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite__has_bits(tile, tilemap->diagonal_flip_mask);
}

double aseprite_fixed_to_double(aseprite_fixed value) {
  return (double)value / 65536.0;
}

#endif
#endif
