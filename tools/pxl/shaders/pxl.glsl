// The inputs of the pxl fragment shaders, and pxl_cover(). Include this file
// in each fragment shader, after #version. tools/pxl/shaders.rb finds it.
//
//   #version 450
//   #include "pxl.glsl"
//
// v_uv:      the position in the texture. For a shape: the position from the
//            origin of the shape, in target pixels.
// v_color:   tint, straight alpha.
// v_overlay: rgb is mixed into the result by a.
// v_params:  x = 1 ignores the texture, y = 1 uses sharp filtering, z is the
//            shape kind / 255.
// v_shape:   the size of a shape. The meaning depends on the kind.

#ifndef PXL_GLSL
#define PXL_GLSL

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;
layout(location = 4) in vec4 v_shape;

// The shape kinds. Keep them as pxl__shape in pxl.h.
const int PXL_SHAPE_NONE   = 0;
const int PXL_SHAPE_CIRCLE = 1; // v_shape.x: the radius.

// Rounds to a whole number. Use it for values that must be whole numbers:
// interpolation can add a small error to them.
int pxl_whole(float value) {
  return int(floor(value + 0.5));
}

ivec2 pxl_whole(vec2 value) {
  return ivec2(floor(value + 0.5));
}

// Checks if a pixel is in a circle of whole pixels, with the rule of
// pxl_draw_circle(). p is the pixel from the center pixel.
bool pxl_in_circle(ivec2 p, int radius) {
  return p.x * p.x + p.y * p.y <= radius * (radius + 1);
}

// Gets the part of the pixel that the shape covers, 0 to 1. Draws that are
// not shapes cover all of the pixel.
float pxl_shape_coverage() {
  int kind = pxl_whole(v_params.z * 255.0);
  if (kind == PXL_SHAPE_CIRCLE) {
    // The origin is the center of the center pixel, so v_uv is a whole
    // number at each pixel center.
    return pxl_in_circle(pxl_whole(v_uv), pxl_whole(v_shape.x)) ? 1.0 : 0.0;
  }
  return 1.0;
}

// Multiplies a premultiplied color by the coverage of the shape, and
// discards the pixels outside of the shape. Call it last in main(), because
// derivatives after a discard are not defined.
vec4 pxl_cover(vec4 color) {
  float coverage = pxl_shape_coverage();
  if (coverage <= 0.0) {
    discard;
  }
  return color * coverage;
}

#endif
