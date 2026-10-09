// Tests the aseprite.h options: a custom allocator, a custom assert and no
// stdio. It also makes each allocation fail in turn. Usage: test_options DIR

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocations        = 0;
static size_t releases           = 0;
static size_t failing_allocation = 0;
static size_t asserts            = 0;

// Counts allocations. It fails the allocation that the test selects.
static void* counting_malloc(size_t size) {
  allocations++;
  if (allocations == failing_allocation) {
    return nullptr;
  }
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

// Loads a fixture with each allocation failing in turn. No load may leak.
static bool check_allocation_failures(const char* name) {
  size_t size   = 0;
  uint8_t* data = read_fixture(name, &size);
  if (!data) {
    return false;
  }
  allocations        = 0;
  releases           = 0;
  failing_allocation = 0;
  aseprite_sprite sprite;
  bool ok       = aseprite_load_memory(data, size, &sprite) == ASEPRITE_OK;
  size_t needed = allocations;
  aseprite_free(&sprite);
  ok = ok && needed > 0 && releases == allocations;

  for (size_t i = 1; ok && i <= needed; i++) {
    allocations        = 0;
    releases           = 0;
    failing_allocation = i;
    ok =
        aseprite_load_memory(data, size, &sprite) == ASEPRITE_ERROR_NO_MEMORY &&
        sprite.memory == nullptr && releases == allocations - 1;
  }
  failing_allocation = 0;
  free(data);
  if (!ok) {
    fprintf(stderr, "allocation failure test failed for %s\n", name);
  }
  return ok;
}

TEST_CASE(test_allocation_failures) {
  static const char* const names[] = {
      "rgba", "indexed", "tilemap", "properties", "tags", "slices",
  };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    REQUIRE(check_allocation_failures(names[i]));
  }
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
  RUN_TEST_CASE(test_allocation_failures);
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
