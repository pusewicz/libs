// Measures the frame time when a draw uses a blend mode for the first time:
// pxl creates the pipeline in pxl_end_frame.

#define PXL_IMPLEMENTATION
#include "pxl.h"

#include <stdio.h>

static double frame_ms(pxl_context* pxl, SDL_GPUDevice* device,
                       pxl_texture* texture, pxl_blend blend) {
  Uint64 t0 = SDL_GetPerformanceCounter();
  pxl_begin_frame(pxl);
  pxl_set_blend(pxl, blend);
  pxl_draw_sprite(pxl, texture, &(pxl_sprite){});
  pxl_end_frame(pxl);
  Uint64 t1 = SDL_GetPerformanceCounter();
  SDL_WaitForGPUIdle(device);
  return (double)(t1 - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

int main(void) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return 1;
  }
  SDL_GPUDevice* device = SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
  pxl_context* pxl =
      pxl_create(&(pxl_desc){.device = device, .width = 320, .height = 180});
  pxl_texture* texture = pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 4, .height = 4});
  if (!device || !pxl || !texture) {
    fprintf(stderr, "%s\n", SDL_GetError());
    return 1;
  }
  const char* names[] = {"alpha", "add", "multiply", "none"};
  for (int blend = 0; blend < 4; ++blend) {
    double first  = frame_ms(pxl, device, texture, (pxl_blend)blend);
    double second = frame_ms(pxl, device, texture, (pxl_blend)blend);
    printf("%-9s first frame %7.3f ms, next frame %7.3f ms\n", names[blend],
           first, second);
  }
  pxl_destroy_texture(pxl, texture);
  pxl_destroy(pxl);
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
  return 0;
}
