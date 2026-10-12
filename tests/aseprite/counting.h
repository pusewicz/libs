// A counting allocator for ASEPRITE_ALLOC and ASEPRITE_FREE. It keeps the size
// of each block in a header before the block. It counts the live blocks and
// the calls that break the rules of the macros.

#ifndef COUNTING_H
#define COUNTING_H

#include <stdckdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct counting_header {
  alignas(max_align_t) size_t size;
} counting_header;

// The calls of counting_alloc.
static size_t allocations = 0;
// The call of counting_alloc that fails, counted from 1. 0 fails none.
static size_t failing_allocation = 0;
static size_t live_blocks        = 0;
static size_t violations         = 0;

// Allocates a block. Counts a violation for a bad size or alignment. Fails
// the allocation that the test selects.
static void* counting_alloc(size_t size, size_t alignment) {
  allocations++;
  if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
      alignment > alignof(max_align_t)) {
    violations++;
  }
  if (allocations == failing_allocation) {
    return nullptr;
  }
  size_t total = 0;
  if (ckd_add(&total, sizeof(counting_header), size)) {
    return nullptr;
  }
  counting_header* header = malloc(total);
  if (!header) {
    return nullptr;
  }
  header->size = size;
  live_blocks++;
  return header + 1;
}

// Releases a block. Counts a violation for nullptr or for a size that is not
// the size of the allocation.
static void counting_free(void* pointer, size_t size) {
  if (!pointer) {
    violations++;
    return;
  }
  counting_header* header = (counting_header*)pointer - 1;
  if (header->size != size) {
    violations++;
  }
  live_blocks--;
  free(header);
}

// Tells if no call broke a rule and no block is live.
static bool counting_is_clean() {
  return violations == 0 && live_blocks == 0;
}

#endif
