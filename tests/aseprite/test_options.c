// Tests the aseprite.h options: ASEPRITE_MALLOC and ASEPRITE_FREE for the
// default allocator, a custom assert and no stdio. The allocator struct and
// the caller block are in test_aseprite.c. Usage: test_options DIR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocations = 0;
static size_t releases    = 0;
static size_t asserts     = 0;

// Counts allocations.
static void* counting_malloc(size_t size) {
  allocations++;
  return malloc(size);
}

// Counts releases.
static void counting_free(void* pointer) {
  if (pointer) {
    releases++;
  }
  free(pointer);
}

// Counts asserts.
static void counting_assert(bool condition) {
  asserts++;
  if (!condition) {
    abort();
  }
}

#define ASEPRITE_NO_STDIO
#define ASEPRITE_MALLOC(size) counting_malloc(size)
#define ASEPRITE_FREE(pointer) counting_free(pointer)
#define ASEPRITE_ASSERT(expr) counting_assert(expr)
#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"

#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/aseprite";

// Reads a fixture file of 1 MiB or less. The caller frees the data.
static uint8_t* read_fixture(const char* name, size_t* size) {
  char path[1024];
  int length = snprintf(path, sizeof path, "%s/%s.aseprite", fixture_dir, name);
  if (length < 0 || (size_t)length >= sizeof path) {
    return nullptr;
  }
  FILE* file = fopen(path, "rb");
  if (!file) {
    return nullptr;
  }
  uint8_t* data = malloc(1 << 20);
  *size         = data ? fread(data, 1, 1 << 20, file) : 0;
  (void)fclose(file);
  return data;
}

TEST_CASE(test_macros_back_the_default_allocator) {
  size_t size   = 0;
  uint8_t* data = read_fixture("rgba", &size);
  if (!data) {
    return false;
  }
  allocations = 0;
  releases    = 0;
  aseprite_sprite sprite;
  bool ok = aseprite_load_memory(data, size, nullptr, &sprite) == ASEPRITE_OK &&
            allocations > 0 && releases == 0;
  aseprite_free(&sprite);
  ok = ok && releases == allocations;
  free(data);
  REQUIRE(ok);
  return true;
}

TEST_CASE(test_caller_block_does_not_allocate) {
  alignas(max_align_t) static unsigned char block[1 << 16];
  size_t size   = 0;
  uint8_t* data = read_fixture("rgba", &size);
  if (!data) {
    return false;
  }
  aseprite_sprite sprite;
  bool ok = aseprite_load_memory(data, size, nullptr, &sprite) == ASEPRITE_OK;
  size_t used = sprite.memory_used;
  aseprite_free(&sprite);

  ok = ok && used <= sizeof block;
  if (ok) {
    allocations              = 0;
    releases                 = 0;
    aseprite_options options = {.memory = block, .memory_size = used};
    ok = aseprite_load_memory(data, size, &options, &sprite) == ASEPRITE_OK;
    aseprite_free(&sprite);
    ok = ok && allocations == 0 && releases == 0;
  }
  free(data);
  REQUIRE(ok);
  return true;
}

TEST_CASE(test_custom_assert) {
  size_t before          = asserts;
  aseprite_sprite sprite = {};
  aseprite_free(&sprite);
  REQUIRE(asserts > before);
  return true;
}

// Runs all the tests.
static TEST_SUITE(suite_options) {
  RUN_TEST_CASE(test_macros_back_the_default_allocator);
  RUN_TEST_CASE(test_caller_block_does_not_allocate);
  RUN_TEST_CASE(test_custom_assert);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_options);
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
