#version 450

// The default fragment shader. Use it as a template for custom shaders.
//
// v_color:   tint, straight alpha.
// v_overlay: rgb is mixed into the result by a.
// v_params:  x = 1 ignores the texture, y = 1 uses sharp filtering.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;

// Snaps uv to texel centers, except within one screen pixel of a texel edge.
// Use it with a linear sampler: texels stay sharp at any scale.
vec2 sharp_uv(vec2 uv, vec2 size) {
  vec2 texel = uv * size;
  vec2 seam  = floor(texel + 0.5);
  vec2 width = max(fwidth(texel), vec2(1e-5));
  texel      = seam + clamp((texel - seam) / width, -0.5, 0.5);
  return texel / size;
}

void main() {
  vec2 size  = vec2(textureSize(u_texture, 0));
  vec2 uv    = mix(v_uv, sharp_uv(v_uv, size), v_params.y);
  vec4 texel = mix(texture(u_texture, uv), vec4(1.0), v_params.x);
  vec4 color = texel * vec4(v_color.rgb * v_color.a, v_color.a);
  color.rgb  = mix(color.rgb, v_overlay.rgb * color.a, v_overlay.a);
  o_color    = color;
}
