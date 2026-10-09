// Tests the pxl.h options: a custom allocator and a custom assert. It also
// makes each allocation fail in turn. The tests need a GPU device. Without
// one they are skipped. Set PXL_TEST_REQUIRE_GPU to make them fail instead.

#include <stdckdint.h>
#include <stddef.h>
#include <stdio.h>
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
static size_t asserts            = 0;

// Allocates a block with the size in a header before it. Counts a violation
// for a bad size or alignment. Fails the allocation that the test selects.
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

// Counts asserts.
static void counting_assert(bool condition) {
  asserts++;
  if (!condition) {
    abort();
  }
}

#define PXL_ALLOC(size, alignment) counting_alloc(size, alignment)
#define PXL_FREE(pointer, size) counting_free(pointer, size)
#define PXL_ASSERT(condition) counting_assert(condition)
#define PXL_IMPLEMENTATION
#include "pxl.h"
#include "test_shaders.h"

#define PICO_UNIT_IMPLEMENTATION
#include "pico_unit.h"

static SDL_GPUDevice* device = nullptr;

/**
 * Creates a context, draws a frame with all kinds of draws and destroys the
 * context. Every allocation of pxl is on the way: the context, textures,
 * shaders, fonts with advances, text that does not fit the stack buffer, the
 * built-in font and an array that grows. Allocations may fail on the way.
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
  const uint8_t advances[] = {1, 2, 3, 4};
  pxl_font* font           = pxl_create_font(pxl, &(pxl_font_desc){
                                                      .texture      = texture,
                                                      .glyph_width  = 1,
                                                      .glyph_height = 1,
                                                      .advances     = advances,
                                                  });
  pxl_shader* shader       = pxl_create_shader(pxl, &slots_frag);
  bool ok                  = texture && font && shader;
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  if (texture) {
    pxl_draw_sprite(pxl, texture, &(pxl_sprite){.x = 4, .y = 4});
  }
  pxl_draw_circle(pxl, 16, 16, 8, pxl_white);
  pxl_set_blend(pxl, PXL_BLEND_ADD);
  pxl_draw_line(pxl, 0, 0, 31, 31, pxl_white);

  pxl_set_font(pxl, font);
  pxl_draw_text(pxl, 1, 1, pxl_white, " !");
  ok = pxl_measure_text(pxl, " !").x == 3.0f && ok;
  pxl_set_font(pxl, nullptr);
  pxl_draw_text(pxl, 1, 1, pxl_white, "%300s", "long text on the heap");
  ok = pxl_measure_text(pxl, "%300s", "x").x > 0.0f && ok;

  pxl_set_shader(pxl, shader);
  pxl_set_shader_texture(pxl, 1, texture);
  for (int i = 0; i < 300; ++i) {
    const float uniforms[4] = {(float)i / 300.0f};
    ok = pxl_set_uniforms(pxl, uniforms, sizeof uniforms) && ok;
    pxl_draw_rect(pxl, (float)(i % 32), 0, 1, 1, pxl_white);
  }
  ok = pxl_end_frame(pxl) && ok;
  pxl_destroy_shader(pxl, shader);
  pxl_destroy_font(pxl, font);
  pxl_destroy_texture(pxl, texture);
  pxl_destroy(pxl);
  return ok;
}

TEST_CASE(test_custom_allocator_and_assert) {
  allocations        = 0;
  failing_allocation = 0;
  asserts            = 0;
  REQUIRE(run_frame());
  REQUIRE(allocations > 0);
  REQUIRE(counting_is_clean());
  REQUIRE(asserts > 0);
  return true;
}

TEST_CASE(test_allocation_failures) {
  allocations        = 0;
  failing_allocation = 0;
  REQUIRE(run_frame());
  size_t needed = allocations;
  for (size_t i = 1; i <= needed; ++i) {
    allocations        = 0;
    failing_allocation = i;
    REQUIRE(!run_frame());
    REQUIRE(counting_is_clean());
  }
  failing_allocation = 0;
  return true;
}

static TEST_SUITE(suite_options) {
  RUN_TEST_CASE(test_custom_allocator_and_assert);
  RUN_TEST_CASE(test_allocation_failures);
}

/**
 * Reports that the tests cannot run, because the SDL function `failed`
 * failed. Returns false if PXL_TEST_REQUIRE_GPU is set: then they must run.
 */
static bool skip_tests(const char* failed) {
  if (getenv("PXL_TEST_REQUIRE_GPU") != nullptr) {
    fprintf(stderr, "%s: %s: PXL_TEST_REQUIRE_GPU is set\n", failed,
            SDL_GetError());
    return false;
  }
  printf("%s: %s: tests skipped\n", failed, SDL_GetError());
  return true;
}

int main() {
  bool ok = true;
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    ok = skip_tests("SDL_Init");
  } else {
    device = SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
    if (device) {
      RUN_TEST_SUITE(suite_options);
    } else {
      ok = skip_tests("SDL_CreateGPUDevice");
    }
  }
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
  pu_print_stats();
  return ok && !pu_test_failed() ? 0 : 1;
}
