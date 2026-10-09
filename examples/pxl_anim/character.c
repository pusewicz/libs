// Plays an animated character from an Aseprite file, for example the Orc of
// the free Tiny RPG Character Asset Pack:
//
//   character Orc.aseprite
//
// The layers "shadow", "body" and "fx" become parts. A file without them has
// one part. The tags "Idle", "Walk", "Attack01", "Attack02", "Hurt" and
// "Death" become the moves of the keys:
//
//   Left, Right  walk          J, K   attack
//   H            hurt          X      die; then any key gets up
//   Up, Down     play the next or the previous clip
//   1, 2, 3      show or hide a part

#define SDL_MAIN_USE_CALLBACKS
#define ASEPRITE_IMPLEMENTATION
#define PXL_IMPLEMENTATION
#define PXL_ANIM_IMPLEMENTATION
#include <SDL3/SDL_main.h>

#include "aseprite.h"
#include "pxl.h"
#include "pxl_anim.h"

static constexpr int canvas_width   = 320;
static constexpr int canvas_height  = 180;
static constexpr float ground       = 120.0f;
static constexpr float walk_speed   = 40.0f;
static constexpr uint32_t max_parts = 9;

// The clips of the moves. pxl_anim_none when the file has no such tag.
typedef struct moves {
  uint32_t idle;
  uint32_t walk;
  uint32_t attack;
  uint32_t attack2;
  uint32_t hurt;
  uint32_t death;
} moves;

typedef struct app {
  SDL_Window* window;
  SDL_GPUDevice* device;
  pxl_context* pxl;
  pxl_anim_sheet sheet;
  pxl_texture* atlas;
  moves moves;
  pxl_anim_player player;
  pxl_vec2 feet; // The origin of the draws, in frame pixels.
  float x;
  bool left;     // The character looks left.
  bool busy;     // A move plays that the walk keys do not stop.
  bool browsing; // A clip from Up or Down plays.
  bool dead;
  bool hidden[max_parts];
  uint64_t ticks;
} app;

// Makes the sheet and the atlas texture from an Aseprite file.
static bool load_sheet(app* a, const char* path) {
  aseprite_sprite sprite;
  aseprite_result loaded = aseprite_load_file(path, &sprite);
  if (loaded != ASEPRITE_OK) {
    SDL_Log("%s: %s", path, aseprite_result_string(loaded));
    return false;
  }
  static const char* const parts[] = {"shadow", "body", "fx"};
  pxl_anim_result result           = pxl_anim_load_aseprite(
      &a->sheet, &sprite,
      &(pxl_anim_aseprite_desc){.parts = parts, .part_count = 3});
  if (result == PXL_ANIM_ERROR_NO_LAYER) {
    result = pxl_anim_load_aseprite(&a->sheet, &sprite, nullptr);
  }
  aseprite_free(&sprite);
  if (result != PXL_ANIM_OK) {
    SDL_Log("%s: %s", path, pxl_anim_result_string(result));
    return false;
  }
  a->atlas =
      pxl_create_texture(a->pxl, &(pxl_texture_desc){
                                     .width  = (int)a->sheet.atlas_width,
                                     .height = (int)a->sheet.atlas_height,
                                     .pixels = a->sheet.atlas,
                                 });
  pxl_anim_free_atlas(&a->sheet);
  return a->atlas != nullptr;
}

// Finds the feet: the middle of the frame at the bottom of frame 0.
static pxl_vec2 find_feet(const pxl_anim_sheet* sheet) {
  uint32_t bottom = 0;
  for (uint32_t part = 0; part < sheet->part_count; part++) {
    const pxl_anim_image* image = pxl_anim_get_image(sheet, 0, part);
    if (image->height > 0 && image->offset_y + image->height > bottom) {
      bottom = image->offset_y + image->height;
    }
  }
  return (pxl_vec2){
      .x = (float)sheet->width / 2.0f,
      .y = (float)(bottom > 0 ? bottom : sheet->height),
  };
}

// Plays a clip of a move, unless it plays already. repeat replaces the
// passes of the tag: the Orc has 2 passes for Idle, but it must not stop.
static void play_move(app* a, uint32_t clip, uint32_t repeat) {
  if (clip == pxl_anim_none ||
      (a->player.sheet && a->player.clip == clip && !a->player.finished)) {
    return;
  }
  pxl_anim_play(&a->player, &a->sheet, clip);
  a->player.repeat = repeat;
}

// Plays a move that the walk keys do not stop.
static void play_busy(app* a, uint32_t clip) {
  if (clip != pxl_anim_none && !a->dead) {
    pxl_anim_play(&a->player, &a->sheet, clip);
    a->player.repeat = 1;
    a->busy          = true;
    a->browsing      = false;
  }
}

// Plays the next or the previous clip of the sheet with its own passes.
static void browse(app* a, int step) {
  uint32_t count = a->sheet.clip_count;
  uint32_t clip  = (a->player.clip + count + (uint32_t)step) % count;
  pxl_anim_play(&a->player, &a->sheet, clip);
  a->busy     = true;
  a->browsing = true;
  a->dead     = false;
}

SDL_AppResult SDL_AppInit(void** state, int argc, char* argv[]) {
  if (argc != 2) {
    SDL_Log("usage: character FILE.aseprite");
    return SDL_APP_FAILURE;
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return SDL_APP_FAILURE;
  }
  app* a = SDL_calloc(1, sizeof *a);
  *state = a;
  if (!a) {
    return SDL_APP_FAILURE;
  }
  a->window =
      SDL_CreateWindow("pxl_anim: character", 960, 540,
                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  a->device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  if (!a->window || !a->device) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  a->pxl = pxl_create(&(pxl_desc){
      .device = a->device,
      .window = a->window,
      .width  = canvas_width,
      .height = canvas_height,
  });
  if (!a->pxl) {
    SDL_Log("%s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  if (!load_sheet(a, argv[1])) {
    return SDL_APP_FAILURE;
  }
  a->moves = (moves){
      .idle    = pxl_anim_find_clip(&a->sheet, "Idle"),
      .walk    = pxl_anim_find_clip(&a->sheet, "Walk"),
      .attack  = pxl_anim_find_clip(&a->sheet, "Attack01"),
      .attack2 = pxl_anim_find_clip(&a->sheet, "Attack02"),
      .hurt    = pxl_anim_find_clip(&a->sheet, "Hurt"),
      .death   = pxl_anim_find_clip(&a->sheet, "Death"),
  };
  a->feet = find_feet(&a->sheet);
  a->x    = canvas_width / 2.0f;
  pxl_anim_play(&a->player, &a->sheet,
                a->moves.idle != pxl_anim_none ? a->moves.idle : 0);
  a->player.repeat = 0;
  a->ticks         = SDL_GetTicksNS();
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
  app* a = state;
  if (event->type == SDL_EVENT_QUIT) {
    return SDL_APP_SUCCESS;
  }
  if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
    return SDL_APP_CONTINUE;
  }
  SDL_Keycode key = event->key.key;
  if (key == SDLK_ESCAPE) {
    return SDL_APP_SUCCESS;
  }
  if (key >= SDLK_1 && key < SDLK_1 + max_parts) {
    uint32_t part   = (uint32_t)(key - SDLK_1);
    a->hidden[part] = !a->hidden[part];
  } else if (key == SDLK_UP || key == SDLK_DOWN) {
    browse(a, key == SDLK_UP ? 1 : -1);
  } else if (a->dead) {
    a->dead = false;
    a->busy = false;
  } else if (key == SDLK_J) {
    play_busy(a, a->moves.attack);
  } else if (key == SDLK_K) {
    play_busy(a, a->moves.attack2);
  } else if (key == SDLK_H) {
    play_busy(a, a->moves.hurt);
  } else if (key == SDLK_X && a->moves.death != pxl_anim_none) {
    play_busy(a, a->moves.death);
    a->dead = true;
  }
  return SDL_APP_CONTINUE;
}

// Moves the character and picks its clip.
static void update(app* a, float dt) {
  uint32_t events = pxl_anim_update(&a->player, dt);
  if ((events & PXL_ANIM_EVENT_FINISH) && !a->dead) {
    a->busy = false;
  }
  if (a->dead || (a->busy && !a->browsing)) {
    return;
  }
  const bool* keys = SDL_GetKeyboardState(nullptr);
  int direction = (int)keys[SDL_SCANCODE_RIGHT] - (int)keys[SDL_SCANCODE_LEFT];
  if (direction != 0) {
    a->busy     = false;
    a->browsing = false;
    a->left     = direction < 0;
    a->x        = SDL_clamp(a->x + ((float)direction * walk_speed * dt), 0.0f,
                            (float)canvas_width);
    play_move(a, a->moves.walk, 0);
  } else if (!a->busy) {
    play_move(a, a->moves.idle, 0);
  }
}

static void draw(app* a) {
  pxl_context* pxl = a->pxl;
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_rgb(0x1d2b53));
  pxl_draw_rect(pxl, 0, ground, canvas_width, canvas_height - ground,
                pxl_rgb(0x3b4a33));

  // Draw the parts in order: the shadow, then the body, then the effects.
  for (uint32_t part = 0; part < a->sheet.part_count; part++) {
    if (part < max_parts && a->hidden[part]) {
      continue;
    }
    pxl_anim_draw(pxl, a->atlas, &a->player, part,
                  &(pxl_sprite){
                      .x      = a->x,
                      .y      = ground,
                      .origin = a->feet,
                      .flip_x = a->left,
                  });
  }

  const pxl_anim_clip* clip = &a->sheet.clips[a->player.clip];
  pxl_draw_text(pxl, 4, 4, pxl_white, "%s  frame %u of %u  pass %u%s",
                clip->name, (unsigned)(a->player.frame - clip->from + 1),
                (unsigned)(clip->to - clip->from + 1),
                (unsigned)a->player.pass + 1,
                a->player.finished ? "  (end)" : "");
  for (uint32_t part = 0; part < a->sheet.part_count && part < max_parts;
       part++) {
    pxl_draw_text(pxl, 4, 16 + (10 * (float)part),
                  a->hidden[part] ? pxl_rgb(0x5f574f) : pxl_rgb(0xc2c3c7),
                  "%u: %s", (unsigned)part + 1,
                  a->sheet.part_names[part][0] ? a->sheet.part_names[part]
                                               : "all layers");
  }
  pxl_draw_text(pxl, 4, canvas_height - 22, pxl_rgb(0xc2c3c7),
                "left, right: walk   J, K: attack   H: hurt   X: die");
  pxl_draw_text(pxl, 4, canvas_height - 12, pxl_rgb(0xc2c3c7),
                "up, down: clips   1-3: parts");
  pxl_end_frame(pxl);
}

SDL_AppResult SDL_AppIterate(void* state) {
  app* a       = state;
  uint64_t now = SDL_GetTicksNS();
  float dt     = (float)(now - a->ticks) / 1e9f;
  a->ticks     = now;
  update(a, SDL_min(dt, 0.1f));
  draw(a);
  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, [[maybe_unused]] SDL_AppResult result) {
  app* a = state;
  if (a) {
    if (a->pxl) {
      pxl_destroy_texture(a->pxl, a->atlas);
      pxl_destroy(a->pxl);
    }
    pxl_anim_free(&a->sheet);
    SDL_DestroyGPUDevice(a->device);
    SDL_DestroyWindow(a->window);
    SDL_free(a);
  }
}
