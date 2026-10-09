// libFuzzer target for aseprite_load_memory().

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"
#include "walk.h"

// libFuzzer calls this function by its name.
// NOLINTNEXTLINE(misc-use-internal-linkage,readability-identifier-naming)
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  aseprite_sprite sprite;
  if (aseprite_load_memory(data, size, nullptr, &sprite) == ASEPRITE_OK) {
    volatile uint64_t sum = walk_sprite(&sprite);
    (void)sum;
    aseprite_free(&sprite);
  }
  return 0;
}
