// Tests the file buffer of aseprite_load_file with a counting allocator. It
// makes each allocation fail in turn. Usage: test_file DIR

#include <stdio.h>
#include <stdlib.h>

#include "counting.h"

#define ASEPRITE_ALLOC(size, alignment) counting_alloc(size, alignment)
#define ASEPRITE_FREE(pointer, size) counting_free(pointer, size)
#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"

#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/aseprite";

// Writes the path of a fixture. Returns false if the path is too long.
static bool fixture_path(char* path, size_t capacity, const char* name) {
  int length = snprintf(path, capacity, "%s/%s.aseprite", fixture_dir, name);
  return length >= 0 && (size_t)length < capacity;
}

// Loads a fixture from the disk with each allocation failing in turn. The
// first allocation is the file buffer. No load may leak.
static bool check_file_failures(const char* name) {
  char path[1024];
  if (!fixture_path(path, sizeof path, name)) {
    return false;
  }
  allocations        = 0;
  failing_allocation = 0;
  aseprite_sprite sprite;
  bool ok       = aseprite_load_file(path, &sprite) == ASEPRITE_OK;
  size_t needed = allocations;
  aseprite_free(&sprite);
  ok = ok && needed > 1 && counting_is_clean();

  for (size_t i = 1; ok && i <= needed; i++) {
    allocations        = 0;
    failing_allocation = i;
    ok = aseprite_load_file(path, &sprite) == ASEPRITE_ERROR_NO_MEMORY &&
         sprite.memory == nullptr && counting_is_clean();
  }
  failing_allocation = 0;
  if (!ok) {
    fprintf(stderr, "file failure test failed for %s\n", name);
  }
  return ok;
}

TEST_CASE(test_file_allocation_failures) {
  static const char* const names[] = {"rgba", "tilemap", "large"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    REQUIRE(check_file_failures(names[i]));
  }
  return true;
}

TEST_CASE(test_missing_file_allocates_nothing) {
  allocations = 0;
  aseprite_sprite sprite;
  REQUIRE(aseprite_load_file("build/no_such_file.aseprite", &sprite) ==
          ASEPRITE_ERROR_IO);
  REQUIRE(allocations == 0);
  REQUIRE(counting_is_clean());
  return true;
}

// Runs all the tests.
static TEST_SUITE(suite_file) {
  RUN_TEST_CASE(test_file_allocation_failures);
  RUN_TEST_CASE(test_missing_file_allocates_nothing);
}

int main(int argc, char** argv) {
  if (argc > 1) {
    fixture_dir = argv[1];
  }
  RUN_TEST_SUITE(suite_file);
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
