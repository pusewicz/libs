#version 450

// A test shader: returns the color of slot 1 plus the uniform color. Slot 0
// gives the alpha.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_overlay;
layout(location = 3) in vec4 v_params;

layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_texture;
layout(set = 2, binding = 1) uniform sampler2D u_extra;

layout(set = 3, binding = 0) uniform slots_uniforms {
  vec4 u_add;
};

void main() {
  float alpha = texture(u_texture, v_uv).a;
  vec4 extra  = texture(u_extra, vec2(0.5));
  o_color     = vec4(clamp(extra.rgb + u_add.rgb, 0.0, 1.0), 1.0) * alpha;
}
