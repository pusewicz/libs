# pxl.h performance report

Scope: `pxl.h` 0.1.0 at commit `8e3ab73`. Date: 2026-10-11.

This report finds where pxl spends time, tries 10 changes as working
prototypes, measures each one, and recommends what to adopt. All prototypes
are in `patches/`. Each applies to the current `pxl.h` with `git apply`.

## 1. Summary

pxl is CPU-bound. For a 320x180 canvas the GPU work is small. The cost is in
the CPU work that each draw call does before the GPU sees it:

- Every sprite costs about 460 instructions to record. Only about a quarter
  of that is writing the vertices. The rest is batch bookkeeping that repeats
  for each quad, even when no state changed.
- Lines and circles are expensive because each one is many small quads
  (about 45 per line, 18 per filled circle and 57 per circle outline in the
  benchmark), and each quad pays the full per-draw bookkeeping.
- Each change of texture starts a new GPU draw call. In SDL plus the driver
  that costs about 1 µs of CPU per draw call here, 10x a sprite.
- Each `pxl_update_texture` creates a transfer buffer and submits its own
  command buffer: about 50 µs per call here.
- The first frame that uses a new blend mode or shader compiles a GPU
  pipeline inside `pxl_end_frame`: a 1 to 7 ms stall here.

Gain is total CPU time per frame (record + end of frame), compared with the
current `pxl.h`: the median of 5 runs that alternate between the builds.
This shared VM has about ±10% noise, so treat smaller differences as noise.

| Change | Gain (wall clock) | Risk | Verdict |
|---|---|---|---|
| A. Batch fast path: skip the batch compare when no state changed | 1.02x–1.17x, typically 1.1x | medium: each state setter must close the batch | adopt, with a debug cross-check |
| A2. Lean batch compare: no new invariant | 1.01x–1.09x | low | adopt if A is too fragile |
| B. Static 16-bit quad index buffer | 1.13x–1.43x; triangles 0.92x | low | adopt |
| C. Write vertices into the mapped transfer buffer | 1.24x–2.05x; end of frame 5x shorter | medium: write-combined memory on D3D12 and Metal | measure on real GPUs first |
| D. Deferred texture updates in the frame's command buffer | 11x on texture updates | low | adopt |
| E. 4 texture slots in the default shader | 6.8x when draws alternate textures; 1–9% slower elsewhere | medium: shader change, more GPU work | optional |
| F. Shapes: merge equal rows, reserve all quads of a shape at once | circle outlines 1.56x alone; see section 6 for the set | low | adopt |
| G. Text: reserve a string at once, 32-bit glyph math | 1.21x on text | low | adopt |
| H. Create pipelines at `pxl_create` / `pxl_create_shader` | removes a 1–7 ms first-use stall | low | adopt |
| I. Inline `floorf` (a libm call on baseline x86-64) | 0.88x–1.08x | — | reject |
| J. Compiler flags (`-O3`, `-msse4.1`, `-march=x86-64-v3`) | `-msse4.1`: rects 1.10x–1.23x | none (user choice) | document |

The recommended set (A, B, D, F and G, `patches/best.patch`) gives, with the
same pixels as now:

| sprites | rotated sprites | rects | text | circles | circle outlines | lines | rect outlines | game frame | texture updates | triangles |
|---|---|---|---|---|---|---|---|---|---|---|
| 1.32x | 1.29x | 1.24x | 1.31x | 1.62x | 2.98x | 1.59x | 1.39x | 1.25x | 10.95x | 0.88x |

The output is byte-identical to the current `pxl.h` in all 12 benchmark
scenes, and all tests pass with ASan and UBSan.

## 2. Method

### Machine

- Intel Xeon @ 2.80 GHz, 4 vCPU, shared VM, Ubuntu 24.04.
- clang 20.1.2. gcc 13 and 14 cannot compile `pxl.h`: gcc 14 reports
  `__STDC_VERSION__` as 202000L, not 202311L.
- SDL 3.2.24, built from source (offscreen video, Vulkan GPU).
- GPU: Mesa lavapipe 25.2.8, Vulkan on the CPU. No hardware GPU is available.

Lavapipe does not show real GPU speed. So this report measures the CPU side:
the time in pxl, SDL and the driver while the application thread records and
submits a frame. The GPU work runs after each frame, outside the timed
regions. GPU-side effects (shader cost, upload bandwidth, write-combined
memory) need a check on real hardware. The report says where.

### Harness

`bench/bench.c` creates an offscreen 320x180 context and draws 12 scenes:

| Scene | Content per frame |
|---|---|
| sprites | 20,000 16x16 sprites from one texture |
| sprites_rot | the same, with rotation, scale and tint |
| sprites_2tex | 2,000 sprites that alternate between two textures |
| rects | 20,000 `pxl_draw_rect` |
| text | 500 `pxl_draw_text` calls with 4 format arguments, 40 characters each |
| circles | 1,000 filled circles, radius 1–30 |
| circle_lines | 1,000 circle outlines |
| lines | 2,000 lines between random points |
| rect_lines | 5,000 rectangle outlines |
| triangles | 10,000 small triangles in one `pxl_draw_triangles` call |
| update_texture | 50 `pxl_update_texture` calls (32x32), a sprite after each |
| mixed | a game frame: 240 tiles, 2,000 sprites, health bars, additive lights, 10 lines of text |

Two measurements:

1. **Instruction counts** (callgrind). Deterministic, so small differences
   are real. Used for the record phase (from `pxl_begin_frame` to the last
   draw). Not used for the end of the frame: callgrind counts `memcpy` in a
   way that does not follow the number of bytes.
2. **Wall clock**. Record time and `pxl_end_frame` time (geometry upload,
   command encoding, submit). The median of 40 frames, then the median of 5
   runs that alternate between the builds.

### Correctness checks for each prototype

- The 29 pxl tests, built with the flags of `compile_flags.txt` (`-Werror`,
  `-Wconversion` and the others) and ASan and UBSan. `best.patch` also passes
  `rake test:pxl:test_pxl test:pxl:test_options` in a copy of the repository.
- Pixel comparison: each scene is drawn once more and the canvas is read
  back. All prototypes give byte-identical canvases in all 12 scenes. To
  check that the comparison can fail, I planted a bug that drops one quad per
  draw call: 9 of 12 scenes differed.
- The batch fast path (A) ran with a cross-check that also runs the old path
  for each draw and aborts if they disagree: no disagreement in the tests or
  the scenes.
- The inline floor (I) was compared with `floorf` for all 2^32 float inputs:
  no difference except the sign of zero for -0.0.
- The texture update change (D) has its own test (`bench/update_in_frame.c`):
  two updates around draws in one frame, update then destroy, a partial
  update, and an update-only frame. The results match the current `pxl.h`.

## 3. Baseline: where the time goes

| Scene | Instructions per item (record) | CPU time per frame (record + end) |
|---|---|---|
| sprites | 461 per sprite | 1.81 ms |
| rects | 401 per rect | 1.64 ms |
| text | 386 per character | 1.42 ms |
| circles | 7,125 per circle | 1.51 ms |
| circle_lines | 20,536 per circle | 4.62 ms |
| lines | 16,545 per line | 6.73 ms |
| triangles | 180 per triangle | 0.53 ms |
| sprites_2tex | 519 per sprite, plus ~2,000 per draw call in SDL and the driver | 2.15 ms, of which 1.92 ms end of frame |
| update_texture | 46,504 per update | 2.61 ms |

One sprite (`pxl_draw_sprite`), 461 instructions:

| Part | Instructions | Share |
|---|---|---|
| Sprite math: transform, 4 corners, snap, UVs | ~151 | 33% |
| `pxl__draw_command`: fill a 160-byte command, compare it with the last | ~132 | 29% |
| Write 4 vertices and 6 indices | ~108 | 23% |
| `pxl__reserve`: two array growth checks | ~50 | 11% |
| Benchmark loop | ~20 | 4% |

Then `pxl_end_frame` copies 136 bytes per sprite (4 vertices of 28 bytes and
6 indices of 4 bytes) into the transfer buffer. For 20,000 sprites that is
2.7 MB and about 0.5 ms here.

A line costs about 370 instructions per quad: about 108 for the Bresenham
steps, 96 to write the quad and 163 for batch bookkeeping. For text,
`SDL_vsnprintf` is 11% of the cost.

## 4. The changes

Each section says what pxl does now, the change, why it helps, the result
and the trade-offs. Instruction counts are for the record phase. Wall-clock
gains compare with the current `pxl.h`.

### A. Batch fast path (`patches/fastpath.patch`)

**Now.** For each draw, `pxl__draw_command` fills a complete
`pxl__command` (160 bytes: target, shader, blend, clip, uniforms and 4
texture/sampler bindings), then `pxl__same_state` compares it field by field
with the last command. In a long run of sprites nothing changes, but this
work repeats for each quad.

**Change.** A `batch_open` flag on the context means "the last command uses
the current state". All functions that change the batch state clear it:
`pxl_set_target`, `pxl_set_blend`, `pxl_set_clip`, `pxl_reset_clip`,
`pxl_set_shader`, `pxl_set_shader_texture`, `pxl_set_uniforms`, `pxl_pop`,
`pxl_clear`, `pxl_begin_frame`, the end of a frame, `pxl_destroy_texture`,
`pxl_destroy_shader` and a canvas change. With an open batch, a draw only
compares the texture and sampler of slot 0. The flag is set only for shaders
with one texture slot, because `pxl_set_texture_filter` and
`pxl_set_texture_wrap` can change the sampler of slots 1 to 3 without the
context.

**Result.** Instructions: sprites 461 → 394 (−15%), rects 401 → 334
(−17%), lines 16,545 → 13,542 (−18%). Wall: 1.02x–1.17x, typically 1.1x.

**Trade-off.** This adds an invariant. A future state field that does not
close the batch is a silent batching bug. If you adopt it, keep the
cross-check from this study behind a debug macro: run both paths and assert
that they give the same command.

### A2. Lean batch compare (`patches/lean.patch`)

**Change.** No flag. Compare the current state directly with the last
command, without filling a new command first. The full path runs only when
the state differs.

**Result.** Instructions: sprites 461 → 429 (−7%), lines −8%. Wall:
1.01x–1.09x. About half the gain of A, with no new invariant.

### B. Static quad index buffer (`patches/quadidx.patch`)

**Now.** Each quad writes 4 vertices (112 bytes) and 6 `uint32_t` indices
(24 bytes). The indices are always `4q + {0, 1, 2, 0, 2, 3}`, but pxl writes,
copies and uploads them every frame, into a second GPU buffer.

**Change.** `pxl_create` makes one 16-bit index buffer for 16,384 quads
(192 KiB) and never changes it. All geometry becomes quads. A command stores
its first quad and its quad count. It draws in steps of at most 16,384 quads
with `vertex_offset = 4 * first_quad`, so 16-bit indices reach any vertex.
All three SDL backends support this: Vulkan `vertexOffset`, D3D12
`BaseVertexLocation` and Metal `baseVertex`. A triangle becomes a quad whose
fourth corner repeats the third; the second triangle of that quad has no
area.

**Why it helps.** 18% fewer bytes per quad (136 → 112), no index writes, one
dynamic GPU buffer instead of two, and half the index bytes for the GPU to
read.

**Result.** Instructions: sprites 461 → 421 (−9%), triangles 180 → 150.
Wall: sprites 1.13x, rects 1.16x, text 1.16x, circles 1.39x, circle
outlines 1.43x, lines 1.32x.

**Trade-offs.**
- Triangles need 4 vertices instead of 3 vertices and 3 indices: 112 bytes
  instead of 96 (+17%), and the GPU rejects one empty triangle for each.
  `pxl_draw_triangles` is 0.92x (8% slower).
- A batch of more than 16,384 quads is more than one GPU draw call, so
  `pxl_stats.draw_calls` reports 2 for 20,000 sprites. The cost is one more
  `SDL_DrawGPUIndexedPrimitives` call per 16,384 quads.
- `pxl_stats.indices` now counts drawn indices, not uploaded indices.

### C. Write vertices into the transfer buffer (`patches/direct.patch`)

**Now.** pxl writes vertices into a CPU array. `pxl_end_frame` maps the
transfer buffer, copies the array into it and records a GPU copy into the
vertex buffer. The CPU writes each byte twice.

**Change** (on top of B). At the first draw of a frame, map the transfer
buffer with `cycle = true` and write the vertices into it. If it fills up,
make a transfer buffer twice the size, copy, and release the old one. Unmap
in `pxl_end_frame`, or at the end of a frame that does not record.

**Result.** End of frame for 20,000 sprites: 469 µs → 89 µs (compared with
B). Wall, compared with the current `pxl.h`: sprites 1.50x, rects 1.36x,
circles 1.84x, lines 2.02x, circle outlines 2.05x.

**Trade-offs.** This is the biggest single gain, but lavapipe shows the best
case. SDL makes upload transfer buffers write-combined on D3D12 (an UPLOAD
heap) and on Metal (`MTLResourceCPUCacheModeWriteCombined`). On Vulkan it
asks for `HOST_CACHED` memory. In write-combined memory:
- writes are fast only if they are sequential and fill whole cache lines.
  pxl writes whole vertices in order, so this should hold, but it must be
  measured;
- reads are very slow. pxl does not read the vertices back, except to copy
  them when the buffer grows mid-frame. That is rare, but it can stall the
  frame that grows the buffer.

Measure this on Windows with a discrete GPU and on Apple Silicon before you
adopt it. It also makes the vertex storage depend on the SDL map and unmap
rules for the whole frame. It does not combine with D as written: D puts the
staged pixels in front of the vertices in the same transfer buffer.

### D. Deferred texture updates (`patches/uploads.patch`)

**Now.** Each `pxl_update_texture` creates a transfer buffer, maps it,
copies the pixels, acquires a command buffer, records a copy pass, submits
and releases the transfer buffer. On a real driver, "create a buffer" is a
memory allocation, and each submit is a queue submission.

**Change.** In a frame, `pxl_update_texture` copies (and premultiplies) the
pixels into one growing staging array and records the target area. In
`pxl_end_frame`, the staged pixels go into the same transfer buffer as the
geometry, and one copy pass uploads them before the first render pass.
Outside of a frame, the update still happens at once. If a frame ends without
recording (no command buffer or no swapchain texture), pxl submits the staged
updates alone, so no update is lost. Offsets are aligned to 512 bytes, the
D3D12 placement alignment, so the SDL D3D12 backend does not copy the data
again for the offset. (It still copies rows that are not a multiple of 256
bytes.)

**Why the meaning stays the same.** The documentation says: "All draws of
the current frame see the new pixels." Now that is true because the update
is submitted before the frame. With the change it is true because the copy
pass comes before the frame's render passes in the same command buffer.

**Result.** 50 updates per frame: 2.54 ms → 0.23 ms (11x), about 4.6 µs per
32x32 update instead of 50 µs.

**Trade-offs.** The staged pixels stay in memory until the end of the
frame, about as much memory as the transfer buffers use now. The path for a
frame that does not record was not tested here: it needs a failing swapchain.

### E. Four texture slots in the default shader (`patches/multitex.patch`, `patches/multitex-sprite.frag`)

**Now.** A new texture starts a new batch, so a scene that alternates two
textures makes one GPU draw call per sprite. Each draw call costs about 1 µs
of CPU in SDL and the driver here: 2,000 sprites take 1.92 ms in
`pxl_end_frame`.

**Change.** The default fragment shader declares 4 samplers. Each vertex
stores the slot of its texture in `params[2]`, and the shader selects the
slot. A batch of the default shader collects up to 4 textures; the fifth
starts a new batch. Plain color draws use any slot. The shader uses
`textureLod(..., 0)` in the branches. pxl textures have one mip level, so
this gives the same texels as `texture()`, and needs no derivatives in
non-uniform control flow. Custom shaders keep the current behavior.

**Result.** sprites_2tex: 2,000 draw calls → 1; 2.15 ms → 0.32 ms (6.8x).
The pixels are identical. Other scenes take 1–9% more CPU, because
`pxl__draw_command` does more work and 4 bindings change per batch. On
lavapipe the GPU time of the other scenes is 0–39% higher. Lavapipe runs the
shader on the CPU, so this is only a hint of the GPU cost. The test
`test_batching` fails, because it expects that two textures need two draw
calls. That is the intended change.

**Trade-offs.** The fragment shader branches on a value that can change
from one triangle to the next, so a GPU wave can run more than one branch.
Only the SPIR-V was rebuilt for this prototype: `rake pxl:shaders` must make
the DXIL and the MSL again. Before you add this, check whether real games
alternate textures. A texture atlas gives the same gain with no change in
pxl.

### F. Lines and circles (`patches/circles.patch`, and `pxl__fills` in `patches/bulk.patch`)

**Now.** `pxl__line` emits one quad per straight run of pixels; a diagonal
line is one quad per pixel. `pxl_draw_circle_lines` calls
`pxl__circle_span` three times per row (each with a `sqrtf`) and emits one or
two quads per row. Each quad goes through `pxl__fill` → `pxl__quad` →
`pxl__append` → `pxl__draw_command` → `pxl__reserve`.

**Changes.**
1. Circle outlines (`circles.patch`): reuse the spans of the rows above and
   below instead of computing them again (one `sqrtf` per row), and merge
   rows with the same edges into one taller quad, as `pxl_draw_circle`
   already does. Quads per outline: 57 → 32.
2. Bulk reservation (in `bulk.patch`): a shape reserves its maximum number of
   quads once (a line: `min(pixels, minor + 1)` runs; a filled circle: one
   per row; an outline: two per row), writes each run with a small
   plain-color writer, and gives back the unused quads at the end. The batch
   bookkeeping then happens once per shape, not once per run.

**Result.**
- 1 alone: circle outline 20,536 → 13,131 instructions; wall 1.56x.
- 2, added to A, B, D, 1 and G: lines 10,018 → 5,783 instructions,
  circles 4,361 → 2,902, outlines 7,847 → 4,977. Wall: 1.09x–1.12x more.
  The wall gain is smaller than the instruction gain, because the vertex
  writes and the end of the frame do not change.
- The full recommended set: lines 1.59x, circles 1.62x, outlines 2.98x.

**Trade-off.** "Give back the unused quads" assumes that the shape's quads
are the end of the last command. That is true because nothing else can draw
between the reservation and the end of the shape.

### G. Text (`patches/text.patch`)

**Now.** `pxl__draw_string` draws each glyph with `pxl__draw_region`, so
each glyph pays the full batch bookkeeping. It finds the glyph cell with
64-bit `%` and `/`.

**Change.** Count the glyphs of the string first, reserve them all at once,
then write the quads. Use 32-bit unsigned division for the cell.

**Result.** Instructions per character: 386 → 259 (−33%). Wall: text 1.21x.

**More.** `SDL_vsnprintf` is 11% of the text cost. A format string without
`%` formats to itself, so pxl could skip the formatting for it. Not measured.

### H. Pipeline creation on first use (measured, not prototyped)

**Now.** `pxl__get_pipeline` creates a pipeline the first time a
combination of shader, blend mode and target format is used, inside
`pxl_end_frame`. `bench/hitch.c` measures the frame that first uses each
blend mode:

| Blend | First frame | Next frame |
|---|---|---|
| alpha (also the first frame of the context) | 7.5 ms | 0.13 ms |
| add | 1.9 ms | 0.06 ms |
| multiply | 1.2 ms | 0.09 ms |
| none | 1.7 ms | 0.07 ms |

On a real driver, a pipeline compiles SPIR-V, DXIL or MSL into GPU code. That
can take longer, and it happens in the middle of the game: the first
explosion with additive blend drops a frame.

**Change.** In `pxl_create`, create the pipelines of the default shader for
the 4 blend modes and the RGBA8 target format, and the pipeline that shows
the canvas in the swapchain format. In `pxl_create_shader`, create the 4
pipelines of the new shader. This moves the cost to load time. It is a small
change. It was not prototyped, because the only result is that the stall
goes away.

### I. Inline floor (`patches/round.patch`): rejected

**Now.** At baseline x86-64 (SSE2 only), `floorf` is a call into libm.
`pxl_draw_rect` calls it 8 times per rectangle, `pxl_draw_sprite` twice.

**Change.** An inline floor that converts to `int32_t` and back for values
below 2^23. It matches `floorf` for all 2^32 inputs, except the sign of zero.

**Result.** A tight loop shows 4.2 ns per call for `floorf` and 1.6 ns for
the inline version. In pxl alone, the wall clock is 0.96x–1.04x: no gain.
glibc picks an SSE4.1 `floorf` at run time, so the call is cheap. In the
optimized set (7 alternating runs), the inline floor makes rects 1.08x but
triangles 0.88x and sprites, circles and lines about 0.95x. Do not adopt it.

Better options:
- Build with `-msse4.1` (x86-64-v2, any x86-64 CPU since about 2008): the
  compiler inlines `roundss`. In the optimized set rects are 1.23x, and the
  other scenes stay within the noise (0.94x–1.01x). See J.
- When the transform has no rotation, `pxl_draw_rect` can round 2 corners
  instead of 4, so it calls `floorf` 4 times instead of 8. Not measured.

### J. Compiler flags

Same code, clang 20, compared with `-O2`:

| Flags | Effect |
|---|---|
| `-O3` | no change |
| `-O2 -msse4.1` | rects 1.10x–1.13x, triangles 1.04x–1.13x (inline `floorf`); the other scenes do not change |
| `-O2 -march=x86-64-v3` | the same as `-msse4.1` |

`PXL_ASSERT` uses `SDL_assert`, which is off when `__OPTIMIZE__` is defined
and `_DEBUG` and `DEBUG` are not. So the asserts cost nothing in optimized
builds. LTO does not apply: the implementation is one translation unit.

## 5. Ideas that were not prototyped

**Acquire the swapchain texture late.** `pxl_end_frame` calls
`SDL_WaitAndAcquireGPUSwapchainTexture` before it records anything. That
call blocks when too many frames are in flight. After the wait, pxl copies
the geometry and records all passes. If pxl recorded the canvas passes first
and acquired the swapchain texture only for the last pass, the CPU work would
overlap the wait. That reduces latency, and it helps throughput when only
one frame can be in flight. The blocker: the quad that shows the canvas is
part of the geometry, and its position depends on the swapchain size. Fix:
make that quad cover the canvas size and place it with `SDL_SetGPUViewport`.
This needs a real window, so it was not measured here.

**Frames in flight and present mode.** The user owns the device and the
window, so they can call `SDL_SetGPUAllowedFramesInFlight` and
`SDL_SetGPUSwapchainParameters` (for example with
`SDL_GPU_PRESENTMODE_MAILBOX`). The pxl documentation could mention them.

**Smaller vertices.** `pxl__vertex` is 28 bytes. `params` uses 2 bits of its
4 bytes, and `overlay` is usually zero. Moving the 2 flags into spare bits
gives 24 bytes (−14%). A per-batch "sharp" uniform would also free one flag.
This matters only when the copy of the geometry is the bottleneck (see C).

**Instanced sprites (vertex pulling).** Store one record per sprite (corners
or an affine transform, the UV rectangle, color, overlay and flags: about 48
bytes) in a storage buffer, and let the vertex shader make the 4 corners from
the vertex index. That is less than half of the 112 bytes of 4 vertices, and
less CPU work per sprite. But `pxl_draw_triangles` needs a second path, and
the order of the draws must stay correct across both paths. This is a
redesign.

**Shapes in the shader.** One quad per circle and a per-pixel test in the
fragment shader (`dx² + dy² ≤ r(r + 1)` at pixel centers) would replace 18
to 57 quads per circle. A Bresenham line in the shader needs the same
tie-breaks as `pxl__line` to stay pixel-exact. Both need more per-vertex data.

**For users of pxl.** Put sprites in atlases. Draw in an order that keeps
the same texture, blend and target together. `pxl_get_stats` shows the
number of draw calls: if it is much higher than the number of textures and
state changes, the batches break.

## 6. Results

### CPU time per frame, wall clock (final run)

Median µs (record + end of frame), and the gain against the current
`pxl.h`. "Set" is the recommended set A+B+D+F+G (`best.patch`).

| Scene | Current | A | A2 | B | C (with B) | E | Set | Set + inline floor |
|---|---|---|---|---|---|---|---|---|
| sprites | 1807 | 1.11x | 1.07x | 1.13x | 1.50x | 0.97x | **1.32x** | 1.26x |
| sprites_rot | 2360 | 1.12x | 1.09x | 1.17x | 1.37x | 0.99x | **1.29x** | 1.30x |
| sprites_2tex | 2146 | 0.98x | 1.01x | 1.03x | 0.96x | **6.79x** | 0.98x | 0.99x |
| rects | 1644 | 1.02x | 1.01x | 1.16x | 1.36x | 0.91x | **1.24x** | 1.40x |
| text | 1416 | 1.10x | 1.01x | 1.16x | 1.40x | 0.95x | **1.31x** | 1.45x |
| circles | 1513 | 1.12x | 1.09x | 1.39x | 1.84x | 0.94x | **1.62x** | 1.56x |
| circle_lines | 4617 | 1.17x | 1.04x | 1.43x | 2.05x | 0.98x | **2.98x** | 2.82x |
| lines | 6734 | 1.10x | 1.07x | 1.32x | 2.02x | 0.96x | **1.59x** | 1.50x |
| rect_lines | 1486 | 1.09x | 1.05x | 1.22x | 1.74x | 0.92x | **1.39x** | 1.40x |
| triangles | 531 | 1.01x | 0.97x | 0.92x | 1.24x | 1.01x | **0.88x** | 0.80x |
| update_texture | 2614 | 1.01x | 1.03x | 0.91x | 0.96x | 1.03x | **10.95x** | 12.28x |
| mixed | 320 | 1.02x | 1.03x | 1.10x | 1.24x | 0.95x | **1.25x** | 1.23x |

`update_texture` is unchanged by A, A2, B, C and E; its 0.91x–1.03x shows
the noise. The separate 7-run comparison in section 4 I is the better
measure of the inline floor.

### Instructions per item, record phase (callgrind)

| Scene | Current | A | A2 | B | G, F1 or D alone | A+B+D+F+G + inline floor |
|---|---|---|---|---|---|---|
| sprites | 461 | 394 | 429 | 421 | 461 | 350 |
| sprites_rot | 546 | 479 | 514 | 506 | 546 | 440 |
| rects | 401 | 334 | 370 | 322 | 401 | 279 |
| text | 386 | 336 | 362 | 356 | 259 (G) | 244 |
| circles | 7,125 | 5,934 | 6,574 | 5,430 | 7,125 | 2,902 |
| circle_lines | 20,536 | 16,733 | 18,777 | 16,462 | 13,131 (F1) | 4,977 |
| lines | 16,545 | 13,542 | 15,155 | 12,955 | 16,545 | 5,783 |
| rect_lines | 1,313 | 1,055 | 1,193 | 1,042 | 1,313 | 775 |
| triangles | 180 | 180 | 180 | 150 | 180 | 179 |
| update_texture | 46,504 | 46,994 | 47,467 | 47,011 | 27,563 (D) | 27,453 |
| mixed | 623 | 533 | 580 | 561 | 623 | 457 |

The last column includes the inline floor (I), which adds a few
instructions per rounding. The recommended set leaves it out.

## 7. Recommendations, in order

1. **D. Deferred texture updates.** The largest gain for any game that
   streams texture data. Small change, same documented behavior.
2. **H. Create the pipelines up front.** Removes frame stalls. Small change.
3. **F. Shapes: merge rows and reserve per shape.** Pixel-identical, local
   to the shape functions.
4. **B. Static quad index buffer.** 1.1x–1.4x for everything that draws
   quads, and less memory traffic. Accept 8–12% slower `pxl_draw_triangles`.
5. **A. Batch fast path** with a debug cross-check, or **A2** if you prefer
   no new invariant.
6. **G. Text batching.**
7. **C. Direct writes into the transfer buffer**, after a measurement on
   D3D12 and Metal hardware. It is the largest gain for draw-heavy frames.
8. **E. Multi-texture batching**, only if real games alternate textures
   often and atlases are not an option. Measure the GPU cost first.
9. Do not adopt I. Document J and the SDL settings in section 5.

## 8. Files

- `patches/*.patch`: one prototype each, against `pxl.h` at `8e3ab73`:
  `fastpath` (A), `lean` (A2), `quadidx` (B), `direct` (C, includes B),
  `uploads` (D), `multitex` (E), `circles` (F1), `text` (G), `round` (I).
  `combined` is A+B+D+F1+G+I, `bulk` adds F2, and `best` is `bulk` without I:
  the recommended set.
- `patches/multitex-sprite.frag`: the 4-slot fragment shader for E.
- `bench/`: the benchmark and the checks. `env.sh` needs `SP` (a work
  directory with `variants/<name>/pxl.h`) and `PREFIX` (an SDL 3.2 install).
  `build.sh`, `test.sh`, `same.sh`, `cg.sh` and `wall.py` build, test,
  compare pixels, count instructions and time the variants. They are the
  scripts used for this report, not a polished tool.
- `results/`: the raw callgrind and wall-clock output. `wall-run1.txt` was
  measured before the triangle loop of B was fixed and before `best`
  existed. Its other columns agree with `wall-run2.txt` within the noise.

These are prototypes for measurement. They pass the tests and give the same
pixels, but they are not finished changes. They need documentation, tests
for the new paths (for example a batch of more than 16,384 quads, and an
update in a frame that does not record), and `clang-tidy` and
`clang-format` runs.
