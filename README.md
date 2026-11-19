# libs

Single-header C23 libraries for game development. Copy one header into your
project. Define `<NAME>_IMPLEMENTATION` in one C file before you include it.
All libraries use the [zlib license](LICENSE).

The libraries need a C23 compiler. They are tested with clang 19 to 23 and
gcc 15 and 16. gcc 14 and MSVC do not support enough of C23.

| Library                    | Description                                   |
| -------------------------- | --------------------------------------------- |
| [aseprite.h](aseprite.h)   | Reads Aseprite files (`.ase`, `.aseprite`).   |
| [pxl.h](pxl.h)             | Draws pixel-art games with SDL3 GPU.          |

## aseprite.h

aseprite.h reads Aseprite files into an `aseprite_sprite`: the frames,
layers, cels, palettes, tags, slices and tilesets. It uses only the C
standard library.

```c
aseprite_sprite sprite;
aseprite_result result = aseprite_load_file("hero.aseprite", &sprite);
if (result != ASEPRITE_OK) {
  fprintf(stderr, "%s\n", aseprite_result_string(result));
  return 1;
}

for (uint32_t i = 0; i < sprite.frame_count; i++) {
  const aseprite_frame* frame = &sprite.frames[i];
  const aseprite_cel* cel     = aseprite_frame_cel(frame, 0); // Layer 0.
  if (cel && cel->image.pixels) {
    // Draw cel->image at cel->x, cel->y for frame->duration milliseconds.
  }
}

aseprite_free(&sprite);
```

- Loading from memory with `aseprite_load_memory()` or from a file with
  `aseprite_load_file()`. On error, the sprite is empty and
  `aseprite_result_string()` tells why.
- Chunks: layer, cel, cel extra, palette, old palette, tags, slice, tileset,
  user data, color profile and external files. It skips other chunks.
- A built-in zlib inflate for the cel images, tilemaps and tileset images.
- Layers with their group tree, blend mode and opacity. Linked cels share
  the data of their source cel. Each frame has the index of its palette,
  because the palette can change from frame to frame.
- User data (text, color and properties) of the sprite, layers, cels, tags,
  slices, tilesets and tiles.
- Helpers: `aseprite_frame_cel()` finds the cel of a layer in a frame.
  `aseprite_tile_id()`, `aseprite_tile_x_flip()`, `aseprite_tile_y_flip()`
  and `aseprite_tile_diagonal_flip()` read a tile of a tilemap.
  `aseprite_bytes_per_pixel()` gives the size of a pixel.
  `aseprite_fixed_to_double()` converts 16.16 fixed point numbers.
- Memory: the library owns all the memory of a sprite. `aseprite_free()`
  releases it. The sprite does not use the file data after the load.
- Options: `ASEPRITE_MALLOC` and `ASEPRITE_FREE` replace the allocator.
  Define both. `ASEPRITE_ASSERT` replaces `assert`. `ASEPRITE_NO_STDIO`
  removes `aseprite_load_file()`.

aseprite.h does not blend layers. The pixels keep the color depth of the
sprite: palette indices, grayscale with alpha, or RGBA. It does not open
other files: for an external tileset, it gives the file ID and the tileset
ID.

See [`examples/aseprite/`](examples/aseprite/) and the comments in
`aseprite.h`. `dump.c` prints the content of Aseprite files.

The tests load good and bad fixture files and make each allocation fail in
turn. `rake fuzz[SECONDS]` fuzzes the loader with libFuzzer. To check your
own files, run `rake sweep[DIR:DIR...]`.

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
- PNG and BMP loading, and texture read-back for screenshots. SDL older
  than 3.4 loads BMP only.

See [`examples/pxl/`](examples/pxl/) and the comments in `pxl.h`.

| Backend | Status |
| --- | --- |
| Metal (macOS) | Tested, also in CI |
| Vulkan (Linux) | Tested in CI on lavapipe with the validation layers |
| Direct3D 12 (Windows) | Tested in CI with MSYS2 clang, on a runner without a GPU. DXC validates the shaders. |

pxl.h is tested with SDL 3.2 and 3.4.

## Development

Run `bin/setup` to install the tools and packages that CI uses. It supports
Debian or Ubuntu, macOS and MSYS2. Or install Ruby, clang 19 or gcc 15 or
newer, clang-format, clang-tidy, pkg-config and SDL3 yourself. Then run
`rake check`. `rake test` uses the compiler in
`CC`, or `cc`.

CI runs the checks on each pull request: on Linux with gcc and clang, on
macOS with Apple clang and on Windows with MSYS2 clang. The format check
uses clang-format 23. The Shaders job checks that the generated files are
up to date.

`rake pxl:generate` compiles the shaders of pxl.h and embeds them with its
font. It needs glslc, spirv-cross and spirv-val. If DXC is not in PATH, it
downloads the DXC release for Linux. It runs it directly on Linux x86_64, and
in Docker on macOS.
