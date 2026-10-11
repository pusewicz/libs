// Tests the parts of the GPU layer (gpu.c) that the tests of pxl do not use:
// the drivers, buffer downloads, copies, blits and the missing features. It
// runs in the browser: rake pxl:playground:test.

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

static void suite_gpu() {
  RUN_TEST_CASE(test_drivers);
  RUN_TEST_CASE(test_formats);
  RUN_TEST_CASE(test_buffer_round_trip);
  RUN_TEST_CASE(test_copy_and_blit);
  RUN_TEST_CASE(test_missing_features);
}

int main() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    printf("SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
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
