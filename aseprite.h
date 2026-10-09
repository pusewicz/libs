/*
 * aseprite.h - read Aseprite files (.ase, .aseprite).
 *
 * Version: 0.2.0
 * SPDX-License-Identifier: Zlib
 * Copyright (c) 2026 Piotr Usewicz
 *
 * This library reads all the chunks of an Aseprite file into an
 * aseprite_sprite. It decompresses the cel and tileset images. The pixels
 * keep the color depth of the sprite. aseprite_render_frame() blends the
 * layers of a frame into RGBA pixels, with the same result as Aseprite.
 *
 * Usage:
 *
 *   #define ASEPRITE_IMPLEMENTATION // In one C file only.
 *   #include "aseprite.h"
 *
 *   aseprite_sprite sprite;
 *   aseprite_result result = aseprite_load_file("hero.aseprite", &sprite);
 *   if (result != ASEPRITE_OK) {
 *     fprintf(stderr, "%s\n", aseprite_result_string(result));
 *   }
 *
 *   // All the visible layers of frame 0, as RGBA pixels.
 *   aseprite_color* pixels =
 *       malloc(sizeof *pixels * sprite.width * sprite.height);
 *   result = aseprite_render_frame(&sprite, 0, nullptr, pixels);
 *   ...
 *   aseprite_free(&sprite);
 *
 * Link with libm (-lm) on Linux.
 *
 * Options. Define them before you include this file:
 *
 *   ASEPRITE_MALLOC(size)  Replace malloc. Define ASEPRITE_FREE also.
 *   ASEPRITE_FREE(pointer) Replace free. Define ASEPRITE_MALLOC also.
 *   ASEPRITE_ASSERT(expr)  Replace assert.
 *   ASEPRITE_NO_STDIO      Remove aseprite_load_file. Define it for all files.
 *
 * File format:
 * https://github.com/aseprite/aseprite/blob/c9baf298a43c1e31ede7812392f80de155d5f6a2/docs/ase-file-specs.md
 */

#ifndef ASEPRITE_H
#define ASEPRITE_H

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "aseprite.h requires C23"
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

/**
 * Tells if a layer shows: the layer and all its parent groups are visible.
 *
 * @param sprite The sprite.
 * @param layer  The layer index.
 * @return true when the layer shows.
 */
bool aseprite_layer_visible(const aseprite_sprite* sprite, uint32_t layer);

/**
 * Renders a frame like Aseprite does. It blends the cels of the image and
 * tilemap layers in their order and z-index, with their opacity and blend
 * mode. It does not draw reference layers. When the sprite has
 * ASEPRITE_SPRITE_FLAG_GROUP_BLEND, it blends each group separately first,
 * then blends the group with its opacity and blend mode.
 *
 * @param sprite The sprite.
 * @param frame  The frame index.
 * @param layers nullptr to draw the layers that show. Else layer_count flags:
 *               the function draws the image and tilemap layers that have
 *               true, also when they are hidden. It does not read the flags
 *               of groups.
 * @param pixels Receives width * height colors with straight alpha, row by
 *               row from top to bottom.
 * @return ASEPRITE_OK, ASEPRITE_ERROR_NO_MEMORY, or ASEPRITE_ERROR_MALFORMED
 *         when groups nest too deep.
 */
[[__nodiscard__]] aseprite_result
aseprite_render_frame(const aseprite_sprite* sprite, uint32_t frame,
                      const bool* layers, aseprite_color* pixels);

#endif

#ifdef ASEPRITE_IMPLEMENTATION
#ifndef ASEPRITE_IMPLEMENTATION_INCLUDED
#define ASEPRITE_IMPLEMENTATION_INCLUDED

#include <math.h>
#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(ASEPRITE_MALLOC) != defined(ASEPRITE_FREE)
#error "Define both ASEPRITE_MALLOC and ASEPRITE_FREE, or none of them"
#endif

#ifndef ASEPRITE_MALLOC
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
  ASEPRITE_CHUNK_OLD_PALETTE = 0x0004,
  // The specification says 0x0011. Aseprite and the FLI format use 11.
  ASEPRITE_CHUNK_OLD_PALETTE_64 = 0x000B,
  ASEPRITE_CHUNK_LAYER          = 0x2004,
  ASEPRITE_CHUNK_CEL            = 0x2005,
  ASEPRITE_CHUNK_CEL_EXTRA      = 0x2006,
  ASEPRITE_CHUNK_COLOR_PROFILE  = 0x2007,
  ASEPRITE_CHUNK_EXTERNAL_FILES = 0x2008,
  ASEPRITE_CHUNK_TAGS           = 0x2018,
  ASEPRITE_CHUNK_PALETTE        = 0x2019,
  ASEPRITE_CHUNK_USER_DATA      = 0x2020,
  ASEPRITE_CHUNK_SLICE          = 0x2022,
  ASEPRITE_CHUNK_TILESET        = 0x2023,
};

constexpr uint16_t aseprite_file_magic         = 0xA5E0;
constexpr uint16_t aseprite_frame_magic        = 0xF1FA;
constexpr size_t aseprite_header_size          = 128;
constexpr size_t aseprite_frame_header_size    = 16;
constexpr size_t aseprite_chunk_header_size    = 6;
constexpr uint32_t aseprite_max_palette_size   = 65536;
constexpr uint32_t aseprite_max_property_depth = 128;
constexpr uint32_t aseprite_max_group_depth    = 64;
// Deflate gives at most 1032 bytes for each input byte.
constexpr size_t aseprite_max_inflate_ratio = 1032;
constexpr size_t aseprite_block_size        = (size_t)64 * 1024;
constexpr size_t aseprite_alignment         = alignof(max_align_t);

// Minimum sizes of records. They limit counts before an allocation.
constexpr size_t aseprite_min_tag_size           = 19;
constexpr size_t aseprite_min_external_file_size = 14;
constexpr size_t aseprite_min_slice_key_size     = 20;
constexpr size_t aseprite_min_property_size      = 5;
constexpr size_t aseprite_min_mixed_item_size    = 3;
constexpr size_t aseprite_min_map_size           = 8;

/* ---- Memory ------------------------------------------------------------ */

typedef struct aseprite_block aseprite_block;

struct aseprite_block {
  aseprite_block* next;
  size_t used;
  size_t size;
  alignas(max_align_t) unsigned char data[];
};

typedef struct aseprite_arena {
  aseprite_block* head;
} aseprite_arena;

// Releases a list of blocks.
static void aseprite_arena_free(aseprite_block* block) {
  while (block) {
    aseprite_block* next = block->next;
    ASEPRITE_FREE(block);
    block = next;
  }
}

// Allocates a block with room for size bytes.
static aseprite_block* aseprite_new_block(size_t size) {
  size_t total = 0;
  if (ckd_add(&total, sizeof(aseprite_block), size)) {
    return nullptr;
  }
  aseprite_block* block = ASEPRITE_MALLOC(total);
  if (block) {
    block->next = nullptr;
    block->used = 0;
    block->size = size;
  }
  return block;
}

// Returns aligned memory that lives until aseprite_arena_free().
static void* aseprite_alloc(aseprite_arena* arena, size_t size) {
  size_t rounded = 0;
  if (ckd_add(&rounded, size, aseprite_alignment - 1)) {
    return nullptr;
  }
  rounded &= ~(aseprite_alignment - 1);

  aseprite_block* head = arena->head;
  if (head && head->size - head->used >= rounded) {
    void* memory = head->data + head->used;
    head->used += rounded;
    return memory;
  }

  if (head && rounded > aseprite_block_size / 4) {
    aseprite_block* block = aseprite_new_block(rounded);
    if (!block) {
      return nullptr;
    }
    block->used = rounded;
    block->next = head->next;
    head->next  = block;
    return block->data;
  }

  size_t size_of_block =
      rounded > aseprite_block_size ? rounded : aseprite_block_size;
  aseprite_block* block = aseprite_new_block(size_of_block);
  if (!block) {
    return nullptr;
  }
  block->used = rounded;
  block->next = head;
  arena->head = block;
  return block->data;
}

// Returns zeroed memory for count items, or nullptr on failure.
static void* aseprite_alloc_array(aseprite_arena* arena, size_t count,
                                  size_t size) {
  size_t total = 0;
  if (ckd_mul(&total, count, size)) {
    return nullptr;
  }
  void* memory = aseprite_alloc(arena, total);
  if (memory) {
    memset(memory, 0, total);
  }
  return memory;
}

/* ---- Reader ------------------------------------------------------------ */

// Reads little endian data. A read past the end sets truncated and gives 0.
typedef struct aseprite_reader {
  const uint8_t* at;
  const uint8_t* end;
  bool truncated;
} aseprite_reader;

// Makes a reader for size bytes of data.
static aseprite_reader aseprite_reader_make(const uint8_t* data, size_t size) {
  return (aseprite_reader){.at = data, .end = data + size};
}

// Gets the number of bytes that the reader has not read.
static size_t aseprite_remaining(const aseprite_reader* reader) {
  return (size_t)(reader->end - reader->at);
}

// Reads size bytes. Gives nullptr when the data ends too soon.
static const uint8_t* aseprite_take(aseprite_reader* reader, size_t size) {
  if (size > aseprite_remaining(reader)) {
    reader->at        = reader->end;
    reader->truncated = true;
    return nullptr;
  }
  const uint8_t* data = reader->at;
  reader->at += size;
  return data;
}

// Skips size bytes.
static void aseprite_skip(aseprite_reader* reader, size_t size) {
  (void)aseprite_take(reader, size);
}

// Reads size bytes into a new reader.
static aseprite_reader aseprite_sub_reader(aseprite_reader* reader,
                                           size_t size) {
  const uint8_t* data = aseprite_take(reader, size);
  if (!data) {
    return (aseprite_reader){
        .at        = reader->end,
        .end       = reader->end,
        .truncated = true,
    };
  }
  return aseprite_reader_make(data, size);
}

// Reads a BYTE.
static uint8_t aseprite_u8(aseprite_reader* reader) {
  const uint8_t* data = aseprite_take(reader, 1);
  return data ? data[0] : 0;
}

// Reads a WORD.
static uint16_t aseprite_u16(aseprite_reader* reader) {
  const uint8_t* data = aseprite_take(reader, 2);
  if (!data) {
    return 0;
  }
  return (uint16_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8));
}

// Reads a DWORD.
static uint32_t aseprite_u32(aseprite_reader* reader) {
  const uint8_t* data = aseprite_take(reader, 4);
  if (!data) {
    return 0;
  }
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

// Reads a QWORD.
static uint64_t aseprite_u64(aseprite_reader* reader) {
  uint64_t low  = aseprite_u32(reader);
  uint64_t high = aseprite_u32(reader);
  return low | (high << 32);
}

// Reads a signed byte.
static int32_t aseprite_i8(aseprite_reader* reader) {
  int32_t value = aseprite_u8(reader);
  return value < 128 ? value : value - 256;
}

// Reads a SHORT.
static int16_t aseprite_i16(aseprite_reader* reader) {
  uint16_t bits = aseprite_u16(reader);
  int16_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a LONG.
static int32_t aseprite_i32(aseprite_reader* reader) {
  uint32_t bits = aseprite_u32(reader);
  int32_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a LONG64.
static int64_t aseprite_i64(aseprite_reader* reader) {
  uint64_t bits = aseprite_u64(reader);
  int64_t value = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a FLOAT.
static float aseprite_f32(aseprite_reader* reader) {
  uint32_t bits = aseprite_u32(reader);
  float value   = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

// Reads a DOUBLE.
static double aseprite_f64(aseprite_reader* reader) {
  uint64_t bits = aseprite_u64(reader);
  double value  = 0;
  memcpy(&value, &bits, sizeof value);
  return value;
}

/* ---- Inflate (RFC 1950, RFC 1951) -------------------------------------- */

constexpr uint32_t aseprite_fast_bits = 9;

typedef struct aseprite_huffman {
  uint16_t fast[1U << aseprite_fast_bits]; // (length << 9) | symbol, or 0.
  uint16_t first_code[16];
  uint16_t first_symbol[16];
  uint32_t max_code[17];
  uint8_t lengths[288];
  uint16_t symbols[288];
} aseprite_huffman;

typedef struct aseprite_inflater {
  const uint8_t* in;
  const uint8_t* in_end;
  uint64_t bits;
  uint32_t bit_count;
  uint32_t padding; // Zero bytes added after the end of the input.
  uint8_t* out;
  size_t out_size;
  size_t out_pos;
} aseprite_inflater;

constexpr uint16_t aseprite_length_base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23,  27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
constexpr uint8_t aseprite_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
constexpr uint16_t aseprite_distance_base[30] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
constexpr uint8_t aseprite_distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};
constexpr uint8_t aseprite_code_length_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
};

// Reverses the order of the low count bits.
static uint32_t aseprite_reverse_bits(uint32_t value, uint32_t count) {
  uint32_t result = 0;
  for (uint32_t i = 0; i < count; i++) {
    result = (result << 1) | (value & 1);
    value >>= 1;
  }
  return result;
}

// Builds a canonical Huffman decoder. Returns false for bad lengths.
static bool aseprite_build_huffman(aseprite_huffman* huffman,
                                   const uint8_t* lengths, uint32_t count) {
  uint32_t sizes[16]     = {};
  uint32_t next_code[16] = {};
  *huffman               = (aseprite_huffman){};
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
    if (length <= aseprite_fast_bits) {
      uint16_t entry = (uint16_t)((length << aseprite_fast_bits) | i);
      for (uint32_t j = aseprite_reverse_bits(next_code[length], length);
           j < (1U << aseprite_fast_bits); j += 1U << length) {
        huffman->fast[j] = entry;
      }
    }
    next_code[length]++;
  }
  return true;
}

// Fills the bit buffer. It adds zero bytes after the end of the input.
static void aseprite_fill_bits(aseprite_inflater* inflater) {
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
static uint32_t aseprite_bits(aseprite_inflater* inflater, uint32_t count) {
  if (inflater->bit_count < count) {
    aseprite_fill_bits(inflater);
  }
  uint32_t value = (uint32_t)(inflater->bits & ((1ULL << count) - 1));
  inflater->bits >>= count;
  inflater->bit_count -= count;
  return value;
}

// Returns the next symbol, or -1 when the code is not valid.
static int32_t aseprite_decode(aseprite_inflater* inflater,
                               const aseprite_huffman* huffman) {
  if (inflater->bit_count < 16) {
    aseprite_fill_bits(inflater);
  }
  uint32_t entry =
      huffman->fast[inflater->bits & ((1U << aseprite_fast_bits) - 1)];
  if (entry != 0) {
    uint32_t length = entry >> aseprite_fast_bits;
    inflater->bits >>= length;
    inflater->bit_count -= length;
    return (int32_t)(entry & ((1U << aseprite_fast_bits) - 1));
  }

  uint32_t code =
      aseprite_reverse_bits((uint32_t)(inflater->bits & 0xFFFF), 16);
  uint32_t length = aseprite_fast_bits + 1;
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
static bool aseprite_inflate_codes(aseprite_inflater* inflater,
                                   const aseprite_huffman* literals,
                                   const aseprite_huffman* distances) {
  for (;;) {
    int32_t symbol = aseprite_decode(inflater, literals);
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
    size_t length = aseprite_length_base[length_code] +
                    aseprite_bits(inflater, aseprite_length_extra[length_code]);

    int32_t distance_code = aseprite_decode(inflater, distances);
    if (distance_code < 0 || distance_code >= 30) {
      return false;
    }
    size_t distance =
        aseprite_distance_base[distance_code] +
        aseprite_bits(inflater, aseprite_distance_extra[distance_code]);

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
static bool aseprite_inflate_stored(aseprite_inflater* inflater) {
  (void)aseprite_bits(inflater, inflater->bit_count & 7);
  uint32_t length = aseprite_bits(inflater, 16);
  uint32_t check  = aseprite_bits(inflater, 16);
  if ((length ^ 0xFFFF) != check ||
      length > inflater->out_size - inflater->out_pos) {
    return false;
  }
  while (length > 0 && inflater->bit_count >= 8) {
    inflater->out[inflater->out_pos++] = (uint8_t)aseprite_bits(inflater, 8);
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
static bool aseprite_inflate_fixed(aseprite_inflater* inflater) {
  uint8_t lengths[288];
  memset(lengths, 8, 144);
  memset(lengths + 144, 9, 112);
  memset(lengths + 256, 7, 24);
  memset(lengths + 280, 8, 8);
  uint8_t distance_lengths[30];
  memset(distance_lengths, 5, sizeof distance_lengths);

  aseprite_huffman literals;
  aseprite_huffman distances;
  if (!aseprite_build_huffman(&literals, lengths, 288) ||
      !aseprite_build_huffman(&distances, distance_lengths, 30)) {
    return false;
  }
  return aseprite_inflate_codes(inflater, &literals, &distances);
}

// Decodes a block with dynamic Huffman codes.
static bool aseprite_inflate_dynamic(aseprite_inflater* inflater) {
  uint32_t literal_count     = aseprite_bits(inflater, 5) + 257;
  uint32_t distance_count    = aseprite_bits(inflater, 5) + 1;
  uint32_t code_length_count = aseprite_bits(inflater, 4) + 4;
  if (literal_count > 286) {
    return false;
  }

  uint8_t code_lengths[19] = {};
  for (uint32_t i = 0; i < code_length_count; i++) {
    code_lengths[aseprite_code_length_order[i]] =
        (uint8_t)aseprite_bits(inflater, 3);
  }
  aseprite_huffman code_huffman;
  if (!aseprite_build_huffman(&code_huffman, code_lengths, 19)) {
    return false;
  }

  uint8_t lengths[286 + 32] = {};
  uint32_t total            = literal_count + distance_count;
  uint32_t count            = 0;
  while (count < total) {
    int32_t symbol = aseprite_decode(inflater, &code_huffman);
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
      repeat = 3 + aseprite_bits(inflater, 2);
    } else if (symbol == 17) {
      repeat = 3 + aseprite_bits(inflater, 3);
    } else {
      repeat = 11 + aseprite_bits(inflater, 7);
    }
    if (repeat > total - count) {
      return false;
    }
    memset(lengths + count, value, repeat);
    count += repeat;
  }

  aseprite_huffman literals;
  aseprite_huffman distances;
  if (!aseprite_build_huffman(&literals, lengths, literal_count) ||
      !aseprite_build_huffman(&distances, lengths + literal_count,
                              distance_count)) {
    return false;
  }
  return aseprite_inflate_codes(inflater, &literals, &distances);
}

// Calculates the Adler-32 checksum of the data.
static uint32_t aseprite_adler32(const uint8_t* data, size_t size) {
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
static aseprite_result aseprite_inflate(const uint8_t* in, size_t in_size,
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

  aseprite_inflater inflater = {
      .in       = in + 2,
      .in_end   = in + in_size,
      .out      = out,
      .out_size = out_size,
  };
  bool last = false;
  while (!last) {
    last    = aseprite_bits(&inflater, 1) != 0;
    bool ok = false;
    switch (aseprite_bits(&inflater, 2)) {
    case 0:
      ok = aseprite_inflate_stored(&inflater);
      break;
    case 1:
      ok = aseprite_inflate_fixed(&inflater);
      break;
    case 2:
      ok = aseprite_inflate_dynamic(&inflater);
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

  (void)aseprite_bits(&inflater, inflater.bit_count & 7);
  uint32_t checksum = 0;
  for (int i = 0; i < 4; i++) {
    checksum = (checksum << 8) | aseprite_bits(&inflater, 8);
  }
  if (inflater.padding * 8 > inflater.bit_count ||
      checksum != aseprite_adler32(out, out_size)) {
    return ASEPRITE_ERROR_COMPRESSION;
  }
  return ASEPRITE_OK;
}

/* ---- Frames and chunks ------------------------------------------------- */

typedef struct aseprite_frame_header {
  aseprite_reader chunks;
  uint32_t chunk_count;
  uint16_t duration;
} aseprite_frame_header;

typedef struct aseprite_chunk {
  uint16_t type;
  aseprite_reader data;
} aseprite_chunk;

// Reads a frame header and makes a reader for its chunks.
static aseprite_result aseprite_read_frame_header(aseprite_reader* file,
                                                  aseprite_frame_header* out) {
  if (aseprite_remaining(file) < aseprite_frame_header_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  uint32_t size            = aseprite_u32(file);
  uint16_t magic           = aseprite_u16(file);
  uint16_t old_chunk_count = aseprite_u16(file);
  uint16_t duration        = aseprite_u16(file);
  aseprite_skip(file, 2);
  uint32_t chunk_count = aseprite_u32(file);
  if (magic != aseprite_frame_magic || size < aseprite_frame_header_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (size - aseprite_frame_header_size > aseprite_remaining(file)) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  *out = (aseprite_frame_header){
      .chunks = aseprite_sub_reader(file, size - aseprite_frame_header_size),
      .chunk_count = chunk_count != 0 ? chunk_count : old_chunk_count,
      .duration    = duration,
  };
  return ASEPRITE_OK;
}

// Reads a chunk header and makes a reader for its data.
static aseprite_result aseprite_read_chunk(aseprite_reader* frame,
                                           aseprite_chunk* out) {
  if (aseprite_remaining(frame) < aseprite_chunk_header_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  uint32_t size = aseprite_u32(frame);
  uint16_t type = aseprite_u16(frame);
  if (size < aseprite_chunk_header_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  if (size - aseprite_chunk_header_size > aseprite_remaining(frame)) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  *out = (aseprite_chunk){
      .type = type,
      .data = aseprite_sub_reader(frame, size - aseprite_chunk_header_size),
  };
  return ASEPRITE_OK;
}

/* ---- Parser ------------------------------------------------------------ */

// The object that gets the next user data chunk.
typedef enum aseprite_target : uint8_t {
  ASEPRITE_TARGET_NONE,
  ASEPRITE_TARGET_OBJECT,
  ASEPRITE_TARGET_TAGS,
  ASEPRITE_TARGET_TILESET,
  ASEPRITE_TARGET_TILES,
} aseprite_target;

typedef struct aseprite_parser {
  aseprite_sprite* sprite;
  aseprite_arena arena;
  aseprite_target target;
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
} aseprite_parser;

// Returns zeroed memory for count items, or nullptr on failure.
static void* aseprite_parser_alloc(aseprite_parser* parser, size_t count,
                                   size_t size) {
  return aseprite_alloc_array(&parser->arena, count, size);
}

// Reads a STRING into a new string that ends with a zero byte.
static aseprite_result aseprite_read_string(aseprite_parser* parser,
                                            aseprite_reader* reader,
                                            char** out) {
  uint16_t length     = aseprite_u16(reader);
  const uint8_t* data = aseprite_take(reader, length);
  if (!data) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  char* string = aseprite_alloc(&parser->arena, (size_t)length + 1);
  if (!string) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  memcpy(string, data, length);
  string[length] = '\0';
  *out           = string;
  return ASEPRITE_OK;
}

// Gets the size of a tile, or 0 when the size is not supported.
static uint32_t aseprite_bytes_per_tile(uint16_t bits) {
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
static aseprite_result aseprite_inflate_to(aseprite_parser* parser,
                                           aseprite_reader* reader,
                                           size_t out_size, size_t capacity,
                                           uint8_t** out) {
  size_t in_size = aseprite_remaining(reader);
  if (out_size / aseprite_max_inflate_ratio > in_size) {
    return ASEPRITE_ERROR_COMPRESSION;
  }
  uint8_t* buffer = aseprite_alloc(&parser->arena, capacity);
  if (!buffer) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  const uint8_t* in      = aseprite_take(reader, in_size);
  aseprite_result result = aseprite_inflate(in, in_size, buffer, out_size);
  if (result == ASEPRITE_OK) {
    *out = buffer;
  }
  return result;
}

// Calculates the size of an image in bytes.
static aseprite_result aseprite_image_size(const aseprite_sprite* sprite,
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
static aseprite_result aseprite_read_header(aseprite_reader* file,
                                            aseprite_sprite* sprite) {
  if (aseprite_remaining(file) >= 6 &&
      ((uint32_t)file->at[4] | ((uint32_t)file->at[5] << 8)) !=
          aseprite_file_magic) {
    return ASEPRITE_ERROR_NOT_ASEPRITE;
  }
  aseprite_reader header = aseprite_sub_reader(file, aseprite_header_size);
  if (header.truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }

  aseprite_skip(&header, 6);
  sprite->frame_count = aseprite_u16(&header);
  sprite->width       = aseprite_u16(&header);
  sprite->height      = aseprite_u16(&header);
  sprite->depth       = (aseprite_color_depth)aseprite_u16(&header);
  sprite->flags       = aseprite_u32(&header);
  sprite->speed       = aseprite_u16(&header);
  aseprite_skip(&header, 8);
  uint8_t transparent_index = aseprite_u8(&header);
  aseprite_skip(&header, 3);
  uint16_t color_count = aseprite_u16(&header);
  uint8_t pixel_width  = aseprite_u8(&header);
  uint8_t pixel_height = aseprite_u8(&header);
  sprite->grid_x       = aseprite_i16(&header);
  sprite->grid_y       = aseprite_i16(&header);
  sprite->grid_width   = aseprite_u16(&header);
  sprite->grid_height  = aseprite_u16(&header);

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
static aseprite_result aseprite_add_count(uint32_t* count, size_t value) {
  if (value > UINT32_MAX || ckd_add(count, *count, (uint32_t)value)) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  return ASEPRITE_OK;
}

// Counts the objects in one chunk.
static aseprite_result aseprite_count_chunk(aseprite_parser* parser,
                                            aseprite_frame* frame,
                                            aseprite_chunk* chunk,
                                            bool* palette_changes) {
  aseprite_reader* data = &chunk->data;
  switch (chunk->type) {
  case ASEPRITE_CHUNK_OLD_PALETTE:
  case ASEPRITE_CHUNK_OLD_PALETTE_64:
    *palette_changes = *palette_changes || !parser->palette_chunk_seen;
    return ASEPRITE_OK;
  case ASEPRITE_CHUNK_PALETTE:
    parser->palette_chunk_seen = true;
    *palette_changes           = true;
    return ASEPRITE_OK;
  case ASEPRITE_CHUNK_LAYER:
    return aseprite_add_count(&parser->layer_capacity, 1);
  case ASEPRITE_CHUNK_CEL:
    return aseprite_add_count(&frame->cel_count, 1);
  case ASEPRITE_CHUNK_SLICE:
    return aseprite_add_count(&parser->slice_capacity, 1);
  case ASEPRITE_CHUNK_TILESET:
    return aseprite_add_count(&parser->tileset_capacity, 1);
  case ASEPRITE_CHUNK_TAGS: {
    uint16_t count = aseprite_u16(data);
    aseprite_skip(data, 8);
    if (count > aseprite_remaining(data) / aseprite_min_tag_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    return aseprite_add_count(&parser->tag_capacity, count);
  }
  case ASEPRITE_CHUNK_EXTERNAL_FILES: {
    uint32_t count = aseprite_u32(data);
    aseprite_skip(data, 8);
    if (count > aseprite_remaining(data) / aseprite_min_external_file_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    return aseprite_add_count(&parser->external_file_capacity, count);
  }
  default:
    return ASEPRITE_OK;
  }
}

// Checks the frame and chunk sizes and counts the objects.
static aseprite_result aseprite_count(aseprite_parser* parser,
                                      aseprite_reader file) {
  aseprite_sprite* sprite  = parser->sprite;
  parser->palette_capacity = 1;
  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    aseprite_frame_header header;
    aseprite_result result = aseprite_read_frame_header(&file, &header);
    if (result != ASEPRITE_OK) {
      return result;
    }
    bool palette_changes = false;
    for (uint32_t j = 0; j < header.chunk_count; j++) {
      aseprite_chunk chunk;
      result = aseprite_read_chunk(&header.chunks, &chunk);
      if (result == ASEPRITE_OK) {
        result = aseprite_count_chunk(parser, &sprite->frames[i], &chunk,
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
static aseprite_result aseprite_resize_palette(aseprite_parser* parser,
                                               aseprite_palette* palette,
                                               uint32_t count) {
  aseprite_color* colors = aseprite_parser_alloc(parser, count, sizeof *colors);
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
    names = aseprite_parser_alloc(parser, count, sizeof *names);
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
static aseprite_result aseprite_frame_palette(aseprite_parser* parser,
                                              uint32_t frame_index,
                                              aseprite_palette** out) {
  aseprite_sprite* sprite   = parser->sprite;
  aseprite_palette* palette = &sprite->palettes[sprite->palette_count - 1];
  if (frame_index != parser->palette_frame) {
    ASEPRITE_ASSERT(sprite->palette_count < parser->palette_capacity);
    aseprite_palette* copy = &sprite->palettes[sprite->palette_count];
    *copy                  = *palette;
    aseprite_result result =
        aseprite_resize_palette(parser, copy, palette->count);
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
static aseprite_result aseprite_read_old_palette(aseprite_parser* parser,
                                                 uint32_t frame_index,
                                                 aseprite_reader* reader,
                                                 bool six_bit) {
  if (parser->palette_chunk_seen) {
    return ASEPRITE_OK;
  }
  aseprite_palette* palette = nullptr;
  aseprite_result result =
      aseprite_frame_palette(parser, frame_index, &palette);
  if (result != ASEPRITE_OK) {
    return result;
  }

  uint16_t packet_count = aseprite_u16(reader);
  uint32_t index        = 0;
  for (uint16_t i = 0; i < packet_count; i++) {
    index += aseprite_u8(reader);
    uint32_t count = aseprite_u8(reader);
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
      result = aseprite_resize_palette(parser, palette, index + count);
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
    for (uint32_t j = 0; j < count; j++) {
      uint8_t rgb[3] = {};
      for (int k = 0; k < 3; k++) {
        uint32_t value = aseprite_u8(reader);
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
static aseprite_result aseprite_read_palette(aseprite_parser* parser,
                                             uint32_t frame_index,
                                             aseprite_reader* reader) {
  parser->palette_chunk_seen = true;
  uint32_t size              = aseprite_u32(reader);
  uint32_t first             = aseprite_u32(reader);
  uint32_t last              = aseprite_u32(reader);
  aseprite_skip(reader, 8);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (size > aseprite_max_palette_size) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  aseprite_palette* palette = nullptr;
  aseprite_result result =
      aseprite_frame_palette(parser, frame_index, &palette);
  if (result == ASEPRITE_OK && size > 0) {
    result = aseprite_resize_palette(parser, palette, size);
  }
  if (result != ASEPRITE_OK || first > last) {
    return result;
  }
  if (last >= palette->count) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  for (uint32_t i = first; i <= last; i++) {
    uint16_t flags        = aseprite_u16(reader);
    aseprite_color* color = &palette->colors[i];
    color->r              = aseprite_u8(reader);
    color->g              = aseprite_u8(reader);
    color->b              = aseprite_u8(reader);
    color->a              = aseprite_u8(reader);
    if (reader->truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    if ((flags & 1) == 0) {
      continue;
    }
    if (!palette->names) {
      palette->names =
          aseprite_parser_alloc(parser, palette->count, sizeof *palette->names);
      if (!palette->names) {
        return ASEPRITE_ERROR_NO_MEMORY;
      }
    }
    result = aseprite_read_string(parser, reader, &palette->names[i]);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Sets the user data that the next user data chunk fills.
static void aseprite_set_target(aseprite_parser* parser,
                                aseprite_user_data* user_data) {
  parser->target = user_data ? ASEPRITE_TARGET_OBJECT : ASEPRITE_TARGET_NONE;
  parser->target_user_data = user_data;
}

// Finds the index of the tileset with an ID.
static aseprite_result aseprite_find_tileset(const aseprite_sprite* sprite,
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
static int32_t aseprite_layer_parent(const aseprite_sprite* sprite,
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
static aseprite_result aseprite_read_layer(aseprite_parser* parser,
                                           aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->layer_count < parser->layer_capacity);
  aseprite_layer* layer = &sprite->layers[sprite->layer_count];

  layer->flags       = aseprite_u16(reader);
  layer->type        = (aseprite_layer_type)aseprite_u16(reader);
  layer->child_level = aseprite_u16(reader);
  aseprite_skip(reader, 4);
  uint16_t blend_mode = aseprite_u16(reader);
  uint8_t opacity     = aseprite_u8(reader);
  aseprite_skip(reader, 3);
  aseprite_result result = aseprite_read_string(parser, reader, &layer->name);
  if (result != ASEPRITE_OK) {
    return result;
  }

  if (layer->type == ASEPRITE_LAYER_TYPE_TILEMAP) {
    uint32_t id = aseprite_u32(reader);
    if (reader->truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    result = aseprite_find_tileset(sprite, id, &layer->tileset);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  if (layer->type <= ASEPRITE_LAYER_TYPE_TILEMAP &&
      (sprite->flags & ASEPRITE_SPRITE_FLAG_LAYER_UUID) != 0) {
    const uint8_t* uuid = aseprite_take(reader, sizeof layer->uuid);
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
  layer->parent = aseprite_layer_parent(sprite, layer->child_level);
  sprite->layer_count++;
  aseprite_set_target(parser, &layer->user_data);
  return ASEPRITE_OK;
}

// Reads the image of a raw or compressed cel.
static aseprite_result aseprite_read_image_cel(aseprite_parser* parser,
                                               aseprite_reader* reader,
                                               aseprite_cel* cel) {
  aseprite_sprite* sprite = parser->sprite;
  uint16_t width          = aseprite_u16(reader);
  uint16_t height         = aseprite_u16(reader);
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
  aseprite_result result = aseprite_image_size(sprite, width, height, &size);
  if (result != ASEPRITE_OK) {
    return result;
  }
  uint8_t* pixels = nullptr;
  if (cel->type == ASEPRITE_CEL_TYPE_RAW) {
    const uint8_t* data = aseprite_take(reader, size);
    if (!data) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    pixels = aseprite_alloc(&parser->arena, size);
    if (!pixels) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    memcpy(pixels, data, size);
  } else {
    result = aseprite_inflate_to(parser, reader, size, size, &pixels);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  cel->image =
      (aseprite_image){.width = width, .height = height, .pixels = pixels};
  return ASEPRITE_OK;
}

// Reads the tiles of a compressed tilemap cel.
static aseprite_result aseprite_read_tilemap_cel(aseprite_parser* parser,
                                                 aseprite_reader* reader,
                                                 aseprite_cel* cel) {
  aseprite_tilemap* tilemap   = &cel->tilemap;
  tilemap->width              = aseprite_u16(reader);
  tilemap->height             = aseprite_u16(reader);
  tilemap->bits_per_tile      = aseprite_u16(reader);
  tilemap->id_mask            = aseprite_u32(reader);
  tilemap->x_flip_mask        = aseprite_u32(reader);
  tilemap->y_flip_mask        = aseprite_u32(reader);
  tilemap->diagonal_flip_mask = aseprite_u32(reader);
  aseprite_skip(reader, 10);
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if (parser->sprite->layers[cel->layer].type != ASEPRITE_LAYER_TYPE_TILEMAP) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  uint32_t tile_size = aseprite_bytes_per_tile(tilemap->bits_per_tile);
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
      aseprite_inflate_to(parser, reader, out_size, capacity, &bytes);
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
static aseprite_result aseprite_read_linked_cel(aseprite_parser* parser,
                                                uint32_t frame_index,
                                                aseprite_reader* reader,
                                                aseprite_cel* cel) {
  cel->linked_frame = aseprite_u16(reader);
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
static aseprite_result aseprite_read_cel(aseprite_parser* parser,
                                         uint32_t frame_index,
                                         aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  aseprite_cel cel        = {};
  cel.layer               = aseprite_u16(reader);
  cel.x                   = aseprite_i16(reader);
  cel.y                   = aseprite_i16(reader);
  cel.opacity             = aseprite_u8(reader);
  cel.type                = (aseprite_cel_type)aseprite_u16(reader);
  cel.z_index             = aseprite_i16(reader);
  aseprite_skip(reader, 5);
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
    result = aseprite_read_image_cel(parser, reader, &cel);
    break;
  case ASEPRITE_CEL_TYPE_LINKED:
    result = aseprite_read_linked_cel(parser, frame_index, reader, &cel);
    break;
  case ASEPRITE_CEL_TYPE_COMPRESSED_TILEMAP:
    result = aseprite_read_tilemap_cel(parser, reader, &cel);
    break;
  default:
    break;
  }
  if (result != ASEPRITE_OK) {
    return result;
  }
  if (!cel.image.pixels && !cel.tilemap.tiles) {
    aseprite_set_target(parser, nullptr);
    return ASEPRITE_OK;
  }

  aseprite_frame* frame = &sprite->frames[frame_index];
  ASEPRITE_ASSERT(frame->cel_count < parser->cel_capacity);
  aseprite_cel* stored = &frame->cels[frame->cel_count++];
  *stored              = cel;
  parser->last_cel     = stored;
  aseprite_set_target(parser, &stored->user_data);
  return ASEPRITE_OK;
}

// Reads a cel extra chunk into the last cel.
static aseprite_result aseprite_read_cel_extra(aseprite_parser* parser,
                                               aseprite_reader* reader) {
  uint32_t flags = aseprite_u32(reader);
  if ((flags & 1) == 0) {
    return ASEPRITE_OK;
  }
  aseprite_fixed x      = aseprite_i32(reader);
  aseprite_fixed y      = aseprite_i32(reader);
  aseprite_fixed width  = aseprite_i32(reader);
  aseprite_fixed height = aseprite_i32(reader);
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
static aseprite_result aseprite_read_color_profile(aseprite_parser* parser,
                                                   aseprite_reader* reader) {
  aseprite_color_profile* profile = &parser->sprite->color_profile;
  *profile                        = (aseprite_color_profile){};
  profile->type  = (aseprite_color_profile_type)aseprite_u16(reader);
  profile->flags = aseprite_u16(reader);
  profile->gamma = aseprite_i32(reader);
  if (profile->type == ASEPRITE_COLOR_PROFILE_ICC) {
    aseprite_skip(reader, 8);
    uint32_t size       = aseprite_u32(reader);
    const uint8_t* data = aseprite_take(reader, size);
    if (!data) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    profile->icc = aseprite_alloc(&parser->arena, size);
    if (!profile->icc) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    memcpy(profile->icc, data, size);
    profile->icc_size = size;
  }
  return reader->truncated ? ASEPRITE_ERROR_TRUNCATED : ASEPRITE_OK;
}

// Reads an external files chunk.
static aseprite_result aseprite_read_external_files(aseprite_parser* parser,
                                                    aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  uint32_t count          = aseprite_u32(reader);
  aseprite_skip(reader, 8);
  ASEPRITE_ASSERT(count <=
                  parser->external_file_capacity - sprite->external_file_count);
  for (uint32_t i = 0; i < count; i++) {
    aseprite_external_file* file =
        &sprite->external_files[sprite->external_file_count++];
    file->id   = aseprite_u32(reader);
    file->type = (aseprite_external_file_type)aseprite_u8(reader);
    aseprite_skip(reader, 7);
    aseprite_result result = aseprite_read_string(parser, reader, &file->name);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Reads a tags chunk.
static aseprite_result aseprite_read_tags(aseprite_parser* parser,
                                          aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  uint16_t count          = aseprite_u16(reader);
  aseprite_skip(reader, 8);
  ASEPRITE_ASSERT(count <= parser->tag_capacity - sprite->tag_count);
  uint32_t first = sprite->tag_count;
  for (uint16_t i = 0; i < count; i++) {
    aseprite_tag* tag = &sprite->tags[sprite->tag_count++];
    tag->from         = aseprite_u16(reader);
    tag->to           = aseprite_u16(reader);
    uint8_t direction = aseprite_u8(reader);
    tag->direction    = direction <= ASEPRITE_DIRECTION_PING_PONG_REVERSE
                            ? (aseprite_direction)direction
                            : ASEPRITE_DIRECTION_FORWARD;
    tag->repeat       = aseprite_u16(reader);
    aseprite_skip(reader, 6);
    tag->color.r = aseprite_u8(reader);
    tag->color.g = aseprite_u8(reader);
    tag->color.b = aseprite_u8(reader);
    tag->color.a = 255;
    aseprite_skip(reader, 1);
    aseprite_result result = aseprite_read_string(parser, reader, &tag->name);
    if (result != ASEPRITE_OK) {
      return result;
    }
    if (tag->from > tag->to || tag->to >= sprite->frame_count) {
      return ASEPRITE_ERROR_MALFORMED;
    }
  }
  if (count == 0) {
    aseprite_set_target(parser, nullptr);
  } else {
    parser->target   = ASEPRITE_TARGET_TAGS;
    parser->next_tag = first;
    parser->end_tag  = sprite->tag_count;
  }
  return ASEPRITE_OK;
}

// Reads a slice chunk.
static aseprite_result aseprite_read_slice(aseprite_parser* parser,
                                           aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->slice_count < parser->slice_capacity);
  aseprite_slice* slice = &sprite->slices[sprite->slice_count];
  slice->key_count      = aseprite_u32(reader);
  slice->flags          = aseprite_u32(reader);
  aseprite_skip(reader, 4);
  aseprite_result result = aseprite_read_string(parser, reader, &slice->name);
  if (result != ASEPRITE_OK) {
    return result;
  }

  bool nine_patch = (slice->flags & ASEPRITE_SLICE_FLAG_NINE_PATCH) != 0;
  bool pivot      = (slice->flags & ASEPRITE_SLICE_FLAG_PIVOT) != 0;
  size_t key_size =
      aseprite_min_slice_key_size + (nine_patch ? 16 : 0) + (pivot ? 8 : 0);
  if (slice->key_count > aseprite_remaining(reader) / key_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  slice->keys =
      aseprite_parser_alloc(parser, slice->key_count, sizeof *slice->keys);
  if (!slice->keys) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < slice->key_count; i++) {
    aseprite_slice_key* key = &slice->keys[i];
    key->frame              = aseprite_u32(reader);
    key->x                  = aseprite_i32(reader);
    key->y                  = aseprite_i32(reader);
    key->width              = aseprite_u32(reader);
    key->height             = aseprite_u32(reader);
    if (nine_patch) {
      key->center_x      = aseprite_i32(reader);
      key->center_y      = aseprite_i32(reader);
      key->center_width  = aseprite_u32(reader);
      key->center_height = aseprite_u32(reader);
    }
    if (pivot) {
      key->pivot_x = aseprite_i32(reader);
      key->pivot_y = aseprite_i32(reader);
    }
  }
  sprite->slice_count++;
  aseprite_set_target(parser, &slice->user_data);
  return ASEPRITE_OK;
}

// Reads a tileset chunk.
static aseprite_result aseprite_read_tileset(aseprite_parser* parser,
                                             aseprite_reader* reader) {
  aseprite_sprite* sprite = parser->sprite;
  ASEPRITE_ASSERT(sprite->tileset_count < parser->tileset_capacity);
  aseprite_tileset* tileset = &sprite->tilesets[sprite->tileset_count];
  tileset->id               = aseprite_u32(reader);
  tileset->flags            = aseprite_u32(reader);
  tileset->tile_count       = aseprite_u32(reader);
  tileset->tile_width       = aseprite_u16(reader);
  tileset->tile_height      = aseprite_u16(reader);
  tileset->base_index       = aseprite_i16(reader);
  aseprite_skip(reader, 14);
  aseprite_result result = aseprite_read_string(parser, reader, &tileset->name);
  if (result != ASEPRITE_OK) {
    return result;
  }
  if (tileset->tile_width == 0 || tileset->tile_height == 0) {
    return ASEPRITE_ERROR_MALFORMED;
  }

  if ((tileset->flags & ASEPRITE_TILESET_FLAG_EXTERNAL) != 0) {
    tileset->external_file    = aseprite_u32(reader);
    tileset->external_tileset = aseprite_u32(reader);
  }
  if ((tileset->flags & ASEPRITE_TILESET_FLAG_EMBEDDED) != 0 &&
      tileset->tile_count > 0) {
    uint32_t length      = aseprite_u32(reader);
    aseprite_reader data = aseprite_sub_reader(reader, length);
    if (data.truncated) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    uint32_t height = 0;
    size_t size     = 0;
    if (ckd_mul(&height, (uint32_t)tileset->tile_height, tileset->tile_count)) {
      return ASEPRITE_ERROR_MALFORMED;
    }
    result = aseprite_image_size(sprite, tileset->tile_width, height, &size);
    if (result == ASEPRITE_OK) {
      result = aseprite_inflate_to(parser, &data, size, size,
                                   &tileset->image.pixels);
    }
    if (result != ASEPRITE_OK) {
      return result;
    }
    tileset->image.width  = tileset->tile_width;
    tileset->image.height = height;
  }
  sprite->tileset_count++;
  parser->target  = ASEPRITE_TARGET_TILESET;
  parser->tileset = tileset;
  return ASEPRITE_OK;
}

// Gets the smallest size of a property value, or 0 for an unknown type.
static size_t aseprite_property_min_size(uint16_t type) {
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

static aseprite_result aseprite_read_properties(aseprite_parser* parser,
                                                aseprite_reader* reader,
                                                uint32_t depth,
                                                aseprite_property* out);

// Reads a property value of a type.
static aseprite_result aseprite_read_value(aseprite_parser* parser,
                                           aseprite_reader* reader,
                                           uint16_t type, uint32_t depth,
                                           aseprite_property* out) {
  if (depth > aseprite_max_property_depth) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  out->type = (aseprite_property_type)type;
  switch (type) {
  case ASEPRITE_PROPERTY_TYPE_BOOL:
    out->value.boolean = aseprite_u8(reader) != 0;
    break;
  case ASEPRITE_PROPERTY_TYPE_INT8:
    out->value.signed_int = aseprite_i8(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT8:
    out->value.unsigned_int = aseprite_u8(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT16:
    out->value.signed_int = aseprite_i16(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT16:
    out->value.unsigned_int = aseprite_u16(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT32:
    out->value.signed_int = aseprite_i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT32:
    out->value.unsigned_int = aseprite_u32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_INT64:
    out->value.signed_int = aseprite_i64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_UINT64:
    out->value.unsigned_int = aseprite_u64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_FIXED:
    out->value.fixed = aseprite_i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_FLOAT:
    out->value.float32 = aseprite_f32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_DOUBLE:
    out->value.float64 = aseprite_f64(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_STRING:
    return aseprite_read_string(parser, reader, &out->value.string);
  case ASEPRITE_PROPERTY_TYPE_POINT:
    out->value.point.x = aseprite_i32(reader);
    out->value.point.y = aseprite_i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_SIZE:
    out->value.size.width  = aseprite_i32(reader);
    out->value.size.height = aseprite_i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_RECT:
    out->value.rect.x      = aseprite_i32(reader);
    out->value.rect.y      = aseprite_i32(reader);
    out->value.rect.width  = aseprite_i32(reader);
    out->value.rect.height = aseprite_i32(reader);
    break;
  case ASEPRITE_PROPERTY_TYPE_VECTOR: {
    uint32_t count     = aseprite_u32(reader);
    uint16_t item_type = aseprite_u16(reader);
    size_t item_size   = item_type == 0 ? aseprite_min_mixed_item_size
                                        : aseprite_property_min_size(item_type);
    if (item_size == 0) {
      return ASEPRITE_ERROR_MALFORMED;
    }
    if (count > aseprite_remaining(reader) / item_size) {
      return ASEPRITE_ERROR_TRUNCATED;
    }
    aseprite_property* items =
        aseprite_parser_alloc(parser, count, sizeof *items);
    if (!items) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }
    out->value.list.count = count;
    out->value.list.items = items;
    for (uint32_t i = 0; i < count; i++) {
      uint16_t type_of_item = item_type == 0 ? aseprite_u16(reader) : item_type;
      aseprite_result result = aseprite_read_value(parser, reader, type_of_item,
                                                   depth + 1, &items[i]);
      if (result != ASEPRITE_OK) {
        return result;
      }
    }
    break;
  }
  case ASEPRITE_PROPERTY_TYPE_MAP:
    return aseprite_read_properties(parser, reader, depth, out);
  case ASEPRITE_PROPERTY_TYPE_UUID: {
    const uint8_t* uuid = aseprite_take(reader, sizeof out->value.uuid);
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
static aseprite_result aseprite_read_properties(aseprite_parser* parser,
                                                aseprite_reader* reader,
                                                uint32_t depth,
                                                aseprite_property* out) {
  uint32_t count = aseprite_u32(reader);
  if (count > aseprite_remaining(reader) / aseprite_min_property_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  aseprite_property* properties =
      aseprite_parser_alloc(parser, count, sizeof *properties);
  if (!properties) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  out->value.list.count = count;
  out->value.list.items = properties;
  for (uint32_t i = 0; i < count; i++) {
    aseprite_result result =
        aseprite_read_string(parser, reader, &properties[i].name);
    if (result != ASEPRITE_OK) {
      return result;
    }
    uint16_t type = aseprite_u16(reader);
    result =
        aseprite_read_value(parser, reader, type, depth + 1, &properties[i]);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  return ASEPRITE_OK;
}

// Reads the property maps of a user data chunk.
static aseprite_result aseprite_read_property_maps(aseprite_parser* parser,
                                                   aseprite_reader* reader,
                                                   aseprite_user_data* out) {
  uint32_t count = aseprite_u32(reader);
  if (count > aseprite_remaining(reader) / aseprite_min_map_size) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  aseprite_property_map* maps =
      aseprite_parser_alloc(parser, count, sizeof *maps);
  if (!maps) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < count; i++) {
    maps[i].key            = aseprite_u32(reader);
    aseprite_property map  = {};
    aseprite_result result = aseprite_read_properties(parser, reader, 0, &map);
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
static aseprite_result aseprite_read_user_data(aseprite_parser* parser,
                                               aseprite_reader* reader,
                                               aseprite_user_data* out) {
  *out = (aseprite_user_data){.flags = aseprite_u32(reader)};
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_TEXT) != 0) {
    aseprite_result result = aseprite_read_string(parser, reader, &out->text);
    if (result != ASEPRITE_OK) {
      return result;
    }
  }
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_COLOR) != 0) {
    out->color.r = aseprite_u8(reader);
    out->color.g = aseprite_u8(reader);
    out->color.b = aseprite_u8(reader);
    out->color.a = aseprite_u8(reader);
  }
  if (reader->truncated) {
    return ASEPRITE_ERROR_TRUNCATED;
  }
  if ((out->flags & ASEPRITE_USER_DATA_FLAG_PROPERTIES) == 0) {
    return ASEPRITE_OK;
  }

  // Aseprite keeps the sprite when the properties are not valid. So does
  // this reader: it drops the properties and continues.
  uint32_t size = aseprite_u32(reader);
  if (size < 4) {
    return ASEPRITE_OK;
  }
  size_t available           = aseprite_remaining(reader);
  aseprite_reader properties = aseprite_reader_make(
      reader->at, size - 4 < available ? size - 4 : available);
  aseprite_result result =
      aseprite_read_property_maps(parser, &properties, out);
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
static uint32_t aseprite_count_user_data(aseprite_reader frame,
                                         uint32_t chunk_count) {
  uint32_t count = 0;
  for (; count < chunk_count; count++) {
    aseprite_chunk chunk;
    if (aseprite_read_chunk(&frame, &chunk) != ASEPRITE_OK ||
        chunk.type != ASEPRITE_CHUNK_USER_DATA) {
      break;
    }
  }
  return count;
}

// Reads a user data chunk into the object that gets it.
static aseprite_result aseprite_read_user_data_chunk(aseprite_parser* parser,
                                                     aseprite_reader* reader,
                                                     aseprite_reader frame,
                                                     uint32_t chunks_left) {
  aseprite_sprite* sprite       = parser->sprite;
  aseprite_tileset* tileset     = parser->tileset;
  aseprite_user_data* user_data = nullptr;
  switch (parser->target) {
  case ASEPRITE_TARGET_NONE:
    return ASEPRITE_OK;
  case ASEPRITE_TARGET_OBJECT:
    user_data = parser->target_user_data;
    break;
  case ASEPRITE_TARGET_TAGS:
    user_data = &sprite->tags[parser->next_tag++].user_data;
    if (parser->next_tag == parser->end_tag) {
      aseprite_set_target(parser, nullptr);
    }
    break;
  case ASEPRITE_TARGET_TILESET: {
    // The tileset gets this chunk. Its tiles get the next ones, in order.
    user_data      = &tileset->user_data;
    uint32_t count = aseprite_count_user_data(frame, chunks_left);
    if (count > tileset->tile_count) {
      count = tileset->tile_count;
    }
    aseprite_set_target(parser, nullptr);
    if (count > 0) {
      tileset->tile_user_data =
          aseprite_parser_alloc(parser, count, sizeof *tileset->tile_user_data);
      if (!tileset->tile_user_data) {
        return ASEPRITE_ERROR_NO_MEMORY;
      }
      tileset->tile_user_data_count = count;
      parser->target                = ASEPRITE_TARGET_TILES;
      parser->next_tile             = 0;
    }
    break;
  }
  case ASEPRITE_TARGET_TILES:
    user_data = &tileset->tile_user_data[parser->next_tile++];
    if (parser->next_tile == tileset->tile_user_data_count) {
      aseprite_set_target(parser, nullptr);
    }
    break;
  }
  if (!user_data) {
    return ASEPRITE_OK;
  }
  return aseprite_read_user_data(parser, reader, user_data);
}

// Reads one chunk.
static aseprite_result aseprite_read_chunk_data(aseprite_parser* parser,
                                                uint32_t frame_index,
                                                aseprite_chunk* chunk,
                                                const aseprite_reader* frame,
                                                uint32_t chunks_left) {
  aseprite_reader* data = &chunk->data;
  if (parser->target == ASEPRITE_TARGET_TILES &&
      chunk->type != ASEPRITE_CHUNK_USER_DATA) {
    aseprite_set_target(parser, nullptr);
  }
  switch (chunk->type) {
  case ASEPRITE_CHUNK_OLD_PALETTE:
    return aseprite_read_old_palette(parser, frame_index, data, false);
  case ASEPRITE_CHUNK_OLD_PALETTE_64:
    return aseprite_read_old_palette(parser, frame_index, data, true);
  case ASEPRITE_CHUNK_PALETTE:
    return aseprite_read_palette(parser, frame_index, data);
  case ASEPRITE_CHUNK_LAYER:
    return aseprite_read_layer(parser, data);
  case ASEPRITE_CHUNK_CEL:
    return aseprite_read_cel(parser, frame_index, data);
  case ASEPRITE_CHUNK_CEL_EXTRA:
    return aseprite_read_cel_extra(parser, data);
  case ASEPRITE_CHUNK_COLOR_PROFILE:
    return aseprite_read_color_profile(parser, data);
  case ASEPRITE_CHUNK_EXTERNAL_FILES:
    return aseprite_read_external_files(parser, data);
  case ASEPRITE_CHUNK_TAGS:
    return aseprite_read_tags(parser, data);
  case ASEPRITE_CHUNK_SLICE:
    return aseprite_read_slice(parser, data);
  case ASEPRITE_CHUNK_TILESET:
    return aseprite_read_tileset(parser, data);
  case ASEPRITE_CHUNK_USER_DATA:
    return aseprite_read_user_data_chunk(parser, data, *frame, chunks_left);
  default:
    return ASEPRITE_OK;
  }
}

// Allocates the arrays that the first pass counted.
static aseprite_result aseprite_alloc_objects(aseprite_parser* parser) {
  aseprite_sprite* sprite = parser->sprite;
  sprite->layers = aseprite_parser_alloc(parser, parser->layer_capacity,
                                         sizeof *sprite->layers);
  sprite->tags =
      aseprite_parser_alloc(parser, parser->tag_capacity, sizeof *sprite->tags);
  sprite->slices   = aseprite_parser_alloc(parser, parser->slice_capacity,
                                           sizeof *sprite->slices);
  sprite->tilesets = aseprite_parser_alloc(parser, parser->tileset_capacity,
                                           sizeof *sprite->tilesets);
  sprite->external_files = aseprite_parser_alloc(
      parser, parser->external_file_capacity, sizeof *sprite->external_files);
  sprite->palettes = aseprite_parser_alloc(parser, parser->palette_capacity,
                                           sizeof *sprite->palettes);
  if (!sprite->layers || !sprite->tags || !sprite->slices ||
      !sprite->tilesets || !sprite->external_files || !sprite->palettes) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  sprite->palette_count = 1;
  return aseprite_resize_palette(parser, &sprite->palettes[0],
                                 sprite->color_count);
}

// Reads all the frames and their chunks.
static aseprite_result aseprite_read_frames(aseprite_parser* parser,
                                            aseprite_reader file) {
  aseprite_sprite* sprite = parser->sprite;
  aseprite_result result  = aseprite_alloc_objects(parser);
  if (result != ASEPRITE_OK) {
    return result;
  }

  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    aseprite_frame_header header;
    result = aseprite_read_frame_header(&file, &header);
    if (result != ASEPRITE_OK) {
      return result;
    }
    aseprite_frame* frame = &sprite->frames[i];
    frame->duration = header.duration != 0 ? header.duration : sprite->speed;
    frame->palette  = sprite->palette_count - 1;
    parser->cel_capacity = frame->cel_count;
    frame->cel_count     = 0;
    frame->cels          = aseprite_parser_alloc(parser, parser->cel_capacity,
                                                 sizeof *frame->cels);
    if (!frame->cels) {
      return ASEPRITE_ERROR_NO_MEMORY;
    }

    for (uint32_t j = 0; j < header.chunk_count; j++) {
      aseprite_chunk chunk;
      result = aseprite_read_chunk(&header.chunks, &chunk);
      if (result == ASEPRITE_OK) {
        result = aseprite_read_chunk_data(parser, i, &chunk, &header.chunks,
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

/* ---- Blend modes ------------------------------------------------------- */

// The blend functions give the same pixels as Aseprite with its "new blend
// method". They follow src/doc/blend_funcs.cpp of Aseprite, which has this
// license:
//
//   Copyright (c) 2018-present Igara Studio S.A.
//   Copyright (c) 2001-2018 David Capello
//
//   Permission is hereby granted, free of charge, to any person obtaining a
//   copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be included
//   in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Divides by 256 and rounds down, also for negative values.
static int32_t aseprite_div256(int32_t value) {
  return value >= 0 ? value / 256 : -((255 - value) / 256);
}

// Multiplies two values as fractions of 255, with rounding. The first value
// can be negative.
static int32_t aseprite_mul8(int32_t a, int32_t b) {
  int32_t t = (a * b) + 128;
  return aseprite_div256(aseprite_div256(t) + t);
}

// Divides a by b as fractions of 255, with rounding. 0 <= a < b.
static int32_t aseprite_div8(int32_t a, int32_t b) {
  return ((a * 255) + (b / 2)) / b;
}

// Limits a value to a channel.
static uint8_t aseprite_channel(int32_t value) {
  if (value < 0) {
    return 0;
  }
  return value > 255 ? 255 : (uint8_t)value;
}

// Blends src over backdrop.
static aseprite_color aseprite_blend_normal(aseprite_color backdrop,
                                            aseprite_color src,
                                            int32_t opacity) {
  if (backdrop.a == 0) {
    src.a = (uint8_t)aseprite_mul8(src.a, opacity);
    return src;
  }
  if (src.a == 0) {
    return backdrop;
  }
  int32_t sa = aseprite_mul8(src.a, opacity);
  int32_t ra = sa + backdrop.a - aseprite_mul8(backdrop.a, sa);
  return (aseprite_color){
      .r = (uint8_t)(backdrop.r + ((src.r - backdrop.r) * sa / ra)),
      .g = (uint8_t)(backdrop.g + ((src.g - backdrop.g) * sa / ra)),
      .b = (uint8_t)(backdrop.b + ((src.b - backdrop.b) * sa / ra)),
      .a = (uint8_t)ra,
  };
}

// Moves backdrop towards src by an amount.
static aseprite_color aseprite_blend_merge(aseprite_color backdrop,
                                           aseprite_color src, int32_t amount) {
  aseprite_color result = backdrop;
  if (backdrop.a == 0) {
    result = src;
  } else if (src.a != 0) {
    result.r =
        (uint8_t)(backdrop.r + aseprite_mul8(src.r - backdrop.r, amount));
    result.g =
        (uint8_t)(backdrop.g + aseprite_mul8(src.g - backdrop.g, amount));
    result.b =
        (uint8_t)(backdrop.b + aseprite_mul8(src.b - backdrop.b, amount));
  }
  result.a = (uint8_t)(backdrop.a + aseprite_mul8(src.a - backdrop.a, amount));
  if (result.a == 0) {
    result = (aseprite_color){};
  }
  return result;
}

static int32_t aseprite_screen(int32_t b, int32_t s) {
  return b + s - aseprite_mul8(b, s);
}

static int32_t aseprite_hard_light(int32_t b, int32_t s) {
  return s < 128 ? aseprite_mul8(b, s * 2) : aseprite_screen(b, (s * 2) - 255);
}

static int32_t aseprite_color_dodge(int32_t b, int32_t s) {
  if (b == 0) {
    return 0;
  }
  s = 255 - s;
  return b >= s ? 255 : aseprite_div8(b, s);
}

static int32_t aseprite_color_burn(int32_t b, int32_t s) {
  if (b == 255) {
    return 255;
  }
  b = 255 - b;
  return b >= s ? 0 : 255 - aseprite_div8(b, s);
}

// fma(a, b, c) is a * b + c, see aseprite_lum().
static int32_t aseprite_soft_light(int32_t b_value, int32_t s_value) {
  double b = (double)b_value / 255.0;
  double s = (double)s_value / 255.0;
  double d = b <= 0.25 ? fma(fma(16, b, -12), b, 4) * b : sqrt(b);
  double r = s <= 0.5 ? fma(-(fma(-2.0, s, 1.0) * b), 1.0 - b, b)
                      : fma(fma(2.0, s, -1.0), d - b, b);
  return (int32_t)fma(r, 255, 0.5);
}

static int32_t aseprite_divide(int32_t b, int32_t s) {
  if (b == 0) {
    return 0;
  }
  return b >= s ? 255 : aseprite_div8(b, s);
}

// Blends one channel with a separable blend mode.
static int32_t aseprite_blend_channel(aseprite_blend_mode mode, int32_t b,
                                      int32_t s) {
  switch (mode) {
  case ASEPRITE_BLEND_MULTIPLY:
    return aseprite_mul8(b, s);
  case ASEPRITE_BLEND_SCREEN:
    return aseprite_screen(b, s);
  case ASEPRITE_BLEND_OVERLAY:
    return aseprite_hard_light(s, b);
  case ASEPRITE_BLEND_DARKEN:
    return b < s ? b : s;
  case ASEPRITE_BLEND_LIGHTEN:
    return b > s ? b : s;
  case ASEPRITE_BLEND_COLOR_DODGE:
    return aseprite_color_dodge(b, s);
  case ASEPRITE_BLEND_COLOR_BURN:
    return aseprite_color_burn(b, s);
  case ASEPRITE_BLEND_HARD_LIGHT:
    return aseprite_hard_light(b, s);
  case ASEPRITE_BLEND_SOFT_LIGHT:
    return aseprite_soft_light(b, s);
  case ASEPRITE_BLEND_DIFFERENCE:
    return b > s ? b - s : s - b;
  case ASEPRITE_BLEND_EXCLUSION:
    return b + s - (2 * aseprite_mul8(b, s));
  case ASEPRITE_BLEND_ADDITION:
    return b + s;
  case ASEPRITE_BLEND_SUBTRACT:
    return b - s;
  case ASEPRITE_BLEND_DIVIDE:
    return aseprite_divide(b, s);
  default:
    return s;
  }
}

// A color with channels from 0 to 1, for the non-separable blend modes.
typedef struct aseprite_rgb {
  double r;
  double g;
  double b;
} aseprite_rgb;

static aseprite_rgb aseprite_to_rgb(aseprite_color color) {
  return (aseprite_rgb){
      .r = (double)color.r / 255.0,
      .g = (double)color.g / 255.0,
      .b = (double)color.b / 255.0,
  };
}

static double aseprite_min3(aseprite_rgb c) {
  double gb = c.g < c.b ? c.g : c.b;
  return c.r < gb ? c.r : gb;
}

static double aseprite_max3(aseprite_rgb c) {
  double gb = c.g > c.b ? c.g : c.b;
  return c.r > gb ? c.r : gb;
}

// Aseprite on Apple silicon fuses a * b + c into one operation with one
// rounding (clang -ffp-contract=on). fma() does the same here, so the result
// does not change with the compiler. Without it, a pixel can be 1 off.
static double aseprite_lum(aseprite_rgb c) {
  return fma(0.11, c.b, fma(0.3, c.r, 0.59 * c.g));
}

static double aseprite_sat(aseprite_rgb c) {
  return aseprite_max3(c) - aseprite_min3(c);
}

static aseprite_rgb aseprite_clip_color(aseprite_rgb c) {
  double l = aseprite_lum(c);
  double n = aseprite_min3(c);
  double x = aseprite_max3(c);
  if (n < 0) {
    c.r = l + (((c.r - l) * l) / (l - n));
    c.g = l + (((c.g - l) * l) / (l - n));
    c.b = l + (((c.b - l) * l) / (l - n));
  }
  if (x > 1) {
    c.r = l + (((c.r - l) * (1 - l)) / (x - l));
    c.g = l + (((c.g - l) * (1 - l)) / (x - l));
    c.b = l + (((c.b - l) * (1 - l)) / (x - l));
  }
  return c;
}

static aseprite_rgb aseprite_set_lum(aseprite_rgb c, double l) {
  double d = l - aseprite_lum(c);
  c.r += d;
  c.g += d;
  c.b += d;
  return aseprite_clip_color(c);
}

static aseprite_rgb aseprite_set_sat(aseprite_rgb c, double s) {
  double low   = aseprite_min3(c);
  double range = aseprite_max3(c) - low;
  if (range <= 0.0) {
    return (aseprite_rgb){};
  }
  return (aseprite_rgb){
      .r = ((c.r - low) * s) / range,
      .g = ((c.g - low) * s) / range,
      .b = ((c.b - low) * s) / range,
  };
}

// Converts a channel from 0 to 1 back to 0 to 255. It rounds down.
static uint8_t aseprite_from_unit(double value) {
  return aseprite_channel((int32_t)(255.0 * value));
}

// Mixes the colors of backdrop and src with a blend mode. The result has the
// alpha of src.
static aseprite_color aseprite_blend_colors(aseprite_blend_mode mode,
                                            aseprite_color backdrop,
                                            aseprite_color src) {
  aseprite_rgb b = aseprite_to_rgb(backdrop);
  aseprite_rgb s = aseprite_to_rgb(src);
  aseprite_rgb mixed;
  switch (mode) {
  case ASEPRITE_BLEND_HUE:
    mixed =
        aseprite_set_lum(aseprite_set_sat(s, aseprite_sat(b)), aseprite_lum(b));
    break;
  case ASEPRITE_BLEND_SATURATION:
    mixed =
        aseprite_set_lum(aseprite_set_sat(b, aseprite_sat(s)), aseprite_lum(b));
    break;
  case ASEPRITE_BLEND_COLOR:
    mixed = aseprite_set_lum(s, aseprite_lum(b));
    break;
  case ASEPRITE_BLEND_LUMINOSITY:
    mixed = aseprite_set_lum(b, aseprite_lum(s));
    break;
  default:
    return (aseprite_color){
        .r = aseprite_channel(aseprite_blend_channel(mode, backdrop.r, src.r)),
        .g = aseprite_channel(aseprite_blend_channel(mode, backdrop.g, src.g)),
        .b = aseprite_channel(aseprite_blend_channel(mode, backdrop.b, src.b)),
        .a = src.a,
    };
  }
  return (aseprite_color){
      .r = aseprite_from_unit(mixed.r),
      .g = aseprite_from_unit(mixed.g),
      .b = aseprite_from_unit(mixed.b),
      .a = src.a,
  };
}

// Blends src over backdrop with a blend mode and an opacity. Unknown modes
// blend like the normal mode.
static aseprite_color aseprite_blend(aseprite_blend_mode mode,
                                     aseprite_color backdrop,
                                     aseprite_color src, int32_t opacity) {
  aseprite_color normal = aseprite_blend_normal(backdrop, src, opacity);
  if (mode == ASEPRITE_BLEND_NORMAL || mode > ASEPRITE_BLEND_DIVIDE ||
      backdrop.a == 0) {
    return normal;
  }
  aseprite_color mixed = aseprite_blend_normal(
      backdrop, aseprite_blend_colors(mode, backdrop, src), opacity);
  aseprite_color merged = aseprite_blend_merge(normal, mixed, backdrop.a);
  int32_t amount = aseprite_mul8(backdrop.a, aseprite_mul8(src.a, opacity));
  return aseprite_blend_merge(merged, mixed, amount);
}

/* ---- Render ------------------------------------------------------------ */

// A layer to draw and its place in the drawing order.
typedef struct aseprite_plan_item {
  uint32_t layer;
  int32_t order; // The position of the layer plus the z-index of its cel.
  int16_t z_index;
} aseprite_plan_item;

// The layers of a render pass.
typedef enum aseprite_pass : uint8_t {
  ASEPRITE_PASS_ALL,
  ASEPRITE_PASS_BACKGROUND,
  ASEPRITE_PASS_OTHERS,
} aseprite_pass;

// The state of a frame render.
typedef struct aseprite_renderer {
  const aseprite_sprite* sprite;
  const aseprite_palette* palette;
  const bool* layers;
  const aseprite_cel** cels; // The cel of each layer in the frame.
  aseprite_plan_item* items; // Space for layer_count items.
  uint32_t bytes_per_pixel;
  bool compose_groups;
} aseprite_renderer;

// Tells if a color is 0 in all channels: the transparent color of RGBA
// images.
static bool aseprite_is_clear(aseprite_color color) {
  return color.r == 0 && color.g == 0 && color.b == 0 && color.a == 0;
}

// Reads a pixel of an image. Returns false for the transparent color:
// Aseprite does not draw it.
static bool aseprite_read_color(const aseprite_renderer* r,
                                const uint8_t* pixel, aseprite_color* color) {
  switch (r->sprite->depth) {
  case ASEPRITE_DEPTH_RGBA:
    *color = (aseprite_color){pixel[0], pixel[1], pixel[2], pixel[3]};
    return !aseprite_is_clear(*color);
  case ASEPRITE_DEPTH_GRAYSCALE:
    *color = (aseprite_color){pixel[0], pixel[0], pixel[0], pixel[1]};
    return pixel[0] != 0 || pixel[1] != 0;
  case ASEPRITE_DEPTH_INDEXED:
    if (pixel[0] == r->sprite->transparent_index) {
      return false;
    }
    *color = pixel[0] < r->palette->count ? r->palette->colors[pixel[0]]
                                          : (aseprite_color){};
    return true;
  }
  return false;
}

// Draws an image with its top-left corner at (x, y).
static void aseprite_draw_image(const aseprite_renderer* r,
                                aseprite_color* target,
                                const aseprite_image* image, int64_t x,
                                int64_t y, int32_t opacity,
                                aseprite_blend_mode mode) {
  int64_t width  = r->sprite->width;
  int64_t height = r->sprite->height;
  int64_t left   = x > 0 ? x : 0;
  int64_t top    = y > 0 ? y : 0;
  int64_t right  = x + image->width < width ? x + image->width : width;
  int64_t bottom = y + image->height < height ? y + image->height : height;
  for (int64_t row = top; row < bottom; row++) {
    const uint8_t* src =
        image->pixels +
        ((((size_t)(row - y) * image->width) + (size_t)(left - x)) *
         r->bytes_per_pixel);
    aseprite_color* dst = target + ((size_t)row * (size_t)width) + left;
    for (int64_t column = left; column < right; column++) {
      aseprite_color color;
      if (aseprite_read_color(r, src, &color)) {
        *dst = aseprite_blend(mode, *dst, color, opacity);
      }
      src += r->bytes_per_pixel;
      dst++;
    }
  }
}

// Finds the tiles of a row or column that touch the canvas.
static void aseprite_tile_range(int64_t origin, int64_t tile_size,
                                uint32_t count, int64_t canvas_size,
                                uint32_t* begin, uint32_t* end) {
  int64_t first = origin >= 0 ? 0 : -origin / tile_size;
  int64_t last  = 0;
  if (canvas_size > origin) {
    last = (canvas_size - origin + tile_size - 1) / tile_size;
  }
  if (last > count) {
    last = count;
  }
  if (first > last) {
    first = last;
  }
  *begin = (uint32_t)first;
  *end   = (uint32_t)last;
}

// Draws one tile with its top-left corner at (x, y).
static void aseprite_draw_tile(const aseprite_renderer* r,
                               aseprite_color* target,
                               const aseprite_tileset* tileset,
                               const aseprite_tilemap* tilemap, uint32_t tile,
                               int64_t x, int64_t y, int32_t opacity,
                               aseprite_blend_mode mode) {
  int64_t tile_width  = tileset->tile_width;
  int64_t tile_height = tileset->tile_height;
  bool flip_x         = aseprite_tile_x_flip(tilemap, tile);
  bool flip_y         = aseprite_tile_y_flip(tilemap, tile);
  bool flip_diagonal  = aseprite_tile_diagonal_flip(tilemap, tile);
  // A diagonal flip of a tile that is not square uses the square part only.
  int64_t limit_x = tile_width;
  int64_t limit_y = tile_height;
  if (flip_diagonal) {
    limit_x = tile_width < tile_height ? tile_width : tile_height;
    limit_y = limit_x;
  }
  const uint8_t* pixels =
      tileset->image.pixels +
      ((size_t)aseprite_tile_id(tilemap, tile) * (size_t)tile_width *
       (size_t)tile_height * r->bytes_per_pixel);
  int64_t width  = r->sprite->width;
  int64_t height = r->sprite->height;
  int64_t left   = x > 0 ? x : 0;
  int64_t top    = y > 0 ? y : 0;
  int64_t right  = x + tile_width < width ? x + tile_width : width;
  int64_t bottom = y + tile_height < height ? y + tile_height : height;
  for (int64_t row = top; row < bottom; row++) {
    for (int64_t column = left; column < right; column++) {
      int64_t source_x = flip_x ? tile_width - 1 - (column - x) : column - x;
      int64_t source_y = flip_y ? tile_height - 1 - (row - y) : row - y;
      if (flip_diagonal) {
        int64_t swap = source_x;
        source_x     = source_y;
        source_y     = swap;
      }
      if (source_x >= limit_x || source_y >= limit_y) {
        continue;
      }
      const uint8_t* src =
          pixels +
          ((((size_t)source_y * (size_t)tile_width) + (size_t)source_x) *
           r->bytes_per_pixel);
      aseprite_color color;
      if (aseprite_read_color(r, src, &color)) {
        aseprite_color* dst = target + ((size_t)row * (size_t)width) + column;
        *dst                = aseprite_blend(mode, *dst, color, opacity);
      }
    }
  }
}

// Draws the tiles of a tilemap cel.
static void aseprite_draw_tilemap(const aseprite_renderer* r,
                                  aseprite_color* target,
                                  const aseprite_cel* cel,
                                  const aseprite_tileset* tileset,
                                  int32_t opacity, aseprite_blend_mode mode) {
  const aseprite_tilemap* tilemap = &cel->tilemap;
  int64_t tile_width              = tileset->tile_width;
  int64_t tile_height             = tileset->tile_height;
  if (!tileset->image.pixels || tile_width == 0 || tile_height == 0) {
    return;
  }
  uint32_t first_column = 0;
  uint32_t last_column  = 0;
  uint32_t first_row    = 0;
  uint32_t last_row     = 0;
  aseprite_tile_range(cel->x, tile_width, tilemap->width, r->sprite->width,
                      &first_column, &last_column);
  aseprite_tile_range(cel->y, tile_height, tilemap->height, r->sprite->height,
                      &first_row, &last_row);
  for (uint32_t row = first_row; row < last_row; row++) {
    for (uint32_t column = first_column; column < last_column; column++) {
      uint32_t tile = tilemap->tiles[((size_t)row * tilemap->width) + column];
      // Aseprite does not draw the tile value 0, nor tiles that the
      // tileset does not have.
      if (tile == 0 || aseprite_tile_id(tilemap, tile) >= tileset->tile_count) {
        continue;
      }
      aseprite_draw_tile(r, target, tileset, tilemap, tile,
                         cel->x + ((int64_t)column * tile_width),
                         cel->y + ((int64_t)row * tile_height), opacity, mode);
    }
  }
}

// Draws the cel of a layer, if the frame has one.
static void aseprite_draw_cel(const aseprite_renderer* r, uint32_t index,
                              aseprite_color* target) {
  const aseprite_cel* cel = r->cels[index];
  if (!cel) {
    return;
  }
  const aseprite_layer* layer = &r->sprite->layers[index];
  int32_t opacity             = aseprite_mul8(cel->opacity, layer->opacity);
  if (cel->image.pixels) {
    aseprite_draw_image(r, target, &cel->image, cel->x, cel->y, opacity,
                        layer->blend_mode);
  } else if (cel->tilemap.tiles && layer->type == ASEPRITE_LAYER_TYPE_TILEMAP &&
             layer->tileset < r->sprite->tileset_count) {
    aseprite_draw_tilemap(r, target, cel, &r->sprite->tilesets[layer->tileset],
                          opacity, layer->blend_mode);
  }
}

// Tells if a render draws an image or tilemap layer.
static bool aseprite_draws_layer(const aseprite_renderer* r, uint32_t index) {
  const aseprite_layer* layer = &r->sprite->layers[index];
  if (layer->type != ASEPRITE_LAYER_TYPE_IMAGE &&
      layer->type != ASEPRITE_LAYER_TYPE_TILEMAP) {
    return false;
  }
  if ((layer->flags & ASEPRITE_LAYER_FLAG_REFERENCE) != 0) {
    return false;
  }
  return r->layers ? r->layers[index]
                   : aseprite_layer_visible(r->sprite, index);
}

// Tells if a render draws a group. Only composed groups are drawn as one.
static bool aseprite_draws_group(const aseprite_renderer* r, uint32_t index) {
  return r->compose_groups &&
         (r->layers || aseprite_layer_visible(r->sprite, index));
}

// Orders plan items by order, then by z-index.
static int aseprite_compare_items(const void* a, const void* b) {
  const aseprite_plan_item* x = a;
  const aseprite_plan_item* y = b;
  if (x->order != y->order) {
    return x->order < y->order ? -1 : 1;
  }
  if (x->z_index != y->z_index) {
    return x->z_index < y->z_index ? -1 : 1;
  }
  return 0;
}

// Lists the layers to draw in a group, in the drawing order. The group -1 is
// the root. Without group composition, the root lists the layers of all
// groups. The layers of a group follow the group in the file, so the scan
// stops at the first layer outside it.
static size_t aseprite_plan(const aseprite_renderer* r, int32_t group,
                            aseprite_plan_item* items) {
  const aseprite_sprite* sprite = r->sprite;
  size_t count                  = 0;
  int32_t position              = 0;
  bool sort                     = false;
  for (uint32_t i = (uint32_t)(group + 1);
       i < sprite->layer_count && sprite->layers[i].parent >= group; i++) {
    const aseprite_layer* layer = &sprite->layers[i];
    if (r->compose_groups && layer->parent != group) {
      continue;
    }
    position  = r->compose_groups ? position + 1 : (int32_t)i;
    bool draw = layer->type == ASEPRITE_LAYER_TYPE_GROUP
                    ? aseprite_draws_group(r, i)
                    : aseprite_draws_layer(r, i);
    if (!draw) {
      continue;
    }
    int16_t z_index = r->cels[i] ? r->cels[i]->z_index : 0;
    items[count++]  = (aseprite_plan_item){
        .layer   = i,
        .order   = position + z_index,
        .z_index = z_index,
    };
    sort = sort || z_index != 0;
  }
  if (sort) {
    qsort(items, count, sizeof *items, aseprite_compare_items);
  }
  return count;
}

static aseprite_result aseprite_render_group(aseprite_renderer* r,
                                             uint32_t group, size_t used,
                                             uint32_t depth,
                                             aseprite_color* target);

// Draws the layers of a group in their order. used is the count of plan
// items that the parent groups use.
static aseprite_result aseprite_render_layers(aseprite_renderer* r,
                                              int32_t group, size_t used,
                                              uint32_t depth,
                                              aseprite_pass pass,
                                              aseprite_color* target) {
  aseprite_plan_item* items = r->items + used;
  size_t count              = aseprite_plan(r, group, items);
  for (size_t i = 0; i < count; i++) {
    uint32_t index              = items[i].layer;
    const aseprite_layer* layer = &r->sprite->layers[index];
    bool is_group               = layer->type == ASEPRITE_LAYER_TYPE_GROUP;
    bool background =
        !is_group && (layer->flags & ASEPRITE_LAYER_FLAG_BACKGROUND) != 0;
    if ((pass == ASEPRITE_PASS_BACKGROUND && !background) ||
        (pass == ASEPRITE_PASS_OTHERS && background)) {
      continue;
    }
    if (is_group) {
      aseprite_result result =
          aseprite_render_group(r, index, used + count, depth + 1, target);
      if (result != ASEPRITE_OK) {
        return result;
      }
    } else {
      aseprite_draw_cel(r, index, target);
    }
  }
  return ASEPRITE_OK;
}

// Draws the layers of a group into an empty image, then blends the image
// with the opacity and blend mode of the group.
static aseprite_result aseprite_render_group(aseprite_renderer* r,
                                             uint32_t group, size_t used,
                                             uint32_t depth,
                                             aseprite_color* target) {
  if (depth > aseprite_max_group_depth) {
    return ASEPRITE_ERROR_MALFORMED;
  }
  size_t count = (size_t)r->sprite->width * r->sprite->height;
  size_t size  = 0;
  if (ckd_mul(&size, count, sizeof(aseprite_color))) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  aseprite_color* image = ASEPRITE_MALLOC(size > 0 ? size : 1);
  if (!image) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  memset(image, 0, size);
  aseprite_result result = aseprite_render_layers(
      r, (int32_t)group, used, depth, ASEPRITE_PASS_ALL, image);
  if (result == ASEPRITE_OK) {
    const aseprite_layer* layer = &r->sprite->layers[group];
    for (size_t i = 0; i < count; i++) {
      aseprite_color color = image[i];
      if (!aseprite_is_clear(color)) {
        target[i] =
            aseprite_blend(layer->blend_mode, target[i], color, layer->opacity);
      }
    }
  }
  ASEPRITE_FREE(image);
  return result;
}

// Gets the color under all layers. An indexed sprite shows the color of the
// transparent index under its background layer.
static aseprite_color aseprite_backdrop(const aseprite_renderer* r) {
  const aseprite_sprite* sprite = r->sprite;
  if (sprite->depth != ASEPRITE_DEPTH_INDEXED) {
    return (aseprite_color){};
  }
  for (uint32_t i = 0; i < sprite->layer_count; i++) {
    if ((sprite->layers[i].flags & ASEPRITE_LAYER_FLAG_BACKGROUND) != 0 &&
        aseprite_draws_layer(r, i)) {
      return sprite->transparent_index < r->palette->count
                 ? r->palette->colors[sprite->transparent_index]
                 : (aseprite_color){};
    }
  }
  return (aseprite_color){};
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

  aseprite_parser parser = {
      .sprite           = sprite,
      .target           = ASEPRITE_TARGET_OBJECT,
      .target_user_data = &sprite->user_data,
  };
  aseprite_reader file   = aseprite_reader_make(data, size);
  aseprite_result result = aseprite_read_header(&file, sprite);
  if (result == ASEPRITE_OK &&
      sprite->frame_count >
          aseprite_remaining(&file) / aseprite_frame_header_size) {
    result = ASEPRITE_ERROR_TRUNCATED;
  }
  if (result == ASEPRITE_OK) {
    sprite->frames = aseprite_parser_alloc(&parser, sprite->frame_count,
                                           sizeof *sprite->frames);
    if (!sprite->frames) {
      result = ASEPRITE_ERROR_NO_MEMORY;
    }
  }
  if (result == ASEPRITE_OK) {
    result = aseprite_count(&parser, file);
  }
  if (result == ASEPRITE_OK) {
    result = aseprite_read_frames(&parser, file);
  }
  if (result != ASEPRITE_OK) {
    aseprite_arena_free(parser.arena.head);
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
  aseprite_arena_free(sprite->memory);
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
static bool aseprite_has_bits(uint32_t tile, uint32_t mask) {
  return mask != 0 && (tile & mask) == mask;
}

bool aseprite_tile_x_flip(const aseprite_tilemap* tilemap, uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite_has_bits(tile, tilemap->x_flip_mask);
}

bool aseprite_tile_y_flip(const aseprite_tilemap* tilemap, uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite_has_bits(tile, tilemap->y_flip_mask);
}

bool aseprite_tile_diagonal_flip(const aseprite_tilemap* tilemap,
                                 uint32_t tile) {
  ASEPRITE_ASSERT(tilemap);
  return aseprite_has_bits(tile, tilemap->diagonal_flip_mask);
}

double aseprite_fixed_to_double(aseprite_fixed value) {
  return (double)value / 65536.0;
}

bool aseprite_layer_visible(const aseprite_sprite* sprite, uint32_t layer) {
  ASEPRITE_ASSERT(sprite);
  ASEPRITE_ASSERT(layer < sprite->layer_count);
  // A parent comes before its children, so the loop ends.
  for (int64_t i = layer; i >= 0; i = sprite->layers[i].parent) {
    if ((sprite->layers[i].flags & ASEPRITE_LAYER_FLAG_VISIBLE) == 0) {
      return false;
    }
  }
  return true;
}

aseprite_result aseprite_render_frame(const aseprite_sprite* sprite,
                                      uint32_t frame, const bool* layers,
                                      aseprite_color* pixels) {
  ASEPRITE_ASSERT(sprite);
  ASEPRITE_ASSERT(frame < sprite->frame_count);
  ASEPRITE_ASSERT(pixels);
  static const aseprite_palette no_palette = {};
  const aseprite_frame* source             = &sprite->frames[frame];
  aseprite_renderer r                      = {
      .sprite          = sprite,
      .palette         = source->palette < sprite->palette_count
                             ? &sprite->palettes[source->palette]
                             : &no_palette,
      .layers          = layers,
      .bytes_per_pixel = aseprite_bytes_per_pixel(sprite->depth),
      .compose_groups = (sprite->flags & ASEPRITE_SPRITE_FLAG_GROUP_BLEND) != 0,
  };
  size_t count            = (size_t)sprite->width * sprite->height;
  aseprite_color backdrop = aseprite_backdrop(&r);
  for (size_t i = 0; i < count; i++) {
    pixels[i] = backdrop;
  }
  if (sprite->layer_count == 0) {
    return ASEPRITE_OK;
  }

  // One block holds the cel of each layer and the plan items.
  size_t cels_size  = 0;
  size_t items_size = 0;
  size_t size       = 0;
  if (ckd_mul(&cels_size, (size_t)sprite->layer_count, sizeof *r.cels) ||
      ckd_mul(&items_size, (size_t)sprite->layer_count, sizeof *r.items) ||
      ckd_add(&size, cels_size, items_size)) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  void* memory = ASEPRITE_MALLOC(size);
  if (!memory) {
    return ASEPRITE_ERROR_NO_MEMORY;
  }
  r.cels  = memory;
  r.items = (aseprite_plan_item*)(void*)((unsigned char*)memory + cels_size);
  for (uint32_t i = 0; i < sprite->layer_count; i++) {
    r.cels[i] = nullptr;
  }
  for (uint32_t i = 0; i < source->cel_count; i++) {
    const aseprite_cel* cel = &source->cels[i];
    if (!r.cels[cel->layer]) {
      r.cels[cel->layer] = cel;
    }
  }

  // Aseprite draws the background layer first, then the other layers.
  aseprite_result result =
      aseprite_render_layers(&r, -1, 0, 0, ASEPRITE_PASS_BACKGROUND, pixels);
  if (result == ASEPRITE_OK) {
    result = aseprite_render_layers(&r, -1, 0, 0, ASEPRITE_PASS_OTHERS, pixels);
  }
  ASEPRITE_FREE(memory);
  return result;
}

#endif
#endif
