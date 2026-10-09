# libs

Single-header C23 libraries for game development. Copy one header into your
project. Define `<NAME>_IMPLEMENTATION` in one C file before you include it.
All libraries use the [zlib license](LICENSE).

| Library                    | Description                                   |
| -------------------------- | --------------------------------------------- |
| [aseprite.h](aseprite.h)   | Reads Aseprite files (`.ase`, `.aseprite`).   |
| [pxl.h](pxl.h)             | Draws pixel-art games with SDL3 GPU.          |

## aseprite.h

aseprite reads a file into an `aseprite_sprite`. A load uses malloc and free
unless you choose the memory.

```c
aseprite_sprite sprite;
aseprite_result result = aseprite_load_file("hero.aseprite", nullptr, &sprite);
```

To load without any allocation, give a block. The sprite reports the bytes it
used, so you can size the block for the next load. A block that is too small
gives `ASEPRITE_ERROR_NO_MEMORY`.

```c
alignas(max_align_t) static unsigned char block[1 << 20];

aseprite_sprite sprite;
aseprite_options options = {.memory = block, .memory_size = sizeof block};
if (aseprite_load_file("hero.aseprite", &options, &sprite) == ASEPRITE_OK) {
  // sprite.memory_used is the least block size for this file.
  aseprite_free(&sprite); // The block is free again.
}
```

To use your own allocator, for example an arena, set `options.allocator`:
`alloc(user, size, alignment)` and `release(user, pointer, size)`.

## pxl.h

pxl draws into a small canvas, for example 320 x 180, and scales it to the
window without blur. It needs SDL 3.2 or newer.

```c
pxl_context* pxl = pxl_create(&(pxl_desc){
    .device = device, .window = window, .width = 320, .height = 180});

pxl_begin_frame(pxl);
pxl_clear(pxl, pxl_rgb(0x1d2b53));
pxl_draw_sprite(pxl, hero, &(pxl_sprite){.x = 160, .y = 90, .flip_x = true});
pxl_draw_circle(pxl, 40, 40, 10, pxl_rgb(0xff004d));
pxl_draw_text(pxl, 4, 4, pxl_white, "score: %d", score);
pxl_end_frame(pxl);
```

- Scale modes: integer, fit and stretch, with letterbox. Fit and stretch use
  sharp filtering: square texels with smooth edges.
- Sprites: source areas, origin, scale, rotation, flip, tint and a flash
  overlay. Positions snap to whole pixels.
- Exact pixel shapes: pixels, Bresenham lines, rectangles, circles, triangles.
- Text with a built-in 5 x 7 font, or with your bitmap fonts.
- Render targets, blend modes, clip areas, a transform stack, nine-slice.
- Batching: shapes and sprites share draw calls.
- Custom fragment shaders with uniforms and extra textures. Compile them with
  `tools/pxl/shaders.rb`.
- PNG and BMP loading, and texture read-back for screenshots.
- Fixed memory: the limits in `pxl_desc` set the size of a context, and pxl
  never grows it. Give your own block to `pxl_create_in()`, sized with
  `pxl_memory_size()`, and pxl does not allocate. A draw over a limit is
  dropped and `pxl_end_frame()` returns false. SDL still allocates the GPU
  objects; `SDL_SetMemoryFunctions()` controls that.

See [`examples/pxl/`](examples/pxl/) and the comments in `pxl.h`.

| Backend | Status |
| --- | --- |
| Metal (macOS) | Tested, also in CI |
| Vulkan (Linux) | Tested in CI on lavapipe with the validation layers |
| Direct3D 12 (Windows) | Tested in CI with MSYS2 clang, on a runner without a GPU. DXC validates the shaders. |

pxl.h needs a C23 compiler. It is tested with clang 19 to 23 and gcc 15 and
16, and with SDL 3.2 and 3.4. gcc 14 and MSVC do not support enough of C23.

## Development

Install Ruby, clang 19 or gcc 15 or newer, clang-format, clang-tidy,
pkg-config and SDL3. Then run `rake check`. `rake test` uses the compiler in
`CC`, or `cc`.

CI runs the checks on each pull request: on Linux with gcc and clang, on
macOS with Apple clang and on Windows with MSYS2 clang. The format check
uses clang-format 23. The Shaders job checks that the generated files are
up to date.

`rake pxl:generate` compiles the shaders of pxl.h and embeds them with its
font. It needs glslc, spirv-cross and spirv-val. If DXC is not in PATH, it
downloads the DXC release for Linux. It runs it directly on Linux x86_64, and
in Docker on macOS.
