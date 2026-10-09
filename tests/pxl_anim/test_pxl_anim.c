// Tests for pxl_anim.h. Usage: test_pxl_anim FIXTURE_DIR
//
// tests/pxl_anim/fixtures.rb writes the fixture files.

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"
#define PXL_ANIM_IMPLEMENTATION
#include "pxl_anim.h"
// A second include must not define the implementation again.
#include "pxl_anim.h" // NOLINT(readability-duplicate-include)

#define PICO_UNIT_IMPLEMENTATION
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/pxl_anim";

/* ---- Sheets in memory -------------------------------------------------- */

static constexpr size_t max_frames = 8;

// A sheet without images, for the player tests.
typedef struct test_sheet {
  pxl_anim_sheet sheet;
  uint16_t durations[max_frames];
  pxl_anim_clip clip;
} test_sheet;

// Makes a sheet of frames of 100 ms with one clip.
static void make_sheet(test_sheet* t, uint32_t from, uint32_t to,
                       pxl_anim_direction direction, uint32_t repeat) {
  *t = (test_sheet){
      .sheet = {.frame_count = max_frames, .part_count = 1, .clip_count = 1},
      .clip =
          {
              .name      = "clip",
              .from      = from,
              .to        = to,
              .direction = direction,
              .repeat    = repeat,
          },
  };
  for (size_t i = 0; i < max_frames; i++) {
    t->durations[i] = 100;
  }
  t->sheet.durations = t->durations;
  t->sheet.clips     = &t->clip;
}

// Plays a clip frame by frame and writes the frames that show, as digits.
// The text ends with "." when the clip finishes.
static void play_frames(const test_sheet* t, uint32_t repeat_override,
                        char* text, size_t size) {
  pxl_anim_player player = {};
  pxl_anim_play(&player, &t->sheet, 0);
  if (repeat_override != pxl_anim_none) {
    player.repeat = repeat_override;
  }
  size_t length  = 0;
  text[length++] = (char)('0' + player.frame);
  while (length + 1 < size) {
    float time      = 0.1f;
    uint32_t events = 0;
    while (events == 0 && time > 0.0f) {
      events = pxl_anim_step(&player, &time);
    }
    if (events & PXL_ANIM_EVENT_FINISH) {
      text[length++] = '.';
      break;
    }
    text[length++] = (char)('0' + player.frame);
  }
  text[length] = '\0';
}

// Tells if a clip shows the expected frames.
static bool check_frames(uint32_t from, uint32_t to,
                         pxl_anim_direction direction, uint32_t repeat,
                         const char* expected) {
  test_sheet t;
  make_sheet(&t, from, to, direction, repeat);
  char actual[32];
  play_frames(&t, pxl_anim_none, actual, strlen(expected) + 1);
  if (strcmp(actual, expected) != 0) {
    fprintf(stderr, "frames %s, expected %s\n", actual, expected);
    return false;
  }
  return true;
}

/* ---- Player ------------------------------------------------------------ */

// The expected frames come from the playback tests of Aseprite
// (src/doc/playback_tests.cpp).
TEST_CASE(test_directions) {
  REQUIRE(check_frames(1, 2, PXL_ANIM_FORWARD, 2, "1212."));
  REQUIRE(check_frames(0, 3, PXL_ANIM_REVERSE, 2, "32103210."));
  REQUIRE(check_frames(0, 3, PXL_ANIM_PING_PONG, 2, "0123210."));
  REQUIRE(check_frames(0, 3, PXL_ANIM_PING_PONG_REVERSE, 2, "3210123."));
  REQUIRE(check_frames(0, 3, PXL_ANIM_PING_PONG_REVERSE, 1, "3210."));
  REQUIRE(check_frames(0, 1, PXL_ANIM_PING_PONG, 3, "0101."));
  return true;
}

TEST_CASE(test_no_end) {
  REQUIRE(check_frames(0, 2, PXL_ANIM_FORWARD, 0, "012012012012"));
  REQUIRE(check_frames(0, 3, PXL_ANIM_PING_PONG, 0, "0123210123210"));
  REQUIRE(check_frames(0, 2, PXL_ANIM_PING_PONG_REVERSE, 0, "2101210121"));
  return true;
}

TEST_CASE(test_one_frame) {
  REQUIRE(check_frames(4, 4, PXL_ANIM_FORWARD, 3, "444."));
  REQUIRE(check_frames(4, 4, PXL_ANIM_PING_PONG, 3, "444."));
  REQUIRE(check_frames(4, 4, PXL_ANIM_PING_PONG_REVERSE, 0, "44444"));
  return true;
}

TEST_CASE(test_repeat_override) {
  test_sheet t;
  make_sheet(&t, 0, 1, PXL_ANIM_FORWARD, 2);
  char text[16];
  play_frames(&t, 0, text, 8);
  REQUIRE(strcmp(text, "0101010") == 0);
  play_frames(&t, 1, text, 8);
  REQUIRE(strcmp(text, "01.") == 0);
  return true;
}

TEST_CASE(test_update_events) {
  test_sheet t;
  make_sheet(&t, 0, 2, PXL_ANIM_FORWARD, 2);
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_update(&player, 1.0f) == 0);
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(pxl_anim_update(&player, 0.05f) == 0);
  REQUIRE(player.frame == 0);
  REQUIRE(pxl_anim_update(&player, 0.05f) == PXL_ANIM_EVENT_FRAME);
  REQUIRE(player.frame == 1);
  REQUIRE(pxl_anim_update(&player, 0.25f) ==
          (PXL_ANIM_EVENT_FRAME | PXL_ANIM_EVENT_LOOP));
  REQUIRE(player.frame == 0 && player.pass == 1);
  REQUIRE(player.time > 0.049f && player.time < 0.051f);
  REQUIRE(pxl_anim_update(&player, 10.0f) ==
          (PXL_ANIM_EVENT_FRAME | PXL_ANIM_EVENT_FINISH));
  REQUIRE(player.finished && player.frame == 2 && player.pass == 2);
  REQUIRE(pxl_anim_update(&player, 1.0f) == 0);
  return true;
}

TEST_CASE(test_speed_and_pause) {
  test_sheet t;
  make_sheet(&t, 0, 3, PXL_ANIM_FORWARD, 0);
  pxl_anim_player player = {.speed = 2.0f};
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(player.speed == 2.0f);
  REQUIRE(pxl_anim_update(&player, 0.075f) == PXL_ANIM_EVENT_FRAME);
  REQUIRE(player.frame == 1);
  player.paused = true;
  REQUIRE(pxl_anim_update(&player, 1.0f) == 0);
  REQUIRE(player.frame == 1);
  player.paused = false;
  player.speed  = 0.0f;
  REQUIRE(pxl_anim_update(&player, 0.05f) == PXL_ANIM_EVENT_FRAME);
  REQUIRE(player.frame == 2);
  return true;
}

TEST_CASE(test_bad_times) {
  test_sheet t;
  make_sheet(&t, 0, 3, PXL_ANIM_FORWARD, 0);
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(pxl_anim_update(&player, -1.0f) == 0);
  REQUIRE(pxl_anim_update(&player, 0.0f) == 0);
  REQUIRE(pxl_anim_update(&player, INFINITY) == 0);
  REQUIRE(pxl_anim_update(&player, NAN) == 0);
  float time = NAN;
  REQUIRE(pxl_anim_step(&player, &time) == 0);
  REQUIRE(player.frame == 0 && player.time == 0.0f);

  // The largest speed and time: the time is in double precision, so it
  // stays finite, and the update ends at once.
  player.speed = FLT_MAX;
  pxl_anim_update(&player, FLT_MAX);
  REQUIRE(player.frame <= 3 && !player.finished);
  return true;
}

TEST_CASE(test_long_time) {
  // The frame after 1000000.25 seconds: 0.25 s into a cycle of 0.4 s.
  test_sheet t;
  make_sheet(&t, 0, 3, PXL_ANIM_FORWARD, 0);
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(pxl_anim_update(&player, 1000000.25f) ==
          (PXL_ANIM_EVENT_FRAME | PXL_ANIM_EVENT_LOOP));
  REQUIRE(player.frame == 2);

  // Ping-pong: after the first frame, cycles of 0.6 s show 1 2 3 2 1 0.
  // 1000.25 s is 0.55 s into a cycle: frame 0 at the end of pass 3333.
  make_sheet(&t, 0, 3, PXL_ANIM_PING_PONG, 0);
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  pxl_anim_update(&player, 1000.25f);
  REQUIRE(player.frame == 0 && player.pass == 3333 && player.step < 0);

  // A clip with an end finishes on its last frame.
  make_sheet(&t, 0, 3, PXL_ANIM_PING_PONG, 5);
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(pxl_anim_update(&player, 1.0e9f) & PXL_ANIM_EVENT_FINISH);
  REQUIRE(player.finished && player.frame == 3 && player.pass == 5);
  return true;
}

TEST_CASE(test_zero_durations) {
  test_sheet t;
  make_sheet(&t, 0, 3, PXL_ANIM_PING_PONG, 0);
  for (size_t i = 0; i < max_frames; i++) {
    t.durations[i] = 0;
  }
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  // A frame of 0 ms lasts 1 ms.
  pxl_anim_update(&player, 0.0015f);
  REQUIRE(player.frame == 1);
  pxl_anim_update(&player, 100000.0f);
  REQUIRE(!player.finished);

  t.clip.repeat = 3;
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(pxl_anim_update(&player, 1.0f) & PXL_ANIM_EVENT_FINISH);
  REQUIRE(player.frame == 3);
  return true;
}

TEST_CASE(test_step_sees_each_frame) {
  test_sheet t;
  make_sheet(&t, 2, 5, PXL_ANIM_FORWARD, 1);
  pxl_anim_player player = {};
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  float time      = 10.0f;
  uint32_t events = 0;
  char frames[8]  = {};
  size_t count    = 0;
  while ((events = pxl_anim_step(&player, &time)) != 0) {
    if (events & PXL_ANIM_EVENT_FRAME) {
      frames[count++] = (char)('0' + player.frame);
    }
  }
  REQUIRE(strcmp(frames, "345") == 0);
  REQUIRE(player.finished);
  REQUIRE(time > 9.59f && time < 9.61f);
  return true;
}

TEST_CASE(test_play) {
  test_sheet t;
  make_sheet(&t, 1, 3, PXL_ANIM_REVERSE, 4);
  pxl_anim_player player = {.frame = 7};
  REQUIRE(!pxl_anim_play(&player, &t.sheet, 1));
  REQUIRE(player.frame == 7 && player.sheet == nullptr);
  REQUIRE(pxl_anim_play(&player, &t.sheet, 0));
  REQUIRE(player.frame == 3 && player.repeat == 4 && player.pass == 0);
  REQUIRE(pxl_anim_find_clip(&t.sheet, "clip") == 0);
  REQUIRE(pxl_anim_find_clip(&t.sheet, "other") == pxl_anim_none);
  return true;
}

/* ---- Aseprite ---------------------------------------------------------- */

// Loads a fixture file. Without the fixtures, the tests cannot run.
static void load_fixture(const char* name, aseprite_sprite* sprite) {
  char path[1024];
  int length = snprintf(path, sizeof path, "%s/%s.aseprite", fixture_dir, name);
  if (length < 0 || (size_t)length >= sizeof path ||
      aseprite_load_file(path, sprite) != ASEPRITE_OK) {
    fprintf(stderr, "cannot load %s/%s.aseprite\n", fixture_dir, name);
    exit(1);
  }
}

// Loads a fixture into a sheet.
static pxl_anim_result load_sheet(const char* name, pxl_anim_sheet* sheet,
                                  const pxl_anim_aseprite_desc* desc) {
  aseprite_sprite sprite;
  load_fixture(name, &sprite);
  pxl_anim_result result = pxl_anim_load_aseprite(sheet, &sprite, desc);
  aseprite_free(&sprite);
  return result;
}

static const char* const character_parts[] = {
    "shadow", "char", "fx", "body", "hidden",
};

static const pxl_anim_aseprite_desc character_desc = {
    .parts      = character_parts,
    .part_count = sizeof character_parts / sizeof character_parts[0],
};

// Tells if two images are the same area of the atlas.
static bool same_area(const pxl_anim_image* a, const pxl_anim_image* b) {
  return a->x == b->x && a->y == b->y && a->width == b->width &&
         a->height == b->height;
}

TEST_CASE(test_sheet_data) {
  pxl_anim_sheet sheet = {};
  REQUIRE(load_sheet("character", &sheet, &character_desc) == PXL_ANIM_OK);
  REQUIRE(sheet.width == 16 && sheet.height == 16);
  REQUIRE(sheet.frame_count == 6 && sheet.part_count == 5);
  static const uint16_t durations[] = {100, 50, 100, 200, 100, 100};
  REQUIRE(memcmp(sheet.durations, durations, sizeof durations) == 0);
  for (uint32_t i = 0; i < sheet.part_count; i++) {
    REQUIRE(strcmp(sheet.part_names[i], character_parts[i]) == 0);
    REQUIRE(pxl_anim_find_part(&sheet, character_parts[i]) == i);
  }
  REQUIRE(pxl_anim_find_part(&sheet, "sketch") == pxl_anim_none);

  REQUIRE(sheet.clip_count == 5);
  const pxl_anim_clip* walk = &sheet.clips[pxl_anim_find_clip(&sheet, "walk")];
  REQUIRE(walk->from == 1 && walk->to == 3 && walk->repeat == 2);
  REQUIRE(walk->direction == PXL_ANIM_REVERSE);
  const pxl_anim_clip* back = &sheet.clips[pxl_anim_find_clip(&sheet, "back")];
  REQUIRE(back->direction == PXL_ANIM_PING_PONG_REVERSE && back->repeat == 0);
  REQUIRE(sheet.clips[4].direction == PXL_ANIM_FORWARD);
  pxl_anim_free(&sheet);
  REQUIRE(sheet.memory == nullptr && sheet.atlas == nullptr);
  pxl_anim_free(&sheet);
  return true;
}

TEST_CASE(test_sheet_images) {
  pxl_anim_sheet sheet = {};
  REQUIRE(load_sheet("character", &sheet, &character_desc) == PXL_ANIM_OK);
  const pxl_anim_image* shadow = pxl_anim_get_image(&sheet, 0, 0);
  REQUIRE(shadow->width == 6 && shadow->height == 2);
  REQUIRE(shadow->offset_x == 5 && shadow->offset_y == 14);
  for (uint32_t frame = 1; frame < sheet.frame_count; frame++) {
    REQUIRE(same_area(pxl_anim_get_image(&sheet, frame, 0), shadow));
  }

  // The group part has the body, not the hidden sketch.
  const pxl_anim_image* group = pxl_anim_get_image(&sheet, 2, 1);
  REQUIRE(group->width == 4 && group->height == 4);
  REQUIRE(group->offset_x == 8 && group->offset_y == 8);
  REQUIRE(same_area(group, pxl_anim_get_image(&sheet, 2, 3)));

  // The same body in frames 4 and 5 at other places.
  const pxl_anim_image* body4 = pxl_anim_get_image(&sheet, 4, 3);
  const pxl_anim_image* body5 = pxl_anim_get_image(&sheet, 5, 3);
  REQUIRE(same_area(body4, body5));
  REQUIRE(body4->offset_x == 10 && body5->offset_x == 11);

  // Effects only in frames 3 and 4.
  for (uint32_t frame = 0; frame < sheet.frame_count; frame++) {
    const pxl_anim_image* fx = pxl_anim_get_image(&sheet, frame, 2);
    bool has_fx              = frame == 3 || frame == 4;
    REQUIRE(has_fx ? fx->width == 2 && fx->height == 2
                   : fx->width == 0 && fx->height == 0);
  }

  // A part shows a hidden layer when it names it.
  const pxl_anim_image* hidden = pxl_anim_get_image(&sheet, 0, 4);
  REQUIRE(hidden->width == 1 && hidden->offset_x == 2);
  pxl_anim_free(&sheet);
  return true;
}

// Tells if two areas with 1 pixel of space around them overlap.
static bool overlap(const pxl_anim_image* a, const pxl_anim_image* b) {
  return a->x < b->x + b->width + 1 && b->x < a->x + a->width + 1 &&
         a->y < b->y + b->height + 1 && b->y < a->y + a->height + 1;
}

// Checks that the atlas has each image of a sheet once, in its bounds, with
// space between the images, and with the pixels that the part renders.
static bool check_atlas(const pxl_anim_sheet* sheet,
                        const aseprite_sprite* sprite, const bool* masks) {
  size_t count = (size_t)sheet->frame_count * sheet->part_count;
  aseprite_color* pixels =
      malloc(sizeof *pixels * sprite->width * sprite->height);
  bool ok = pixels != nullptr;
  for (size_t i = 0; ok && i < count; i++) {
    const pxl_anim_image* a = &sheet->images[i];
    if (a->width == 0) {
      continue;
    }
    ok = a->x + a->width <= sheet->atlas_width &&
         a->y + a->height <= sheet->atlas_height;
    for (size_t k = 0; ok && k < i; k++) {
      const pxl_anim_image* b = &sheet->images[k];
      ok = b->width == 0 || same_area(a, b) || !overlap(a, b);
    }
    uint32_t frame = (uint32_t)(i / sheet->part_count);
    uint32_t part  = (uint32_t)(i % sheet->part_count);
    const bool* mask =
        masks ? masks + ((size_t)part * sprite->layer_count) : nullptr;
    ok =
        ok && aseprite_render_frame(sprite, frame, mask, pixels) == ASEPRITE_OK;
    for (uint32_t y = 0; ok && y < sprite->height; y++) {
      for (uint32_t x = 0; ok && x < sprite->width; x++) {
        aseprite_color expected = pixels[(y * sprite->width) + x];
        bool inside = x >= a->offset_x && x < a->offset_x + a->width &&
                      y >= a->offset_y && y < a->offset_y + a->height;
        if (!inside) {
          ok = expected.a == 0;
          continue;
        }
        const uint8_t* actual =
            sheet->atlas +
            ((((size_t)(a->y + y - a->offset_y) * sheet->atlas_width) + a->x +
              x - a->offset_x) *
             4);
        ok = actual[3] == expected.a &&
             (expected.a == 0 ||
              (actual[0] == expected.r && actual[1] == expected.g &&
               actual[2] == expected.b));
      }
    }
  }
  free(pixels);
  return ok;
}

TEST_CASE(test_atlas) {
  aseprite_sprite sprite;
  load_fixture("character", &sprite);
  pxl_anim_sheet sheet = {};
  REQUIRE(pxl_anim_load_aseprite(&sheet, &sprite, &character_desc) ==
          PXL_ANIM_OK);
  // The masks of the parts: shadow, char, fx, body, hidden.
  static const bool masks[5][6] = {
      {true, false, false, false, false, false},
      {false, true, true, false, false, false},
      {false, false, false, false, true, false},
      {false, false, true, false, false, false},
      {false, false, false, false, false, true},
  };
  REQUIRE(check_atlas(&sheet, &sprite, &masks[0][0]));
  pxl_anim_free(&sheet);

  // One part of the layers that show.
  REQUIRE(pxl_anim_load_aseprite(&sheet, &sprite, nullptr) == PXL_ANIM_OK);
  REQUIRE(sheet.part_count == 1 && strcmp(sheet.part_names[0], "") == 0);
  REQUIRE(check_atlas(&sheet, &sprite, nullptr));
  pxl_anim_free_atlas(&sheet);
  REQUIRE(sheet.atlas == nullptr && sheet.images != nullptr);
  pxl_anim_free(&sheet);
  aseprite_free(&sprite);
  return true;
}

TEST_CASE(test_empty_sheet) {
  pxl_anim_sheet sheet = {};
  REQUIRE(load_sheet("empty", &sheet, nullptr) == PXL_ANIM_OK);
  REQUIRE(sheet.atlas_width == 1 && sheet.atlas_height == 1);
  REQUIRE(sheet.atlas[3] == 0);
  REQUIRE(pxl_anim_get_image(&sheet, 1, 0)->width == 0);
  REQUIRE(sheet.clip_count == 1);
  pxl_anim_free(&sheet);
  return true;
}

TEST_CASE(test_atlas_size) {
  pxl_anim_sheet sheet        = {};
  pxl_anim_aseprite_desc desc = {.max_size = 100};
  REQUIRE(load_sheet("big", &sheet, &desc) == PXL_ANIM_ERROR_TOO_LARGE);
  REQUIRE(sheet.memory == nullptr);
  // Two images of 64 x 64 and the space between them need 129 pixels.
  desc.max_size = 128;
  REQUIRE(load_sheet("big", &sheet, &desc) == PXL_ANIM_ERROR_TOO_LARGE);
  desc.max_size = 129;
  REQUIRE(load_sheet("big", &sheet, &desc) == PXL_ANIM_OK);
  REQUIRE(sheet.atlas_width == 64 && sheet.atlas_height == 129);
  pxl_anim_free(&sheet);
  REQUIRE(load_sheet("big", &sheet, nullptr) == PXL_ANIM_OK);
  REQUIRE(sheet.atlas_width == 64 && sheet.atlas_height == 129);
  pxl_anim_free(&sheet);
  return true;
}

TEST_CASE(test_default_clip) {
  pxl_anim_sheet sheet = {};
  REQUIRE(load_sheet("big", &sheet, nullptr) == PXL_ANIM_OK);
  const pxl_anim_clip* clip = sheet.clip_count == 1 ? sheet.clips : nullptr;
  bool ok = clip && strcmp(clip->name, "") == 0 && clip->from == 0 &&
            clip->to == 1 && clip->direction == PXL_ANIM_FORWARD &&
            clip->repeat == 0;
  pxl_anim_free(&sheet);
  REQUIRE(ok);
  return true;
}

TEST_CASE(test_missing_layer) {
  pxl_anim_sheet sheet              = {};
  const pxl_anim_aseprite_desc desc = {
      .parts      = (const char* const[]){"body", "cape"},
      .part_count = 2,
  };
  pxl_anim_result result = load_sheet("character", &sheet, &desc);
  bool empty             = sheet.memory == nullptr && sheet.atlas == nullptr;
  pxl_anim_free(&sheet);
  REQUIRE(result == PXL_ANIM_ERROR_NO_LAYER && empty);
  return true;
}

TEST_CASE(test_result_strings) {
  for (int i = 0; i <= PXL_ANIM_ERROR_MALFORMED; i++) {
    const char* text = pxl_anim_result_string((pxl_anim_result)i);
    REQUIRE(text && strcmp(text, "unknown result") != 0);
  }
  REQUIRE(strcmp(pxl_anim_result_string((pxl_anim_result)99),
                 "unknown result") == 0);
  return true;
}

/* ---- Main -------------------------------------------------------------- */

// Runs all the tests.
static TEST_SUITE(suite_pxl_anim) {
  RUN_TEST_CASE(test_directions);
  RUN_TEST_CASE(test_no_end);
  RUN_TEST_CASE(test_one_frame);
  RUN_TEST_CASE(test_repeat_override);
  RUN_TEST_CASE(test_update_events);
  RUN_TEST_CASE(test_speed_and_pause);
  RUN_TEST_CASE(test_bad_times);
  RUN_TEST_CASE(test_long_time);
  RUN_TEST_CASE(test_zero_durations);
  RUN_TEST_CASE(test_step_sees_each_frame);
  RUN_TEST_CASE(test_play);
  RUN_TEST_CASE(test_sheet_data);
  RUN_TEST_CASE(test_sheet_images);
  RUN_TEST_CASE(test_atlas);
  RUN_TEST_CASE(test_empty_sheet);
  RUN_TEST_CASE(test_atlas_size);
  RUN_TEST_CASE(test_default_clip);
  RUN_TEST_CASE(test_missing_layer);
  RUN_TEST_CASE(test_result_strings);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_pxl_anim);
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
