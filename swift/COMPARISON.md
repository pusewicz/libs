# pxl.h in Swift

This is a port of `pxl.h` 0.1.0 to Swift 6.4. It exists to compare the two
languages for the same library: API, data types, performance, size and
legibility. The port does the same work: the 29 test cases of
`tests/pxl/test_pxl.c` are ported one to one and pass. Four new tests check
layouts, NaN coordinates, the GPU and the vertex memory.

Environment: Apple Silicon (arm64), macOS 27.0, Metal, Swift 6.4-RELEASE,
Apple clang 21.0.0, SDL 3.4.18. Linux and Windows are not tested. The
`CSDL3` module map uses pkg-config, so it should work with `libsdl3-dev`.

## Summary

|                                   | C (`pxl.h`)              | Swift                    |
| --------------------------------- | ------------------------ | ------------------------ |
| Handwritten source                | 3,413 lines (2,554 code) | 2,774 lines (1,995 code) |
| Tests                             | 925 lines                | 701 lines                |
| CPU time to record a frame        | 1×                       | 0.88× to 1.11×           |
| CPU time of `end_frame`           | 1×                       | 1×                       |
| Machine code of the library       | 23.8 KB                  | 81.0 KB                  |
| Stripped `hello` program          | 73 KB                    | 192 KB, and the runtime  |
| Clean optimized build of library  | 0.23 s                   | 3.7 s                    |

The Swift version is shorter, safer and easier to call. It records sprites
12% faster than C, shapes as fast and text 11% slower. It gets there only
after 11 measured changes to the hot path. A direct port that keeps the draw
state in a class is 5× to 6× slower. See [Performance](#performance).

## Run it

```sh
rake swift:test       # Build and run the Swift tests
rake swift:lint       # swift-format lint --strict
rake "swift:bench[7]" # Run the C and the Swift benchmark in turns
rake swift:size       # Source lines, machine code, binaries, build time
rake swift:generate   # Write Generated.swift and TestShaders.swift from pxl.h
swift run --package-path swift hello
```

`rake check` runs none of these: CI has no Swift toolchain.

## Layout

```
swift/
  Package.swift              swift-tools-version 6.4, Swift 6 language mode,
                             warnings are errors, ExistentialAny,
                             InternalImportsByDefault, MemberImportVisibility
  Sources/CSDL3/             The SDL3 module map, from pkg-config
  Sources/Pxl/               The library
    Types.swift              Color, Vec2, Rect, PixelRect, Transform, enums,
                             Vertex, Stats, PxlError
    Device.swift             Device: owns the SDL GPU device
    Texture.swift            Texture: create, load, update, read
    Shader.swift             ShaderCode, ShaderDescriptor, Shader, pipelines
    Font.swift               Font and the built-in font
    Context.swift            Context: canvas, viewport, window
    State.swift, Drawing.swift   The public draw API. It forwards to Recorder.
    Recorder.swift           Draw state, batching, shapes, text
    Batch.swift              Vertices, quads, commands, samplers, the
                             GrowableBuffer of the geometry, GPU buffers
    Frame.swift              begin and end of a frame, render passes
    Generated.swift          Shaders and font, from swift/tools/generate.rb
  Sources/hello/             Port of examples/pxl/hello.c
  Sources/pxl-bench/         Swift benchmark
  Benchmarks/bench.c         C benchmark with the same workloads
  Tests/PxlTests/            Swift Testing port of tests/pxl/test_pxl.c
  tools/                     generate.rb, bench.rb, size.rb
```

The generated data comes from `pxl.h` and `tests/pxl/test_shaders.h`, so the
port needs no shader compiler. SPIR-V and DXIL are `[UInt8]` literals. MSL is
text, so it is a raw string literal that you can read.

## API

The C API takes a context pointer and a descriptor struct. The Swift API uses
methods, properties, argument labels and default values. Every descriptor
struct of the C API goes away except `ShaderDescriptor`, which generated code
fills.

| C                                                        | Swift                                                     |
| -------------------------------------------------------- | --------------------------------------------------------- |
| `SDL_CreateGPUDevice(pxl_shader_formats, debug, nullptr)` | `try Device(debug:driver:)`                              |
| `pxl_create(&(pxl_desc){...})`, `pxl_destroy`             | `try Context(device:window:resolution:scaleMode:letterbox:)`; the last reference goes |
| `pxl_begin_frame`, `pxl_end_frame`                        | `beginFrame()`, `try endFrame()`                          |
| `pxl_end_frame_into(ctx, cmd, tex, fmt, w, h)`            | `try endFrame(into:target:format:width:height:)`          |
| `pxl_get_canvas`, `pxl_set_resolution`, `pxl_get_viewport`, `pxl_get_stats`, scale mode | `canvas`, `resolution`, `viewport`, `stats`, `scaleMode` |
| `pxl_window_to_canvas(ctx, x, y)`                         | `windowToCanvas(_:)`                                      |
| `pxl_create_texture(ctx, &(pxl_texture_desc){...})`       | `try Texture(device:width:height:pixels:premultiplied:renderTarget:filter:wrap:)` |
| `pxl_create_texture_from_surface`, `pxl_load_texture`     | `try Texture(device:surface:)`, `try Texture(device:path:)` |
| `pxl_destroy_texture`                                     | The last reference goes. A draw keeps its texture until the frame ends. |
| `pxl_update_texture`, `pxl_read_texture`                  | `try texture.update(x:y:width:height:pixels:)`, `try texture.read() -> [Color]` |
| `pxl_texture_width`, `_height`, `_handle`, `pxl_set_texture_filter`, `_wrap` | `width`, `height`, `handle`, `filter`, `wrap` |
| `pxl_push`, `pxl_pop`                                     | `push()`, `pop()`, `withSavedState { ... }`               |
| `pxl_translate`, `pxl_rotate`, `pxl_scale`, `pxl_set_transform` | `translate(x:y:)`, `rotate(by:)`, `scale(x:y:)`, `transform` |
| `pxl_transform_point(t, p)`, `pxl_transform_inverse(t)`   | `t * p`, `t.inverse`                                      |
| `pxl_set_target`, `pxl_get_width`, `pxl_get_height`       | `target`, `width`, `height`                               |
| `pxl_set_blend`, `pxl_set_clip`, `pxl_reset_clip`, `pxl_set_snap` | `blend`, `clip = PixelRect(...)`, `clip = nil`, `snap` |
| `pxl_clear`                                               | `clear(_:)`                                               |
| `pxl_draw_sprite(ctx, tex, &(pxl_sprite){...})`           | `drawSprite(_:at:source:origin:scale:rotation:flipX:flipY:color:overlay:)` |
| `pxl_draw_nine_slice(ctx, tex, &(pxl_nine_slice){...})`   | `drawNineSlice(_:in:left:top:right:bottom:source:color:)` |
| `pxl_draw_pixel`, `_line`, `_rect`, `_rect_lines`         | `drawPixel(at:color:)`, `drawLine(from:to:color:)`, `drawRect(_:color:)`, `drawRectLines(_:color:)` |
| `pxl_draw_circle`, `_circle_lines`, `_triangle`, `_triangles` | `drawCircle(at:radius:color:)`, `drawCircleLines(...)`, `drawTriangle(_:_:_:color:)`, `drawTriangles(_:texture:)` |
| `pxl_create_font`, `pxl_destroy_font`, `pxl_set_font`     | `try Font(texture:glyphWidth:glyphHeight:...)`, none, `font` |
| `pxl_draw_text(ctx, x, y, color, "score: %d", s)`         | `drawText("score: \(s)", at:color:)`                      |
| `pxl_measure_text(ctx, "%d", n)`                          | `measureText("\(n)")`                                     |
| `pxl_create_shader`, `pxl_destroy_shader`, `pxl_set_shader` | `try Shader(device:_:)`, none, `shader`                 |
| `pxl_set_shader_texture`, `pxl_set_uniforms(ctx, &u, sizeof u)` | `setShaderTexture(_:slot:)`, `setUniforms(SIMD4<Float>(...))` |

The draw loop of `hello`:

```c
// C
pxl_begin_frame(a->pxl);
pxl_clear(a->pxl, pxl_rgb(0x1d2b53));
pxl_draw_sprite(a->pxl, a->smiley,
                &(pxl_sprite){
                    .x      = 160,
                    .y      = 90 + (10 * sinf(t * 3)),
                    .origin = {4, 4},
                    .scale  = {4, 4},
                });
pxl_draw_text(a->pxl, 4, 4, pxl_white, "Hello, pixels!");
pxl_end_frame(a->pxl);
```

```swift
// Swift
pxl.beginFrame()
pxl.clear(Color(rgb: 0x1d2b53))
pxl.drawSprite(smiley, at: [160, 90 + 10 * SDL_sinf(t * 3)], origin: [4, 4], scale: [4, 4])
pxl.drawText("Hello, pixels!", at: [4, 4], color: .white)
try pxl.endFrame()
```

The text art of `hello` becomes a multi-line string literal and a
`[Character: Color]` palette, in place of the 128-entry `art_palette` array.
The SDL part stays C: `SDL_Init`, `SDL_CreateWindow`, `SDL_PollEvent`. Swift
does not import macros that use `SDL_UINT64_C`, so `SDL_WINDOW_RESIZABLE`
needs a constant in `Sources/CSDL3/shim.h`.

## Data types

- **Ownership.** `Device`, `Texture` and `Shader` are classes. Each `deinit`
  releases its SDL object. Textures, shaders and contexts hold their `Device`,
  so C's rule "destroy the textures and shaders before the device" cannot be
  broken. `Context` is a class too: one object for one window. `Font` is an
  immutable final class, as `pxl_font*` is a pointer.
- **Release during a frame.** C keeps a `next_garbage` list and frees it after
  the frame. In Swift, a recorded draw holds its texture, so ARC frees the
  texture when the frame ends. The `releaseDuringFrame` test proves this with
  a `weak let` reference that is still set before `endFrame()` and nil after.
  Each shader keeps its own pipeline cache. Its `deinit` releases the cache, so
  `pxl__release_pipelines` goes too.
- **Defaults.** C uses "zero means the default": `scale` 0 means 1, `color`
  zero means white, a zero `src` means the whole texture, `count` 0 means all
  cells. Swift uses real default values and `Optional`: `scale: Vec2 = .one`,
  `color: Color = .white`, `source: Rect? = nil`, `count: Int? = nil`.
- **Geometry.** `Vec2` is `SIMD2<Float>`, so `[4, 4]` is a vector and
  arithmetic works. `Transform` has a `*` operator for transforms and points.
  The GPU vertex keeps four `Float` fields: with `SIMD2<Float>` fields, its
  alignment becomes 8 and its stride grows from 28 to 32 bytes. A test checks
  the stride.
- **Fixed arrays.** Bindings and shader slots are `[4 of Texture?]`
  (`InlineArray`), with no heap memory. The count must be a literal, so it
  cannot be `Shader.maxTextures`.
- **Names.** `PXL_BLEND_NONE` is `.replace`: `.none` would be confused with
  `Optional.none`. `PXL_WRAP_REPEAT` is `` `repeat` `` in the declaration and
  `.repeat` at the call.
- **Integers.** `Int` replaces `int`, `size_t` and `int64_t` in the API and
  in the algorithms. Conversions to `UInt32` and `Int32` happen only at the
  SDL calls, with `UInt32(exactly:)` where a value can be too large.
- **Floats.** `Int(Float.nan)` stops a Swift program. C's
  `(int)fminf(fmaxf(v, -limit), limit)` gives the lower limit for NaN. The
  port keeps this with `Float.minimum` and `Float.maximum`, which follow
  `fminf` and `fmaxf`. `Swift.min` and `Swift.max` return NaN. The
  `coordinatesSurviveNaN` test checks this.
- **Text.** String interpolation replaces printf formats. Swift strings are
  always valid Unicode, so the UTF-8 decoder of C goes. The port reads
  `unicodeScalars`, not `Character`s, because `"\r\n"` is one `Character`.
- **Errors.** Functions that fail throw `PxlError` (typed throws), not
  `nullptr` with `SDL_GetError()`. Draws do not throw. As in C, they mark the
  frame, and `endFrame()` throws the first error.
- **Uniforms.** `setUniforms(_:)` takes any `BitwiseCopyable` value, for
  example `SIMD4<Float>`, in place of a pointer and a size.

## Performance

Both benchmarks draw the same workloads into an offscreen 320 × 180 canvas:

- `sprites`: 20,000 sprites, a quarter of them turned, all scaled, half
  flipped.
- `shapes`: 2,000 each of circles, lines and rectangle edges. This tests the
  CPU algorithms that split shapes into quads.
- `text`: 500 lines of `"frame %d score %d"` and `"frame \(f) score \(s)"`.

Each run takes 30 warm-up frames, then 300 timed frames (`PXL_BENCH_FRAMES`
changes this). It times the draws (`begin_frame` to the last draw) and
`end_frame` apart. It waits for the GPU outside of the timed part. Both print
the draw calls, vertices and indices of the last frame, and `tools/bench.rb`
stops if they differ. They do not differ.

Record time per frame in ms:

| Step                                                                | sprites      | shapes       | text         |
| ------------------------------------------------------------------- | ------------ | ------------ | ------------ |
| C, `-O2`                                                            | 0.44         | 0.81         | 0.17         |
| 1. Swift, draw state in the `Context` class                         | 2.20 (5.0×)  | 4.94 (6.1×)  | 0.86 (5.0×)  |
| 2. Draw state in a `Recorder` struct                                | 1.62         | 2.68         | 0.63         |
| 3. Join commands in place, read texture settings once, `OutputSpan`  | 0.85         | 1.85         | 0.32         |
| 4. Compare draws by a `DrawKey` without references                  | 0.70 (1.59×) | 1.21 (1.47×) | 0.26 (1.52×) |
| 5. Own memory for the geometry, a `Quad` struct, no SIMD literals   | 0.65         | 1.02         | 0.21         |
| 6. Helpers on small values, not on the `Recorder`                   | 0.60         | 0.99         | 0.21         |
| 7. No run-time exclusivity check on two properties                  | 0.44         | 0.98         | 0.21         |
| 8. `@inline(always)` on small helpers                               | 0.40         | 1.00         | 0.20         |
| 9. The last command outside the array                               | 0.40         | 0.97         | 0.20         |
| 10. Wrapping arithmetic in bounded loops, `Font` as a class         | 0.39         | 0.94         | 0.19         |
| 11. An inlined plain-color path, `if let` in place of `??` (final)  | 0.39 (0.88×) | 0.83 (1.00×) | 0.19 (1.11×) |

`end_frame` is the same in C and Swift in all steps: 0.044, 0.10 and 0.026
ms. The final row is the median of 7 runs that take turns with C
(`rake swift:bench[7]`). Rows 5 to 10 are the median of 5 such runs, and
rows 1 to 4 one run each. C stays between 0.44 and 0.45 ms for sprites in
all runs.

At step 4, compiler modes could not close the gap. Even with all checks off,
Swift was 1.3× C. The gap was in the shape of the code, not in the checks:

| Swift 6.4 release build, step 4        | sprites | shapes | text  |
| -------------------------------------- | ------- | ------ | ----- |
| default                                | 1.60×   | 1.45×  | 1.53× |
| `-cross-module-optimization`           | 1.60×   | 1.42×  | 1.59× |
| `-enforce-exclusivity=unchecked`       | 1.47×   | 1.43×  | 1.56× |
| `-Ounchecked`                          | 1.44×   | 1.31×  | 1.26× |
| all three                              | 1.36×   | 1.30×  | 1.27× |

What each step changes, from the Time Profiler and the disassembly:

1. **A direct port** keeps `state` and `batch` in the `Context` class. Swift
   checks exclusive access to a stored property of a class at run time, at
   each access (`swift_beginAccess`, 16% of the time). It cannot prove that
   the class property stays the same during a call, so it copies
   `DrawState` and the last `Command` on each draw, and retains and releases
   their references. Retains and releases alone are 31% of the time.
2. **A struct for the hot state.** `Context` keeps a `Recorder` struct, and
   the public methods forward to its `mutating` methods. In them, `self` is
   `inout`, so Swift checks exclusive access at compile time, and the
   `DrawState` copies go.
3. **In-place changes.** `Array`'s subscript getter copies the element.
   `batch.commands[last].join(...)` is a `mutating` method, so it goes
   through the `_modify` accessor and changes the command in place. Reading
   through `Span` did not help. A draw reads `texture.filter` and
   `texture.wrap` once, as one `Sampling` value.
4. **No references in the hot comparison.** Draws compare a `DrawKey` of
   `ObjectIdentifier`s, blend, clip and uniforms. A command takes strong
   references only when it starts.
5. **Own memory for the geometry.** `GrowableBuffer` is a `~Copyable` struct
   with a `deinit` that holds the vertices and indices, as C's `realloc`
   arrays do. `append(n)` checks the capacity once and returns the memory for
   the draw to write. `Array.append` checks uniqueness and capacity on each
   call. The corners of a quad were `[4 of Vec2]`, and copies of an
   `InlineArray` went through `memmove`; now they are a `Quad` struct. The
   vertex params were a `SIMD4<UInt8>` array literal, which went through an
   unspecialized protocol witness; now they are four named bytes.
6. **No copies of `self`.** A non-mutating method of a large struct gets
   `self` by value: each sprite copied the 384-byte `Recorder` with `memcpy`
   to call `snapped(_:)`. The helpers moved to the small values that they use:
   `Quad.snapped(if:)`, `Transform.vertex(_:_:snapped:)`, `Circle.init` and
   `Font.measure(_:)`.
7. **Two run-time checks off.** `Context.recorder` and `Texture.sampling` are
   `@exclusivity(unchecked)`, an attribute that the standard library uses but
   that the language guide does not document. Each sprite paid two `swift_beginAccess` calls,
   17% of its time, and this step alone took sprites from 1.34× to 1.00×. The
   checks cannot find a fault here: neither type is `Sendable`, and no access
   calls code that could access the property again. The doc comments say so.
8. **`@inline(always)`** (SE-0496) on 20 small helpers: the vertex and quad
   initializers, the buffer `append`, the transforms and the rounding. C gets
   the same from its single translation unit. On large functions it costs
   more than it saves: inlining `beginDraw` into every draw made text 5%
   slower, and inlining the glyph helpers made it 6% slower.
9. **The last command outside the array.** Each draw joins the last command.
   `batch.commands[last]` goes through `Array`'s `_modify`, with a uniqueness
   check and a bounds check. `Batch.lastCommand` is a `Command?` that the draw
   changes in place.
10. **Arithmetic without overflow checks** in the inner loops of lines and
    circles, and for the six indices of a quad: `&+`, `&-` and `&*`. A
    comment at each place names the bound: coordinates are clamped to ±2²⁴,
    a radius to 2¹⁴, and each draw checks that its indices fit in 32 bits.
    `Font` became a final class, because `state.font ?? defaultFont` copied a
    struct with two references for each text.
11. **A plain-color path.** Shapes make one quad for each run of pixels.
    `beginPlainDraw` is inlined into them, and textured draws keep a call.
    `x ?? y` returns a new strong reference, but `if let` borrows. With
    `if let`, the retain and release of the target and the shader on each
    quad went away. `DrawKey ==` is written by hand, so that it is inlined.

After step 11, no Swift runtime function is in the top 10 of any workload.
Text keeps the heap memory of string interpolation, about 8% of its profile.

Sprites are faster in Swift than in C. For each draw, `pxl.h` builds a whole
command of about 150 bytes on the stack to compare it with the last one. The
port compares a `DrawKey` of about 80 bytes. That is a likely reason, not a
measured one.

### Storage for the geometry

Each draw writes its vertices and indices into the storage of the frame. With
all else as in step 11, only the storage changed (median of 5 runs that take
turns with C, record time):

| Storage                                      | sprites | shapes | text  |
| -------------------------------------------- | ------- | ------ | ----- |
| `GrowableBuffer`: raw memory (final)         | 0.87×   | 1.00×  | 1.10× |
| `InlineArray` for each quad, then copied     | 0.86×   | 1.05×  | 1.16× |
| `Array`, `append` for each value             | 1.08×   | 1.11×  | 1.24× |
| `ContiguousArray`, `append` for each value   | 1.09×   | 1.20×  | 1.32× |
| `ContiguousArray`, `OutputSpan`              | 1.10×   | 1.28×  | 1.41× |
| `UniqueArray` (Swift 6.4), `OutputSpan`      | 1.04×   | 1.16×  | 1.27× |
| `UniqueArray` (Swift 6.4), `append`          | 1.39×   | 1.61×  | 1.53× |

- `Array` and `ContiguousArray` check uniqueness and capacity on each
  `append`. Their elements here are structs, so `Array` needs no bridging
  check either, and the two are the same within noise.
- `OutputSpan` checks capacity once for each quad, but it keeps its own count
  and checks it on each value. It was slower than `append` for `Array` and
  `ContiguousArray`, and faster for `UniqueArray`.
- `UniqueArray` is noncopyable, so it needs no uniqueness check. In Swift
  6.4 it was slower than `Array` all the same.
- `InlineArray` is fixed in size, so it cannot hold the frame. As a staging
  area for one quad it costs 5% on shapes and text. Copies of an `InlineArray`
  value went through `memmove` in step 5.

Other Swift 6.4 features do not apply. `@specialized` controls generic
specialization, and the hot path is not generic. The `borrow` and `mutate`
accessors avoid copies through an accessor, and the hot path has no such
copies left. They cannot remove the retain of `state.target ?? canvas`,
because `??` makes a new value.

### Fairness

- Both versions do the same work for each draw: the same checks, one batch
  comparison and one quad at a time. A Swift-only shortcut would be faster,
  for example one batch check for a whole string of text, but C could do the
  same, so the port does not use it.
- The C benchmark compiles the implementation in the same translation unit,
  so the compiler can inline every `pxl_*` call. The Swift benchmark calls
  across a module boundary, with SwiftPM's default cross-module optimization.
  `@inline(always)` inside the module stands in for C's translation unit.
- `text` keeps the heap memory of string interpolation. A string of more than
  15 bytes is on the heap. That is the cost of the Swift idiom, and the
  benchmark keeps it.
- No unsafe build flags: no `-Ounchecked` and no
  `-enforce-exclusivity=unchecked`. Swift still checks array bounds and most
  arithmetic. Three things are unchecked, each in one place and documented:
  the two `@exclusivity(unchecked)` properties, the raw memory of
  `GrowableBuffer` (with a unit test), and the wrapping arithmetic of the
  bounded loops.
- The GPU work is the same, so a game that is GPU bound sees no difference.

## Size and legibility

| Size                                           | C       | Swift   |
| ---------------------------------------------- | ------- | ------- |
| Handwritten library, all lines                 | 3,413   | 2,774   |
| Handwritten library, code lines (scc)          | 2,554   | 1,995   |
| Generated data                                 | 1,253   | 1,142   |
| Tests                                          | 925     | 701     |
| `hello` (with `art.h` for C)                   | 137     | 62      |
| Benchmark                                      | 174     | 130     |
| Library machine code (`__text`)                | 23.8 KB | 81.0 KB |
| Library, all loaded sections                   | 39.2 KB | 121 KB  |
| Stripped `hello`                               | 73 KB   | 192 KB  |
| Clean optimized build of the library           | 0.23 s  | 3.7 s   |

The Swift library has 22% fewer code lines than C. The speed cost 142 code
lines over step 4, mostly `GrowableBuffer`, `Quad`, the plain-color path and
comments on the unchecked code. 126 code lines are the public forwarders in
`State.swift` and `Drawing.swift`, which do the job of the declaration
section of `pxl.h`. The machine code is 3.4× larger: Swift specializes and
inlines standard library code into the module, and adds type metadata. Steps
5 to 11 made it smaller, from 89.5 KB, because the `Array` and `OutputSpan`
code went. The Swift runtime is part of macOS, so it is not in the 192 KB. On
Linux and Windows a program also ships the runtime libraries.

### Default values replace descriptor rules

```c
// C: pxl_draw_sprite
pxl_rect src = pxl__source(texture, sprite->src);
float sx     = sprite->scale.x != 0.0f ? sprite->scale.x : 1.0f;
float sy     = sprite->scale.y != 0.0f ? sprite->scale.y : 1.0f;
if (sprite->flip_x) {
  sx = -sx;
}
if (sprite->flip_y) {
  sy = -sy;
}
...
const pxl_transform t = pxl__multiply(&ctx->state.transform, &local);
pxl_vec2 p[]          = {
    pxl__apply(&t, 0.0f, 0.0f),
    pxl__apply(&t, src.w, 0.0f),
    pxl__apply(&t, src.w, src.h),
    pxl__apply(&t, 0.0f, src.h),
};
pxl__snap_quad(ctx, p);
...
pxl_color color = pxl__is_zero(sprite->color) ? pxl_white : sprite->color;
pxl__quad(ctx, texture, p, uv, color, sprite->overlay);
```

```swift
// Swift: Recorder.drawSprite
let src = source ?? Rect(x: 0, y: 0, width: Float(texture.width), height: Float(texture.height))
let sx = flipX ? -scale.x : scale.x
let sy = flipY ? -scale.y : scale.y
...
let t = state.transform * local
let quad = Quad(
  topLeft: t * Vec2(0, 0), topRight: t * Vec2(src.width, 0),
  bottomRight: t * Vec2(src.width, src.height), bottomLeft: t * Vec2(0, src.height)
).snapped(if: state.snap)
addQuad(texture, quad, uv: texture.uv(of: src), color: color, overlay: overlay)
```

### `defer` replaces cleanup on each path

```c
// C: pxl_create_texture_from_surface
SDL_Surface* rgba = surface;
if (surface->format != SDL_PIXELFORMAT_RGBA32) {
  rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
  if (!rgba) {
    return nullptr;
  }
}
pxl_texture* texture = nullptr;
if (SDL_LockSurface(rgba)) {
  texture = pxl__create_texture(ctx, &(pxl_texture_desc){...}, (size_t)rgba->pitch);
  SDL_UnlockSurface(rgba);
}
if (rgba != surface) {
  SDL_DestroySurface(rgba);
}
return texture;
```

```swift
// Swift: Texture(device:surface:)
let rgba =
  surface.pointee.format == SDL_PIXELFORMAT_RGBA32
  ? surface : SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32)
guard let rgba else { throw .sdl() }
defer {
  if rgba != surface { SDL_DestroySurface(rgba) }
}
guard SDL_LockSurface(rgba) else { throw .sdl() }
defer { SDL_UnlockSurface(rgba) }
let image = rgba.pointee
try self.init(device: device, width: Int(image.w), height: Int(image.h), ...)
try upload(x: 0, y: 0, width: width, height: height, pixels: image.pixels, ...)
```

If the upload throws after `self.init`, `deinit` releases the GPU texture.
C needs `pxl__free_texture` on that path.

### The fast path is not the obvious path

```swift
// Step 1: the natural port, 5× slower than C. Each access to state or batch
// is a run-time check, and the last command is copied.
public final class Context {
  var state = DrawState()
  var batch = Batch()
  public func drawSprite(...) { ... batch.commands[last].canJoin(..., state: state) ... }
}

// Step 11: 0.88× to 1.11× C.
public final class Context {
  @exclusivity(unchecked) var recorder: Recorder  // a ~Copyable struct
  public func drawSprite(...) { recorder.drawSprite(...) }
}
struct Batch: ~Copyable {
  var vertices = GrowableBuffer<BatchVertex>(...)  // raw memory, one check per draw
  var lastCommand: Command?                        // joined in place
}
extension Recorder {
  private var drawKey: DrawKey {
    // `state.target ?? canvas` would retain the target.
    let target =
      if let target = state.target { ObjectIdentifier(target) } else { ObjectIdentifier(canvas) }
    ...
  }
}
```

C has none of these traps: a field access is a load, and a copy is a copy
you write. In Swift, the expensive parts are not visible in the source: a
run-time check on a class property, a copy of `self` to call a method, a
retain from `??`, a uniqueness check in an array access. The profiler and the
disassembly found each of them.

The C algorithms port to Swift almost line for line. `pxl__line`,
`pxl__circle_span` and the nine-slice code keep their structure, with `Int`
for `int64_t`, `abs` for `llabs` and `[4 of Float]` for `float[4]`.

C interop has a cost. An SDL info struct needs `var info = T()` and one line
for each field, because Swift has no designated initializers for C structs.
A pointer inside an info struct needs nested `withUnsafePointer` closures (see
`Shader.pipeline`).

## Differences and limits

- **Out of memory stops the program.** Swift cannot recover from a failed
  allocation, so `PXL_MALLOC`, `PXL_REALLOC` and `PXL_FREE` have no port, and
  `tests/pxl/test_options.c` has no Swift test. `PXL_ASSERT` maps to `assert`
  and `precondition`.
- **Some errors cannot happen.** A context without a device does not compile,
  so the first case of `test_create_errors` has no port.
- **Lifetimes end at the last use.** ARC can release a local after its last
  use, before the end of its scope. `hello` puts the context in a
  `run(window:)` function, so that the context releases the window before
  `SDL_DestroyWindow`.
- **The push stack has no limit.** C has 32 entries.
- **The first error of a frame wins.** In C, the last one does.
- **SDL 3.4 or newer.** `Texture(device:path:)` uses `SDL_LoadSurface`. C
  falls back to BMP on older SDL, with a preprocessor check that Swift cannot
  do.
- **SDL needs the main thread on macOS.** Swift Testing runs tests on other
  threads, so the GPU tests run on the main actor. `Context` is not
  `Sendable`, so Swift 6 rejects code that uses one context from two threads.
  C only documents this rule.
- **Floats behave the same.** Clang may fuse `a * x + c * y` into one FMA, and
  Swift does not. `Float.pi` is one ulp smaller than `SDL_PI_F`. Neither
  changed a pixel in the tests.
- **One file or a package.** `pxl.h` is one file to copy. The Swift port is a
  SwiftPM package with a system library target for SDL3.

## Verdict

A Swift port of pxl is shorter, harder to misuse and easier to read at the
call site. Ownership, errors, defaults and text are better in Swift. The
algorithms are the same in both.

Swift can match C here: the final port records sprites 12% faster than C,
shapes as fast and text 11% slower, with the same work for each draw and the
same public API. But the direct port is 5× to 6× slower, and the fix took 11
measured steps. Most of the gap was not Swift's checks: with all of them off,
step 4 was still 1.3× C. The gap was in how Swift passes and copies values:
run-time checks on class properties, copies of a large `self` to call a
method, retains from `??`, and uniqueness checks in array accesses. Each fix
is small, but each needs the profiler or the disassembly to find. C needs
none of this.

The speed cost 142 code lines, two documented `@exclusivity(unchecked)`
properties, `@inline(always)` on small helpers, raw memory for the geometry
behind a small `~Copyable` type, and wrapping arithmetic in two loops. The
library still has 22% fewer code lines than C. It builds 16× slower, and its
machine code is 3.4× larger.

A `~Copyable` `Context` struct would remove the forwarders and the
`@exclusivity(unchecked)` on `recorder`. It was not tried: step 6 showed that
a non-mutating method of a large struct copies `self`, and a `~Copyable`
`Context` would bring that copy to every public getter, across the module
boundary. It would also change the contract for users: `var pxl`, `inout` in
closures, `withSavedState { pxl in ... }`, and no capture in escaping
closures.
