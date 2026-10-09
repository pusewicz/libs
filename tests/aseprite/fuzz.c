// libFuzzer target for aseprite_load_memory(). A load that works must also
// work in a caller block of memory_used bytes.

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
    size_t used = sprite.memory_used;
    aseprite_free(&sprite);

    // The extra bytes catch a write after the end of the block. The block is
    // aligned by hand, because the Windows C runtime has no aligned_alloc.
    unsigned char* storage = malloc(used + (2 * alignof(max_align_t)));
    walk_check(storage != nullptr, "block");
    size_t padding = (size_t)(-(uintptr_t)storage) & (alignof(max_align_t) - 1);
    unsigned char* block     = storage + padding;
    aseprite_options options = {.memory = block, .memory_size = used};
    walk_check(aseprite_load_memory(data, size, &options, &sprite) ==
                   ASEPRITE_OK,
               "memory_used is enough");
    walk_check(sprite.memory_used == used, "memory_used is stable");
    aseprite_free(&sprite);
    free(storage);
  }
  return 0;
}
