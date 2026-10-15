// The inputs of the pxl fragment shaders, and pxl_cover(). Include this file
// in each fragment shader, after #version. tools/pxl/shaders.rb finds it.
//
//   #version 450
//   #include "pxl.glsl"
//
// v_uv:      the position in the texture.
// v_color:   tint, straight alpha.
// v_overlay: rgb is mixed into the result by a.
// v_params:  x = 1 ignores the texture, y = 1 uses sharp filtering.
// v_shape:   the size of a shape. pxl_cover() reads it.

#ifndef PXL_GLSL
#define PXL_GLSL

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;
layout(location = 4) in vec4 v_shape;

// Gets the part of the pixel that the shape covers, 0 to 1. Draws that are
// not shapes cover all of the pixel.
float pxl_shape_coverage() {
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
