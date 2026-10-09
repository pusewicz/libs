// libFuzzer target for aseprite_load_memory() and aseprite_render_frame().

#define ASEPRITE_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>

#include "aseprite.h"
#include "walk.h"

static constexpr uint16_t max_render_size   = 256;
static constexpr uint32_t max_render_frames = 8;

// Renders the first frames of a sprite, with the visible layers and with all
// layers. It skips big sprites: they are slow, not more interesting.
static void render(const aseprite_sprite* sprite) {
  if (sprite->width > max_render_size || sprite->height > max_render_size) {
    return;
  }
  aseprite_color* pixels =
      malloc(sizeof *pixels * sprite->width * sprite->height);
  bool* layers = malloc(sprite->layer_count + (size_t)1);
  if (pixels && layers) {
    memset(layers, true, sprite->layer_count);
    for (uint32_t frame = 0;
         frame < sprite->frame_count && frame < max_render_frames; frame++) {
      (void)aseprite_render_frame(sprite, frame, nullptr, pixels);
      (void)aseprite_render_frame(sprite, frame, layers, pixels);
    }
  }
  free(layers);
  free(pixels);
}

// libFuzzer calls this function by its name.
// NOLINTNEXTLINE(misc-use-internal-linkage,readability-identifier-naming)
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  aseprite_sprite sprite;
  if (aseprite_load_memory(data, size, &sprite) == ASEPRITE_OK) {
    volatile uint64_t sum = walk_sprite(&sprite);
    (void)sum;
    render(&sprite);
    aseprite_free(&sprite);
  }
  return 0;
}
