// Tests the pxl.h options: a custom allocator and a custom assert. It also
// makes each allocation fail in turn. The tests need a GPU device; without
// one they are skipped.

#include <stdio.h>
#include <stdlib.h>

static size_t calls        = 0;
static size_t failing_call = 0;
static long blocks         = 0;
static size_t asserts      = 0;

// Counts allocations. It fails the call that the test selects.
static void* counting_malloc(size_t size) {
  if (++calls == failing_call) {
    return nullptr;
  }
  void* pointer = malloc(size);
  blocks += pointer != nullptr;
  return pointer;
}

// Counts releases.
static void counting_free(void* pointer) {
  blocks -= pointer != nullptr;
  free(pointer);
}

// Counts asserts.
static void counting_assert(bool condition) {
  asserts++;
  if (!condition) {
    abort();
  }
}

#define PXL_MALLOC(size) counting_malloc(size)
#define PXL_FREE(pointer) counting_free(pointer)
#define PXL_ASSERT(condition) counting_assert(condition)
#define PXL_IMPLEMENTATION
#include "pxl.h"

#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"

static SDL_GPUDevice* device = nullptr;

/**
 * Creates a context, draws a frame with all kinds of draws and destroys the
 * context. Allocations may fail on the way.
 *
 * @return true if all of it worked.
 */
static bool run_frame() {
  pxl_context* pxl = pxl_create(&(pxl_desc){
      .device = device,
      .width  = 32,
      .height = 32,
  });
  if (!pxl) {
    return false;
  }
  const pxl_color pixels[] = {pxl_white, pxl_black, pxl_black, pxl_white};
  pxl_texture* texture     = pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 2, .height = 2, .pixels = pixels});
  bool ok = texture != nullptr;
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  if (texture) {
    pxl_draw_sprite(pxl, texture, &(pxl_sprite){.x = 4, .y = 4});
  }
  pxl_draw_circle(pxl, 16, 16, 8, pxl_white);
  pxl_set_blend(pxl, PXL_BLEND_ADD);
  pxl_draw_line(pxl, 0, 0, 31, 31, pxl_white);
  const float uniforms[4] = {};
  ok = pxl_set_uniforms(pxl, uniforms, sizeof uniforms) && ok;
  pxl_draw_text(pxl, 1, 1, pxl_white, "%300s", "long text");
  ok = pxl_end_frame(pxl) && ok;
  pxl_destroy_texture(pxl, texture);
  pxl_destroy(pxl);
  return ok;
}

TEST_CASE(test_custom_allocator_and_assert) {
  calls        = 0;
  failing_call = 0;
  blocks       = 0;
  asserts      = 0;
  REQUIRE(run_frame());
  REQUIRE(calls == 1);
  REQUIRE(blocks == 0);
  REQUIRE(asserts > 0);
  return true;
}

TEST_CASE(test_allocation_failures) {
  calls        = 0;
  failing_call = 0;
  REQUIRE(run_frame());
  size_t needed = calls;
  for (size_t call = 1; call <= needed; ++call) {
    calls        = 0;
    failing_call = call;
    blocks       = 0;
    REQUIRE(!run_frame());
    REQUIRE(blocks == 0);
  }
  return true;
}

static TEST_SUITE(suite_options) {
  RUN_TEST_CASE(test_custom_allocator_and_assert);
  RUN_TEST_CASE(test_allocation_failures);
}

int main() {
  if (SDL_Init(SDL_INIT_VIDEO)) {
    device = SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
  }
  if (device) {
    RUN_TEST_SUITE(suite_options);
  } else {
    printf("no GPU device (%s): tests skipped\n", SDL_GetError());
  }
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
