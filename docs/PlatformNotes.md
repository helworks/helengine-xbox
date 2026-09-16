# Xbox Platform Notes (v0.1 native cube)

## Identities

| Item | Value |
| --- | --- |
| nxdk | `https://github.com/XboxDev/nxdk.git` @ `29638d0b001f179b73c3513489af10ddc2986216` (2026-06-05), submodules recursive |
| Local checkout | `C:\dev\helworks\builds\helengine-xbox\dependencies\nxdk`, LF-normalized (`core.autocrlf=false`) |
| Image base | `ghcr.io/xboxdev/nxdk-buildbase@sha256:b14cd17f59449c0c3bdc86dc04dde411cdd0eaaeb3539fbe85a598dec3846f52` (Alpine 3.22) |
| Compiler / linker | Alpine clang 20.1.6, LLD 20.1.6; target `i386-pc-win32 -march=pentium3` via `nxdk-cxx` / `nxdk-link` |
| Host compiler (tests) | g++ 14.2.0 (Alpine) |
| Shader tools | `tools/vp20compiler` (NV_vertex_program 1.1 text), `tools/fp20compiler` (RC1.0 text). NVIDIA `cgc` is not used. |
| Packaging | `tools/cxbe` (`-OUT -TITLE`), `tools/extract-xiso -c <dir> <iso>` (v2.7.1) |
| SDK library flags | built once in the image with `NXDK_ONLY=y CFLAGS=-O2 CXXFLAGS=-O2`; application Debug/Release only changes application flags |

The exact image ID is in `toolchain-lock.txt`; `scripts/build.ps1` refuses any
other image ID or checkout commit.

## API worksheet (pinned sources)

Paths are relative to the nxdk checkout.

| Topic | Pinned behaviour | Source |
| --- | --- | --- |
| Video mode | `XVideoSetMode(640, 480, 32, REFRESH_DEFAULT)` returns `BOOL`; `XVideoGetMode()` reports the selected `width/height/bpp/refresh`. The probe requires exactly 640x480x32. | `lib/hal/video.h` |
| pbkit init | `pb_init()` returns 0 on success; negative codes: -2/-3 allocation, -4 IRQ hook, -5 PLL, -6/-7 instance memory, -8 already running, -9/-10 DMA start, -11 surface allocation. It allocates 3 colour buffers (front + 2 back, "triple buffering") and one depth/stencil surface, installs the GPU interrupt and starts the DMA push-buffer engine. | `lib/pbkit/pbkit.c:2183` (pb_init), `:2860-2935` (surfaces) |
| Back buffer | `pb_back_buffer_width/height/pitch()`; extent comes from `XVideoGetMode`. Colour format `NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8` (pitch 64-byte aligned, 16 KiB aligned allocation, write-combined). | `lib/pbkit/pbkit.c:61`, `:1579-1591`, `:2870-2916` |
| Depth format | `NV097_SET_SURFACE_FORMAT_ZETA_Z24S8` only (asserted); `pb_ZScale = 0xFFFFFF`; viewport depth range programmed as `[0, 1] * pb_ZScale`. | `lib/pbkit/pbkit.c:62`, `:2903-2904`, `:1938-1975` |
| Frame sequence | `pb_wait_for_vbl(); pb_reset(); pb_target_back_buffer(); pb_erase_depth_stencil_buffer(0,0,w,h)` (depth 0xFFFFFF, stencil 0, assumes LEQUAL); `pb_fill(0,0,w,h,argb)`; draw; `pb_draw_text_screen()`; `while (pb_busy()) {}`; `while (pb_finished()) {}` (returns 1 while the 3-entry swap table is full). | `samples/triangle/main.c`, `lib/pbkit/pbkit.c:1746,1780,1984,2010`, `lib/pbkit/pbkit_draw.c` |
| Default GPU state after pb_init | depth func LEQUAL, depth write on, blend/alpha test/fog/dither off, colour mask ARGB, `FRONT_FACE=0x900 (CW)`, `CULL_FACE=0x405 (BACK)`, cull enabled, shade model Gouraud, `SHADER_TYPE_INTERNAL`, all four texture stages disabled, stencil enabled (func always). Depth test enable is pushed by `set_draw_buffer` only when the depth surface binding changes, so the renderer re-pushes its own state each frame. | `lib/pbkit/pbkit.c:3068-3135`, `:1690-1706` |
| Vertex storage | `MmAllocateContiguousMemoryEx(bytes, 0, 0x03FFAFFF, 0, PAGE_READWRITE \| PAGE_WRITECOMBINE)`; GPU address = pointer `& 0x03FFFFFF`; freed with `MmFreeContiguousMemory`. Write sequentially, never read back. | `lib/xboxkrnl/xboxkrnl.h:3409,3464`, `samples/mesh/main.c` |
| Vertex arrays | `NV097_SET_VERTEX_DATA_ARRAY_FORMAT + slot*4` = `MASK(TYPE, F=2) \| MASK(SIZE, n) \| MASK(STRIDE, bytes)`; `NV097_SET_VERTEX_DATA_ARRAY_OFFSET + slot*4` = GPU address. Unused slots: type F, size 0, stride 0. Slot 0 = position, slot 3 = diffuse. | `lib/pbkit/nv_regs.h:480-490`, `samples/triangle/main.c` |
| Vertex program upload | `SET_TRANSFORM_PROGRAM_START 0`; `SET_TRANSFORM_EXECUTION_MODE = MODE_PROGRAM \| RANGE_MODE_PRIV`; `SET_TRANSFORM_PROGRAM_CXT_WRITE_EN 0`; `SET_TRANSFORM_PROGRAM_LOAD 0`; then `SET_TRANSFORM_PROGRAM` with 4 dwords per instruction. | `samples/triangle/main.c` init_shader, `lib/pbkit/nv_regs.h:761-771` |
| Shader compiler | `vp20compiler <file>` reads `!!VP1.0`/`!!VP1.1` text (VP1.1 needed for DPH/RCC/SUB/ABS), prints `0x.., 0x.., 0x.., 0x..,` per instruction plus `//` comments; program `c[N]` becomes hardware constant `N + 96`. One constant register per instruction. `fp20compiler <file>` reads `!!RC1.0` text and prints `pb_push1(p, NV097_SET_COMBINER_...); p += 2;` statements (requires a `p` from `pb_begin`). | `tools/vp20compiler/nvvertparse.c:1382`, `tools/vp20compiler/main.c:517-560`, `tools/fp20compiler/main.cpp`, `tools/fp20compiler/rc1.0_grammar.y` |
| Constants | `NV097_SET_TRANSFORM_CONSTANT_LOAD = 96 + first`, then `NV097_SET_TRANSFORM_CONSTANT` with 4 floats per register. | `samples/mesh/main.c` |
| Indexed draw | `SET_BEGIN_END TRIANGLES`; `0x40000000 \| NV097_ARRAY_ELEMENT16` (non-incrementing) with two 16-bit indices per dword (low half first); odd tail via `NV097_ARRAY_ELEMENT32`; `SET_BEGIN_END END`. Blocks kept well under pbkit's 128-dword begin/end limit. | `samples/mesh/main.c` draw_indices, `lib/pbkit/nv_regs.h:505-518`, `lib/pbkit/pbkit_pushbuffer.h` |
| Drain / shutdown | `pb_busy()` = DMA get != put or PGRAPH busy. `pb_kill()` jumps the DMA to head, waits (bounded by `TICKSTIMEOUT`), waits for pending swaps, uninstalls the interrupt, frees pbkit's own buffers and restores the video state. Returning from `main` reaches `_PDCLIB_Exit` = `HalReturnToFirmware(HalQuickRebootRoutine)`. | `lib/pbkit/pbkit.c:1557,2037`, `lib/pdclib/platform/xbox/functions/_PDCLIB/_PDCLIB_Exit.c` |
| Text overlay | `pb_print` / `pb_draw_text_screen` render with `pb_fill` rectangle clears only; they do not touch 3D state. 16 rows x 60 columns. | `lib/pbkit/pbkit_print.c` |
| Diagnostics | `debugPrint` writes to the HAL debug framebuffer (visible via `pb_show_debug_screen`); `DbgPrint` goes to the kernel debug channel. | `lib/hal/debug.h`, `lib/xboxkrnl/xboxkrnl.h:4386` |

## Conventions used by the probe

**CPU math** (`XboxMath.hpp`): row-major `Matrix4`, column-vector
multiplication, `clip = Projection * View * Model * position`, right-handed
view looking down -Z, clip depth `z/w` in `[0, 1]` (`m22 = far/(near-far)`,
`m23 = far*near/(near-far)`, `m32 = -1`).

**Vertex layout**: `CubeVertex { float Position[3]; float Color[4]; }`, 28
bytes, offsets 0 and 12 (static_asserted). Float colour deliberately avoids
packed-colour byte-order questions in this first probe.

**NV2A boundary** (`shaders/unlit.vsh`): the program computes the clip
position with four `DPH` against `c[0..3]` (rows of the clip matrix), divides
by w (`RCC`), then applies pbkit's viewport mapping itself:
`x' = x * (w/2) + (0.53125 + w/2)`, `y' = y * (-h/2) + (0.53125 + h/2)`,
`z' = z * 16777215`, `o[HPOS].w = clip w`. These are the same numbers
`pb_set_viewport` programmed into the viewport registers; in vertex-program
mode the GPU does not apply them again (the pinned samples output screen
space the same way, and xemu's program path converts screen space back to
clip space: `hw/xbox/nv2a/pgraph/glsl/vsh-prog.c`). Perspective division and
viewport conversion therefore happen exactly once, on the GPU.

**Winding and culling**: the cube is wound counter-clockwise when viewed from
outside (`dot(cross(b-a, c-a), centroid) > 0`, tested). The renderer sets
`NV097_SET_FRONT_FACE = CCW (0x901)`, `CULL_FACE = BACK (0x405)`, cull
enabled. Evidence for the register semantics: the pinned mesh sample's 432
triangles are all clockwise-outward against their own normals (analysed from
`samples/mesh/verts.h`) and render correctly with pbkit's default
`FRONT_FACE = CW`; xemu's `SET_FRONT_FACE` handler stores CCW as 1 and its GL
path calls `glFrontFace(FRONTFACE ? GL_CW : GL_CCW)` with the comment
"Winding is reverse here because clip-space y-coordinates are inverted", i.e.
`0x901` means "counter-clockwise as seen on the Xbox screen is front". This is
the documented API-boundary choice to revisit if a runtime test shows the
inside faces; `-DisableCulling` and `-ReverseTriangleOrder` exist for that.

**Depth**: depth test on, depth write on, `LEQUAL`, depth cleared to
`0xFFFFFF` (far) every frame by `pb_erase_depth_stencil_buffer`.

**Disabled state per frame**: lighting, specular, fog, blending, alpha test,
stencil test, all four texture stages. Colour mask writes ARGB.

**Resource rules**: one `MmAllocateContiguousMemoryEx` of 27 vertices (24 cube
+ 3 triangle, 756 bytes) at `Initialize`; nothing is allocated in `Draw`; the
CPU index array lives in ordinary memory and is copied into the push buffer;
the allocation is released only after `XboxVideo::Drain()` succeeded and before
`pb_kill`. Counters (allocations, releases, draws, triangles) are shown on the
overlay and logged at `renderer-ready`, at frame 3600 and at shutdown.

**Diagnostic poses** (`-Pose`): Baseline view `Translation(0,0,-5)`; Far
`(0,0,-12)`; NearPlane `(0,0,-1.3)` so the cube pierces the near plane; ScreenEdge
`(2.2,1.2,-4)` so the cube crosses the right/top edges. Model is always
`RotationY(30°) * RotationX(-20°)`.

## Build and packaging notes

- nxdk's Makefile writes `.obj`/`.d` files beside sources, so `build.ps1` stages
  `Makefile` + `src/` into `<OutputRoot>/work` and runs `make` there;
  `OUTPUT_DIR=/work/disc` holds only `default.xbe` and is what `extract-xiso -c`
  packs into `game.iso`.
- The Makefile validates `CONFIGURATION`, `PROBE`, `POSE`, `STOP_AFTER_FRAMES`
  (nonnegative integer), `DISABLE_CULLING`, `REVERSE_TRIANGLES` before any rule
  runs and maps Clear=0, Triangle=1, Cube=2 to `HELENGINE_XBOX_PROBE`.
- `-Werror -Wall -Wextra -std=c++17`; no exceptions (nxdk-cxx passes
  `-fno-exceptions`), no STL containers, no heap use outside the one GPU
  allocation.
- Link map review (Cube/Debug): only `libpdclib`, `libpbkit`, `libwinapi`,
  `libnxdk_hal`, `libxboxrt`, `libnxdk`, `libnxdk_automount_d` and the six probe
  objects. No SDL, lwIP, libc++, engine or Core objects.
- LLD warns that `.debug_*` section names exceed 8 characters in Debug builds;
  harmless for cxbe.

## Verification state

| Check | Result |
| --- | --- |
| Toolchain image build | pass (`scripts/prepare_toolchain.ps1`, lock regenerated) |
| CPU contract tests | pass, 588 checks (`scripts/test-native.ps1`) |
| Clear / Triangle / Cube Debug builds | pass; XBE magic `XBEH`, XISO lists `/default.xbe` |
| Cube Release build | pass |
| Diagnostic variant builds | pass: `-Pose Far`, `-Pose NearPlane`, `-Pose ScreenEdge`, `-DisableCulling`, `-ReverseTriangleOrder`, `-StopAfterFrames 300` (Cube/Debug) |
| Negative build checks | invalid `-Probe`, negative `-StopAfterFrames`, nonexistent nxdk checkout, unlocked image tag and existing output directory all fail before Docker with no `build-info.json` |
| Negative launch checks | no emulator path, nonexistent emulator, missing image, image copied without its build record, and a tampered image (hash mismatch) all refuse to launch with concrete diagnostics |
| Emulator boot and visual cube | **not verified**: xemu is not installed under `C:\dev\helworks\emus` or the usual install locations, and its MCPX/BIOS firmware cannot be supplied by this repository |
| 60-second stability, finite-frame shutdown probe | **not verified** (same blocker) |
| Physical hardware | **pending** |

## Known limitations

- 640x480 32-bit only; no textures, lighting, MSAA or offscreen passes.
- Rotation is not implemented (static pose by design).
- Finite-frame runs end with nxdk's quick reboot after a five-second hold on
  the debug console; there is no controller input or menu.
- The front-face convention above is derived from pinned-source evidence and
  the emulator's implementation, not yet from a live frame.
