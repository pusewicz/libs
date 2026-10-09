// Tests the pxl_anim.h options: a custom allocator and a custom assert. It
// also makes each allocation fail in turn. Usage: test_options FIXTURE_DIR

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

#define ASEPRITE_MALLOC(size) counting_malloc(size)
#define ASEPRITE_FREE(pointer) counting_free(pointer)
#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"

#define PXL_ANIM_MALLOC(size) counting_malloc(size)
#define PXL_ANIM_FREE(pointer) counting_free(pointer)
#define PXL_ANIM_ASSERT(expr) counting_assert(expr)
#define PXL_ANIM_IMPLEMENTATION
#include "pxl_anim.h"

#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"

static const char* fixture_dir = "build/fixtures/pxl_anim";

// Makes a sheet with each allocation failing in turn. No build may leak.
static bool check_allocation_failures(const char* name,
                                      const pxl_anim_aseprite_desc* desc) {
  char path[1024];
  int length = snprintf(path, sizeof path, "%s/%s.aseprite", fixture_dir, name);
  aseprite_sprite sprite;
  if (length < 0 || (size_t)length >= sizeof path ||
      aseprite_load_file(path, &sprite) != ASEPRITE_OK) {
    fprintf(stderr, "cannot load %s\n", path);
    return false;
  }
  allocations        = 0;
  releases           = 0;
  failing_allocation = 0;
  pxl_anim_sheet sheet;
  bool ok       = pxl_anim_load_aseprite(&sheet, &sprite, desc) == PXL_ANIM_OK;
  size_t needed = allocations;
  pxl_anim_free(&sheet);
  ok = ok && needed > 0 && releases == allocations;

  for (size_t i = 1; ok && i <= needed; i++) {
    allocations        = 0;
    releases           = 0;
    failing_allocation = i;
    ok                 = pxl_anim_load_aseprite(&sheet, &sprite, desc) ==
                             PXL_ANIM_ERROR_NO_MEMORY &&
                         sheet.memory == nullptr && sheet.atlas == nullptr &&
                         releases == allocations - 1;
  }
  failing_allocation = 0;
  aseprite_free(&sprite);
  if (!ok) {
    fprintf(stderr, "allocation failure test failed for %s\n", name);
  }
  return ok;
}

TEST_CASE(test_allocation_failures) {
  static const char* const parts[]  = {"shadow", "char", "fx"};
  const pxl_anim_aseprite_desc desc = {.parts = parts, .part_count = 3};
  REQUIRE(check_allocation_failures("character", &desc));
  REQUIRE(check_allocation_failures("character", nullptr));
  // The second image of big.aseprite makes the pixel pool grow.
  REQUIRE(check_allocation_failures("big", nullptr));
  return true;
}

TEST_CASE(test_custom_assert) {
  size_t before        = asserts;
  pxl_anim_sheet sheet = {};
  pxl_anim_free(&sheet);
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
