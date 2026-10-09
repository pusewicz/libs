// Renders each frame of an Aseprite file with aseprite_render_frame() and
// writes the RGBA pixels to OUTPUT_DIR/<frame>.rgba. compare.rb compares
// them with the frames that Aseprite exports.
//
// Usage: render FILE OUTPUT_DIR

#define ASEPRITE_IMPLEMENTATION
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "aseprite.h"

// Writes the pixels of a frame to a file.
static bool write_frame(const char* directory, uint32_t frame,
                        const aseprite_color* pixels, size_t count) {
  char path[4096];
  int length =
      snprintf(path, sizeof path, "%s/%" PRIu32 ".rgba", directory, frame);
  if (length < 0 || (size_t)length >= sizeof path) {
    fprintf(stderr, "path is too long: %s\n", directory);
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    perror(path);
    return false;
  }
  bool written = fwrite(pixels, sizeof *pixels, count, file) == count;
  if (fclose(file) != 0 || !written) {
    perror(path);
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: render FILE OUTPUT_DIR\n");
    return 2;
  }
  aseprite_sprite sprite;
  aseprite_result result = aseprite_load_file(argv[1], &sprite);
  if (result != ASEPRITE_OK) {
    fprintf(stderr, "%s: %s\n", argv[1], aseprite_result_string(result));
    return 1;
  }
  size_t count           = (size_t)sprite.width * sprite.height;
  aseprite_color* pixels = malloc((count > 0 ? count : 1) * sizeof *pixels);
  bool ok                = pixels != nullptr;
  for (uint32_t frame = 0; ok && frame < sprite.frame_count; frame++) {
    result = aseprite_render_frame(&sprite, frame, nullptr, pixels);
    if (result != ASEPRITE_OK) {
      fprintf(stderr, "%s: frame %" PRIu32 ": %s\n", argv[1], frame,
              aseprite_result_string(result));
      ok = false;
    } else {
      ok = write_frame(argv[2], frame, pixels, count);
    }
  }
  free(pixels);
  aseprite_free(&sprite);
  return ok ? 0 : 1;
}
