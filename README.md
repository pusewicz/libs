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

Try it in the [playground](https://pusewicz.github.io/libs/pxl/): edit the
code on the left, and the browser compiles it and runs it on the right.

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

### Playground

The [playground](https://pusewicz.github.io/libs/pxl/) compiles C with pxl.h
in the browser, as you type, and runs the program next to the code. A program
that does not compile shows its errors, and the last good program keeps
running. GitHub Pages hosts it: the Pages workflow tests it and deploys it
from `main`.

- Clang and LLD in WebAssembly ([YoWASP](https://yowasp.org/)) compile the
  code into a side module. The first visit downloads about 26 MB.
- An Emscripten build of SDL and pxl.h loads the side module and runs it.
- SDL has no GPU backend for the web, so
  [`playground/pxl/gpu.c`](playground/pxl/gpu.c) implements the SDL GPU API
  on WebGL 2. It translates SPIR-V shaders to GLSL ES with SPIRV-Cross. The
  tests of pxl pass on it.

Limits: programs use the SDL main callbacks (`SDL_MAIN_USE_CALLBACKS`). The
playground has the implementation of pxl.h already, so `PXL_IMPLEMENTATION`
and the options of pxl.h have no effect. There are no files to load. The GPU
layer has no compute, storage buffers or textures, indirect draws, textures
that are not 2D, or multisampling.

| Backend | Status |
| --- | --- |
| Metal (macOS) | Tested, also in CI |
| Vulkan (Linux) | Tested in CI on lavapipe with the validation layers |
| Direct3D 12 (Windows) | Tested in CI with MSYS2 clang, on a runner without a GPU. DXC validates the shaders. |

pxl.h needs a C23 compiler. It is tested with clang 19 to 23 and gcc 15 and
16, and with SDL 3.2 and 3.4. gcc 14 and MSVC do not support enough of C23.

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

`rake pxl:playground` builds the playground into `build/playground/site`. It
needs Emscripten 6.0.12 (`emcc` in PATH), Node.js 22 and git. Then
`rake pxl:playground:serve` serves it on http://localhost:8000/pxl/, and
`rake pxl:playground:test` runs the tests of pxl and of the playground in
headless Chromium.
