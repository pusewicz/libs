#version 450

// A test shader: returns the color of slot 1 plus the uniform color. Slot 0
// gives the alpha.

#include "pxl.glsl"

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_extra;

layout(set = 3, binding = 0) uniform slots_uniforms {
  vec4 u_add;
};

void main() {
  float alpha = texture(u_texture, v_uv).a;
  vec4 extra  = texture(u_extra, vec2(0.5));
  vec3 color  = clamp(extra.rgb + u_add.rgb, 0.0, 1.0);
  o_color     = pxl_cover(vec4(color, 1.0) * alpha);
}
