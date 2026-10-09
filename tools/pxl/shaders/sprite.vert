#version 450

// Transforms target pixel coordinates to clip space.

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_overlay;
layout(location = 4) in vec4 a_params;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec4 v_overlay;
layout(location = 3) out vec4 v_params;

// xy: scale, zw: offset.
layout(set = 1, binding = 0) uniform pxl_projection {
  vec4 u_projection;
};

void main() {
  gl_Position = vec4(a_position * u_projection.xy + u_projection.zw, 0.0, 1.0);
  v_uv        = a_uv;
  v_color     = a_color;
  v_overlay   = a_overlay;
  v_params    = a_params;
}
