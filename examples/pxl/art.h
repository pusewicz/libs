// Makes textures from text art, so the examples need no image files.

#ifndef ART_H
#define ART_H

#include "pxl.h"

/** Maps a character of the art to a color. Characters not set are clear. */
typedef pxl_color art_palette[128];

/**
 * Makes a texture from text art. Each character is a pixel.
 *
 * @param pxl The context.
 * @param width, height The size in pixels. art has width * height
 *                      characters, rows top to bottom.
 * @param art The pixels.
 * @param palette The color of each character.
 * @return The texture, or nullptr. Free it with pxl_destroy_texture().
 */
static inline pxl_texture* art_texture(pxl_context* pxl, int width, int height,
                                       const char* art,
                                       const art_palette palette) {
  size_t count      = (size_t)width * (size_t)height;
  pxl_color* pixels = SDL_calloc(count, sizeof *pixels);
  if (!pixels) {
    return nullptr;
  }
  for (size_t i = 0; i < count && art[i]; ++i) {
    pixels[i] = palette[(unsigned char)art[i] & 127u];
  }
  pxl_texture* texture = pxl_create_texture(pxl, &(pxl_texture_desc){
                                                     .width  = width,
                                                     .height = height,
                                                     .pixels = pixels,
                                                 });
  SDL_free(pixels);
  return texture;
}

#endif
