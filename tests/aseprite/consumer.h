// Functions of consumer.c, a file that uses aseprite.h without the
// implementation.

#ifndef CONSUMER_H
#define CONSUMER_H

#include "aseprite.h"

/**
 * Calls aseprite_result_string() from an other translation unit.
 *
 * @param result The result.
 * @return A static string.
 */
const char* consumer_result_string(aseprite_result result);

/**
 * Calls aseprite_bytes_per_pixel() from an other translation unit.
 *
 * @param depth The color depth.
 * @return The number of bytes in one pixel.
 */
uint32_t consumer_bytes_per_pixel(aseprite_color_depth depth);

#endif
