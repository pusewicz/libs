# libs

Single-header C23 libraries for game development. Copy one header into your
project. Define `<NAME>_IMPLEMENTATION` in one C file before you include it.
All libraries use the [zlib license](LICENSE).

| Library                    | Description                                   |
| -------------------------- | --------------------------------------------- |
| [aseprite.h](aseprite.h)   | Reads Aseprite files (`.ase`, `.aseprite`).   |
| [pxl.h](pxl.h)             | Draws pixel-art games with SDL3 GPU.          |

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

See [`examples/pxl/`](examples/pxl/) and the comments in `pxl.h`.

| Backend | Status |
| --- | --- |
| Metal (macOS) | Tested |
| Vulkan (Linux) | Tested on lavapipe with the validation layers |
| Direct3D 12 (Windows) | Builds with MinGW gcc and clang. DXC validates the shaders. Not run yet. |

pxl.h needs a C23 compiler. It is tested with clang 19 to 23 and gcc 15 and
16, and with SDL 3.2 and 3.4. gcc 14 and MSVC do not support enough of C23.

## Development

Install Ruby, clang and gcc 15 or newer, pkg-config and SDL3. Then run
`rake check`.

`rake pxl:generate` compiles the shaders of pxl.h and embeds them with its
font. It needs glslc, spirv-cross and spirv-val, and DXC or Docker.
