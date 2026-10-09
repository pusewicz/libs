# A Linux system for the tests: gcc, clang, SDL3, and Vulkan on the CPU
# (lavapipe) with the validation layers. `rake linux` uses it.
FROM ubuntu:26.04

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang clang-tidy gcc libc6-dev libclang-rt-dev pkg-config rake ruby \
        libsdl3-dev libvulkan1 mesa-vulkan-drivers vulkan-validationlayers \
    && rm -rf /var/lib/apt/lists/*

# Run without a display.
ENV SDL_VIDEO_DRIVER=offscreen
WORKDIR /work
