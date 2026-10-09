# libs

Single-header C23 libraries for game development. Copy one header into your
project. Define `<NAME>_IMPLEMENTATION` in one C file before you include it.
All libraries use the [zlib license](LICENSE).

| Library                  | Description                                             |
| ------------------------ | ------------------------------------------------------- |
| [aseprite.h](aseprite.h) | Reads and renders Aseprite files (`.ase`, `.aseprite`). |
| [pxl.h](pxl.h)           | Draws pixel-art games with SDL3 GPU.                    |
| [pxl_anim.h](pxl_anim.h) | Plays sprite animations, for example from Aseprite.     |

## aseprite.h

aseprite.h reads all the chunks of a file. `aseprite_render_frame()` blends
the layers of a frame into RGBA pixels with the same result as Aseprite: all
19 blend modes, opacity, z-index, groups, tilemaps, indexed and grayscale
sprites. `rake compare` checks the pixels against Aseprite. The blend
functions follow Aseprite's `src/doc/blend_funcs.cpp`, which has the MIT
license; aseprite.h has its notice.

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
| Metal (macOS) | Tested, also in CI |
| Vulkan (Linux) | Tested in CI on lavapipe with the validation layers |
| Direct3D 12 (Windows) | Tested in CI with MSYS2 clang, on a runner without a GPU. DXC validates the shaders. |

pxl.h needs a C23 compiler. It is tested with clang 19 to 23 and gcc 15 and
16, and with SDL 3.2 and 3.4. gcc 14 and MSVC do not support enough of C23.

## pxl_anim.h

pxl_anim plays frame animations. `pxl_anim_load_aseprite()` makes a sheet
from an Aseprite file: the tags become clips and named layers become parts,
for example a shadow, a body and effects that you draw separately. It
trims each image and packs it into one atlas. Images that are the same, such
as a shadow that does not move, share one area of the atlas.

```c
pxl_anim_sheet sheet;
pxl_anim_load_aseprite(&sheet, &sprite, &(pxl_anim_aseprite_desc){
    .parts = (const char*[]){"shadow", "body", "fx"}, .part_count = 3});
pxl_texture* atlas = pxl_create_texture(pxl, &(pxl_texture_desc){
    .width = (int)sheet.atlas_width, .height = (int)sheet.atlas_height,
    .pixels = sheet.atlas});

pxl_anim_player orc = {};
pxl_anim_play(&orc, &sheet, pxl_anim_find_clip(&sheet, "Walk"));
pxl_anim_update(&orc, seconds);
for (uint32_t part = 0; part < sheet.part_count; part++) {
    pxl_anim_draw(pxl, atlas, &orc, part, &(pxl_sprite){
        .x = x, .y = y, .origin = {50, 60}, .flip_x = left});
}
```

- Forward, reverse and ping-pong clips with a number of passes, or no end,
  like Aseprite plays them. Change the passes of a clip when you play it.
- Events for new frames, loops and the end of a clip. `pxl_anim_step()`
  stops at each frame, so that a slow update does not skip a hit frame.
- Speed and pause. A long time does not take a long loop.
- The core uses only the C standard library. Include `aseprite.h` and
  `pxl.h` before `pxl_anim.h` to get the import and the draw functions.

See [`examples/pxl_anim/character.c`](examples/pxl_anim/character.c). It plays
the Orc of the free Tiny RPG Character Asset Pack, or any other Aseprite
file.

## Development

Install Ruby, clang 19 or gcc 15 or newer, clang-format, clang-tidy,
pkg-config and SDL3. Then run `rake check`. `rake test` uses the compiler in
`CC`, or `cc`.

CI runs the checks on each pull request: on Linux with gcc and clang, on
macOS with Apple clang and on Windows with MSYS2 clang. The format check
uses clang-format 23. The Shaders job checks that the generated files are
up to date.

`rake compare` compares `aseprite_render_frame()` with Aseprite. It needs
Aseprite.

`rake pxl:generate` compiles the shaders of pxl.h and embeds them with its
font. It needs glslc, spirv-cross and spirv-val. If DXC is not in PATH, it
downloads the DXC release for Linux. It runs it directly on Linux x86_64, and
in Docker on macOS.
