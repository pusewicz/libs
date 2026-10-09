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

    // The extra bytes catch a write after the end of the block.
    void* block =
        aligned_alloc(alignof(max_align_t), used + alignof(max_align_t));
    walk_check(block != nullptr, "block");
    aseprite_options options = {.memory = block, .memory_size = used};
    walk_check(aseprite_load_memory(data, size, &options, &sprite) ==
                   ASEPRITE_OK,
               "memory_used is enough");
    walk_check(sprite.memory_used == used, "memory_used is stable");
    aseprite_free(&sprite);
    free(block);
  }
  return 0;
}
