#version 450

// The default fragment shader with 4 texture slots. v_params.z selects the
// slot of the draw, so draws with up to 4 textures share one draw call.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture0;
layout(set = 2, binding = 1) uniform sampler2D u_texture1;
layout(set = 2, binding = 2) uniform sampler2D u_texture2;
layout(set = 2, binding = 3) uniform sampler2D u_texture3;

vec2 sharp_uv(vec2 uv, vec2 size) {
  vec2 texel = uv * size;
  vec2 seam  = floor(texel + 0.5);
  vec2 width = max(fwidth(texel), vec2(1e-5));
  texel      = seam + clamp((texel - seam) / width, -0.5, 0.5);
  return texel / size;
}

void main() {
  int slot = int(v_params.z * 255.0 + 0.5);
  ivec2 isize;
  if (slot == 0) {
    isize = textureSize(u_texture0, 0);
  } else if (slot == 1) {
    isize = textureSize(u_texture1, 0);
  } else if (slot == 2) {
    isize = textureSize(u_texture2, 0);
  } else {
    isize = textureSize(u_texture3, 0);
  }
  vec2 size = vec2(isize);
  vec2 uv   = mix(v_uv, sharp_uv(v_uv, size), v_params.y);
  // The textures have one level, so textureLod gives the same texels as
  // texture, and needs no derivatives in the branches.
  vec4 sampled;
  if (slot == 0) {
    sampled = textureLod(u_texture0, uv, 0.0);
  } else if (slot == 1) {
    sampled = textureLod(u_texture1, uv, 0.0);
  } else if (slot == 2) {
    sampled = textureLod(u_texture2, uv, 0.0);
  } else {
    sampled = textureLod(u_texture3, uv, 0.0);
  }
  vec4 texel = mix(sampled, vec4(1.0), v_params.x);
  vec4 color = texel * vec4(v_color.rgb * v_color.a, v_color.a);
  color.rgb  = mix(color.rgb, v_overlay.rgb * color.a, v_overlay.a);
  o_color    = color;
}
