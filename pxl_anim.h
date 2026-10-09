/*
 * pxl_anim.h - sprite animation for pixel-art games.
 *
 * Version: 0.1.0
 * SPDX-License-Identifier: Zlib
 * Copyright (c) 2026 Piotr Usewicz
 *
 * pxl_anim plays frame animations. A sheet has frames with durations, clips
 * and an atlas. A clip is a named run of frames that plays forward, in
 * reverse or ping-pong, a number of times or with no end. A frame has one or
 * more parts, for example a shadow, a body and effects, that you can draw
 * separately. The atlas is one RGBA image with the trimmed image of each part
 * in each frame. A player plays a clip of a sheet.
 *
 * The core uses only the C standard library. Two parts use other libraries
 * of this collection. Include them before this file:
 *
 *   aseprite.h  Adds pxl_anim_load_aseprite(). It makes a sheet from an
 *               Aseprite file: the tags become clips and layers become
 *               parts.
 *   pxl.h       Adds pxl_anim_draw() and pxl_anim_sprite().
 *
 * Include them before this file also in the file with the implementation.
 * Else their functions are not compiled and the link fails.
 *
 * Usage:
 *
 *   #define ASEPRITE_IMPLEMENTATION
 *   #include "aseprite.h"
 *   #define PXL_IMPLEMENTATION
 *   #include "pxl.h"
 *   #define PXL_ANIM_IMPLEMENTATION // In one C file only.
 *   #include "pxl_anim.h"
 *
 *   // Make a sheet. Each part is a layer or a group of the file.
 *   pxl_anim_sheet sheet;
 *   pxl_anim_result result = pxl_anim_load_aseprite(
 *       &sheet, &sprite,
 *       &(pxl_anim_aseprite_desc){
 *           .parts = (const char*[]){"shadow", "body"}, .part_count = 2});
 *   pxl_texture* atlas = pxl_create_texture(
 *       pxl, &(pxl_texture_desc){.width  = (int)sheet.atlas_width,
 *                                .height = (int)sheet.atlas_height,
 *                                .pixels = sheet.atlas});
 *   pxl_anim_free_atlas(&sheet);
 *
 *   // Play a clip. Draw a part with the origin at the feet.
 *   pxl_anim_player orc = {};
 *   pxl_anim_play(&orc, &sheet, pxl_anim_find_clip(&sheet, "Walk"));
 *   pxl_anim_update(&orc, seconds);
 *   pxl_anim_draw(pxl, atlas, &orc, pxl_anim_find_part(&sheet, "body"),
 *                 &(pxl_sprite){.x = x, .y = y, .origin = {50, 60}});
 *   ...
 *   pxl_anim_free(&sheet);
 *
 * Options. Define them before you include this file:
 *
 *   PXL_ANIM_MALLOC(size)  Replace malloc. Define PXL_ANIM_FREE also.
 *   PXL_ANIM_FREE(pointer) Replace free. Define PXL_ANIM_MALLOC also.
 *   PXL_ANIM_ASSERT(expr)  Replace assert.
 *
 * Link with libm (-lm) on Linux.
 */

#ifndef PXL_ANIM_H
#define PXL_ANIM_H

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#error "pxl_anim.h requires C23"
#endif

#include <stddef.h>
#include <stdint.h>

/** The index that the find functions return when there is no match. */
static constexpr uint32_t pxl_anim_none = UINT32_MAX;

/** The default of pxl_anim_aseprite_desc.max_size. */
static constexpr uint32_t pxl_anim_default_max_size = 4096;

/** The result of a sheet build. */
typedef enum pxl_anim_result : uint8_t {
  PXL_ANIM_OK,              /**< The build is successful. */
  PXL_ANIM_ERROR_NO_MEMORY, /**< An allocation failed. */
  PXL_ANIM_ERROR_NO_LAYER,  /**< The file has no layer with a part name. */
  PXL_ANIM_ERROR_TOO_LARGE, /**< The images do not fit in the atlas. */
  PXL_ANIM_ERROR_MALFORMED, /**< The file has a value that is not valid. */
} pxl_anim_result;

/** The order of the frames of a clip. The values are as in Aseprite. */
typedef enum pxl_anim_direction : uint8_t {
  PXL_ANIM_FORWARD           = 0, /**< From the first to the last frame. */
  PXL_ANIM_REVERSE           = 1, /**< From the last to the first frame. */
  PXL_ANIM_PING_PONG         = 2, /**< Forward, then back, and so on. */
  PXL_ANIM_PING_PONG_REVERSE = 3, /**< Back, then forward, and so on. */
} pxl_anim_direction;

/** The bits that pxl_anim_update() and pxl_anim_step() return. */
enum : uint32_t {
  PXL_ANIM_EVENT_FRAME  = 1, /**< A frame started. */
  PXL_ANIM_EVENT_LOOP   = 2, /**< A pass of the clip ended, the next started. */
  PXL_ANIM_EVENT_FINISH = 4, /**< The last pass ended. The frame stays. */
};

/** The image of a part in a frame: an area of the atlas. */
typedef struct pxl_anim_image {
  uint16_t x;        /**< The left edge in the atlas. */
  uint16_t y;        /**< The top edge in the atlas. */
  uint16_t width;    /**< 0 when the part has no pixels in the frame. */
  uint16_t height;   /**< 0 when the part has no pixels in the frame. */
  uint16_t offset_x; /**< The left edge in the frame. */
  uint16_t offset_y; /**< The top edge in the frame. */
} pxl_anim_image;

/**
 * A clip: a run of frames that plays as one animation. An Aseprite tag
 * becomes a clip.
 *
 * A pass plays the frames once in one direction. Ping-pong clips turn after
 * each pass and do not show the turning frame twice: frames 0 to 3 with 2
 * passes show 0 1 2 3 2 1 0.
 */
typedef struct pxl_anim_clip {
  const char* name;
  uint32_t from; /**< The first frame. */
  uint32_t to;   /**< The last frame. Not less than from. */
  pxl_anim_direction direction;
  uint32_t repeat; /**< The number of passes. 0: no end. */
} pxl_anim_clip;

/**
 * Frames, clips and an atlas. pxl_anim_load_aseprite() makes a sheet. The
 * library owns its memory. Release it with pxl_anim_free().
 */
typedef struct pxl_anim_sheet {
  uint32_t width;  /**< The width of a frame. */
  uint32_t height; /**< The height of a frame. */
  uint32_t frame_count;
  uint16_t* durations; /**< The duration of each frame in milliseconds. */
  uint32_t part_count; /**< 1 or more. */
  const char** part_names;
  /** frame_count * part_count images: frame * part_count + part. */
  pxl_anim_image* images;
  uint32_t clip_count;
  pxl_anim_clip* clips;
  uint32_t atlas_width;
  uint32_t atlas_height;
  /**
   * atlas_width * atlas_height RGBA pixels with straight alpha, row by row
   * from top to bottom. nullptr after pxl_anim_free_atlas().
   */
  uint8_t* atlas;
  void* memory; /**< Internal. Do not change. */
} pxl_anim_sheet;

/**
 * Plays a clip of a sheet. Set it to zero, then call pxl_anim_play(). You
 * own it. It does not allocate. Read the fields; change only repeat, speed
 * and paused.
 */
typedef struct pxl_anim_player {
  const pxl_anim_sheet* sheet; /**< nullptr until pxl_anim_play(). */
  uint32_t clip;               /**< The clip index. */
  uint32_t frame;              /**< The frame that shows: a sheet index. */
  /**
   * The number of passes to play. 0: no end. pxl_anim_play() copies it from
   * the clip. Change it to play a clip with an other count.
   */
  uint32_t repeat;
  uint32_t pass; /**< The passes that ended. */
  float time;    /**< The seconds that the frame showed. */
  float speed;   /**< Multiplies the time. 0 means 1. */
  bool paused;   /**< The time does not move. */
  bool finished; /**< The last pass ended. */
  int8_t step;   /**< Internal: 1 or -1, the direction of the pass. */
} pxl_anim_player;

/**
 * Finds a clip by its name.
 *
 * @param sheet The sheet.
 * @param name  The name.
 * @return The index of the first clip with the name, or pxl_anim_none.
 */
uint32_t pxl_anim_find_clip(const pxl_anim_sheet* sheet, const char* name);

/**
 * Finds a part by its name.
 *
 * @param sheet The sheet.
 * @param name  The name.
 * @return The index of the first part with the name, or pxl_anim_none.
 */
uint32_t pxl_anim_find_part(const pxl_anim_sheet* sheet, const char* name);

/**
 * Gets the image of a part in a frame.
 *
 * @param sheet The sheet.
 * @param frame The frame index.
 * @param part  The part index.
 * @return The image. The sheet owns it.
 */
const pxl_anim_image* pxl_anim_get_image(const pxl_anim_sheet* sheet,
                                         uint32_t frame, uint32_t part);

/**
 * Starts a clip at its first frame. The player keeps its speed.
 *
 * @param player The player.
 * @param sheet  The sheet. It must stay valid while the player uses it.
 * @param clip   The clip index.
 * @return false when the sheet has no such clip. The player does not
 *         change then.
 */
bool pxl_anim_play(pxl_anim_player* player, const pxl_anim_sheet* sheet,
                   uint32_t clip);

/**
 * Moves the time of a player forward. It can pass many frames.
 *
 * @param player  The player.
 * @param seconds The time since the last update.
 * @return The PXL_ANIM_EVENT_* bits of what happened, or 0.
 */
uint32_t pxl_anim_update(pxl_anim_player* player, float seconds);

/**
 * Moves the time of a player forward to the start of the next frame at
 * most. Call it in a loop to see each frame, for example to hit on one
 * frame of an attack also when a slow update passes it:
 *
 *   float time = seconds;
 *   uint32_t events;
 *   while ((events = pxl_anim_step(&player, &time)) != 0) {
 *     if ((events & PXL_ANIM_EVENT_FRAME) && player.frame == hit_frame) {
 *       ...
 *     }
 *   }
 *
 * @param player  The player.
 * @param seconds The time to use. Receives the time that is left.
 * @return The PXL_ANIM_EVENT_* bits of the step, or 0 when the time ends
 *         in the frame, the clip finished or the player is paused.
 */
uint32_t pxl_anim_step(pxl_anim_player* player, float* seconds);

/**
 * Releases the memory of a sheet from pxl_anim_load_aseprite() and makes it
 * empty. It is safe to call it on an empty sheet.
 *
 * @param sheet The sheet.
 */
void pxl_anim_free(pxl_anim_sheet* sheet);

/**
 * Releases the atlas pixels of a sheet, for example after you copy them to
 * a texture. The other data stays.
 *
 * @param sheet The sheet.
 */
void pxl_anim_free_atlas(pxl_anim_sheet* sheet);

/**
 * Gets a description of a result.
 *
 * @param result The result.
 * @return A static string.
 */
const char* pxl_anim_result_string(pxl_anim_result result);

#ifdef ASEPRITE_H
/** How pxl_anim_load_aseprite() makes a sheet. Zero values give defaults. */
typedef struct pxl_anim_aseprite_desc {
  /**
   * The names of the parts. A part draws the layers with its name, also
   * when they are hidden, and the layers in the groups with its name that
   * show in the group. nullptr: one part, named "", of the layers that show.
   */
  const char* const* parts;
  size_t part_count;
  /** The largest atlas width and height. 0 means 4096. At most 65535. */
  uint32_t max_size;
} pxl_anim_aseprite_desc;

/**
 * Makes a sheet from an Aseprite file. It renders the parts of each frame
 * with aseprite_render_frame(), trims them and packs them into the atlas
 * with 1 pixel of space between them. Images that are the same share one
 * area of the atlas. The tags become clips. A file without tags gets one
 * clip, named "", of all frames.
 *
 * @param sheet  Receives the sheet. On error it is empty.
 * @param sprite The file. The sheet does not use it after the call.
 * @param desc   The settings, or nullptr for the defaults.
 * @return PXL_ANIM_OK, or the error.
 */
[[__nodiscard__]] pxl_anim_result
pxl_anim_load_aseprite(pxl_anim_sheet* sheet, const aseprite_sprite* sprite,
                       const pxl_anim_aseprite_desc* desc);
#endif

#ifdef PXL_H
/**
 * Makes a pxl_sprite draw the image of a part in a frame. It sets src and
 * moves origin from frame pixels to image pixels. Positions, scale, rotation
 * and flips then work around the same point of the frame for all images.
 *
 * @param sheet  The sheet.
 * @param frame  The frame index.
 * @param part   The part index.
 * @param sprite How to draw the frame. origin is in frame pixels.
 * @return false when the part has no pixels in the frame. Do not draw it
 *         then: pxl draws the whole texture for a src of zero size.
 */
bool pxl_anim_sprite(const pxl_anim_sheet* sheet, uint32_t frame, uint32_t part,
                     pxl_sprite* sprite);

/**
 * Draws a part of the frame that a player shows.
 *
 * @param ctx    The context.
 * @param atlas  A texture with the atlas pixels of the sheet.
 * @param player The player.
 * @param part   The part index.
 * @param sprite How to draw the frame. origin is in frame pixels. The
 *               function ignores src.
 */
void pxl_anim_draw(pxl_context* ctx, pxl_texture* atlas,
                   const pxl_anim_player* player, uint32_t part,
                   const pxl_sprite* sprite);
#endif

#endif

#ifdef PXL_ANIM_IMPLEMENTATION
#ifndef PXL_ANIM_IMPLEMENTATION_INCLUDED
#define PXL_ANIM_IMPLEMENTATION_INCLUDED

#include <math.h>
#include <stdckdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(PXL_ANIM_MALLOC) != defined(PXL_ANIM_FREE)
#error "Define both PXL_ANIM_MALLOC and PXL_ANIM_FREE, or none of them"
#endif

#ifndef PXL_ANIM_MALLOC
#define PXL_ANIM_MALLOC(size) malloc(size)
#define PXL_ANIM_FREE(pointer) free(pointer)
#endif

#ifndef PXL_ANIM_ASSERT
#include <assert.h>
#define PXL_ANIM_ASSERT(expr) assert(expr)
#endif

/* ---- Player ------------------------------------------------------------ */

// Gets the time of a frame in seconds. A frame of 0 milliseconds lasts 1, so
// that time always moves the player.
static double pxl_anim__duration(const pxl_anim_sheet* sheet, uint32_t frame) {
  uint16_t milliseconds = sheet->durations[frame];
  return (milliseconds > 0 ? milliseconds : 1) / 1000.0;
}

static bool pxl_anim__ping_pong(const pxl_anim_clip* clip) {
  return clip->direction == PXL_ANIM_PING_PONG ||
         clip->direction == PXL_ANIM_PING_PONG_REVERSE;
}

// Gets the time after which a player shows the same frames again, and the
// passes in that time. Forward and reverse clips repeat after each pass.
// Ping-pong clips repeat after two passes, without the two turning frames.
static double pxl_anim__cycle(const pxl_anim_sheet* sheet,
                              const pxl_anim_clip* clip, uint32_t* passes) {
  double sum = 0.0;
  for (uint32_t frame = clip->from; frame <= clip->to; frame++) {
    sum += pxl_anim__duration(sheet, frame);
  }
  if (!pxl_anim__ping_pong(clip) || clip->from == clip->to) {
    *passes = 1;
    return sum;
  }
  *passes = 2;
  return (2.0 * sum) - pxl_anim__duration(sheet, clip->from) -
         pxl_anim__duration(sheet, clip->to);
}

// Moves a player to the next frame of its clip. Returns the events.
static uint32_t pxl_anim__next_frame(pxl_anim_player* player) {
  const pxl_anim_clip* clip = &player->sheet->clips[player->clip];
  int64_t next              = (int64_t)player->frame + player->step;
  if (next >= clip->from && next <= clip->to) {
    player->frame = (uint32_t)next;
    return PXL_ANIM_EVENT_FRAME;
  }
  if (player->repeat != 0 && player->pass + 1 >= player->repeat) {
    player->pass     = player->repeat;
    player->finished = true;
    player->time     = 0.0f;
    return PXL_ANIM_EVENT_FINISH;
  }
  player->pass++;
  if (pxl_anim__ping_pong(clip)) {
    player->step = (int8_t)-player->step;
    next         = (int64_t)player->frame + player->step;
    if (next >= clip->from && next <= clip->to) {
      player->frame = (uint32_t)next;
    }
  } else {
    player->frame = player->step > 0 ? clip->from : clip->to;
  }
  return PXL_ANIM_EVENT_FRAME | PXL_ANIM_EVENT_LOOP;
}

static double pxl_anim__speed(const pxl_anim_player* player) {
  PXL_ANIM_ASSERT(player->speed >= 0.0f);
  return player->speed > 0.0f ? (double)player->speed : 1.0;
}

// Tells if the time of a player moves.
static bool pxl_anim__running(const pxl_anim_player* player) {
  return player->sheet && !player->paused && !player->finished;
}

// Tells if a time is a number above 0 and not infinite. NaN fails both
// comparisons. isfinite() of MinGW warns about a double.
static bool pxl_anim__positive(double seconds) {
  return seconds > 0.0 && seconds < (double)INFINITY;
}

// Tells if a ping-pong player shows the first frame of its first pass. Only
// there a cycle does not start and end in the same pass position.
static bool pxl_anim__at_start(const pxl_anim_player* player,
                               const pxl_anim_clip* clip) {
  return pxl_anim__ping_pong(clip) && clip->from != clip->to &&
         player->pass == 0 &&
         player->frame == (player->step > 0 ? clip->from : clip->to);
}

// Skips whole cycles of a long time, so that the time does not take a long
// loop. The player shows the same frame after a cycle. A clip with an end
// skips only the cycles before its last pass.
static uint32_t pxl_anim__skip_cycles(pxl_anim_player* player, double cycle,
                                      uint32_t passes, double* time) {
  double cycles = floor(*time / cycle);
  if (player->repeat == 0) {
    *time = fmod(*time, cycle);
  } else {
    uint32_t left         = player->pass + 1 < player->repeat
                                ? player->repeat - 1 - player->pass
                                : 0;
    uint32_t whole_cycles = left / passes;
    cycles                = fmin(cycles, (double)whole_cycles);
    *time -= cycles * cycle;
  }
  if (cycles < 1.0) {
    return 0;
  }
  player->pass += (uint32_t)fmod(cycles * passes, 4294967296.0);
  return PXL_ANIM_EVENT_FRAME | PXL_ANIM_EVENT_LOOP;
}

// Moves a player by a time in seconds of the clip, to the start of the next
// frame at most. Returns the events and subtracts the time it used.
static uint32_t pxl_anim__advance(pxl_anim_player* player, double* time) {
  double left =
      pxl_anim__duration(player->sheet, player->frame) - (double)player->time;
  left = left > 0.0 ? left : 0.0;
  if (*time < left) {
    player->time = (float)((double)player->time + *time);
    *time        = 0.0;
    return 0;
  }
  *time -= left;
  player->time = 0.0f;
  return pxl_anim__next_frame(player);
}

/* ---- Public functions -------------------------------------------------- */

uint32_t pxl_anim_find_clip(const pxl_anim_sheet* sheet, const char* name) {
  PXL_ANIM_ASSERT(sheet && name);
  for (uint32_t i = 0; i < sheet->clip_count; i++) {
    if (strcmp(sheet->clips[i].name, name) == 0) {
      return i;
    }
  }
  return pxl_anim_none;
}

uint32_t pxl_anim_find_part(const pxl_anim_sheet* sheet, const char* name) {
  PXL_ANIM_ASSERT(sheet && name);
  for (uint32_t i = 0; i < sheet->part_count; i++) {
    if (strcmp(sheet->part_names[i], name) == 0) {
      return i;
    }
  }
  return pxl_anim_none;
}

const pxl_anim_image* pxl_anim_get_image(const pxl_anim_sheet* sheet,
                                         uint32_t frame, uint32_t part) {
  PXL_ANIM_ASSERT(sheet);
  PXL_ANIM_ASSERT(frame < sheet->frame_count && part < sheet->part_count);
  return &sheet->images[((size_t)frame * sheet->part_count) + part];
}

bool pxl_anim_play(pxl_anim_player* player, const pxl_anim_sheet* sheet,
                   uint32_t clip) {
  PXL_ANIM_ASSERT(player && sheet);
  if (clip >= sheet->clip_count) {
    return false;
  }
  const pxl_anim_clip* source = &sheet->clips[clip];
  PXL_ANIM_ASSERT(source->from <= source->to &&
                  source->to < sheet->frame_count);
  bool reverse = source->direction == PXL_ANIM_REVERSE ||
                 source->direction == PXL_ANIM_PING_PONG_REVERSE;
  *player      = (pxl_anim_player){
      .sheet  = sheet,
      .clip   = clip,
      .frame  = reverse ? source->to : source->from,
      .repeat = source->repeat,
      .speed  = player->speed,
      .step   = (int8_t)(reverse ? -1 : 1),
  };
  return true;
}

uint32_t pxl_anim_update(pxl_anim_player* player, float seconds) {
  PXL_ANIM_ASSERT(player);
  if (!pxl_anim__running(player)) {
    return 0;
  }
  // A large speed can make the time infinite.
  double time = (double)seconds * pxl_anim__speed(player);
  if (!pxl_anim__positive(time)) {
    return 0;
  }
  const pxl_anim_clip* clip = &player->sheet->clips[player->clip];
  uint32_t passes           = 1;
  double cycle              = pxl_anim__cycle(player->sheet, clip, &passes);
  bool skipped              = false;
  uint32_t events           = 0;
  while (pxl_anim__running(player) && time > 0.0) {
    if (!skipped && time >= cycle && !pxl_anim__at_start(player, clip)) {
      events |= pxl_anim__skip_cycles(player, cycle, passes, &time);
      skipped = true;
    } else {
      events |= pxl_anim__advance(player, &time);
    }
  }
  return events;
}

uint32_t pxl_anim_step(pxl_anim_player* player, float* seconds) {
  PXL_ANIM_ASSERT(player && seconds);
  if (!pxl_anim__running(player)) {
    return 0;
  }
  double speed = pxl_anim__speed(player);
  double time  = (double)*seconds * speed;
  if (!pxl_anim__positive(time)) {
    return 0;
  }
  uint32_t events = pxl_anim__advance(player, &time);
  *seconds        = (float)(time / speed);
  return events;
}

void pxl_anim_free(pxl_anim_sheet* sheet) {
  PXL_ANIM_ASSERT(sheet);
  PXL_ANIM_FREE(sheet->atlas);
  PXL_ANIM_FREE(sheet->memory);
  *sheet = (pxl_anim_sheet){};
}

void pxl_anim_free_atlas(pxl_anim_sheet* sheet) {
  PXL_ANIM_ASSERT(sheet);
  PXL_ANIM_FREE(sheet->atlas);
  sheet->atlas = nullptr;
}

const char* pxl_anim_result_string(pxl_anim_result result) {
  switch (result) {
  case PXL_ANIM_OK:
    return "success";
  case PXL_ANIM_ERROR_NO_MEMORY:
    return "out of memory";
  case PXL_ANIM_ERROR_NO_LAYER:
    return "the file has no layer with a part name";
  case PXL_ANIM_ERROR_TOO_LARGE:
    return "the images do not fit in the atlas";
  case PXL_ANIM_ERROR_MALFORMED:
    return "the file has a value that is not valid";
  default:
    return "unknown result";
  }
}

/* ---- Aseprite ---------------------------------------------------------- */

#ifdef ASEPRITE_H

// A distinct image: the trimmed pixels of a part in a frame.
typedef struct pxl_anim__unique {
  uint32_t width;
  uint32_t height;
  uint64_t hash;
  size_t pixels; // The first pixel in the pool.
  uint32_t x;    // The place in the atlas.
  uint32_t y;
} pxl_anim__unique;

// An area of the canvas.
typedef struct pxl_anim__box {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} pxl_anim__box;

// The image of a part in a frame.
typedef struct pxl_anim__entry {
  uint32_t unique; // The index of the distinct image, or pxl_anim_none.
  uint16_t offset_x;
  uint16_t offset_y;
} pxl_anim__entry;

// The state of a build. The pool holds the pixels of the distinct images.
typedef struct pxl_anim__builder {
  const aseprite_sprite* sprite;
  const pxl_anim_aseprite_desc* desc;
  uint32_t part_count;
  bool* mask; // layer_count flags, or nullptr for one part of all layers.
  aseprite_color* canvas;
  pxl_anim__entry* entries;
  pxl_anim__unique* uniques;
  size_t unique_count;
  aseprite_color* pool;
  size_t pool_size;
  size_t pool_capacity;
} pxl_anim__builder;

static void pxl_anim__free_builder(pxl_anim__builder* b) {
  PXL_ANIM_FREE(b->mask);
  PXL_ANIM_FREE(b->canvas);
  PXL_ANIM_FREE(b->entries);
  PXL_ANIM_FREE(b->uniques);
  PXL_ANIM_FREE(b->pool);
}

// Allocates count items of a size. count is not 0. Returns nullptr on
// failure.
static void* pxl_anim__alloc(size_t count, size_t size) {
  PXL_ANIM_ASSERT(count > 0);
  size_t bytes = 0;
  if (ckd_mul(&bytes, count, size)) {
    return nullptr;
  }
  return PXL_ANIM_MALLOC(bytes);
}

// Rounds a size up to the alignment of all types.
static bool pxl_anim__align(size_t size, size_t* out) {
  constexpr size_t alignment = alignof(max_align_t);
  if (ckd_add(out, size, alignment - 1)) {
    return false;
  }
  *out &= ~(alignment - 1);
  return true;
}

// Adds space for count items of a size to a total. Returns false on
// overflow.
static bool pxl_anim__reserve(size_t* total, size_t count, size_t size) {
  size_t bytes = 0;
  return !ckd_mul(&bytes, count, size) && pxl_anim__align(bytes, &bytes) &&
         !ckd_add(total, *total, bytes);
}

// Takes count items of a size from a block that pxl_anim__reserve() sized.
static void* pxl_anim__take(unsigned char** cursor, size_t count, size_t size) {
  void* result = *cursor;
  size_t bytes = 0;
  (void)pxl_anim__align(count * size, &bytes);
  *cursor += bytes;
  return result;
}

// Tells if a layer shows inside a group: the layer and its parents up to
// the group are visible.
static bool pxl_anim__shows_in(const aseprite_sprite* sprite, uint32_t layer,
                               uint32_t group) {
  for (int64_t i = layer; i > group; i = sprite->layers[i].parent) {
    if ((sprite->layers[i].flags & ASEPRITE_LAYER_FLAG_VISIBLE) == 0) {
      return false;
    }
  }
  return true;
}

// Selects the layers of a part. Returns false when no layer has the name.
static bool pxl_anim__select(const aseprite_sprite* sprite, const char* name,
                             bool* mask) {
  bool found = false;
  memset(mask, 0, sprite->layer_count * sizeof *mask);
  for (uint32_t i = 0; i < sprite->layer_count; i++) {
    const aseprite_layer* layer = &sprite->layers[i];
    if (!layer->name || strcmp(layer->name, name) != 0) {
      continue;
    }
    found   = true;
    mask[i] = true;
    // The layers of a group follow it in the file.
    for (uint32_t j = i + 1;
         j < sprite->layer_count && sprite->layers[j].parent >= (int32_t)i;
         j++) {
      mask[j] = mask[j] || pxl_anim__shows_in(sprite, j, i);
    }
  }
  return found;
}

// Finds the smallest area with all the pixels that are not transparent.
// Returns false when there is none.
static bool pxl_anim__trim(const aseprite_color* canvas, uint32_t width,
                           uint32_t height, pxl_anim__box* box) {
  uint32_t left   = width;
  uint32_t top    = height;
  uint32_t right  = 0;
  uint32_t bottom = 0;
  for (uint32_t y = 0; y < height; y++) {
    const aseprite_color* row = canvas + ((size_t)y * width);
    for (uint32_t x = 0; x < width; x++) {
      if (row[x].a != 0) {
        left   = x < left ? x : left;
        right  = x + 1 > right ? x + 1 : right;
        top    = y < top ? y : top;
        bottom = y + 1;
      }
    }
  }
  *box = (pxl_anim__box){
      .x      = left,
      .y      = top,
      .width  = right > left ? right - left : 0,
      .height = bottom > top ? bottom - top : 0,
  };
  return box->width > 0;
}

// Makes transparent pixels 0, so that images that look the same are the
// same bytes.
static aseprite_color pxl_anim__clean(aseprite_color color) {
  return color.a == 0 ? (aseprite_color){} : color;
}

// Gets the pixel of an area of the canvas.
static aseprite_color pxl_anim__pixel(const pxl_anim__builder* b,
                                      const pxl_anim__box* box, uint32_t x,
                                      uint32_t y) {
  size_t row = (size_t)box->y + y;
  return pxl_anim__clean(b->canvas[(row * b->sprite->width) + box->x + x]);
}

// Hashes the pixels of an area of the canvas with FNV-1a.
static uint64_t pxl_anim__hash(const pxl_anim__builder* b,
                               const pxl_anim__box* box) {
  uint64_t hash = 14695981039346656037U;
  for (uint32_t y = 0; y < box->height; y++) {
    for (uint32_t x = 0; x < box->width; x++) {
      aseprite_color color  = pxl_anim__pixel(b, box, x, y);
      const uint8_t bytes[] = {color.r, color.g, color.b, color.a};
      for (size_t i = 0; i < sizeof bytes; i++) {
        hash = (hash ^ bytes[i]) * 1099511628211U;
      }
    }
  }
  return hash;
}

// Tells if a distinct image has the pixels of an area of the canvas.
static bool pxl_anim__same(const pxl_anim__builder* b,
                           const pxl_anim__unique* unique,
                           const pxl_anim__box* box) {
  const aseprite_color* pixels = b->pool + unique->pixels;
  for (uint32_t y = 0; y < box->height; y++) {
    for (uint32_t x = 0; x < box->width; x++) {
      aseprite_color a = pxl_anim__pixel(b, box, x, y);
      aseprite_color c = pixels[((size_t)y * box->width) + x];
      if (a.r != c.r || a.g != c.g || a.b != c.b || a.a != c.a) {
        return false;
      }
    }
  }
  return true;
}

// Makes space for count more pixels in the pool.
static bool pxl_anim__grow_pool(pxl_anim__builder* b, size_t count) {
  size_t needed = 0;
  if (ckd_add(&needed, b->pool_size, count)) {
    return false;
  }
  if (b->pool && needed <= b->pool_capacity) {
    return true;
  }
  size_t capacity = b->pool_capacity > 0 ? b->pool_capacity : 4096;
  while (capacity < needed) {
    if (ckd_mul(&capacity, capacity, (size_t)2)) {
      return false;
    }
  }
  aseprite_color* pool = pxl_anim__alloc(capacity, sizeof *pool);
  if (!pool) {
    return false;
  }
  if (b->pool_size > 0) {
    memcpy(pool, b->pool, b->pool_size * sizeof *pool);
  }
  PXL_ANIM_FREE(b->pool);
  b->pool          = pool;
  b->pool_capacity = capacity;
  return true;
}

// Adds the image of an area of the canvas to the distinct images, if it is
// new. Returns its index, or pxl_anim_none when the pool cannot grow.
static uint32_t pxl_anim__add_unique(pxl_anim__builder* b,
                                     const pxl_anim__box* box) {
  uint64_t hash = pxl_anim__hash(b, box);
  for (size_t i = 0; i < b->unique_count; i++) {
    const pxl_anim__unique* unique = &b->uniques[i];
    if (unique->hash == hash && unique->width == box->width &&
        unique->height == box->height && pxl_anim__same(b, unique, box)) {
      return (uint32_t)i;
    }
  }
  size_t count = (size_t)box->width * box->height;
  if (!pxl_anim__grow_pool(b, count)) {
    return pxl_anim_none;
  }
  aseprite_color* pixels = b->pool + b->pool_size;
  for (uint32_t y = 0; y < box->height; y++) {
    for (uint32_t x = 0; x < box->width; x++) {
      pixels[((size_t)y * box->width) + x] = pxl_anim__pixel(b, box, x, y);
    }
  }
  b->uniques[b->unique_count] = (pxl_anim__unique){
      .width  = box->width,
      .height = box->height,
      .hash   = hash,
      .pixels = b->pool_size,
  };
  b->pool_size += count;
  return (uint32_t)b->unique_count++;
}

// Renders and trims the image of each part in each frame.
static pxl_anim_result pxl_anim__collect(pxl_anim__builder* b) {
  const aseprite_sprite* sprite = b->sprite;
  for (uint32_t part = 0; part < b->part_count; part++) {
    if (b->mask) {
      (void)pxl_anim__select(sprite, b->desc->parts[part], b->mask);
    }
    for (uint32_t frame = 0; frame < sprite->frame_count; frame++) {
      aseprite_result result =
          aseprite_render_frame(sprite, frame, b->mask, b->canvas);
      if (result != ASEPRITE_OK) {
        return result == ASEPRITE_ERROR_NO_MEMORY ? PXL_ANIM_ERROR_NO_MEMORY
                                                  : PXL_ANIM_ERROR_MALFORMED;
      }
      pxl_anim__entry* entry =
          &b->entries[((size_t)frame * b->part_count) + part];
      *entry            = (pxl_anim__entry){.unique = pxl_anim_none};
      pxl_anim__box box = {};
      if (!pxl_anim__trim(b->canvas, sprite->width, sprite->height, &box)) {
        continue;
      }
      entry->unique = pxl_anim__add_unique(b, &box);
      if (entry->unique == pxl_anim_none) {
        return PXL_ANIM_ERROR_NO_MEMORY;
      }
      entry->offset_x = (uint16_t)box.x;
      entry->offset_y = (uint16_t)box.y;
    }
  }
  return PXL_ANIM_OK;
}

// Orders distinct images for the packing: the tallest first.
static int pxl_anim__compare_heights(const void* a, const void* b) {
  const pxl_anim__unique* x = *(const pxl_anim__unique* const*)a;
  const pxl_anim__unique* y = *(const pxl_anim__unique* const*)b;
  if (x->height != y->height) {
    return x->height > y->height ? -1 : 1;
  }
  if (x->width != y->width) {
    return x->width > y->width ? -1 : 1;
  }
  if (x->pixels != y->pixels) {
    return x->pixels < y->pixels ? -1 : 1;
  }
  return 0;
}

// Packs images in rows, with 1 pixel between them, in an atlas of a width.
// Gets the size that the images use.
static void pxl_anim__pack_rows(pxl_anim__unique** order, size_t count,
                                uint64_t width, uint64_t* used_width,
                                uint64_t* used_height) {
  uint64_t x          = 0;
  uint64_t y          = 0;
  uint64_t row_height = 0;
  *used_width         = 0;
  *used_height        = 0;
  for (size_t i = 0; i < count; i++) {
    pxl_anim__unique* unique = order[i];
    if (x > 0 && x + unique->width > width) {
      x = 0;
      y += row_height + 1;
      row_height = 0;
    }
    unique->x = (uint32_t)x;
    unique->y = (uint32_t)y;
    x += unique->width;
    *used_width  = x > *used_width ? x : *used_width;
    row_height   = unique->height > row_height ? unique->height : row_height;
    *used_height = y + row_height;
    x++;
  }
}

// Places the distinct images in the atlas. It tries widths that are powers
// of two, from about the square root of the area.
static pxl_anim_result pxl_anim__pack(pxl_anim__builder* b, uint32_t max_size,
                                      uint32_t* width, uint32_t* height) {
  *width  = 1;
  *height = 1;
  if (b->unique_count == 0) {
    return PXL_ANIM_OK;
  }
  pxl_anim__unique** order = pxl_anim__alloc(b->unique_count, sizeof *order);
  if (!order) {
    return PXL_ANIM_ERROR_NO_MEMORY;
  }
  uint64_t area   = 0;
  uint64_t widest = 0;
  for (size_t i = 0; i < b->unique_count; i++) {
    order[i] = &b->uniques[i];
    area += (uint64_t)(order[i]->width + 1) * (order[i]->height + 1);
    widest = order[i]->width > widest ? order[i]->width : widest;
  }
  qsort(order, b->unique_count, sizeof *order, pxl_anim__compare_heights);

  uint64_t try_width = 1;
  while (try_width < widest || try_width * try_width < area) {
    try_width *= 2;
  }
  pxl_anim_result result = PXL_ANIM_ERROR_TOO_LARGE;
  for (; try_width / 2 < max_size; try_width *= 2) {
    uint64_t used_width  = 0;
    uint64_t used_height = 0;
    uint64_t limit       = try_width < max_size ? try_width : max_size;
    pxl_anim__pack_rows(order, b->unique_count, limit, &used_width,
                        &used_height);
    if (used_width <= max_size && used_height <= max_size) {
      *width  = (uint32_t)used_width;
      *height = (uint32_t)used_height;
      result  = PXL_ANIM_OK;
      break;
    }
  }
  PXL_ANIM_FREE(order);
  return result;
}

// Gets the name of a part.
static const char* pxl_anim__part_name(const pxl_anim__builder* b,
                                       uint32_t part) {
  return b->mask ? b->desc->parts[part] : "";
}

// Gets the name of a tag. A tag can have no name.
static const char* pxl_anim__tag_name(const aseprite_tag* tag) {
  return tag->name ? tag->name : "";
}

// Gets the number of clips: one for each tag, or one of all frames when
// there are no tags.
static uint32_t pxl_anim__clip_count(const aseprite_sprite* sprite) {
  return sprite->tag_count > 0 ? sprite->tag_count : 1;
}

// Copies a string into a block.
static const char* pxl_anim__copy_string(unsigned char** cursor,
                                         const char* text) {
  size_t size  = strlen(text) + 1;
  char* result = pxl_anim__take(cursor, size, 1);
  memcpy(result, text, size);
  return result;
}

// Gets the size of the block of a sheet: everything but the atlas.
static bool pxl_anim__sheet_size(const pxl_anim__builder* b, size_t* size) {
  const aseprite_sprite* sprite = b->sprite;
  size_t image_count            = (size_t)sprite->frame_count * b->part_count;
  bool ok = pxl_anim__reserve(size, image_count, sizeof(pxl_anim_image)) &&
            pxl_anim__reserve(size, pxl_anim__clip_count(sprite),
                              sizeof(pxl_anim_clip)) &&
            pxl_anim__reserve(size, b->part_count, sizeof(const char*)) &&
            pxl_anim__reserve(size, sprite->frame_count, sizeof(uint16_t));
  for (uint32_t i = 0; ok && i < b->part_count; i++) {
    ok = pxl_anim__reserve(size, strlen(pxl_anim__part_name(b, i)) + 1, 1);
  }
  for (uint32_t i = 0; ok && i < sprite->tag_count; i++) {
    ok = pxl_anim__reserve(size,
                           strlen(pxl_anim__tag_name(&sprite->tags[i])) + 1, 1);
  }
  return ok && pxl_anim__reserve(size, 1, 1);
}

// Converts a tag to a clip. Unknown directions play forward.
static pxl_anim_clip pxl_anim__clip(const aseprite_tag* tag,
                                    unsigned char** cursor) {
  bool known = tag->direction <= ASEPRITE_DIRECTION_PING_PONG_REVERSE;
  return (pxl_anim_clip){
      .name = pxl_anim__copy_string(cursor, pxl_anim__tag_name(tag)),
      .from = tag->from,
      .to   = tag->to,
      .direction =
          known ? (pxl_anim_direction)tag->direction : PXL_ANIM_FORWARD,
      .repeat = tag->repeat,
  };
}

// Copies the distinct images into the atlas.
static void pxl_anim__fill_atlas(const pxl_anim__builder* b, uint8_t* atlas,
                                 uint32_t atlas_width, size_t atlas_size) {
  memset(atlas, 0, atlas_size);
  for (size_t i = 0; i < b->unique_count; i++) {
    const pxl_anim__unique* unique = &b->uniques[i];
    for (uint32_t y = 0; y < unique->height; y++) {
      size_t row = (size_t)unique->y + y;
      memcpy(atlas + (((row * atlas_width) + unique->x) * 4),
             b->pool + unique->pixels + ((size_t)y * unique->width),
             (size_t)unique->width * 4);
    }
  }
}

// Makes the sheet from the distinct images and the tags.
static pxl_anim_result pxl_anim__finish(const pxl_anim__builder* b,
                                        uint32_t atlas_width,
                                        uint32_t atlas_height,
                                        pxl_anim_sheet* sheet) {
  PXL_ANIM_ASSERT(atlas_width > 0 && atlas_height > 0);
  const aseprite_sprite* sprite = b->sprite;
  size_t size                   = 0;
  size_t atlas_size             = 0;
  bool ok = pxl_anim__sheet_size(b, &size) &&
            !ckd_mul(&atlas_size, (size_t)atlas_width, (size_t)atlas_height) &&
            !ckd_mul(&atlas_size, atlas_size, (size_t)4);
  unsigned char* memory = ok ? PXL_ANIM_MALLOC(size) : nullptr;
  uint8_t* atlas        = memory ? PXL_ANIM_MALLOC(atlas_size) : nullptr;
  if (!atlas) {
    PXL_ANIM_FREE(memory);
    return PXL_ANIM_ERROR_NO_MEMORY;
  }

  size_t image_count    = (size_t)sprite->frame_count * b->part_count;
  unsigned char* cursor = memory;
  *sheet                = (pxl_anim_sheet){
      .width        = sprite->width,
      .height       = sprite->height,
      .frame_count  = sprite->frame_count,
      .part_count   = b->part_count,
      .clip_count   = pxl_anim__clip_count(sprite),
      .atlas_width  = atlas_width,
      .atlas_height = atlas_height,
      .atlas        = atlas,
      .memory       = memory,
  };
  sheet->images = pxl_anim__take(&cursor, image_count, sizeof *sheet->images);
  sheet->clips =
      pxl_anim__take(&cursor, sheet->clip_count, sizeof *sheet->clips);
  sheet->part_names =
      pxl_anim__take(&cursor, b->part_count, sizeof *sheet->part_names);
  sheet->durations =
      pxl_anim__take(&cursor, sprite->frame_count, sizeof *sheet->durations);
  for (uint32_t i = 0; i < sprite->frame_count; i++) {
    sheet->durations[i] = sprite->frames[i].duration;
  }
  for (uint32_t i = 0; i < b->part_count; i++) {
    sheet->part_names[i] =
        pxl_anim__copy_string(&cursor, pxl_anim__part_name(b, i));
  }
  for (uint32_t i = 0; i < sprite->tag_count; i++) {
    sheet->clips[i] = pxl_anim__clip(&sprite->tags[i], &cursor);
  }
  if (sprite->tag_count == 0) {
    sheet->clips[0] = (pxl_anim_clip){
        .name = pxl_anim__copy_string(&cursor, ""),
        .to   = sprite->frame_count - 1,
    };
  }
  for (size_t i = 0; i < image_count; i++) {
    const pxl_anim__entry* entry = &b->entries[i];
    sheet->images[i]             = (pxl_anim_image){};
    if (entry->unique != pxl_anim_none) {
      const pxl_anim__unique* unique = &b->uniques[entry->unique];
      sheet->images[i]               = (pxl_anim_image){
          .x        = (uint16_t)unique->x,
          .y        = (uint16_t)unique->y,
          .width    = (uint16_t)unique->width,
          .height   = (uint16_t)unique->height,
          .offset_x = entry->offset_x,
          .offset_y = entry->offset_y,
      };
    }
  }
  pxl_anim__fill_atlas(b, atlas, atlas_width, atlas_size);
  return PXL_ANIM_OK;
}

// Allocates the memory of a build and checks the names of the parts.
static pxl_anim_result pxl_anim__start(pxl_anim__builder* b) {
  const aseprite_sprite* sprite = b->sprite;
  size_t entry_count            = 0;
  if (ckd_mul(&entry_count, (size_t)sprite->frame_count, b->part_count)) {
    return PXL_ANIM_ERROR_NO_MEMORY;
  }
  if (b->desc->part_count > 0) {
    if (sprite->layer_count == 0) {
      return PXL_ANIM_ERROR_NO_LAYER;
    }
    b->mask = pxl_anim__alloc(sprite->layer_count, sizeof *b->mask);
    if (!b->mask) {
      return PXL_ANIM_ERROR_NO_MEMORY;
    }
  }
  b->canvas  = pxl_anim__alloc((size_t)sprite->width * sprite->height,
                               sizeof *b->canvas);
  b->entries = pxl_anim__alloc(entry_count, sizeof *b->entries);
  b->uniques = pxl_anim__alloc(entry_count, sizeof *b->uniques);
  if (!b->canvas || !b->entries || !b->uniques) {
    return PXL_ANIM_ERROR_NO_MEMORY;
  }
  for (uint32_t i = 0; i < b->desc->part_count; i++) {
    PXL_ANIM_ASSERT(b->desc->parts[i]);
    if (!pxl_anim__select(sprite, b->desc->parts[i], b->mask)) {
      return PXL_ANIM_ERROR_NO_LAYER;
    }
  }
  return PXL_ANIM_OK;
}

pxl_anim_result pxl_anim_load_aseprite(pxl_anim_sheet* sheet,
                                       const aseprite_sprite* sprite,
                                       const pxl_anim_aseprite_desc* desc) {
  PXL_ANIM_ASSERT(sheet && sprite);
  static const pxl_anim_aseprite_desc defaults = {};
  desc                                         = desc ? desc : &defaults;
  *sheet                                       = (pxl_anim_sheet){};
  PXL_ANIM_ASSERT(desc->part_count == 0 || desc->parts);
  uint32_t max_size =
      desc->max_size > 0 ? desc->max_size : pxl_anim_default_max_size;
  max_size = max_size < UINT16_MAX ? max_size : UINT16_MAX;
  // The sheet counts parts in 32 bits.
  uint32_t part_count = 0;
  if (sprite->frame_count == 0 || sprite->width == 0 || sprite->height == 0 ||
      ckd_add(&part_count, desc->part_count, (size_t)0)) {
    return PXL_ANIM_ERROR_MALFORMED;
  }

  pxl_anim__builder b = {
      .sprite     = sprite,
      .desc       = desc,
      .part_count = part_count > 0 ? part_count : 1,
  };
  pxl_anim_result result = pxl_anim__start(&b);
  if (result == PXL_ANIM_OK) {
    result = pxl_anim__collect(&b);
  }
  uint32_t atlas_width  = 0;
  uint32_t atlas_height = 0;
  if (result == PXL_ANIM_OK) {
    result = pxl_anim__pack(&b, max_size, &atlas_width, &atlas_height);
  }
  if (result == PXL_ANIM_OK) {
    result = pxl_anim__finish(&b, atlas_width, atlas_height, sheet);
  }
  pxl_anim__free_builder(&b);
  return result;
}

#endif

/* ---- pxl --------------------------------------------------------------- */

#ifdef PXL_H

bool pxl_anim_sprite(const pxl_anim_sheet* sheet, uint32_t frame, uint32_t part,
                     pxl_sprite* sprite) {
  PXL_ANIM_ASSERT(sprite);
  const pxl_anim_image* image = pxl_anim_get_image(sheet, frame, part);
  if (image->width == 0 || image->height == 0) {
    return false;
  }
  sprite->src = (pxl_rect){
      .x = image->x,
      .y = image->y,
      .w = image->width,
      .h = image->height,
  };
  sprite->origin.x -= image->offset_x;
  sprite->origin.y -= image->offset_y;
  return true;
}

void pxl_anim_draw(pxl_context* ctx, pxl_texture* atlas,
                   const pxl_anim_player* player, uint32_t part,
                   const pxl_sprite* sprite) {
  PXL_ANIM_ASSERT(player && sprite);
  if (!player->sheet) {
    return;
  }
  pxl_sprite copy = *sprite;
  if (pxl_anim_sprite(player->sheet, player->frame, part, &copy)) {
    pxl_draw_sprite(ctx, atlas, &copy);
  }
}

#endif

#endif
#endif
