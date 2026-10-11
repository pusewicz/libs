// The SDL GPU API on WebGL 2, for the pxl playground.
//
// SDL has no GPU backend for the web. The playground build removes SDL_gpu.c
// from SDL and links this file instead. It defines all public SDL GPU
// functions.
//
// - Each command runs when it is recorded. Submit only shows the swapchain.
// - Shaders are SPIR-V. SPIRV-Cross translates them to GLSL ES 3.00 and gives
//   the resources names that are known: sdl_fs_sampler_0, sdl_vs_uniforms_0
//   and sdl_varying_0, for example.
// - Row 0 of a WebGL framebuffer is at the bottom, but row 0 of an SDL
//   texture is at the top. The vertex shaders flip y, so each texture keeps
//   the rows of SDL, and viewports and scissors need no change. The
//   swapchain is a texture too. Submit copies it to the canvas, flipped.
//
// Not supported: compute, storage buffers and textures, indirect draws,
// textures that are not 2D, multisampling, and BGRA formats.

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <spirv_cross_c.h>
#include <stddef.h>
#include <stdint.h>

static const char gpu_driver[] = "webgl2";

constexpr Uint32 gpu_max_color_targets     = 4;
constexpr Uint32 gpu_max_vertex_buffers    = 16;
constexpr Uint32 gpu_max_vertex_attributes = 16;
constexpr Uint32 gpu_max_samplers          = 16;
constexpr Uint32 gpu_max_uniform_buffers   = 4;
/** The texture units: fragment samplers, then vertex samplers. */
constexpr Uint32 gpu_texture_units = 2 * gpu_max_samplers;
/** The texture unit for uploads and other work outside of draws. */
constexpr Uint32 gpu_work_unit = gpu_texture_units - 1;
/** The anisotropy parameter of EXT_texture_filter_anisotropic. */
constexpr GLenum gpu_max_anisotropy = 0x84FE;

typedef enum gpu_stage : Uint8 {
  GPU_STAGE_VERTEX,
  GPU_STAGE_FRAGMENT,
} gpu_stage;

/** The features that this file does not have. */
typedef enum gpu_feature : Uint8 {
  GPU_FEATURE_COMPUTE,
  GPU_FEATURE_STORAGE,
  GPU_FEATURE_INDIRECT,
  GPU_FEATURE_TEXTURE_TYPE,
  GPU_FEATURE_MULTISAMPLE,
  GPU_FEATURE_FORMAT,
  GPU_FEATURE_FILL_MODE,
  GPU_FEATURE_SHADER_FORMAT,
  GPU_FEATURE_COUNT,
} gpu_feature;

static_assert(GPU_FEATURE_COUNT <= 32);

typedef enum gpu_format_kind : Uint8 {
  /** Unsigned normalized: WebGL can always render into it. */
  GPU_FORMAT_UNORM,
  /** Signed normalized: WebGL cannot render into it. */
  GPU_FORMAT_SNORM,
  /** Float: WebGL renders into it with EXT_color_buffer_float. */
  GPU_FORMAT_FLOAT,
  GPU_FORMAT_INT,
  GPU_FORMAT_UINT,
  GPU_FORMAT_DEPTH,
  GPU_FORMAT_DEPTH_STENCIL,
} gpu_format_kind;

/** A texture format in WebGL terms. */
typedef struct gpu_format {
  GLenum internal_format;
  GLenum format;
  GLenum type;
  gpu_format_kind kind;
} gpu_format;

/** A vertex element format in WebGL terms. */
typedef struct gpu_vertex_format {
  GLint size;
  GLenum type;
  bool normalized;
  bool integer;
} gpu_vertex_format;

struct SDL_GPUDevice {
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context;
  char* canvas;
  SDL_PropertiesID props;
  bool float_targets;
  bool anisotropy;
  GLuint vertex_array;
  /** The framebuffer of render passes. */
  GLuint pass_framebuffer;
  /** The framebuffers of copies, blits and downloads. */
  GLuint read_framebuffer, draw_framebuffer;
  GLuint uniform_buffers[2][gpu_max_uniform_buffers];
  /** The textures in the texture units, to break feedback loops. */
  SDL_GPUTexture* units[gpu_texture_units];
  Uint32 enabled_attributes;
  SDL_Window* window;
  SDL_GPUTexture* swapchain;
  /** The features that were reported once: 1 << gpu_feature. */
  Uint32 reported;
};

struct SDL_GPUTexture {
  GLuint texture;
  SDL_GPUTextureFormat sdl_format;
  const gpu_format* format;
  SDL_GPUTextureUsageFlags usage;
  Uint32 width, height, levels;
  bool swapchain;
};

struct SDL_GPUSampler {
  GLuint sampler;
};

struct SDL_GPUBuffer {
  GLuint buffer;
  /** GL_ELEMENT_ARRAY_BUFFER or GL_ARRAY_BUFFER. WebGL keeps them apart. */
  GLenum target;
  Uint32 size;
};

struct SDL_GPUTransferBuffer {
  Uint8* data;
  Uint32 size;
};

struct SDL_GPUShader {
  GLuint shader;
  gpu_stage stage;
  Uint32 num_samplers;
  Uint32 num_uniform_buffers;
};

struct SDL_GPUGraphicsPipeline {
  GLuint program;
  GLenum mode;
  SDL_GPUVertexAttribute attributes[gpu_max_vertex_attributes];
  Uint32 num_attributes;
  /** By buffer slot. */
  Uint32 pitches[gpu_max_vertex_buffers];
  /** By buffer slot. 0: per vertex, else the instance step rate. */
  Uint32 divisors[gpu_max_vertex_buffers];
  SDL_GPURasterizerState rasterizer;
  SDL_GPUDepthStencilState depth_stencil;
  /** WebGL 2 has one blend state for all targets: the one of target 0. */
  SDL_GPUColorTargetBlendState blend;
  bool alpha_to_coverage;
};

/** Never created. The type must exist for the API. */
struct SDL_GPUComputePipeline {
  int unused;
};

struct SDL_GPURenderPass {
  SDL_GPUCommandBuffer* command_buffer;
  SDL_GPUGraphicsPipeline* pipeline;
  SDL_GPUBufferBinding vertex_buffers[gpu_max_vertex_buffers];
  SDL_GPUBufferBinding index_buffer;
  GLenum index_type;
  Uint32 index_size;
  Uint8 stencil_reference;
  bool active;
};

struct SDL_GPUCopyPass {
  SDL_GPUCommandBuffer* command_buffer;
};

struct SDL_GPUComputePass {
  SDL_GPUCommandBuffer* command_buffer;
};

struct SDL_GPUCommandBuffer {
  SDL_GPUDevice* device;
  SDL_GPURenderPass render_pass;
  SDL_GPUCopyPass copy_pass;
  SDL_GPUComputePass compute_pass;
  /** The swapchain texture that Submit shows, or nullptr. */
  SDL_GPUTexture* present;
};

struct SDL_GPUFence {
  int unused;
};

// ---------------------------------------------------------------------------
// Errors

/** Sets the error for a missing feature. Logs it once for each device. */
static void gpu_unsupported(SDL_GPUDevice* device, gpu_feature feature) {
  static const char* const names[GPU_FEATURE_COUNT] = {
      [GPU_FEATURE_COMPUTE]       = "compute",
      [GPU_FEATURE_STORAGE]       = "storage buffers and textures",
      [GPU_FEATURE_INDIRECT]      = "indirect draws",
      [GPU_FEATURE_TEXTURE_TYPE]  = "textures that are not 2D",
      [GPU_FEATURE_MULTISAMPLE]   = "multisampling",
      [GPU_FEATURE_FORMAT]        = "this texture format",
      [GPU_FEATURE_FILL_MODE]     = "the line fill mode",
      [GPU_FEATURE_SHADER_FORMAT] = "shaders that are not SPIR-V",
  };
  SDL_SetError("WebGL 2 GPU: %s is not supported", names[feature]);
  Uint32 bit = 1u << feature;
  if (device && !(device->reported & bit)) {
    device->reported |= bit;
    SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "%s", SDL_GetError());
  }
}

static bool gpu_invalid(const char* parameter) {
  return SDL_InvalidParamError(parameter);
}

// ---------------------------------------------------------------------------
// Formats

static const gpu_format gpu_formats[] = {
    // clang-format off
    [SDL_GPU_TEXTUREFORMAT_R8_UNORM]             = {GL_R8, GL_RED, GL_UNSIGNED_BYTE, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_R8G8_UNORM]           = {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM]       = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB]  = {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM]    = {GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM]         = {GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, GPU_FORMAT_UNORM},
    [SDL_GPU_TEXTUREFORMAT_R8_SNORM]             = {GL_R8_SNORM, GL_RED, GL_BYTE, GPU_FORMAT_SNORM},
    [SDL_GPU_TEXTUREFORMAT_R8G8_SNORM]           = {GL_RG8_SNORM, GL_RG, GL_BYTE, GPU_FORMAT_SNORM},
    [SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM]       = {GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, GPU_FORMAT_SNORM},
    [SDL_GPU_TEXTUREFORMAT_R16_FLOAT]            = {GL_R16F, GL_RED, GL_HALF_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT]         = {GL_RG16F, GL_RG, GL_HALF_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT]   = {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R32_FLOAT]            = {GL_R32F, GL_RED, GL_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT]         = {GL_RG32F, GL_RG, GL_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT]   = {GL_RGBA32F, GL_RGBA, GL_FLOAT, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT]     = {GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, GPU_FORMAT_FLOAT},
    [SDL_GPU_TEXTUREFORMAT_R8_UINT]              = {GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R8G8_UINT]            = {GL_RG8UI, GL_RG_INTEGER, GL_UNSIGNED_BYTE, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT]        = {GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R16_UINT]             = {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R16G16_UINT]          = {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT]    = {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R32_UINT]             = {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R32G32_UINT]          = {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT]    = {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, GPU_FORMAT_UINT},
    [SDL_GPU_TEXTUREFORMAT_R8_INT]               = {GL_R8I, GL_RED_INTEGER, GL_BYTE, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R8G8_INT]             = {GL_RG8I, GL_RG_INTEGER, GL_BYTE, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT]         = {GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R16_INT]              = {GL_R16I, GL_RED_INTEGER, GL_SHORT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R16G16_INT]           = {GL_RG16I, GL_RG_INTEGER, GL_SHORT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT]     = {GL_RGBA16I, GL_RGBA_INTEGER, GL_SHORT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R32_INT]              = {GL_R32I, GL_RED_INTEGER, GL_INT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R32G32_INT]           = {GL_RG32I, GL_RG_INTEGER, GL_INT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT]     = {GL_RGBA32I, GL_RGBA_INTEGER, GL_INT, GPU_FORMAT_INT},
    [SDL_GPU_TEXTUREFORMAT_D16_UNORM]            = {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, GPU_FORMAT_DEPTH},
    [SDL_GPU_TEXTUREFORMAT_D24_UNORM]            = {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GPU_FORMAT_DEPTH},
    [SDL_GPU_TEXTUREFORMAT_D32_FLOAT]            = {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, GPU_FORMAT_DEPTH},
    [SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT]    = {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GPU_FORMAT_DEPTH_STENCIL},
    [SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT]    = {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, GPU_FORMAT_DEPTH_STENCIL},
    // clang-format on
};

/** Returns the WebGL format of a texture format, or nullptr. */
static const gpu_format* gpu_texture_format(SDL_GPUTextureFormat format) {
  if (format <= SDL_GPU_TEXTUREFORMAT_INVALID ||
      (size_t)format >= SDL_arraysize(gpu_formats) ||
      gpu_formats[format].internal_format == 0) {
    return nullptr;
  }
  return &gpu_formats[format];
}

static bool gpu_is_depth(const gpu_format* format) {
  return format->kind == GPU_FORMAT_DEPTH ||
         format->kind == GPU_FORMAT_DEPTH_STENCIL;
}

static bool gpu_can_render(const SDL_GPUDevice* device,
                           const gpu_format* format) {
  switch (format->kind) {
  case GPU_FORMAT_UNORM:
  case GPU_FORMAT_INT:
  case GPU_FORMAT_UINT:
    return true;
  case GPU_FORMAT_FLOAT:
    return device->float_targets;
  default:
    return false;
  }
}

static const gpu_vertex_format gpu_vertex_formats[] = {
    // clang-format off
    [SDL_GPU_VERTEXELEMENTFORMAT_INT]          = {1, GL_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_INT2]         = {2, GL_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_INT3]         = {3, GL_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_INT4]         = {4, GL_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UINT]         = {1, GL_UNSIGNED_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UINT2]        = {2, GL_UNSIGNED_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UINT3]        = {3, GL_UNSIGNED_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UINT4]        = {4, GL_UNSIGNED_INT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_FLOAT]        = {1, GL_FLOAT, false, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2]       = {2, GL_FLOAT, false, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3]       = {3, GL_FLOAT, false, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4]       = {4, GL_FLOAT, false, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_BYTE2]        = {2, GL_BYTE, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_BYTE4]        = {4, GL_BYTE, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2]       = {2, GL_UNSIGNED_BYTE, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4]       = {4, GL_UNSIGNED_BYTE, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM]   = {2, GL_BYTE, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM]   = {4, GL_BYTE, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM]  = {2, GL_UNSIGNED_BYTE, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM]  = {4, GL_UNSIGNED_BYTE, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_SHORT2]       = {2, GL_SHORT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_SHORT4]       = {4, GL_SHORT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_USHORT2]      = {2, GL_UNSIGNED_SHORT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_USHORT4]      = {4, GL_UNSIGNED_SHORT, false, true},
    [SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM]  = {2, GL_SHORT, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM]  = {4, GL_SHORT, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM] = {2, GL_UNSIGNED_SHORT, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM] = {4, GL_UNSIGNED_SHORT, true, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_HALF2]        = {2, GL_HALF_FLOAT, false, false},
    [SDL_GPU_VERTEXELEMENTFORMAT_HALF4]        = {4, GL_HALF_FLOAT, false, false},
    // clang-format on
};

static const GLenum gpu_blend_factors[] = {
    [SDL_GPU_BLENDFACTOR_ZERO]                = GL_ZERO,
    [SDL_GPU_BLENDFACTOR_ONE]                 = GL_ONE,
    [SDL_GPU_BLENDFACTOR_SRC_COLOR]           = GL_SRC_COLOR,
    [SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR] = GL_ONE_MINUS_SRC_COLOR,
    [SDL_GPU_BLENDFACTOR_DST_COLOR]           = GL_DST_COLOR,
    [SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR] = GL_ONE_MINUS_DST_COLOR,
    [SDL_GPU_BLENDFACTOR_SRC_ALPHA]           = GL_SRC_ALPHA,
    [SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA] = GL_ONE_MINUS_SRC_ALPHA,
    [SDL_GPU_BLENDFACTOR_DST_ALPHA]           = GL_DST_ALPHA,
    [SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA] = GL_ONE_MINUS_DST_ALPHA,
    [SDL_GPU_BLENDFACTOR_CONSTANT_COLOR]      = GL_CONSTANT_COLOR,
    [SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR] =
        GL_ONE_MINUS_CONSTANT_COLOR,
    [SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE] = GL_SRC_ALPHA_SATURATE,
};

static const GLenum gpu_blend_ops[] = {
    [SDL_GPU_BLENDOP_ADD]              = GL_FUNC_ADD,
    [SDL_GPU_BLENDOP_SUBTRACT]         = GL_FUNC_SUBTRACT,
    [SDL_GPU_BLENDOP_REVERSE_SUBTRACT] = GL_FUNC_REVERSE_SUBTRACT,
    [SDL_GPU_BLENDOP_MIN]              = GL_MIN,
    [SDL_GPU_BLENDOP_MAX]              = GL_MAX,
};

static const GLenum gpu_compare_ops[] = {
    [SDL_GPU_COMPAREOP_NEVER]            = GL_NEVER,
    [SDL_GPU_COMPAREOP_LESS]             = GL_LESS,
    [SDL_GPU_COMPAREOP_EQUAL]            = GL_EQUAL,
    [SDL_GPU_COMPAREOP_LESS_OR_EQUAL]    = GL_LEQUAL,
    [SDL_GPU_COMPAREOP_GREATER]          = GL_GREATER,
    [SDL_GPU_COMPAREOP_NOT_EQUAL]        = GL_NOTEQUAL,
    [SDL_GPU_COMPAREOP_GREATER_OR_EQUAL] = GL_GEQUAL,
    [SDL_GPU_COMPAREOP_ALWAYS]           = GL_ALWAYS,
};

static const GLenum gpu_stencil_ops[] = {
    [SDL_GPU_STENCILOP_KEEP]                = GL_KEEP,
    [SDL_GPU_STENCILOP_ZERO]                = GL_ZERO,
    [SDL_GPU_STENCILOP_REPLACE]             = GL_REPLACE,
    [SDL_GPU_STENCILOP_INCREMENT_AND_CLAMP] = GL_INCR,
    [SDL_GPU_STENCILOP_DECREMENT_AND_CLAMP] = GL_DECR,
    [SDL_GPU_STENCILOP_INVERT]              = GL_INVERT,
    [SDL_GPU_STENCILOP_INCREMENT_AND_WRAP]  = GL_INCR_WRAP,
    [SDL_GPU_STENCILOP_DECREMENT_AND_WRAP]  = GL_DECR_WRAP,
};

static const GLenum gpu_primitive_modes[] = {
    [SDL_GPU_PRIMITIVETYPE_TRIANGLELIST]  = GL_TRIANGLES,
    [SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP] = GL_TRIANGLE_STRIP,
    [SDL_GPU_PRIMITIVETYPE_LINELIST]      = GL_LINES,
    [SDL_GPU_PRIMITIVETYPE_LINESTRIP]     = GL_LINE_STRIP,
    [SDL_GPU_PRIMITIVETYPE_POINTLIST]     = GL_POINTS,
};

static const GLenum gpu_address_modes[] = {
    [SDL_GPU_SAMPLERADDRESSMODE_REPEAT]          = GL_REPEAT,
    [SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT] = GL_MIRRORED_REPEAT,
    [SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE]   = GL_CLAMP_TO_EDGE,
};

/** Maps an enum with a table. Returns 0 for values outside of the table. */
#define GPU_MAP(table, value)                                                  \
  ((size_t)(value) < SDL_arraysize(table) ? (table)[(size_t)(value)] : 0)

// ---------------------------------------------------------------------------
// Format helpers of the API

Uint32 SDL_GPUTextureFormatTexelBlockSize(SDL_GPUTextureFormat format) {
  if (format >= SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM &&
      format <= SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT) {
    return 16;
  }
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM:
  case SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM:
    return 8;
  case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM:
  case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM:
  case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
  case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
  case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT:
  case SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
    return 16;
  case SDL_GPU_TEXTUREFORMAT_R8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8_SNORM:
  case SDL_GPU_TEXTUREFORMAT_A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8_UINT:
  case SDL_GPU_TEXTUREFORMAT_R8_INT:
    return 1;
  case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM:
  case SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM:
  case SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R16_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R8G8_SNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8_UINT:
  case SDL_GPU_TEXTUREFORMAT_R8G8_INT:
  case SDL_GPU_TEXTUREFORMAT_R16_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R16_SNORM:
  case SDL_GPU_TEXTUREFORMAT_R16_UINT:
  case SDL_GPU_TEXTUREFORMAT_R16_INT:
  case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
    return 2;
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
  case SDL_GPU_TEXTUREFORMAT_R32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R11G11B10_UFLOAT:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
  case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT:
  case SDL_GPU_TEXTUREFORMAT_R16G16_UINT:
  case SDL_GPU_TEXTUREFORMAT_R16G16_INT:
  case SDL_GPU_TEXTUREFORMAT_R16G16_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R16G16_SNORM:
  case SDL_GPU_TEXTUREFORMAT_D24_UNORM:
  case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R32_UINT:
  case SDL_GPU_TEXTUREFORMAT_R32_INT:
  case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
    return 4;
  case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
    return 5;
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_SNORM:
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT:
  case SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R32G32_UINT:
  case SDL_GPU_TEXTUREFORMAT_R32G32_INT:
    return 8;
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT:
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT:
    return 16;
  default:
    return 0;
  }
}

/** Gets the size in texels of a block of a compressed format: 1 x 1 if not. */
static void gpu_block_extent(SDL_GPUTextureFormat format, Uint32* w,
                             Uint32* h) {
  // The ASTC formats come in three runs of 14, with the same block sizes.
  static const Uint8 astc[14][2] = {
      {4, 4}, {5, 4},  {5, 5},  {6, 5},  {6, 6},   {8, 5},   {8, 6},
      {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
  };
  static_assert(SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM_SRGB ==
                SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM + 14);
  static_assert(SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT ==
                SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM + 41);
  *w = 1;
  *h = 1;
  if (format >= SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM &&
      format <= SDL_GPU_TEXTUREFORMAT_BC6H_RGB_UFLOAT) {
    *w = 4;
    *h = 4;
  } else if (format >= SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB &&
             format <= SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB) {
    *w = 4;
    *h = 4;
  } else if (format >= SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM &&
             format <= SDL_GPU_TEXTUREFORMAT_ASTC_12x12_FLOAT) {
    size_t i = (size_t)(format - SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM) % 14;
    *w       = astc[i][0];
    *h       = astc[i][1];
  }
}

Uint32 SDL_CalculateGPUTextureFormatSize(SDL_GPUTextureFormat format,
                                         Uint32 width, Uint32 height,
                                         Uint32 depth_or_layer_count) {
  Uint32 block_w = 1;
  Uint32 block_h = 1;
  gpu_block_extent(format, &block_w, &block_h);
  Uint32 columns = (width + block_w - 1) / block_w;
  Uint32 rows    = (height + block_h - 1) / block_h;
  return depth_or_layer_count * columns * rows *
         SDL_GPUTextureFormatTexelBlockSize(format);
}

SDL_PixelFormat
SDL_GetPixelFormatFromGPUTextureFormat(SDL_GPUTextureFormat format) {
  switch (format) {
  case SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM:
    return SDL_PIXELFORMAT_BGRA4444;
  case SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM:
    return SDL_PIXELFORMAT_BGR565;
  case SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM:
    return SDL_PIXELFORMAT_BGRA5551;
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_SNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB:
    return SDL_PIXELFORMAT_RGBA32;
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:
  case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB:
    return SDL_PIXELFORMAT_BGRA32;
  case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:
    return SDL_PIXELFORMAT_ABGR2101010;
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT:
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM:
    return SDL_PIXELFORMAT_RGBA64;
  case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:
    return SDL_PIXELFORMAT_RGBA64_FLOAT;
  case SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT:
    return SDL_PIXELFORMAT_RGBA128_FLOAT;
  default:
    return SDL_PIXELFORMAT_UNKNOWN;
  }
}

SDL_GPUTextureFormat
SDL_GetGPUTextureFormatFromPixelFormat(SDL_PixelFormat format) {
  switch (format) {
  case SDL_PIXELFORMAT_BGRA4444:
    return SDL_GPU_TEXTUREFORMAT_B4G4R4A4_UNORM;
  case SDL_PIXELFORMAT_BGR565:
    return SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM;
  case SDL_PIXELFORMAT_BGRA5551:
    return SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM;
  case SDL_PIXELFORMAT_BGRA32:
  case SDL_PIXELFORMAT_BGRX32:
    return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  case SDL_PIXELFORMAT_RGBA32:
  case SDL_PIXELFORMAT_RGBX32:
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  case SDL_PIXELFORMAT_ABGR2101010:
    return SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM;
  case SDL_PIXELFORMAT_RGBA64:
    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
  case SDL_PIXELFORMAT_RGBA64_FLOAT:
    return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
  case SDL_PIXELFORMAT_RGBA128_FLOAT:
    return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
  default:
    return SDL_GPU_TEXTUREFORMAT_INVALID;
  }
}

// ---------------------------------------------------------------------------
// Device

// clang-format off
EM_JS(bool, gpu_webgl2_available, (), {
  return typeof WebGL2RenderingContext !== "undefined";
})
// clang-format on

static bool gpu_driver_matches(const char* name) {
  return !name || SDL_strcmp(name, gpu_driver) == 0;
}

bool SDL_GPUSupportsShaderFormats(SDL_GPUShaderFormat format_flags,
                                  const char* name) {
  return gpu_driver_matches(name) &&
         (format_flags & SDL_GPU_SHADERFORMAT_SPIRV) && gpu_webgl2_available();
}

bool SDL_GPUSupportsProperties(SDL_PropertiesID props) {
  const char* name = SDL_GetStringProperty(
      props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, nullptr);
  bool spirv = SDL_GetBooleanProperty(
      props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, false);
  return gpu_driver_matches(name) && spirv && gpu_webgl2_available();
}

SDL_GPUDevice* SDL_CreateGPUDevice(SDL_GPUShaderFormat format_flags,
                                   bool debug_mode, const char* name) {
  SDL_PropertiesID props = SDL_CreateProperties();
  if (!props) {
    return nullptr;
  }
  SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,
                         debug_mode);
  SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, name);
  SDL_SetBooleanProperty(props,
                         SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,
                         (format_flags & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
  SDL_GPUDevice* device = SDL_CreateGPUDeviceWithProperties(props);
  SDL_DestroyProperties(props);
  return device;
}

/** Sets the properties of SDL_GetGPUDeviceProperties(). */
static bool gpu_set_device_properties(SDL_GPUDevice* device) {
  device->props = SDL_CreateProperties();
  if (!device->props) {
    return false;
  }
  const char* renderer = (const char*)glGetString(GL_RENDERER);
  const char* version  = (const char*)glGetString(GL_VERSION);
  SDL_SetStringProperty(device->props, SDL_PROP_GPU_DEVICE_NAME_STRING,
                        renderer ? renderer : "WebGL 2");
  SDL_SetStringProperty(device->props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,
                        gpu_driver);
  SDL_SetStringProperty(device->props,
                        SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,
                        version ? version : "");
  SDL_SetStringProperty(device->props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,
                        "SDL GPU on WebGL 2, for the pxl playground");
  return true;
}

/** Creates the GL objects of the device. The context must be current. */
static void gpu_create_objects(SDL_GPUDevice* device) {
  glGenVertexArrays(1, &device->vertex_array);
  glBindVertexArray(device->vertex_array);
  GLuint framebuffers[3] = {};
  glGenFramebuffers(3, framebuffers);
  device->pass_framebuffer = framebuffers[0];
  device->read_framebuffer = framebuffers[1];
  device->draw_framebuffer = framebuffers[2];
  for (size_t stage = 0; stage < 2; ++stage) {
    glGenBuffers((GLsizei)gpu_max_uniform_buffers,
                 device->uniform_buffers[stage]);
    for (size_t slot = 0; slot < gpu_max_uniform_buffers; ++slot) {
      glBindBufferBase(GL_UNIFORM_BUFFER,
                       (GLuint)((stage * gpu_max_uniform_buffers) + slot),
                       device->uniform_buffers[stage][slot]);
    }
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
}

SDL_GPUDevice* SDL_CreateGPUDeviceWithProperties(SDL_PropertiesID props) {
  const char* name = SDL_GetStringProperty(
      props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, nullptr);
  if (!gpu_driver_matches(name)) {
    SDL_SetError("WebGL 2 GPU: there is no GPU driver named %s", name);
    return nullptr;
  }
  if (!SDL_GetBooleanProperty(
          props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, false)) {
    gpu_unsupported(nullptr, GPU_FEATURE_SHADER_FORMAT);
    return nullptr;
  }
  const char* canvas = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
  if (!canvas || !*canvas) {
    canvas = "#canvas";
  }
  EmscriptenWebGLContextAttributes attributes;
  emscripten_webgl_init_context_attributes(&attributes);
  attributes.majorVersion          = 2;
  attributes.minorVersion          = 0;
  attributes.alpha                 = false;
  attributes.depth                 = false;
  attributes.stencil               = false;
  attributes.antialias             = false;
  attributes.preserveDrawingBuffer = false;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context =
      emscripten_webgl_create_context(canvas, &attributes);
  if (context <= 0) {
    SDL_SetError("WebGL 2 GPU: cannot create a WebGL 2 context on %s", canvas);
    return nullptr;
  }
  SDL_GPUDevice* device = SDL_calloc(1, sizeof *device);
  if (!device) {
    emscripten_webgl_destroy_context(context);
    return nullptr;
  }
  device->context = context;
  device->canvas  = SDL_strdup(canvas);
  emscripten_webgl_make_context_current(context);
  device->float_targets =
      emscripten_webgl_enable_extension(context, "EXT_color_buffer_float");
  device->anisotropy = emscripten_webgl_enable_extension(
      context, "EXT_texture_filter_anisotropic");
  emscripten_webgl_enable_extension(context, "OES_texture_float_linear");
  gpu_create_objects(device);
  if (!device->canvas || !gpu_set_device_properties(device)) {
    SDL_DestroyGPUDevice(device);
    return nullptr;
  }
  return device;
}

static void gpu_free_texture(SDL_GPUDevice* device, SDL_GPUTexture* texture);

void SDL_DestroyGPUDevice(SDL_GPUDevice* device) {
  if (!device) {
    return;
  }
  if (device->window) {
    SDL_ReleaseWindowFromGPUDevice(device, device->window);
  }
  glDeleteVertexArrays(1, &device->vertex_array);
  const GLuint framebuffers[] = {device->pass_framebuffer,
                                 device->read_framebuffer,
                                 device->draw_framebuffer};
  glDeleteFramebuffers(3, framebuffers);
  for (size_t stage = 0; stage < 2; ++stage) {
    glDeleteBuffers((GLsizei)gpu_max_uniform_buffers,
                    device->uniform_buffers[stage]);
  }
  emscripten_webgl_destroy_context(device->context);
  SDL_DestroyProperties(device->props);
  SDL_free(device->canvas);
  SDL_free(device);
}

int SDL_GetNumGPUDrivers() {
  return gpu_webgl2_available() ? 1 : 0;
}

const char* SDL_GetGPUDriver(int index) {
  if (index != 0 || !gpu_webgl2_available()) {
    SDL_InvalidParamError("index");
    return nullptr;
  }
  return gpu_driver;
}

const char* SDL_GetGPUDeviceDriver(SDL_GPUDevice* device) {
  if (!device) {
    gpu_invalid("device");
    return nullptr;
  }
  return gpu_driver;
}

SDL_GPUShaderFormat SDL_GetGPUShaderFormats(SDL_GPUDevice* device) {
  if (!device) {
    gpu_invalid("device");
    return SDL_GPU_SHADERFORMAT_INVALID;
  }
  return SDL_GPU_SHADERFORMAT_SPIRV;
}

SDL_PropertiesID SDL_GetGPUDeviceProperties(SDL_GPUDevice* device) {
  if (!device) {
    gpu_invalid("device");
    return 0;
  }
  return device->props;
}

// ---------------------------------------------------------------------------
// Shaders

/** The names of the resources of a stage in GLSL. */
typedef struct gpu_names {
  const char* sampler;
  const char* uniforms;
} gpu_names;

static const gpu_names gpu_stage_names[] = {
    [GPU_STAGE_VERTEX]   = {"sdl_vs_sampler_", "sdl_vs_uniforms_"},
    [GPU_STAGE_FRAGMENT] = {"sdl_fs_sampler_", "sdl_fs_uniforms_"},
};

/**
 * Names the resources of one type: prefix + the binding, or prefix + the
 * location for stage inputs and outputs.
 */
static void gpu_name_resources(spvc_compiler compiler, spvc_resources resources,
                               spvc_resource_type type, const char* prefix) {
  const spvc_reflected_resource* list = nullptr;
  size_t count                        = 0;
  spvc_resources_get_resource_list_for_type(resources, type, &list, &count);
  bool varying = type == SPVC_RESOURCE_TYPE_STAGE_INPUT ||
                 type == SPVC_RESOURCE_TYPE_STAGE_OUTPUT;
  for (size_t i = 0; i < count; ++i) {
    unsigned number = spvc_compiler_get_decoration(
        compiler, list[i].id,
        varying ? SpvDecorationLocation : SpvDecorationBinding);
    char name[64];
    SDL_snprintf(name, sizeof name, "%s%u", prefix, number);
    if (type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER) {
      // GLSL finds a uniform block by the name of its type.
      spvc_compiler_set_name(compiler, list[i].base_type_id, name);
      SDL_strlcat(name, "_data", sizeof name);
    }
    spvc_compiler_set_name(compiler, list[i].id, name);
  }
}

/** Returns true if a shader uses resources that WebGL 2 does not have. */
static bool gpu_uses_storage(spvc_resources resources) {
  const spvc_resource_type types[] = {
      SPVC_RESOURCE_TYPE_STORAGE_BUFFER,
      SPVC_RESOURCE_TYPE_STORAGE_IMAGE,
  };
  for (size_t i = 0; i < SDL_arraysize(types); ++i) {
    const spvc_reflected_resource* list = nullptr;
    size_t count                        = 0;
    spvc_resources_get_resource_list_for_type(resources, types[i], &list,
                                              &count);
    if (count > 0) {
      return true;
    }
  }
  return false;
}

/** Sets the options of SPIRV-Cross for GLSL ES 3.00 in this file. */
static bool gpu_set_glsl_options(spvc_compiler compiler) {
  spvc_compiler_options options = nullptr;
  if (spvc_compiler_create_compiler_options(compiler, &options) !=
      SPVC_SUCCESS) {
    return false;
  }
  spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_GLSL_VERSION,
                                 300);
  spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_GLSL_ES, true);
  spvc_compiler_options_set_bool(
      options, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_FLOAT_PRECISION_HIGHP,
      true);
  spvc_compiler_options_set_bool(
      options, SPVC_COMPILER_OPTION_GLSL_ES_DEFAULT_INT_PRECISION_HIGHP, true);
  // Keep the rows of SDL in textures, and the depth range of SDL.
  spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_FLIP_VERTEX_Y,
                                 true);
  spvc_compiler_options_set_bool(
      options, SPVC_COMPILER_OPTION_FIXUP_DEPTH_CONVENTION, true);
  return spvc_compiler_install_compiler_options(compiler, options) ==
         SPVC_SUCCESS;
}

/**
 * Translates SPIR-V to GLSL ES 3.00.
 *
 * @return The GLSL, or nullptr. Free it with SDL_free().
 */
static char* gpu_translate(SDL_GPUDevice* device,
                           const SDL_GPUShaderCreateInfo* info,
                           gpu_stage stage) {
  if (info->code_size % sizeof(SpvId) != 0) {
    SDL_SetError("WebGL 2 GPU: the SPIR-V size is not a multiple of 4");
    return nullptr;
  }
  // The code can be unaligned. SPIRV-Cross reads it as 32-bit words.
  SpvId* words = SDL_malloc(info->code_size);
  if (!words) {
    return nullptr;
  }
  SDL_memcpy(words, info->code, info->code_size);
  spvc_context context = nullptr;
  if (spvc_context_create(&context) != SPVC_SUCCESS) {
    SDL_free(words);
    SDL_SetError("WebGL 2 GPU: cannot create a SPIRV-Cross context");
    return nullptr;
  }
  char* glsl                    = nullptr;
  spvc_parsed_ir ir             = nullptr;
  spvc_compiler compiler        = nullptr;
  spvc_resources resources      = nullptr;
  const gpu_names* names        = &gpu_stage_names[stage];
  const SpvExecutionModel model = stage == GPU_STAGE_VERTEX
                                      ? SpvExecutionModelVertex
                                      : SpvExecutionModelFragment;
  bool parsed =
      spvc_context_parse_spirv(context, words, info->code_size / sizeof(SpvId),
                               &ir) == SPVC_SUCCESS &&
      spvc_context_create_compiler(context, SPVC_BACKEND_GLSL, ir,
                                   SPVC_CAPTURE_MODE_TAKE_OWNERSHIP,
                                   &compiler) == SPVC_SUCCESS &&
      (!info->entrypoint || SDL_strcmp(info->entrypoint, "main") == 0 ||
       spvc_compiler_set_entry_point(compiler, info->entrypoint, model) ==
           SPVC_SUCCESS) &&
      gpu_set_glsl_options(compiler) &&
      spvc_compiler_create_shader_resources(compiler, &resources) ==
          SPVC_SUCCESS;
  if (parsed && gpu_uses_storage(resources)) {
    gpu_unsupported(device, GPU_FEATURE_STORAGE);
  } else if (parsed) {
    gpu_name_resources(compiler, resources, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE,
                       names->sampler);
    gpu_name_resources(compiler, resources, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER,
                       names->uniforms);
    // GLSL ES 3.00 links the outputs of a stage to the inputs of the next
    // by name. Name them by location.
    gpu_name_resources(compiler, resources,
                       stage == GPU_STAGE_VERTEX
                           ? SPVC_RESOURCE_TYPE_STAGE_OUTPUT
                           : SPVC_RESOURCE_TYPE_STAGE_INPUT,
                       "sdl_varying_");
    const char* source = nullptr;
    if (spvc_compiler_compile(compiler, &source) == SPVC_SUCCESS) {
      glsl = SDL_strdup(source);
    } else {
      parsed = false;
    }
  }
  if (!parsed) {
    SDL_SetError("WebGL 2 GPU: SPIRV-Cross: %s",
                 spvc_context_get_last_error_string(context));
  }
  spvc_context_destroy(context);
  SDL_free(words);
  return glsl;
}

/** Logs GLSL with line numbers, so that a GLSL error can be found. */
static void gpu_log_glsl(const char* glsl) {
  SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "The GLSL of the shader:");
  int line = 1;
  for (const char* start = glsl; *start;) {
    const char* end = SDL_strchr(start, '\n');
    size_t length   = end ? (size_t)(end - start) : SDL_strlen(start);
    SDL_LogDebug(SDL_LOG_CATEGORY_GPU, "%4d  %.*s", line++, (int)length, start);
    start += length + (end ? 1 : 0);
  }
}

SDL_GPUShader* SDL_CreateGPUShader(SDL_GPUDevice* device,
                                   const SDL_GPUShaderCreateInfo* createinfo) {
  if (!device || !createinfo || !createinfo->code) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  if (!(createinfo->format & SDL_GPU_SHADERFORMAT_SPIRV)) {
    gpu_unsupported(device, GPU_FEATURE_SHADER_FORMAT);
    return nullptr;
  }
  if (createinfo->num_storage_textures > 0 ||
      createinfo->num_storage_buffers > 0) {
    gpu_unsupported(device, GPU_FEATURE_STORAGE);
    return nullptr;
  }
  if (createinfo->num_samplers > gpu_max_samplers ||
      createinfo->num_uniform_buffers > gpu_max_uniform_buffers) {
    SDL_SetError("WebGL 2 GPU: too many samplers or uniform buffers");
    return nullptr;
  }
  gpu_stage stage = createinfo->stage == SDL_GPU_SHADERSTAGE_VERTEX
                        ? GPU_STAGE_VERTEX
                        : GPU_STAGE_FRAGMENT;
  char* glsl      = gpu_translate(device, createinfo, stage);
  if (!glsl) {
    return nullptr;
  }
  GLuint shader = glCreateShader(
      stage == GPU_STAGE_VERTEX ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
  const char* sources[] = {glsl};
  glShaderSource(shader, 1, sources, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    char log[1024] = {};
    glGetShaderInfoLog(shader, (GLsizei)sizeof log, nullptr, log);
    gpu_log_glsl(glsl);
    SDL_SetError("WebGL 2 GPU: the GLSL does not compile: %s", log);
    glDeleteShader(shader);
    SDL_free(glsl);
    return nullptr;
  }
  SDL_free(glsl);
  SDL_GPUShader* result = SDL_malloc(sizeof *result);
  if (!result) {
    glDeleteShader(shader);
    return nullptr;
  }
  *result = (SDL_GPUShader){
      .shader              = shader,
      .stage               = stage,
      .num_samplers        = createinfo->num_samplers,
      .num_uniform_buffers = createinfo->num_uniform_buffers,
  };
  return result;
}

void SDL_ReleaseGPUShader(SDL_GPUDevice* device, SDL_GPUShader* shader) {
  if (!device || !shader) {
    return;
  }
  glDeleteShader(shader->shader);
  SDL_free(shader);
}

// ---------------------------------------------------------------------------
// Textures, samplers and buffers

/** Binds a texture to the work unit. Pass nullptr to unbind it. */
static void gpu_bind_work_texture(SDL_GPUDevice* device,
                                  SDL_GPUTexture* texture) {
  glActiveTexture(GL_TEXTURE0 + gpu_work_unit);
  glBindTexture(GL_TEXTURE_2D, texture ? texture->texture : 0);
  device->units[gpu_work_unit] = texture;
}

/** Creates a texture. Does not check the create info. */
static SDL_GPUTexture* gpu_create_texture(SDL_GPUDevice* device,
                                          SDL_GPUTextureFormat format,
                                          SDL_GPUTextureUsageFlags usage,
                                          Uint32 width, Uint32 height,
                                          Uint32 levels) {
  SDL_GPUTexture* texture = SDL_calloc(1, sizeof *texture);
  if (!texture) {
    return nullptr;
  }
  *texture = (SDL_GPUTexture){
      .sdl_format = format,
      .format     = gpu_texture_format(format),
      .usage      = usage,
      .width      = width,
      .height     = height,
      .levels     = levels,
  };
  while (glGetError() != GL_NO_ERROR) {
  }
  glGenTextures(1, &texture->texture);
  gpu_bind_work_texture(device, texture);
  glTexStorage2D(GL_TEXTURE_2D, (GLsizei)levels,
                 texture->format->internal_format, (GLsizei)width,
                 (GLsizei)height);
  gpu_bind_work_texture(device, nullptr);
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    SDL_SetError("WebGL 2 GPU: cannot create a %u x %u texture (0x%x)",
                 (unsigned)width, (unsigned)height, (unsigned)error);
    glDeleteTextures(1, &texture->texture);
    SDL_free(texture);
    return nullptr;
  }
  return texture;
}

SDL_GPUTexture*
SDL_CreateGPUTexture(SDL_GPUDevice* device,
                     const SDL_GPUTextureCreateInfo* createinfo) {
  if (!device || !createinfo) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  if (createinfo->type != SDL_GPU_TEXTURETYPE_2D ||
      createinfo->layer_count_or_depth > 1) {
    gpu_unsupported(device, GPU_FEATURE_TEXTURE_TYPE);
    return nullptr;
  }
  if (createinfo->sample_count != SDL_GPU_SAMPLECOUNT_1) {
    gpu_unsupported(device, GPU_FEATURE_MULTISAMPLE);
    return nullptr;
  }
  if (!SDL_GPUTextureSupportsFormat(device, createinfo->format,
                                    createinfo->type, createinfo->usage)) {
    gpu_unsupported(device, GPU_FEATURE_FORMAT);
    return nullptr;
  }
  if (createinfo->width == 0 || createinfo->height == 0 ||
      createinfo->num_levels == 0) {
    SDL_SetError("WebGL 2 GPU: the texture size and levels must be positive");
    return nullptr;
  }
  return gpu_create_texture(device, createinfo->format, createinfo->usage,
                            createinfo->width, createinfo->height,
                            createinfo->num_levels);
}

/** Frees a texture, and removes it from the texture units. */
static void gpu_free_texture(SDL_GPUDevice* device, SDL_GPUTexture* texture) {
  for (size_t i = 0; i < gpu_texture_units; ++i) {
    if (device->units[i] == texture) {
      device->units[i] = nullptr;
    }
  }
  glDeleteTextures(1, &texture->texture);
  SDL_free(texture);
}

void SDL_ReleaseGPUTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture) {
  if (device && texture && !texture->swapchain) {
    gpu_free_texture(device, texture);
  }
}

void SDL_SetGPUTextureName([[maybe_unused]] SDL_GPUDevice* device,
                           [[maybe_unused]] SDL_GPUTexture* texture,
                           [[maybe_unused]] const char* text) {}

bool SDL_GPUTextureSupportsFormat(SDL_GPUDevice* device,
                                  SDL_GPUTextureFormat format,
                                  SDL_GPUTextureType type,
                                  SDL_GPUTextureUsageFlags usage) {
  if (!device) {
    return gpu_invalid("device");
  }
  const gpu_format* f = gpu_texture_format(format);
  if (!f || type != SDL_GPU_TEXTURETYPE_2D) {
    return false;
  }
  const SDL_GPUTextureUsageFlags storage =
      SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ |
      SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
      SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE;
  if (usage & storage) {
    return false;
  }
  if ((usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) &&
      !gpu_can_render(device, f)) {
    return false;
  }
  return !(usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) ||
         gpu_is_depth(f);
}

bool SDL_GPUTextureSupportsSampleCount(SDL_GPUDevice* device,
                                       SDL_GPUTextureFormat format,
                                       SDL_GPUSampleCount sample_count) {
  if (!device) {
    return gpu_invalid("device");
  }
  return gpu_texture_format(format) && sample_count == SDL_GPU_SAMPLECOUNT_1;
}

static GLenum gpu_min_filter(const SDL_GPUSamplerCreateInfo* info) {
  bool linear = info->min_filter == SDL_GPU_FILTER_LINEAR;
  if (info->mipmap_mode == SDL_GPU_SAMPLERMIPMAPMODE_LINEAR) {
    return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
  }
  return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
}

SDL_GPUSampler*
SDL_CreateGPUSampler(SDL_GPUDevice* device,
                     const SDL_GPUSamplerCreateInfo* createinfo) {
  if (!device || !createinfo) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  SDL_GPUSampler* sampler = SDL_malloc(sizeof *sampler);
  if (!sampler) {
    return nullptr;
  }
  glGenSamplers(1, &sampler->sampler);
  GLuint s = sampler->sampler;
  glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER,
                      (GLint)gpu_min_filter(createinfo));
  glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER,
                      createinfo->mag_filter == SDL_GPU_FILTER_LINEAR
                          ? (GLint)GL_LINEAR
                          : (GLint)GL_NEAREST);
  glSamplerParameteri(
      s, GL_TEXTURE_WRAP_S,
      (GLint)GPU_MAP(gpu_address_modes, createinfo->address_mode_u));
  glSamplerParameteri(
      s, GL_TEXTURE_WRAP_T,
      (GLint)GPU_MAP(gpu_address_modes, createinfo->address_mode_v));
  glSamplerParameteri(
      s, GL_TEXTURE_WRAP_R,
      (GLint)GPU_MAP(gpu_address_modes, createinfo->address_mode_w));
  glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, createinfo->min_lod);
  glSamplerParameterf(s, GL_TEXTURE_MAX_LOD, createinfo->max_lod);
  if (createinfo->enable_compare) {
    glSamplerParameteri(s, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(
        s, GL_TEXTURE_COMPARE_FUNC,
        (GLint)GPU_MAP(gpu_compare_ops, createinfo->compare_op));
  }
  if (createinfo->enable_anisotropy && device->anisotropy) {
    glSamplerParameterf(s, gpu_max_anisotropy, createinfo->max_anisotropy);
  }
  return sampler;
}

void SDL_ReleaseGPUSampler(SDL_GPUDevice* device, SDL_GPUSampler* sampler) {
  if (device && sampler) {
    glDeleteSamplers(1, &sampler->sampler);
    SDL_free(sampler);
  }
}

SDL_GPUBuffer* SDL_CreateGPUBuffer(SDL_GPUDevice* device,
                                   const SDL_GPUBufferCreateInfo* createinfo) {
  if (!device || !createinfo) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  const SDL_GPUBufferUsageFlags storage =
      SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
      SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
  if (createinfo->usage & storage) {
    gpu_unsupported(device, GPU_FEATURE_STORAGE);
    return nullptr;
  }
  SDL_GPUBuffer* buffer = SDL_malloc(sizeof *buffer);
  if (!buffer) {
    return nullptr;
  }
  *buffer = (SDL_GPUBuffer){
      .target = (createinfo->usage & SDL_GPU_BUFFERUSAGE_INDEX)
                    ? GL_ELEMENT_ARRAY_BUFFER
                    : GL_ARRAY_BUFFER,
      .size   = createinfo->size,
  };
  glGenBuffers(1, &buffer->buffer);
  glBindBuffer(buffer->target, buffer->buffer);
  glBufferData(buffer->target, (GLsizeiptr)createinfo->size, nullptr,
               GL_DYNAMIC_DRAW);
  return buffer;
}

void SDL_ReleaseGPUBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) {
  if (device && buffer) {
    glDeleteBuffers(1, &buffer->buffer);
    SDL_free(buffer);
  }
}

void SDL_SetGPUBufferName([[maybe_unused]] SDL_GPUDevice* device,
                          [[maybe_unused]] SDL_GPUBuffer* buffer,
                          [[maybe_unused]] const char* text) {}

SDL_GPUTransferBuffer*
SDL_CreateGPUTransferBuffer(SDL_GPUDevice* device,
                            const SDL_GPUTransferBufferCreateInfo* createinfo) {
  if (!device || !createinfo) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  SDL_GPUTransferBuffer* buffer = SDL_malloc(sizeof *buffer);
  if (!buffer) {
    return nullptr;
  }
  buffer->size = createinfo->size;
  buffer->data = SDL_calloc(1, createinfo->size ? createinfo->size : 1);
  if (!buffer->data) {
    SDL_free(buffer);
    return nullptr;
  }
  return buffer;
}

void SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice* device,
                                  SDL_GPUTransferBuffer* transfer_buffer) {
  if (device && transfer_buffer) {
    SDL_free(transfer_buffer->data);
    SDL_free(transfer_buffer);
  }
}

// The commands run when they are recorded, so the data of a transfer buffer
// is free to change after each command. Cycling is not needed.
void* SDL_MapGPUTransferBuffer(SDL_GPUDevice* device,
                               SDL_GPUTransferBuffer* transfer_buffer,
                               [[maybe_unused]] bool cycle) {
  if (!device || !transfer_buffer) {
    gpu_invalid(!device ? "device" : "transfer_buffer");
    return nullptr;
  }
  return transfer_buffer->data;
}

void SDL_UnmapGPUTransferBuffer(
    [[maybe_unused]] SDL_GPUDevice* device,
    [[maybe_unused]] SDL_GPUTransferBuffer* buffer) {}

// ---------------------------------------------------------------------------
// Pipelines

/** Links a program and binds its samplers and uniform blocks. */
static GLuint gpu_link_program(const SDL_GPUShader* vertex,
                               const SDL_GPUShader* fragment) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex->shader);
  glAttachShader(program, fragment->shader);
  glLinkProgram(program);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[1024] = {};
    glGetProgramInfoLog(program, (GLsizei)sizeof log, nullptr, log);
    SDL_SetError("WebGL 2 GPU: the shaders do not link: %s", log);
    glDeleteProgram(program);
    return 0;
  }
  glUseProgram(program);
  const SDL_GPUShader* shaders[] = {vertex, fragment};
  for (size_t i = 0; i < SDL_arraysize(shaders); ++i) {
    gpu_stage stage        = shaders[i]->stage;
    const gpu_names* names = &gpu_stage_names[stage];
    // Fragment samplers use units 0 to 15, vertex samplers 16 to 31.
    Uint32 first_unit = stage == GPU_STAGE_FRAGMENT ? 0 : gpu_max_samplers;
    char name[64];
    for (Uint32 slot = 0; slot < shaders[i]->num_samplers; ++slot) {
      SDL_snprintf(name, sizeof name, "%s%u", names->sampler, slot);
      GLint location = glGetUniformLocation(program, name);
      if (location >= 0) {
        glUniform1i(location, (GLint)(first_unit + slot));
      }
    }
    for (Uint32 slot = 0; slot < shaders[i]->num_uniform_buffers; ++slot) {
      SDL_snprintf(name, sizeof name, "%s%u", names->uniforms, slot);
      GLuint index = glGetUniformBlockIndex(program, name);
      if (index != GL_INVALID_INDEX) {
        glUniformBlockBinding(program, index,
                              ((Uint32)stage * gpu_max_uniform_buffers) + slot);
      }
    }
  }
  return program;
}

/** Copies the vertex input of a pipeline. */
static bool gpu_set_vertex_input(SDL_GPUGraphicsPipeline* pipeline,
                                 const SDL_GPUVertexInputState* input) {
  if (input->num_vertex_attributes > gpu_max_vertex_attributes ||
      input->num_vertex_buffers > gpu_max_vertex_buffers) {
    return SDL_SetError("WebGL 2 GPU: too many vertex buffers or attributes");
  }
  for (Uint32 i = 0; i < input->num_vertex_buffers; ++i) {
    const SDL_GPUVertexBufferDescription* d =
        &input->vertex_buffer_descriptions[i];
    if (d->slot >= gpu_max_vertex_buffers) {
      return SDL_SetError("WebGL 2 GPU: vertex buffer slot %u is too high",
                          (unsigned)d->slot);
    }
    pipeline->pitches[d->slot] = d->pitch;
    pipeline->divisors[d->slot] =
        d->input_rate == SDL_GPU_VERTEXINPUTRATE_INSTANCE
            ? SDL_max(d->instance_step_rate, 1u)
            : 0;
  }
  for (Uint32 i = 0; i < input->num_vertex_attributes; ++i) {
    const SDL_GPUVertexAttribute* a = &input->vertex_attributes[i];
    if (a->location >= gpu_max_vertex_attributes ||
        a->buffer_slot >= gpu_max_vertex_buffers ||
        (size_t)a->format >= SDL_arraysize(gpu_vertex_formats) ||
        gpu_vertex_formats[a->format].size == 0) {
      return SDL_SetError("WebGL 2 GPU: vertex attribute %u is not valid",
                          (unsigned)i);
    }
    pipeline->attributes[i] = *a;
  }
  pipeline->num_attributes = input->num_vertex_attributes;
  return true;
}

SDL_GPUGraphicsPipeline* SDL_CreateGPUGraphicsPipeline(
    SDL_GPUDevice* device,
    const SDL_GPUGraphicsPipelineCreateInfo* createinfo) {
  if (!device || !createinfo || !createinfo->vertex_shader ||
      !createinfo->fragment_shader) {
    gpu_invalid(!device ? "device" : "createinfo");
    return nullptr;
  }
  const SDL_GPUGraphicsPipelineTargetInfo* targets = &createinfo->target_info;
  if (targets->num_color_targets > gpu_max_color_targets) {
    SDL_SetError("WebGL 2 GPU: too many color targets");
    return nullptr;
  }
  if (createinfo->rasterizer_state.fill_mode == SDL_GPU_FILLMODE_LINE) {
    gpu_unsupported(device, GPU_FEATURE_FILL_MODE);
    return nullptr;
  }
  if (createinfo->multisample_state.sample_count != SDL_GPU_SAMPLECOUNT_1) {
    gpu_unsupported(device, GPU_FEATURE_MULTISAMPLE);
    return nullptr;
  }
  SDL_GPUGraphicsPipeline* pipeline = SDL_calloc(1, sizeof *pipeline);
  if (!pipeline) {
    return nullptr;
  }
  pipeline->mode = GPU_MAP(gpu_primitive_modes, createinfo->primitive_type);
  pipeline->rasterizer    = createinfo->rasterizer_state;
  pipeline->depth_stencil = createinfo->depth_stencil_state;
  pipeline->alpha_to_coverage =
      createinfo->multisample_state.enable_alpha_to_coverage;
  if (targets->num_color_targets > 0) {
    pipeline->blend = targets->color_target_descriptions[0].blend_state;
  }
  if (!gpu_set_vertex_input(pipeline, &createinfo->vertex_input_state)) {
    SDL_free(pipeline);
    return nullptr;
  }
  pipeline->program =
      gpu_link_program(createinfo->vertex_shader, createinfo->fragment_shader);
  if (!pipeline->program) {
    SDL_free(pipeline);
    return nullptr;
  }
  return pipeline;
}

void SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice* device,
                                    SDL_GPUGraphicsPipeline* pipeline) {
  if (device && pipeline) {
    glDeleteProgram(pipeline->program);
    SDL_free(pipeline);
  }
}

SDL_GPUComputePipeline* SDL_CreateGPUComputePipeline(
    SDL_GPUDevice* device,
    [[maybe_unused]] const SDL_GPUComputePipelineCreateInfo* createinfo) {
  gpu_unsupported(device, GPU_FEATURE_COMPUTE);
  return nullptr;
}

void SDL_ReleaseGPUComputePipeline(
    [[maybe_unused]] SDL_GPUDevice* device,
    [[maybe_unused]] SDL_GPUComputePipeline* compute_pipeline) {}

// ---------------------------------------------------------------------------
// Command buffers

SDL_GPUCommandBuffer* SDL_AcquireGPUCommandBuffer(SDL_GPUDevice* device) {
  if (!device) {
    gpu_invalid("device");
    return nullptr;
  }
  SDL_GPUCommandBuffer* cmd = SDL_calloc(1, sizeof *cmd);
  if (!cmd) {
    return nullptr;
  }
  cmd->device                      = device;
  cmd->render_pass.command_buffer  = cmd;
  cmd->copy_pass.command_buffer    = cmd;
  cmd->compute_pass.command_buffer = cmd;
  return cmd;
}

void SDL_InsertGPUDebugLabel(
    [[maybe_unused]] SDL_GPUCommandBuffer* command_buffer,
    [[maybe_unused]] const char* text) {}

void SDL_PushGPUDebugGroup(
    [[maybe_unused]] SDL_GPUCommandBuffer* command_buffer,
    [[maybe_unused]] const char* name) {}

void SDL_PopGPUDebugGroup(
    [[maybe_unused]] SDL_GPUCommandBuffer* command_buffer) {}

/** Sets the data of a uniform buffer. Draws that come after use it. */
static void gpu_push_uniforms(SDL_GPUCommandBuffer* cmd, gpu_stage stage,
                              Uint32 slot, const void* data, Uint32 length) {
  if (!cmd || !data || slot >= gpu_max_uniform_buffers) {
    gpu_invalid(!cmd ? "command_buffer" : !data ? "data" : "slot_index");
    return;
  }
  glBindBuffer(GL_UNIFORM_BUFFER, cmd->device->uniform_buffers[stage][slot]);
  glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)length, data, GL_DYNAMIC_DRAW);
}

void SDL_PushGPUVertexUniformData(SDL_GPUCommandBuffer* command_buffer,
                                  Uint32 slot_index, const void* data,
                                  Uint32 length) {
  gpu_push_uniforms(command_buffer, GPU_STAGE_VERTEX, slot_index, data, length);
}

void SDL_PushGPUFragmentUniformData(SDL_GPUCommandBuffer* command_buffer,
                                    Uint32 slot_index, const void* data,
                                    Uint32 length) {
  gpu_push_uniforms(command_buffer, GPU_STAGE_FRAGMENT, slot_index, data,
                    length);
}

void SDL_PushGPUComputeUniformData(SDL_GPUCommandBuffer* command_buffer,
                                   [[maybe_unused]] Uint32 slot_index,
                                   [[maybe_unused]] const void* data,
                                   [[maybe_unused]] Uint32 length) {
  gpu_unsupported(command_buffer ? command_buffer->device : nullptr,
                  GPU_FEATURE_COMPUTE);
}

// ---------------------------------------------------------------------------
// Render passes

static Uint32 gpu_mip_size(Uint32 size, Uint32 level) {
  Uint32 scaled = level < 32 ? size >> level : 0;
  return scaled > 0 ? scaled : 1;
}

/**
 * Unbinds the targets of a pass from the texture units. WebGL stops a draw
 * that samples the texture that it draws into, even when the shader does
 * not read it.
 */
static void gpu_unbind_targets(SDL_GPUDevice* device,
                               SDL_GPUTexture* const* targets, size_t count) {
  for (size_t unit = 0; unit < gpu_texture_units; ++unit) {
    for (size_t i = 0; i < count; ++i) {
      if (targets[i] && device->units[unit] == targets[i]) {
        glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
        glBindTexture(GL_TEXTURE_2D, 0);
        device->units[unit] = nullptr;
      }
    }
  }
}

/** Attaches the color targets of a pass to the pass framebuffer. */
static bool gpu_attach_color_targets(const SDL_GPUColorTargetInfo* infos,
                                     Uint32 count) {
  GLenum buffers[gpu_max_color_targets] = {};
  for (Uint32 i = 0; i < gpu_max_color_targets; ++i) {
    const SDL_GPUTexture* t = i < count ? infos[i].texture : nullptr;
    if (i < count && (!t || !(t->usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET))) {
      return SDL_SetError("WebGL 2 GPU: color target %u is not a color "
                          "target texture",
                          (unsigned)i);
    }
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i,
                           GL_TEXTURE_2D, t ? t->texture : 0,
                           t ? (GLint)infos[i].mip_level : 0);
    buffers[i] = t ? GL_COLOR_ATTACHMENT0 + i : GL_NONE;
  }
  glDrawBuffers((GLsizei)SDL_max(count, 1u), buffers);
  return true;
}

/** Attaches the depth target of a pass, or detaches the old one. */
static bool gpu_attach_depth_target(const SDL_GPUDepthStencilTargetInfo* info) {
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                         GL_TEXTURE_2D, 0, 0);
  if (!info) {
    return true;
  }
  const SDL_GPUTexture* t = info->texture;
  if (!t || !(t->usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
    return SDL_SetError("WebGL 2 GPU: the depth target is not a depth "
                        "target texture");
  }
  GLenum attachment = t->format->kind == GPU_FORMAT_DEPTH_STENCIL
                          ? GL_DEPTH_STENCIL_ATTACHMENT
                          : GL_DEPTH_ATTACHMENT;
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachment, GL_TEXTURE_2D,
                         t->texture, info->mip_level);
  return true;
}

/** Clears color target i of the pass framebuffer. */
static void gpu_clear_color(const SDL_GPUColorTargetInfo* info, GLint i) {
  SDL_FColor c = info->clear_color;
  switch (info->texture->format->kind) {
  case GPU_FORMAT_INT: {
    const GLint values[] = {(GLint)c.r, (GLint)c.g, (GLint)c.b, (GLint)c.a};
    glClearBufferiv(GL_COLOR, i, values);
    break;
  }
  case GPU_FORMAT_UINT: {
    const GLuint values[] = {(GLuint)c.r, (GLuint)c.g, (GLuint)c.b,
                             (GLuint)c.a};
    glClearBufferuiv(GL_COLOR, i, values);
    break;
  }
  default: {
    const GLfloat values[] = {c.r, c.g, c.b, c.a};
    glClearBufferfv(GL_COLOR, i, values);
    break;
  }
  }
}

/** Clears the depth and stencil of the pass framebuffer, as the info asks. */
static void gpu_clear_depth(const SDL_GPUDepthStencilTargetInfo* info) {
  bool stencil       = info->texture->format->kind == GPU_FORMAT_DEPTH_STENCIL;
  bool clear_depth   = info->load_op == SDL_GPU_LOADOP_CLEAR;
  bool clear_stencil = stencil && info->stencil_load_op == SDL_GPU_LOADOP_CLEAR;
  glDepthMask(GL_TRUE);
  glStencilMask(0xff);
  if (clear_depth && clear_stencil) {
    glClearBufferfi(GL_DEPTH_STENCIL, 0, info->clear_depth,
                    info->clear_stencil);
  } else if (clear_depth) {
    glClearBufferfv(GL_DEPTH, 0, &info->clear_depth);
  } else if (clear_stencil) {
    const GLint value = info->clear_stencil;
    glClearBufferiv(GL_STENCIL, 0, &value);
  }
}

SDL_GPURenderPass* SDL_BeginGPURenderPass(
    SDL_GPUCommandBuffer* command_buffer,
    const SDL_GPUColorTargetInfo* color_target_infos, Uint32 num_color_targets,
    const SDL_GPUDepthStencilTargetInfo* depth_stencil_target_info) {
  if (!command_buffer) {
    gpu_invalid("command_buffer");
    return nullptr;
  }
  if (num_color_targets > gpu_max_color_targets ||
      (num_color_targets > 0 && !color_target_infos) ||
      (num_color_targets == 0 && !depth_stencil_target_info)) {
    gpu_invalid("color_target_infos");
    return nullptr;
  }
  SDL_GPUDevice* device   = command_buffer->device;
  SDL_GPURenderPass* pass = &command_buffer->render_pass;
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, device->pass_framebuffer);
  if (!gpu_attach_color_targets(color_target_infos, num_color_targets) ||
      !gpu_attach_depth_target(depth_stencil_target_info)) {
    return nullptr;
  }
  GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    SDL_SetError("WebGL 2 GPU: the targets of the render pass do not work "
                 "together (0x%x)",
                 (unsigned)status);
    return nullptr;
  }
  SDL_GPUTexture* targets[gpu_max_color_targets + 1] = {};
  Uint32 width                                       = 0;
  Uint32 height                                      = 0;
  for (Uint32 i = 0; i < num_color_targets; ++i) {
    targets[i] = color_target_infos[i].texture;
  }
  if (num_color_targets > 0) {
    width  = gpu_mip_size(targets[0]->width, color_target_infos[0].mip_level);
    height = gpu_mip_size(targets[0]->height, color_target_infos[0].mip_level);
  }
  if (depth_stencil_target_info) {
    SDL_GPUTexture* depth      = depth_stencil_target_info->texture;
    targets[num_color_targets] = depth;
    if (num_color_targets == 0) {
      width = gpu_mip_size(depth->width, depth_stencil_target_info->mip_level);
      height =
          gpu_mip_size(depth->height, depth_stencil_target_info->mip_level);
    }
  }
  gpu_unbind_targets(device, targets, SDL_arraysize(targets));

  glDisable(GL_SCISSOR_TEST);
  glViewport(0, 0, (GLsizei)width, (GLsizei)height);
  glDepthRangef(0.0f, 1.0f);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  for (Uint32 i = 0; i < num_color_targets; ++i) {
    if (color_target_infos[i].load_op == SDL_GPU_LOADOP_CLEAR) {
      gpu_clear_color(&color_target_infos[i], (GLint)i);
    }
  }
  if (depth_stencil_target_info) {
    gpu_clear_depth(depth_stencil_target_info);
  }
  *pass = (SDL_GPURenderPass){
      .command_buffer = command_buffer,
      .active         = true,
  };
  return pass;
}

static void gpu_apply_stencil(const SDL_GPURenderPass* pass) {
  const SDL_GPUDepthStencilState* ds    = &pass->pipeline->depth_stencil;
  const SDL_GPUStencilOpState* states[] = {&ds->front_stencil_state,
                                           &ds->back_stencil_state};
  const GLenum faces[]                  = {GL_FRONT, GL_BACK};
  for (size_t i = 0; i < 2; ++i) {
    glStencilFuncSeparate(faces[i],
                          GPU_MAP(gpu_compare_ops, states[i]->compare_op),
                          pass->stencil_reference, ds->compare_mask);
    glStencilOpSeparate(faces[i], GPU_MAP(gpu_stencil_ops, states[i]->fail_op),
                        GPU_MAP(gpu_stencil_ops, states[i]->depth_fail_op),
                        GPU_MAP(gpu_stencil_ops, states[i]->pass_op));
  }
  glStencilMask(ds->write_mask);
}

static void gpu_enable(GLenum capability, bool enable) {
  if (enable) {
    glEnable(capability);
  } else {
    glDisable(capability);
  }
}

static void gpu_apply_rasterizer(const SDL_GPURasterizerState* r) {
  gpu_enable(GL_CULL_FACE, r->cull_mode != SDL_GPU_CULLMODE_NONE);
  glCullFace(r->cull_mode == SDL_GPU_CULLMODE_FRONT ? GL_FRONT : GL_BACK);
  // The flip of y turns the winding around.
  glFrontFace(r->front_face == SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE ? GL_CW
                                                                   : GL_CCW);
  gpu_enable(GL_POLYGON_OFFSET_FILL, r->enable_depth_bias);
  glPolygonOffset(r->depth_bias_slope_factor, r->depth_bias_constant_factor);
}

static void gpu_apply_depth(const SDL_GPUDepthStencilState* ds) {
  // WebGL writes depth only when the test is on. ALWAYS writes without a
  // test.
  bool test = ds->enable_depth_test || ds->enable_depth_write;
  gpu_enable(GL_DEPTH_TEST, test);
  glDepthFunc(ds->enable_depth_test ? GPU_MAP(gpu_compare_ops, ds->compare_op)
                                    : GL_ALWAYS);
  glDepthMask(ds->enable_depth_write ? GL_TRUE : GL_FALSE);
  gpu_enable(GL_STENCIL_TEST, ds->enable_stencil_test);
}

static void gpu_apply_blend(const SDL_GPUColorTargetBlendState* b) {
  gpu_enable(GL_BLEND, b->enable_blend);
  if (b->enable_blend) {
    glBlendFuncSeparate(GPU_MAP(gpu_blend_factors, b->src_color_blendfactor),
                        GPU_MAP(gpu_blend_factors, b->dst_color_blendfactor),
                        GPU_MAP(gpu_blend_factors, b->src_alpha_blendfactor),
                        GPU_MAP(gpu_blend_factors, b->dst_alpha_blendfactor));
    glBlendEquationSeparate(GPU_MAP(gpu_blend_ops, b->color_blend_op),
                            GPU_MAP(gpu_blend_ops, b->alpha_blend_op));
  }
  SDL_GPUColorComponentFlags mask =
      b->enable_color_write_mask
          ? b->color_write_mask
          : (SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
             SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A);
  glColorMask((mask & SDL_GPU_COLORCOMPONENT_R) != 0,
              (mask & SDL_GPU_COLORCOMPONENT_G) != 0,
              (mask & SDL_GPU_COLORCOMPONENT_B) != 0,
              (mask & SDL_GPU_COLORCOMPONENT_A) != 0);
}

void SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass* render_pass,
                                 SDL_GPUGraphicsPipeline* graphics_pipeline) {
  if (!render_pass || !graphics_pipeline) {
    gpu_invalid(!render_pass ? "render_pass" : "graphics_pipeline");
    return;
  }
  render_pass->pipeline = graphics_pipeline;
  glUseProgram(graphics_pipeline->program);
  gpu_apply_rasterizer(&graphics_pipeline->rasterizer);
  gpu_apply_depth(&graphics_pipeline->depth_stencil);
  if (graphics_pipeline->depth_stencil.enable_stencil_test) {
    gpu_apply_stencil(render_pass);
  }
  gpu_apply_blend(&graphics_pipeline->blend);
  gpu_enable(GL_SAMPLE_ALPHA_TO_COVERAGE, graphics_pipeline->alpha_to_coverage);
}

void SDL_SetGPUViewport(SDL_GPURenderPass* render_pass,
                        const SDL_GPUViewport* viewport) {
  if (!render_pass || !viewport) {
    gpu_invalid(!render_pass ? "render_pass" : "viewport");
    return;
  }
  glViewport((GLint)viewport->x, (GLint)viewport->y, (GLsizei)viewport->w,
             (GLsizei)viewport->h);
  glDepthRangef(viewport->min_depth, viewport->max_depth);
}

void SDL_SetGPUScissor(SDL_GPURenderPass* render_pass,
                       const SDL_Rect* scissor) {
  if (!render_pass || !scissor) {
    gpu_invalid(!render_pass ? "render_pass" : "scissor");
    return;
  }
  glEnable(GL_SCISSOR_TEST);
  glScissor(scissor->x, scissor->y, scissor->w, scissor->h);
}

void SDL_SetGPUBlendConstants(SDL_GPURenderPass* render_pass,
                              SDL_FColor blend_constants) {
  if (!render_pass) {
    gpu_invalid("render_pass");
    return;
  }
  glBlendColor(blend_constants.r, blend_constants.g, blend_constants.b,
               blend_constants.a);
}

void SDL_SetGPUStencilReference(SDL_GPURenderPass* render_pass,
                                Uint8 reference) {
  if (!render_pass) {
    gpu_invalid("render_pass");
    return;
  }
  render_pass->stencil_reference = reference;
  if (render_pass->pipeline &&
      render_pass->pipeline->depth_stencil.enable_stencil_test) {
    gpu_apply_stencil(render_pass);
  }
}

void SDL_BindGPUVertexBuffers(SDL_GPURenderPass* render_pass, Uint32 first_slot,
                              const SDL_GPUBufferBinding* bindings,
                              Uint32 num_bindings) {
  if (!render_pass || !bindings ||
      first_slot + num_bindings > gpu_max_vertex_buffers) {
    gpu_invalid(!render_pass ? "render_pass" : "bindings");
    return;
  }
  for (Uint32 i = 0; i < num_bindings; ++i) {
    render_pass->vertex_buffers[first_slot + i] = bindings[i];
  }
}

void SDL_BindGPUIndexBuffer(SDL_GPURenderPass* render_pass,
                            const SDL_GPUBufferBinding* binding,
                            SDL_GPUIndexElementSize index_element_size) {
  if (!render_pass || !binding || !binding->buffer) {
    gpu_invalid(!render_pass ? "render_pass" : "binding");
    return;
  }
  bool wide = index_element_size == SDL_GPU_INDEXELEMENTSIZE_32BIT;
  render_pass->index_buffer = *binding;
  render_pass->index_type   = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
  render_pass->index_size   = wide ? 4 : 2;
}

/** Binds textures and samplers to the units from first_unit. */
static void gpu_bind_samplers(SDL_GPURenderPass* pass, Uint32 first_unit,
                              const SDL_GPUTextureSamplerBinding* bindings,
                              Uint32 count) {
  SDL_GPUDevice* device = pass->command_buffer->device;
  for (Uint32 i = 0; i < count; ++i) {
    Uint32 unit = first_unit + i;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D,
                  bindings[i].texture ? bindings[i].texture->texture : 0);
    glBindSampler(unit, bindings[i].sampler ? bindings[i].sampler->sampler : 0);
    device->units[unit] = bindings[i].texture;
  }
}

void SDL_BindGPUVertexSamplers(
    SDL_GPURenderPass* render_pass, Uint32 first_slot,
    const SDL_GPUTextureSamplerBinding* texture_sampler_bindings,
    Uint32 num_bindings) {
  if (!render_pass || !texture_sampler_bindings ||
      first_slot + num_bindings > gpu_max_samplers) {
    gpu_invalid(!render_pass ? "render_pass" : "texture_sampler_bindings");
    return;
  }
  gpu_bind_samplers(render_pass, gpu_max_samplers + first_slot,
                    texture_sampler_bindings, num_bindings);
}

void SDL_BindGPUFragmentSamplers(
    SDL_GPURenderPass* render_pass, Uint32 first_slot,
    const SDL_GPUTextureSamplerBinding* texture_sampler_bindings,
    Uint32 num_bindings) {
  if (!render_pass || !texture_sampler_bindings ||
      first_slot + num_bindings > gpu_max_samplers) {
    gpu_invalid(!render_pass ? "render_pass" : "texture_sampler_bindings");
    return;
  }
  gpu_bind_samplers(render_pass, first_slot, texture_sampler_bindings,
                    num_bindings);
}

static void gpu_storage_unsupported(const SDL_GPURenderPass* pass) {
  gpu_unsupported(pass ? pass->command_buffer->device : nullptr,
                  GPU_FEATURE_STORAGE);
}

void SDL_BindGPUVertexStorageTextures(
    SDL_GPURenderPass* render_pass, [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUTexture* const* storage_textures,
    [[maybe_unused]] Uint32 num_bindings) {
  gpu_storage_unsupported(render_pass);
}

void SDL_BindGPUVertexStorageBuffers(
    SDL_GPURenderPass* render_pass, [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUBuffer* const* storage_buffers,
    [[maybe_unused]] Uint32 num_bindings) {
  gpu_storage_unsupported(render_pass);
}

void SDL_BindGPUFragmentStorageTextures(
    SDL_GPURenderPass* render_pass, [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUTexture* const* storage_textures,
    [[maybe_unused]] Uint32 num_bindings) {
  gpu_storage_unsupported(render_pass);
}

void SDL_BindGPUFragmentStorageBuffers(
    SDL_GPURenderPass* render_pass, [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUBuffer* const* storage_buffers,
    [[maybe_unused]] Uint32 num_bindings) {
  gpu_storage_unsupported(render_pass);
}

/**
 * Points the vertex attributes of the pipeline at the bound vertex buffers.
 * WebGL 2 has no base vertex or base instance, so the offsets move the
 * attributes instead.
 */
static bool gpu_bind_attributes(SDL_GPURenderPass* pass, Sint64 vertex_offset,
                                Uint32 first_instance) {
  const SDL_GPUGraphicsPipeline* p = pass->pipeline;
  SDL_GPUDevice* device            = pass->command_buffer->device;
  if (!p) {
    return SDL_SetError("WebGL 2 GPU: draw without a pipeline");
  }
  Uint32 enabled = 0;
  for (Uint32 i = 0; i < p->num_attributes; ++i) {
    const SDL_GPUVertexAttribute* a     = &p->attributes[i];
    const SDL_GPUBufferBinding* binding = &pass->vertex_buffers[a->buffer_slot];
    const gpu_vertex_format* f          = &gpu_vertex_formats[a->format];
    Uint32 pitch                        = p->pitches[a->buffer_slot];
    Uint32 divisor                      = p->divisors[a->buffer_slot];
    if (!binding->buffer) {
      return SDL_SetError("WebGL 2 GPU: no vertex buffer in slot %u",
                          (unsigned)a->buffer_slot);
    }
    Sint64 first = divisor ? (Sint64)(first_instance / divisor) : vertex_offset;
    Sint64 offset = (Sint64)binding->offset + a->offset + (first * pitch);
    if (offset < 0) {
      return SDL_SetError("WebGL 2 GPU: negative vertex offset");
    }
    glBindBuffer(GL_ARRAY_BUFFER, binding->buffer->buffer);
    const void* pointer = (const void*)(uintptr_t)offset;
    if (f->integer) {
      glVertexAttribIPointer(a->location, f->size, f->type, (GLsizei)pitch,
                             pointer);
    } else {
      glVertexAttribPointer(a->location, f->size, f->type,
                            f->normalized ? GL_TRUE : GL_FALSE, (GLsizei)pitch,
                            pointer);
    }
    glVertexAttribDivisor(a->location, divisor);
    enabled |= 1u << a->location;
  }
  for (Uint32 location = 0; location < gpu_max_vertex_attributes; ++location) {
    Uint32 bit = 1u << location;
    if ((enabled & bit) && !(device->enabled_attributes & bit)) {
      glEnableVertexAttribArray(location);
    } else if (!(enabled & bit) && (device->enabled_attributes & bit)) {
      glDisableVertexAttribArray(location);
    }
  }
  device->enabled_attributes = enabled;
  return true;
}

void SDL_DrawGPUIndexedPrimitives(SDL_GPURenderPass* render_pass,
                                  Uint32 num_indices, Uint32 num_instances,
                                  Uint32 first_index, Sint32 vertex_offset,
                                  Uint32 first_instance) {
  if (!render_pass) {
    gpu_invalid("render_pass");
    return;
  }
  const SDL_GPUBufferBinding* indices = &render_pass->index_buffer;
  if (!indices->buffer) {
    SDL_SetError("WebGL 2 GPU: indexed draw without an index buffer");
    return;
  }
  if (!gpu_bind_attributes(render_pass, vertex_offset, first_instance)) {
    return;
  }
  uintptr_t offset =
      indices->offset + ((uintptr_t)first_index * render_pass->index_size);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->buffer->buffer);
  glDrawElementsInstanced(render_pass->pipeline->mode, (GLsizei)num_indices,
                          render_pass->index_type, (const void*)offset,
                          (GLsizei)num_instances);
}

void SDL_DrawGPUPrimitives(SDL_GPURenderPass* render_pass, Uint32 num_vertices,
                           Uint32 num_instances, Uint32 first_vertex,
                           Uint32 first_instance) {
  if (!render_pass) {
    gpu_invalid("render_pass");
    return;
  }
  if (!gpu_bind_attributes(render_pass, 0, first_instance)) {
    return;
  }
  glDrawArraysInstanced(render_pass->pipeline->mode, (GLint)first_vertex,
                        (GLsizei)num_vertices, (GLsizei)num_instances);
}

void SDL_DrawGPUPrimitivesIndirect(SDL_GPURenderPass* render_pass,
                                   [[maybe_unused]] SDL_GPUBuffer* buffer,
                                   [[maybe_unused]] Uint32 offset,
                                   [[maybe_unused]] Uint32 draw_count) {
  gpu_unsupported(render_pass ? render_pass->command_buffer->device : nullptr,
                  GPU_FEATURE_INDIRECT);
}

void SDL_DrawGPUIndexedPrimitivesIndirect(
    SDL_GPURenderPass* render_pass, [[maybe_unused]] SDL_GPUBuffer* buffer,
    [[maybe_unused]] Uint32 offset, [[maybe_unused]] Uint32 draw_count) {
  gpu_unsupported(render_pass ? render_pass->command_buffer->device : nullptr,
                  GPU_FEATURE_INDIRECT);
}

void SDL_EndGPURenderPass(SDL_GPURenderPass* render_pass) {
  if (render_pass) {
    render_pass->active   = false;
    render_pass->pipeline = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Compute passes

SDL_GPUComputePass* SDL_BeginGPUComputePass(
    SDL_GPUCommandBuffer* command_buffer,
    [[maybe_unused]] const SDL_GPUStorageTextureReadWriteBinding* textures,
    [[maybe_unused]] Uint32 num_storage_texture_bindings,
    [[maybe_unused]] const SDL_GPUStorageBufferReadWriteBinding* buffers,
    [[maybe_unused]] Uint32 num_storage_buffer_bindings) {
  gpu_unsupported(command_buffer ? command_buffer->device : nullptr,
                  GPU_FEATURE_COMPUTE);
  return nullptr;
}

void SDL_BindGPUComputePipeline(
    [[maybe_unused]] SDL_GPUComputePass* compute_pass,
    [[maybe_unused]] SDL_GPUComputePipeline* compute_pipeline) {}

void SDL_BindGPUComputeSamplers(
    [[maybe_unused]] SDL_GPUComputePass* compute_pass,
    [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] const SDL_GPUTextureSamplerBinding* bindings,
    [[maybe_unused]] Uint32 num_bindings) {}

void SDL_BindGPUComputeStorageTextures(
    [[maybe_unused]] SDL_GPUComputePass* compute_pass,
    [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUTexture* const* storage_textures,
    [[maybe_unused]] Uint32 num_bindings) {}

void SDL_BindGPUComputeStorageBuffers(
    [[maybe_unused]] SDL_GPUComputePass* compute_pass,
    [[maybe_unused]] Uint32 first_slot,
    [[maybe_unused]] SDL_GPUBuffer* const* storage_buffers,
    [[maybe_unused]] Uint32 num_bindings) {}

void SDL_DispatchGPUCompute([[maybe_unused]] SDL_GPUComputePass* compute_pass,
                            [[maybe_unused]] Uint32 groupcount_x,
                            [[maybe_unused]] Uint32 groupcount_y,
                            [[maybe_unused]] Uint32 groupcount_z) {}

void SDL_DispatchGPUComputeIndirect(
    [[maybe_unused]] SDL_GPUComputePass* compute_pass,
    [[maybe_unused]] SDL_GPUBuffer* buffer, [[maybe_unused]] Uint32 offset) {}

void SDL_EndGPUComputePass([[maybe_unused]] SDL_GPUComputePass* compute_pass) {}

// ---------------------------------------------------------------------------
// Copy passes

SDL_GPUCopyPass* SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer* command_buffer) {
  if (!command_buffer) {
    gpu_invalid("command_buffer");
    return nullptr;
  }
  return &command_buffer->copy_pass;
}

/**
 * Returns true if `size` bytes from `offset` fit in a buffer of
 * `capacity` bytes. Sets the error if not.
 */
static bool gpu_fits(Uint64 offset, Uint64 size, Uint64 capacity) {
  if (offset > capacity || size > capacity - offset) {
    return SDL_SetError("WebGL 2 GPU: the copy goes outside of the buffer");
  }
  return true;
}

/** Returns the bytes that an area of a texture takes in a transfer buffer. */
static Uint64 gpu_transfer_size(const SDL_GPUTexture* texture,
                                Uint32 pixels_per_row, Uint32 w, Uint32 h) {
  Uint32 texel = SDL_GPUTextureFormatTexelBlockSize(texture->sdl_format);
  Uint64 row   = (Uint64)(pixels_per_row ? pixels_per_row : w) * texel;
  return h == 0 ? 0 : (row * (h - 1)) + ((Uint64)w * texel);
}

void SDL_UploadToGPUTexture(SDL_GPUCopyPass* copy_pass,
                            const SDL_GPUTextureTransferInfo* source,
                            const SDL_GPUTextureRegion* destination,
                            [[maybe_unused]] bool cycle) {
  if (!copy_pass || !source || !source->transfer_buffer || !destination ||
      !destination->texture) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  SDL_GPUTexture* t           = destination->texture;
  SDL_GPUTransferBuffer* from = source->transfer_buffer;
  if (!gpu_fits(source->offset,
                gpu_transfer_size(t, source->pixels_per_row, destination->w,
                                  destination->h),
                from->size)) {
    return;
  }
  SDL_GPUDevice* device = copy_pass->command_buffer->device;
  gpu_bind_work_texture(device, t);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)source->pixels_per_row);
  glTexSubImage2D(
      GL_TEXTURE_2D, (GLint)destination->mip_level, (GLint)destination->x,
      (GLint)destination->y, (GLsizei)destination->w, (GLsizei)destination->h,
      t->format->format, t->format->type, from->data + source->offset);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  gpu_bind_work_texture(device, nullptr);
}

void SDL_UploadToGPUBuffer(SDL_GPUCopyPass* copy_pass,
                           const SDL_GPUTransferBufferLocation* source,
                           const SDL_GPUBufferRegion* destination,
                           [[maybe_unused]] bool cycle) {
  if (!copy_pass || !source || !source->transfer_buffer || !destination ||
      !destination->buffer) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  SDL_GPUBuffer* to           = destination->buffer;
  SDL_GPUTransferBuffer* from = source->transfer_buffer;
  if (!gpu_fits(source->offset, destination->size, from->size) ||
      !gpu_fits(destination->offset, destination->size, to->size)) {
    return;
  }
  glBindBuffer(to->target, to->buffer);
  glBufferSubData(to->target, (GLintptr)destination->offset,
                  (GLsizeiptr)destination->size, from->data + source->offset);
}

/** Attaches a texture level to the bound read framebuffer. */
static void gpu_attach_read(const SDL_GPUTexture* texture, Uint32 mip_level) {
  GLenum attachment = GL_COLOR_ATTACHMENT0;
  if (gpu_is_depth(texture->format)) {
    attachment = texture->format->kind == GPU_FORMAT_DEPTH_STENCIL
                     ? GL_DEPTH_STENCIL_ATTACHMENT
                     : GL_DEPTH_ATTACHMENT;
  }
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, 0, 0);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                         GL_TEXTURE_2D, 0, 0);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, attachment, GL_TEXTURE_2D,
                         texture->texture, (GLint)mip_level);
}

void SDL_CopyGPUTextureToTexture(SDL_GPUCopyPass* copy_pass,
                                 const SDL_GPUTextureLocation* source,
                                 const SDL_GPUTextureLocation* destination,
                                 Uint32 w, Uint32 h, [[maybe_unused]] Uint32 d,
                                 [[maybe_unused]] bool cycle) {
  if (!copy_pass || !source || !source->texture || !destination ||
      !destination->texture) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  SDL_GPUDevice* device = copy_pass->command_buffer->device;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, device->read_framebuffer);
  gpu_attach_read(source->texture, source->mip_level);
  gpu_bind_work_texture(device, destination->texture);
  glCopyTexSubImage2D(GL_TEXTURE_2D, (GLint)destination->mip_level,
                      (GLint)destination->x, (GLint)destination->y,
                      (GLint)source->x, (GLint)source->y, (GLsizei)w,
                      (GLsizei)h);
  gpu_bind_work_texture(device, nullptr);
}

void SDL_CopyGPUBufferToBuffer(SDL_GPUCopyPass* copy_pass,
                               const SDL_GPUBufferLocation* source,
                               const SDL_GPUBufferLocation* destination,
                               Uint32 size, [[maybe_unused]] bool cycle) {
  if (!copy_pass || !source || !source->buffer || !destination ||
      !destination->buffer) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  if (!gpu_fits(source->offset, size, source->buffer->size) ||
      !gpu_fits(destination->offset, size, destination->buffer->size)) {
    return;
  }
  glBindBuffer(GL_COPY_READ_BUFFER, source->buffer->buffer);
  glBindBuffer(GL_COPY_WRITE_BUFFER, destination->buffer->buffer);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                      (GLintptr)source->offset, (GLintptr)destination->offset,
                      (GLsizeiptr)size);
}

void SDL_DownloadFromGPUTexture(SDL_GPUCopyPass* copy_pass,
                                const SDL_GPUTextureRegion* source,
                                const SDL_GPUTextureTransferInfo* destination) {
  if (!copy_pass || !source || !source->texture || !destination ||
      !destination->transfer_buffer) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  const SDL_GPUTexture* t   = source->texture;
  SDL_GPUTransferBuffer* to = destination->transfer_buffer;
  if (!gpu_fits(destination->offset,
                gpu_transfer_size(t, destination->pixels_per_row, source->w,
                                  source->h),
                to->size)) {
    return;
  }
  SDL_GPUDevice* device = copy_pass->command_buffer->device;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, device->read_framebuffer);
  gpu_attach_read(t, source->mip_level);
  glPixelStorei(GL_PACK_ROW_LENGTH, (GLint)destination->pixels_per_row);
  glReadPixels((GLint)source->x, (GLint)source->y, (GLsizei)source->w,
               (GLsizei)source->h, t->format->format, t->format->type,
               to->data + destination->offset);
  glPixelStorei(GL_PACK_ROW_LENGTH, 0);
}

// clang-format off
EM_JS(void, gpu_read_buffer,
      (GLenum target, GLintptr offset, void* data, GLsizeiptr size), {
  GLctx.getBufferSubData(target, offset, HEAPU8, data, size);
})
// clang-format on

void SDL_DownloadFromGPUBuffer(
    SDL_GPUCopyPass* copy_pass, const SDL_GPUBufferRegion* source,
    const SDL_GPUTransferBufferLocation* destination) {
  if (!copy_pass || !source || !source->buffer || !destination ||
      !destination->transfer_buffer) {
    gpu_invalid(!copy_pass ? "copy_pass" : "source or destination");
    return;
  }
  const SDL_GPUBuffer* from = source->buffer;
  SDL_GPUTransferBuffer* to = destination->transfer_buffer;
  if (!gpu_fits(source->offset, source->size, from->size) ||
      !gpu_fits(destination->offset, source->size, to->size)) {
    return;
  }
  glBindBuffer(from->target, from->buffer);
  gpu_read_buffer(from->target, (GLintptr)source->offset,
                  to->data + destination->offset, (GLsizeiptr)source->size);
}

void SDL_EndGPUCopyPass([[maybe_unused]] SDL_GPUCopyPass* copy_pass) {}

void SDL_GenerateMipmapsForGPUTexture(SDL_GPUCommandBuffer* command_buffer,
                                      SDL_GPUTexture* texture) {
  if (!command_buffer || !texture) {
    gpu_invalid(!command_buffer ? "command_buffer" : "texture");
    return;
  }
  gpu_bind_work_texture(command_buffer->device, texture);
  glGenerateMipmap(GL_TEXTURE_2D);
  gpu_bind_work_texture(command_buffer->device, nullptr);
}

void SDL_BlitGPUTexture(SDL_GPUCommandBuffer* command_buffer,
                        const SDL_GPUBlitInfo* info) {
  if (!command_buffer || !info || !info->source.texture ||
      !info->destination.texture) {
    gpu_invalid(!command_buffer ? "command_buffer" : "info");
    return;
  }
  SDL_GPUDevice* device        = command_buffer->device;
  const SDL_GPUBlitRegion* src = &info->source;
  const SDL_GPUBlitRegion* dst = &info->destination;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, device->read_framebuffer);
  gpu_attach_read(src->texture, src->mip_level);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, device->draw_framebuffer);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, dst->texture->texture,
                         (GLint)dst->mip_level);
  const GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
  glDrawBuffers(1, buffers);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  if (info->load_op == SDL_GPU_LOADOP_CLEAR) {
    const GLfloat color[] = {info->clear_color.r, info->clear_color.g,
                             info->clear_color.b, info->clear_color.a};
    glClearBufferfv(GL_COLOR, 0, color);
  }
  bool flip_x  = (info->flip_mode & SDL_FLIP_HORIZONTAL) != 0;
  bool flip_y  = (info->flip_mode & SDL_FLIP_VERTICAL) != 0;
  GLint left   = (GLint)dst->x;
  GLint top    = (GLint)dst->y;
  GLint right  = (GLint)(dst->x + dst->w);
  GLint bottom = (GLint)(dst->y + dst->h);
  GLint x0     = flip_x ? right : left;
  GLint x1     = flip_x ? left : right;
  GLint y0     = flip_y ? bottom : top;
  GLint y1     = flip_y ? top : bottom;
  glBlitFramebuffer(
      (GLint)src->x, (GLint)src->y, (GLint)(src->x + src->w),
      (GLint)(src->y + src->h), x0, y0, x1, y1, GL_COLOR_BUFFER_BIT,
      info->filter == SDL_GPU_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
}

// ---------------------------------------------------------------------------
// Swapchain

/** Returns the canvas selector of a window, or nullptr. */
static const char* gpu_window_canvas(SDL_Window* window) {
  return SDL_GetStringProperty(SDL_GetWindowProperties(window),
                               SDL_PROP_WINDOW_EMSCRIPTEN_CANVAS_ID_STRING,
                               nullptr);
}

bool SDL_WindowSupportsGPUSwapchainComposition(
    SDL_GPUDevice* device, SDL_Window* window,
    SDL_GPUSwapchainComposition swapchain_composition) {
  if (!device || !window) {
    return gpu_invalid(!device ? "device" : "window");
  }
  return swapchain_composition == SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
}

bool SDL_WindowSupportsGPUPresentMode(SDL_GPUDevice* device, SDL_Window* window,
                                      SDL_GPUPresentMode present_mode) {
  if (!device || !window) {
    return gpu_invalid(!device ? "device" : "window");
  }
  // The browser shows each frame at the refresh rate.
  return present_mode == SDL_GPU_PRESENTMODE_VSYNC;
}

bool SDL_ClaimWindowForGPUDevice(SDL_GPUDevice* device, SDL_Window* window) {
  if (!device || !window) {
    return gpu_invalid(!device ? "device" : "window");
  }
  if (device->window) {
    return SDL_SetError("WebGL 2 GPU: the device has a window already");
  }
  const char* canvas = gpu_window_canvas(window);
  if (!canvas || SDL_strcmp(canvas, device->canvas) != 0) {
    return SDL_SetError("WebGL 2 GPU: the window must use the canvas of the "
                        "device, %s",
                        device->canvas);
  }
  device->window = window;
  return true;
}

void SDL_ReleaseWindowFromGPUDevice(SDL_GPUDevice* device, SDL_Window* window) {
  if (!device || !window || device->window != window) {
    return;
  }
  if (device->swapchain) {
    gpu_free_texture(device, device->swapchain);
    device->swapchain = nullptr;
  }
  device->window = nullptr;
}

bool SDL_SetGPUSwapchainParameters(
    SDL_GPUDevice* device, SDL_Window* window,
    SDL_GPUSwapchainComposition swapchain_composition,
    SDL_GPUPresentMode present_mode) {
  if (!SDL_WindowSupportsGPUSwapchainComposition(device, window,
                                                 swapchain_composition) ||
      !SDL_WindowSupportsGPUPresentMode(device, window, present_mode)) {
    return SDL_SetError("WebGL 2 GPU: only SDR composition with VSYNC");
  }
  return true;
}

bool SDL_SetGPUAllowedFramesInFlight(SDL_GPUDevice* device,
                                     Uint32 allowed_frames_in_flight) {
  if (!device || allowed_frames_in_flight < 1 || allowed_frames_in_flight > 3) {
    return gpu_invalid(!device ? "device" : "allowed_frames_in_flight");
  }
  return true;
}

SDL_GPUTextureFormat SDL_GetGPUSwapchainTextureFormat(SDL_GPUDevice* device,
                                                      SDL_Window* window) {
  if (!device || !window) {
    gpu_invalid(!device ? "device" : "window");
    return SDL_GPU_TEXTUREFORMAT_INVALID;
  }
  return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
}

bool SDL_AcquireGPUSwapchainTexture(SDL_GPUCommandBuffer* command_buffer,
                                    SDL_Window* window,
                                    SDL_GPUTexture** swapchain_texture,
                                    Uint32* swapchain_texture_width,
                                    Uint32* swapchain_texture_height) {
  if (!command_buffer || !window || !swapchain_texture) {
    return gpu_invalid(!command_buffer ? "command_buffer"
                       : !window       ? "window"
                                       : "swapchain_texture");
  }
  SDL_GPUDevice* device = command_buffer->device;
  *swapchain_texture    = nullptr;
  if (device->window != window) {
    return SDL_SetError("WebGL 2 GPU: the window is not claimed");
  }
  int w = 0;
  int h = 0;
  emscripten_webgl_get_drawing_buffer_size(device->context, &w, &h);
  if (w <= 0 || h <= 0) {
    return true;
  }
  SDL_GPUTexture* swapchain = device->swapchain;
  if (!swapchain || swapchain->width != (Uint32)w ||
      swapchain->height != (Uint32)h) {
    if (swapchain) {
      gpu_free_texture(device, swapchain);
      device->swapchain = nullptr;
    }
    swapchain = gpu_create_texture(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                   SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
                                       SDL_GPU_TEXTUREUSAGE_SAMPLER,
                                   (Uint32)w, (Uint32)h, 1);
    if (!swapchain) {
      return false;
    }
    swapchain->swapchain = true;
    device->swapchain    = swapchain;
  }
  command_buffer->present = swapchain;
  *swapchain_texture      = swapchain;
  if (swapchain_texture_width) {
    *swapchain_texture_width = swapchain->width;
  }
  if (swapchain_texture_height) {
    *swapchain_texture_height = swapchain->height;
  }
  return true;
}

bool SDL_WaitForGPUSwapchain(SDL_GPUDevice* device, SDL_Window* window) {
  if (!device || !window) {
    return gpu_invalid(!device ? "device" : "window");
  }
  return true;
}

bool SDL_WaitAndAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer* command_buffer,
                                           SDL_Window* window,
                                           SDL_GPUTexture** swapchain_texture,
                                           Uint32* swapchain_texture_width,
                                           Uint32* swapchain_texture_height) {
  return SDL_AcquireGPUSwapchainTexture(
      command_buffer, window, swapchain_texture, swapchain_texture_width,
      swapchain_texture_height);
}

/** Copies the swapchain texture to the canvas, flipped. */
static void gpu_present(SDL_GPUDevice* device, const SDL_GPUTexture* texture) {
  GLint w = (GLint)texture->width;
  GLint h = (GLint)texture->height;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, device->read_framebuffer);
  gpu_attach_read(texture, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  const GLenum back[] = {GL_BACK};
  glDrawBuffers(1, back);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer* command_buffer) {
  if (!command_buffer) {
    return gpu_invalid("command_buffer");
  }
  SDL_GPUDevice* device = command_buffer->device;
  if (command_buffer->present && command_buffer->present == device->swapchain) {
    gpu_present(device, command_buffer->present);
  }
  SDL_free(command_buffer);
  return true;
}

SDL_GPUFence* SDL_SubmitGPUCommandBufferAndAcquireFence(
    SDL_GPUCommandBuffer* command_buffer) {
  if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
    return nullptr;
  }
  return SDL_calloc(1, sizeof(SDL_GPUFence));
}

bool SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer* command_buffer) {
  if (!command_buffer) {
    return gpu_invalid("command_buffer");
  }
  if (command_buffer->present) {
    return SDL_SetError("Cannot cancel command buffer after a swapchain "
                        "texture has been acquired!");
  }
  // The commands ran already. Cancel can only free the command buffer.
  SDL_free(command_buffer);
  return true;
}

// The commands run when they are recorded, and WebGL reads pixels and
// buffers synchronously, so the GPU is always done.

bool SDL_WaitForGPUIdle(SDL_GPUDevice* device) {
  return device ? true : gpu_invalid("device");
}

bool SDL_WaitForGPUFences(SDL_GPUDevice* device, [[maybe_unused]] bool wait_all,
                          SDL_GPUFence* const* fences, Uint32 num_fences) {
  if (!device || (!fences && num_fences > 0)) {
    return gpu_invalid(!device ? "device" : "fences");
  }
  return true;
}

bool SDL_QueryGPUFence(SDL_GPUDevice* device, SDL_GPUFence* fence) {
  if (!device || !fence) {
    return gpu_invalid(!device ? "device" : "fence");
  }
  return true;
}

void SDL_ReleaseGPUFence(SDL_GPUDevice* device, SDL_GPUFence* fence) {
  if (device) {
    SDL_free(fence);
  }
}
