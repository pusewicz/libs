// Tests the pxl.h options: a custom allocator and a custom assert. It also
// makes each allocation fail in turn, and calls functions where pxl asserts.
// The tests need a GPU device; without one they are skipped.

#include <stdio.h>
#include <stdlib.h>

static size_t calls          = 0;
static size_t failing_call   = 0;
static long blocks           = 0;
static size_t asserts        = 0;
static size_t failed_asserts = 0;

// Counts allocations. It fails the call that the test selects.
static void* counting_malloc(size_t size) {
  if (++calls == failing_call) {
    return nullptr;
  }
  void* pointer = malloc(size);
  blocks += pointer != nullptr;
  return pointer;
}

// Counts allocations. It fails the call that the test selects.
static void* counting_realloc(void* pointer, size_t size) {
  if (++calls == failing_call) {
    return nullptr;
  }
  void* grown = realloc(pointer, size);
  blocks += grown && !pointer;
  return grown;
}

// Counts releases.
static void counting_free(void* pointer) {
  blocks -= pointer != nullptr;
  free(pointer);
}

// Counts asserts and the asserts that fail. It does not stop the program, so
// the tests can check what pxl does after a failed assert.
static void counting_assert(bool condition) {
  asserts++;
  failed_asserts += !condition;
}

#define PXL_MALLOC(size) counting_malloc(size)
#define PXL_REALLOC(pointer, size) counting_realloc(pointer, size)
#define PXL_FREE(pointer) counting_free(pointer)
#define PXL_ASSERT(condition) counting_assert(condition)
#define PXL_IMPLEMENTATION
#include "pxl.h"
#include "test_shaders.h"

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
  pxl_draw_text(pxl, 1, 1, pxl_white, "%300s", "long text on the heap");
  ok = pxl_end_frame(pxl) && ok;
  pxl_destroy_texture(pxl, texture);
  pxl_destroy(pxl);
  return ok;
}

TEST_CASE(test_custom_allocator_and_assert) {
  calls          = 0;
  failing_call   = 0;
  blocks         = 0;
  asserts        = 0;
  failed_asserts = 0;
  REQUIRE(run_frame());
  REQUIRE(calls > 0);
  REQUIRE(blocks == 0);
  REQUIRE(asserts > 0);
  REQUIRE(failed_asserts == 0);
  return true;
}

TEST_CASE(test_allocation_failures) {
  calls        = 0;
  failing_call = 0;
  REQUIRE(run_frame());
  size_t needed = calls;
  for (size_t call = 1; call <= needed; ++call) {
    calls          = 0;
    failing_call   = call;
    blocks         = 0;
    failed_asserts = 0;
    REQUIRE(!run_frame());
    REQUIRE(blocks == 0);
    REQUIRE(failed_asserts == 0);
  }
  return true;
}

// Commands outside of a frame fail. They do not change the next frame.
TEST_CASE(test_outside_of_a_frame) {
  failing_call     = 0;
  failed_asserts   = 0;
  pxl_context* pxl = pxl_create(&(pxl_desc){
      .device = device,
      .width  = 1,
      .height = 1,
  });
  REQUIRE(pxl != nullptr);
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  bool ended              = pxl_end_frame(pxl);
  const float uniforms[4] = {};
  bool set                = pxl_set_uniforms(pxl, uniforms, sizeof uniforms);
  pxl_clear(pxl, pxl_white);
  pxl_draw_rect(pxl, 0, 0, 1, 1, pxl_white);
  size_t failed_outside = failed_asserts;
  pxl_begin_frame(pxl);
  ended           = pxl_end_frame(pxl) && ended;
  pxl_color pixel = {};
  bool read       = pxl_read_texture(pxl, pxl_get_canvas(pxl), &pixel);
  pxl_destroy(pxl);
  REQUIRE(ended && read && !set);
  REQUIRE(failed_outside == 3 && failed_asserts == 3);
  REQUIRE(pixel.r == 0 && pixel.a == 255);
  return true;
}

// A draw cannot sample its target, in slot 0 or in a shader slot.
TEST_CASE(test_draw_into_itself) {
  failing_call     = 0;
  failed_asserts   = 0;
  pxl_context* pxl = pxl_create(&(pxl_desc){
      .device = device,
      .width  = 1,
      .height = 1,
  });
  REQUIRE(pxl != nullptr);
  pxl_shader* shader  = pxl_create_shader(pxl, &slots_frag);
  pxl_texture* target = pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 1, .height = 1, .render_target = true});
  pxl_begin_frame(pxl);
  pxl_set_target(pxl, target);
  pxl_draw_sprite(pxl, target, &(pxl_sprite){});
  bool sprite_failed = !pxl_end_frame(pxl);
  pxl_begin_frame(pxl);
  pxl_set_shader(pxl, shader);
  pxl_set_shader_texture(pxl, 1, target);
  pxl_set_target(pxl, target);
  pxl_draw_rect(pxl, 0, 0, 1, 1, pxl_white);
  bool rect_failed = !pxl_end_frame(pxl);
  pxl_destroy_shader(pxl, shader);
  pxl_destroy_texture(pxl, target);
  pxl_destroy(pxl);
  REQUIRE(shader && target);
  REQUIRE(sprite_failed && rect_failed);
  REQUIRE(failed_asserts == 2);
  return true;
}

static TEST_SUITE(suite_options) {
  RUN_TEST_CASE(test_custom_allocator_and_assert);
  RUN_TEST_CASE(test_allocation_failures);
  RUN_TEST_CASE(test_outside_of_a_frame);
  RUN_TEST_CASE(test_draw_into_itself);
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
