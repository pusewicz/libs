// Includes aseprite.h without the implementation. The test links it with an
// other file that has the implementation.

#include "consumer.h"

#include "aseprite.h"

const char* consumer_result_string(aseprite_result result) {
  return aseprite_result_string(result);
}

uint32_t consumer_bytes_per_pixel(aseprite_color_depth depth) {
  return aseprite_bytes_per_pixel(depth);
}
