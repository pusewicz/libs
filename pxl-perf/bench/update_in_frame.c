// Checks texture updates inside a frame. Prints the pixels that the draws show.
#define PXL_IMPLEMENTATION
#include "pxl.h"
#include <stdio.h>

static const pxl_color red = {255, 0, 0, 255}, green = {0, 255, 0, 255}, blue = {0, 0, 255, 255};

int main(void) {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GPUDevice* device = SDL_CreateGPUDevice(pxl_shader_formats, true, nullptr);
  pxl_context* pxl = pxl_create(&(pxl_desc){.device = device, .width = 8, .height = 2});
  pxl_color px4[4] = {red, red, red, red};
  pxl_texture* t = pxl_create_texture(pxl, &(pxl_texture_desc){.width = 2, .height = 2, .pixels = px4});
  pxl_texture* doomed = pxl_create_texture(pxl, &(pxl_texture_desc){.width = 2, .height = 2, .pixels = px4});
  pxl_color canvas[16];

  // 1. Two updates around draws in one frame.
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  for (int i = 0; i < 4; ++i) px4[i] = blue;
  pxl_update_texture(pxl, t, 0, 0, 2, 2, px4);
  pxl_draw_sprite(pxl, t, &(pxl_sprite){.x = 0});
  for (int i = 0; i < 4; ++i) px4[i] = green;
  pxl_update_texture(pxl, t, 0, 0, 2, 2, px4);
  pxl_draw_sprite(pxl, t, &(pxl_sprite){.x = 4});
  // 2. Update a texture, then destroy it in the same frame.
  pxl_update_texture(pxl, doomed, 0, 0, 2, 2, px4);
  pxl_draw_sprite(pxl, doomed, &(pxl_sprite){.x = 6});
  pxl_destroy_texture(pxl, doomed);
  // 3. A partial update: one texel.
  pxl_update_texture(pxl, t, 1, 1, 1, 1, &red);
  bool ok = pxl_end_frame(pxl);
  pxl_read_texture(pxl, pxl_get_canvas(pxl), canvas);
  printf("frame ok=%d first draw: %d,%d,%d second draw: %d,%d,%d destroyed: %d,%d,%d texel(1,1) of 1st: %d,%d,%d\n", ok,
         canvas[0].r, canvas[0].g, canvas[0].b, canvas[4].r, canvas[4].g, canvas[4].b,
         canvas[6].r, canvas[6].g, canvas[6].b, canvas[9].r, canvas[9].g, canvas[9].b);
  // 4. After the frame, the texture holds the last pixels.
  pxl_color tex[4];
  pxl_read_texture(pxl, t, tex);
  printf("texture after frame: %d,%d,%d / %d,%d,%d\n", tex[0].r, tex[0].g, tex[0].b, tex[3].r, tex[3].g, tex[3].b);
  // 5. A frame with only an update, no draws.
  pxl_begin_frame(pxl);
  pxl_update_texture(pxl, t, 0, 0, 1, 1, &blue);
  pxl_end_frame(pxl);
  pxl_read_texture(pxl, t, tex);
  printf("update-only frame: %d,%d,%d\n", tex[0].r, tex[0].g, tex[0].b);
  pxl_destroy_texture(pxl, t);
  pxl_destroy(pxl);
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
}
