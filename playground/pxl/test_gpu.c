// Tests the parts of the GPU layer (gpu.c) that the tests of pxl do not use:
// the drivers, buffer downloads, copies, blits, uniform blocks without data
// and the missing features. It runs in the browser: rake
// pxl:playground:test.

#define PXL_IMPLEMENTATION
#include "pxl.h"
#include "tests/pxl/test_shaders.h"

#define PICO_UNIT_IMPLEMENTATION
#include <SDL3/SDL.h>
#include <stdio.h>

#include "pico_unit.h"

static constexpr Uint32 size = 4;

static SDL_GPUDevice* device;

/** Creates a 4 x 4 RGBA8 texture that is a color target. */
static SDL_GPUTexture* create_target() {
  return SDL_CreateGPUTexture(
      device, &(SDL_GPUTextureCreateInfo){
                  .type                 = SDL_GPU_TEXTURETYPE_2D,
                  .format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                  .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER |
                                          SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,
                  .width                = size,
                  .height               = size,
                  .layer_count_or_depth = 1,
                  .num_levels           = 1,
              });
}

/** Clears a texture to a color. */
static bool clear(SDL_GPUTexture* texture, SDL_FColor color) {
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPURenderPass* pass =
      SDL_BeginGPURenderPass(cmd,
                             &(SDL_GPUColorTargetInfo){
                                 .texture     = texture,
                                 .clear_color = color,
                                 .load_op     = SDL_GPU_LOADOP_CLEAR,
                                 .store_op    = SDL_GPU_STOREOP_STORE,
                             },
                             1, nullptr);
  SDL_EndGPURenderPass(pass);
  return SDL_SubmitGPUCommandBuffer(cmd) && pass;
}

/** Reads the 4 x 4 pixels of a texture: rows top to bottom. */
static bool read_pixels(SDL_GPUTexture* texture, Uint8 pixels[static 1]) {
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                  .size  = size * size * 4,
              });
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);
  SDL_DownloadFromGPUTexture(
      copy,
      &(SDL_GPUTextureRegion){.texture = texture, .w = size, .h = size, .d = 1},
      &(SDL_GPUTextureTransferInfo){.transfer_buffer = transfer});
  SDL_EndGPUCopyPass(copy);
  bool ok = SDL_SubmitGPUCommandBuffer(cmd);
  SDL_memcpy(pixels, SDL_MapGPUTransferBuffer(device, transfer, false),
             size * size * 4);
  SDL_UnmapGPUTransferBuffer(device, transfer);
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  return ok;
}

static bool is_color(const Uint8* pixel, Uint8 r, Uint8 g, Uint8 b) {
  return pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == 255;
}

TEST_CASE(test_drivers) {
  REQUIRE(SDL_GetNumGPUDrivers() == 1);
  REQUIRE(SDL_strcmp(SDL_GetGPUDriver(0), "webgl2") == 0);
  REQUIRE(SDL_strcmp(SDL_GetGPUDeviceDriver(device), "webgl2") == 0);
  REQUIRE(SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV, nullptr));
  REQUIRE(!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_DXIL, nullptr));
  REQUIRE(!SDL_GPUSupportsShaderFormats(SDL_GPU_SHADERFORMAT_SPIRV, "vulkan"));
  REQUIRE(SDL_GetGPUShaderFormats(device) == SDL_GPU_SHADERFORMAT_SPIRV);
  REQUIRE(SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),
                                SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr));
  return true;
}

TEST_CASE(test_formats) {
  REQUIRE(SDL_GPUTextureSupportsFormat(
      device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
      SDL_GPU_TEXTUREUSAGE_COLOR_TARGET));
  REQUIRE(!SDL_GPUTextureSupportsFormat(
      device, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM, SDL_GPU_TEXTURETYPE_2D,
      SDL_GPU_TEXTUREUSAGE_SAMPLER));
  REQUIRE(!SDL_GPUTextureSupportsFormat(
      device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_3D,
      SDL_GPU_TEXTUREUSAGE_SAMPLER));
  REQUIRE(SDL_GPUTextureFormatTexelBlockSize(
              SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT) == 8);
  REQUIRE(SDL_CalculateGPUTextureFormatSize(
              SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, 8, 8, 1) == 32);
  REQUIRE(SDL_CalculateGPUTextureFormatSize(
              SDL_GPU_TEXTUREFORMAT_ASTC_10x5_UNORM_SRGB, 20, 10, 1) == 64);
  return true;
}

TEST_CASE(test_buffer_round_trip) {
  const Uint32 data[]   = {1, 2, 3, 0xdeadbeef};
  SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(
      device, &(SDL_GPUBufferCreateInfo){.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                         .size  = sizeof data});
  SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size  = sizeof data,
              });
  SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
                  .size  = sizeof data,
              });
  REQUIRE(buffer && upload && download);
  SDL_memcpy(SDL_MapGPUTransferBuffer(device, upload, false), data,
             sizeof data);
  SDL_UnmapGPUTransferBuffer(device, upload);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);
  SDL_UploadToGPUBuffer(
      copy, &(SDL_GPUTransferBufferLocation){.transfer_buffer = upload},
      &(SDL_GPUBufferRegion){.buffer = buffer, .size = sizeof data}, false);
  SDL_DownloadFromGPUBuffer(
      copy, &(SDL_GPUBufferRegion){.buffer = buffer, .size = sizeof data},
      &(SDL_GPUTransferBufferLocation){.transfer_buffer = download});
  SDL_EndGPUCopyPass(copy);
  REQUIRE(SDL_SubmitGPUCommandBuffer(cmd));
  const Uint32* read_back = SDL_MapGPUTransferBuffer(device, download, false);
  REQUIRE(SDL_memcmp(read_back, data, sizeof data) == 0);
  SDL_UnmapGPUTransferBuffer(device, download);
  SDL_ReleaseGPUTransferBuffer(device, upload);
  SDL_ReleaseGPUTransferBuffer(device, download);
  SDL_ReleaseGPUBuffer(device, buffer);
  return true;
}

TEST_CASE(test_copy_and_blit) {
  SDL_GPUTexture* source = create_target();
  SDL_GPUTexture* copied = create_target();
  SDL_GPUTexture* blit   = create_target();
  REQUIRE(source && copied && blit);
  REQUIRE(clear(source, (SDL_FColor){1.0f, 0.0f, 0.0f, 1.0f}));
  REQUIRE(clear(copied, (SDL_FColor){0.0f, 0.0f, 1.0f, 1.0f}));
  REQUIRE(clear(blit, (SDL_FColor){0.0f, 0.0f, 1.0f, 1.0f}));

  // Copy the top-left 2 x 2 texels, and blit the top half, flipped, into the
  // bottom half.
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);
  SDL_CopyGPUTextureToTexture(
      copy, &(SDL_GPUTextureLocation){.texture = source},
      &(SDL_GPUTextureLocation){.texture = copied}, 2, 2, 1, false);
  SDL_EndGPUCopyPass(copy);
  SDL_BlitGPUTexture(
      cmd, &(SDL_GPUBlitInfo){
               .source      = {.texture = source, .w = size, .h = 2},
               .destination = {.texture = blit, .y = 2, .w = size, .h = 2},
               .flip_mode   = SDL_FLIP_VERTICAL,
           });
  REQUIRE(SDL_SubmitGPUCommandBuffer(cmd));

  Uint8 pixels[size * size * 4];
  REQUIRE(read_pixels(copied, pixels));
  REQUIRE(is_color(&pixels[0], 255, 0, 0));
  REQUIRE(is_color(&pixels[((1 * size) + 1) * 4], 255, 0, 0));
  REQUIRE(is_color(&pixels[((1 * size) + 2) * 4], 0, 0, 255));
  REQUIRE(is_color(&pixels[((2 * size) + 0) * 4], 0, 0, 255));
  REQUIRE(read_pixels(blit, pixels));
  REQUIRE(is_color(&pixels[((1 * size) + 0) * 4], 0, 0, 255));
  REQUIRE(is_color(&pixels[((2 * size) + 0) * 4], 255, 0, 0));
  REQUIRE(is_color(&pixels[((3 * size) + 3) * 4], 255, 0, 0));
  SDL_ReleaseGPUTexture(device, source);
  SDL_ReleaseGPUTexture(device, copied);
  SDL_ReleaseGPUTexture(device, blit);
  return true;
}

TEST_CASE(test_missing_features) {
  REQUIRE(!SDL_CreateGPUComputePipeline(device,
                                        &(SDL_GPUComputePipelineCreateInfo){}));
  REQUIRE(SDL_strstr(SDL_GetError(), "compute") != nullptr);
  REQUIRE(!SDL_CreateGPUBuffer(
      device, &(SDL_GPUBufferCreateInfo){
                  .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                  .size  = 16,
              }));
  REQUIRE(
      !SDL_CreateGPUShader(device, &(SDL_GPUShaderCreateInfo){
                                       .code      = (const Uint8*)"not SPIR-V",
                                       .code_size = 8,
                                       .format    = SDL_GPU_SHADERFORMAT_SPIRV,
                                       .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
                                   }));
  REQUIRE(SDL_strstr(SDL_GetError(), "SPIRV-Cross") != nullptr);
  return true;
}

// A shader with a uniform block draws when no data was pushed: WebGL needs a
// buffer as large as the block.
TEST_CASE(test_uniforms_without_push) {
  pxl_context* pxl =
      pxl_create(&(pxl_desc){.device = device, .width = 4, .height = 4});
  REQUIRE(pxl != nullptr);
  const pxl_color green = pxl_rgb(0x00ff00);
  pxl_shader* shader    = pxl_create_shader(pxl, &slots_frag);
  pxl_texture* extra    = pxl_create_texture(
      pxl, &(pxl_texture_desc){.width = 1, .height = 1, .pixels = &green});
  pxl_color pixels[16] = {};
  REQUIRE(shader && extra);
  pxl_begin_frame(pxl);
  pxl_clear(pxl, pxl_black);
  pxl_set_shader(pxl, shader);
  pxl_set_shader_texture(pxl, 1, extra);
  pxl_draw_rect(pxl, 0, 0, 4, 4, pxl_white);
  REQUIRE(pxl_end_frame(pxl));
  REQUIRE(pxl_read_texture(pxl, pxl_get_canvas(pxl), pixels));
  REQUIRE(pixels[5].r == 0 && pixels[5].g == 255 && pixels[5].b == 0);
  pxl_destroy_texture(pxl, extra);
  pxl_destroy_shader(pxl, shader);
  pxl_destroy(pxl);
  return true;
}

/** Returns true if the SDL error contains `text`. */
static bool error_has(const char* text) {
  return SDL_strstr(SDL_GetError(), text) != nullptr;
}

// What WebGL cannot do sets the SDL error, instead of failing silently.
TEST_CASE(test_transfer_errors) {
  SDL_GPUTexture* depth = SDL_CreateGPUTexture(
      device, &(SDL_GPUTextureCreateInfo){
                  .format = SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                  .usage  = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                  .width  = size,
                  .height = size,
                  .layer_count_or_depth = 1,
                  .num_levels           = 1,
              });
  SDL_GPUTexture* floats = SDL_CreateGPUTexture(
      device, &(SDL_GPUTextureCreateInfo){
                  .format               = SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
                  .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
                  .width                = size,
                  .height               = size,
                  .layer_count_or_depth = 1,
                  .num_levels           = 1,
              });
  SDL_GPUBuffer* vertices = SDL_CreateGPUBuffer(
      device, &(SDL_GPUBufferCreateInfo){.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                         .size  = 16});
  SDL_GPUBuffer* indices = SDL_CreateGPUBuffer(
      device, &(SDL_GPUBufferCreateInfo){.usage = SDL_GPU_BUFFERUSAGE_INDEX,
                                         .size  = 16});
  SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(
      device, &(SDL_GPUTransferBufferCreateInfo){
                  .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                  .size  = 256,
              });
  REQUIRE(depth && floats && vertices && indices && transfer);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
  SDL_GPUCopyPass* copy     = SDL_BeginGPUCopyPass(cmd);

  SDL_ClearError();
  SDL_DownloadFromGPUTexture(
      copy, &(SDL_GPUTextureRegion){.texture = depth, .w = 1, .h = 1, .d = 1},
      &(SDL_GPUTextureTransferInfo){.transfer_buffer = transfer});
  REQUIRE(error_has("depth"));

  SDL_ClearError();
  SDL_UploadToGPUTexture(
      copy,
      &(SDL_GPUTextureTransferInfo){.transfer_buffer = transfer, .offset = 2},
      &(SDL_GPUTextureRegion){.texture = floats, .w = 1, .h = 1, .d = 1},
      false);
  REQUIRE(error_has("multiple of 4"));

  SDL_ClearError();
  SDL_CopyGPUBufferToBuffer(copy, &(SDL_GPUBufferLocation){.buffer = vertices},
                            &(SDL_GPUBufferLocation){.buffer = indices}, 16,
                            false);
  REQUIRE(error_has("index usage"));

  SDL_EndGPUCopyPass(copy);
  REQUIRE(SDL_SubmitGPUCommandBuffer(cmd));
  SDL_ReleaseGPUTransferBuffer(device, transfer);
  SDL_ReleaseGPUBuffer(device, indices);
  SDL_ReleaseGPUBuffer(device, vertices);
  SDL_ReleaseGPUTexture(device, floats);
  SDL_ReleaseGPUTexture(device, depth);
  return true;
}

static void suite_gpu() {
  RUN_TEST_CASE(test_drivers);
  RUN_TEST_CASE(test_formats);
  RUN_TEST_CASE(test_buffer_round_trip);
  RUN_TEST_CASE(test_copy_and_blit);
  RUN_TEST_CASE(test_missing_features);
  RUN_TEST_CASE(test_uniforms_without_push);
  RUN_TEST_CASE(test_transfer_errors);
}

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  device = SDL_CreateGPUDevice(pxl_shader_formats, false, nullptr);
  if (!device) {
    printf("SDL_CreateGPUDevice: %s\n", SDL_GetError());
    return 1;
  }
  RUN_TEST_SUITE(suite_gpu);
  SDL_DestroyGPUDevice(device);
  SDL_Quit();
  pu_print_stats();
  return pu_test_failed() ? 1 : 0;
}
