#version 450

// Gives a texture the look of an old handheld: four colors from a palette,
// and rows of pixels that wave sideways.
//
// Slot 1 holds the palettes: four texels wide, one palette in each row.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_palettes;

layout(set = 3, binding = 0) uniform retro_uniforms {
  float u_palette; // The row of the palette.
  float u_time;    // In seconds.
  float u_wave;    // The largest move in pixels.
  float u_unused;
};

void main() {
  vec2 size   = vec2(textureSize(u_texture, 0));
  float row   = floor(v_uv.y * size.y);
  float shift = floor(sin((row * 0.25) + (u_time * 4.0)) * u_wave + 0.5);
  vec4 texel  = texture(u_texture, v_uv + vec2(shift / size.x, 0.0));

  float luma  = dot(texel.rgb, vec3(0.299, 0.587, 0.114)) / max(texel.a, 1e-5);
  float shade = min(floor(luma * 4.0), 3.0);
  vec2 cells  = vec2(textureSize(u_palettes, 0));
  vec3 color  = texture(u_palettes, (vec2(shade, u_palette) + 0.5) / cells).rgb;
  o_color     = vec4(color, 1.0) * texel.a * v_color.a;
}
