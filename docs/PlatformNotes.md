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
| Depth mode | `pb_init` leaves `NV097_SET_CONTROL0` (0x290) at `STENCIL_WRITE_ENABLE \| Z_PERSPECTIVE_ENABLE \| TEXTURE_PERSPECTIVE_ENABLE` (0x00110001). `Z_PERSPECTIVE_ENABLE` (bit 16) makes the GPU derive depth from `w` instead of the program's screen-space z. The renderer clears that bit each frame and keeps the other two. See "Depth mode is not pbkit's default" below. | `lib/pbkit/pbkit.c:1718`, `lib/pbkit/nv_regs.h:159-167` |
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

**Depth mode is not pbkit's default.** `pb_init` enables
`NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE`, which makes the GPU ignore the
screen-space z this vertex program computes and derive depth from `w`
instead. For this scene `w` only spans roughly 3 to 7, so after quantisation
nearly every depth comparison is a tie, `LEQUAL` admits the tying fragment,
and the frame degenerates into a painter's algorithm. With back-face culling
enabled that is invisible, because a convex cube's front faces never overlap
each other, so the image looks correct for the wrong reason. It was caught by
the culling-disabled probe: back faces won pixels they should have lost, and
reversing the triangle order changed which ones won. The renderer therefore
pushes `NV097_SET_CONTROL0` with `Z_PERSPECTIVE_ENABLE` cleared and
`STENCIL_WRITE_ENABLE | TEXTURE_PERSPECTIVE_ENABLE` kept. After the fix the
culling-disabled and reversed-order builds produce identical histograms with
every back face at 0.000%, so depth and not draw order decides the frame.

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

## How the rendered frame is verified

xemu strips QEMU's `screendump` from both QMP and HMP, so the frame is
recovered from emulated memory instead of from the host screen:

1. `scripts/capture_emulator_frame.ps1` boots the XISO with `-snapshot` and an
   isolated `-config_path`, then dumps the emulated console's 64 MiB physical
   address space through the QEMU monitor (`pmemsave`).
2. `tools/decode_framebuffer.py` finds pbkit's framebuffer inside that dump,
   writes it out as a PNG and reports a colour histogram.

Nothing belonging to the host desktop is read or captured; the dump contains
only the emulated Xbox's own memory. Because the probe's frame is static and
the three framebuffers are cyclic rotations of one image, the histogram is
alignment-independent, and the text overlay fixes the true origin.

The cube check is falsifiable rather than impressionistic. For the baseline
pose exactly three faces turn toward the camera (+Z red, -X yellow, -Y
magenta) and their opposites must be absent; inverted winding or culling would
show the opposite set, and broken depth would mix both.

## Authored mode

`MODE=Authored` joins the two halves of this repository: the native platform
sources and an editor-generated Core. It requires `GENERATED_CORE_ROOT` and
`CONTENT_ROOT`, both validated before anything compiles so a missing root fails
immediately rather than producing an image that silently carries no scene.

There is no catalog input. Codegen emits `runtime/runtime_scene_catalog_manifest.cpp`
and `runtime/runtime_startup_manifest.cpp` into the generated tree, carrying the
authored scene ids, their cooked paths and the platform identity. Neither unity
translation unit includes them, so they are compiled alongside the Core and
their absence is a named Makefile error. helengine-dc supplies the same data
through a builder-generated C ABI, which predates these manifests; this
platform does not need one.

The generated Core is compiled as a single unity translation unit rather than
added to `SRCS`, because nxdk writes each object beside its source and the
generated root is mounted read-only; its object lands in the invocation
directory instead. Its flags deliberately differ from the platform sources:
C++20 because generated code uses C++20 library facilities, warnings off
because generated code is not written to this repository's `-Werror` bar, RTTI
on for generic implementation dispatch, and exceptions left at nxdk's
`-fno-exceptions` default to match a Core generated with
`codegen-use-exceptions=false`.

The cooked tree is copied next to `default.xbe` before `extract-xiso` runs, so
it is packed into the image and reachable from `D:\` at runtime. The build
fails if the staged disc root carries nothing but the executable, since that
means the cooked tree never arrived.

Measured on the first authored build, against the Core generated for the
`xbox-headless` profile with exceptions disabled:

| Artifact | Diagnostic (Release) | Authored (Release) |
| --- | --- | --- |
| `default.xbe` | 118,784 bytes | 892,928 bytes |
| `main.map` | 97,165 bytes | 1,697,491 bytes |

The map carries 9,706 symbols from the generated Core unity object, including
`SceneManager`, `ContentManager`, `RenderManager3D`, `Entity` and
`PackagedAssetBinarySerializer`, and the XISO listing shows the staged content
beside the executable.

**What an authored build does not yet do:** nothing calls into Core. The host
still runs the native probe, so an authored image renders the same diagnostic
cube. Linking Core and packaging content is the prerequisite for the runtime
host, not a behaviour change on its own.

That sameness is itself the test worth running. Adding roughly 900 KB of code
and static data to the image could break XBE loading, fault in Core's static
initializers before `main`, or starve pbkit's contiguous allocations inside the
64 MiB the console has. None of that happens. The authored image boots under
xemu and renders a frame whose colour histogram is identical to the diagnostic
build's, to three decimal places:

| Face | Diagnostic | Authored |
| --- | --- | --- |
| red +Z | 9.791% | 9.791% |
| yellow -X | 3.998% | 3.998% |
| magenta -Y | 1.133% | 1.133% |
| green -Z, blue +X, cyan +Y | 0.000% | 0.000% |
| clear | 82.47% | 82.47% |

At the 44-second capture the overlay read frame 2652 with 31,824 triangles,
exactly twelve per frame, and counters steady at `alloc 1 free 0`, a frame rate
comparable to the diagnostic runs. pbkit's framebuffers moved to a different
address as the larger executable shifted the memory layout, and allocation still
succeeded. So linking Core costs the renderer nothing measurable and perturbs
neither startup nor steady state.

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
| Emulator boot | pass: xemu 0.8.136 (exe SHA-256 `7da537938ea2ac09f894186ba793c9ae51dff37c95903b0002273b8d363818b7`) with the MCPX ROM (MD5 `d49c52a4102f6df7bcf8d0617ac475ed`), Complex 4627 BIOS and 8 GB HDD image under `C:\dev\helworks\emus\xbox`, launched with `-snapshot` and an isolated config |
| Clear probe | pass: frame is exactly the clear colour `0xFF203040` at 98.22% plus the white overlay, frame counter advancing |
| Triangle probe | pass: one Gouraud triangle, red / green / blue corners matching the vertex data, 1 draw and 1 triangle per frame |
| Cube probe, baseline | pass: red 9.791%, yellow 3.998%, magenta 1.133%, all three back faces 0.000%, 1 draw and 12 triangles per frame |
| Perspective | pass: `-Pose Far` (distance 12 versus 5) covers 2.527% against 14.922%, a ratio of 0.169 where the inverse-square prediction is 0.174 |
| Near-plane clipping | pass: `-Pose NearPlane` fills the view with the clipped red face (98.25%, two colours total), no stray or exploded geometry |
| Screen-edge clipping | pass: `-Pose ScreenEdge` clips cleanly at the frame edges with no back faces |
| Back-face culling and winding | pass: `FRONT_FACE = CCW` confirmed on hardware behaviour, not just inferred; the opposite face set never appears |
| Depth test | pass **after the `Z_PERSPECTIVE_ENABLE` fix**: culling-disabled and culling-disabled-plus-reversed-order builds give identical histograms with all back faces 0.000%. Before the fix these differed and back faces bled through, so depth was not being tested at all |
| Draw-order independence | pass: `-ReverseTriangleOrder` leaves the baseline histogram unchanged |
| 60-second stability | pass: 105 s continuous run, frame 3228 with 38736 triangles (exactly 12 per frame), counters steady at `alloc 1 free 0`, emulator alive and `running` at the end |
| No steady-state allocation | pass: `alloc 1 free 0` across every run, so nothing is allocated inside `Draw` |
| Finite-frame shutdown | partial: `-StopAfterFrames 300` stops at the requested count (captured at frame 298 with 3576 triangles) and leaves the loop without hanging. The drain and reverse-order release run on that path but their diagnostics are printed to the HAL debug console, which this capture method does not read, so they are not directly observed |
| Physical hardware | **pending** |

Pose captures for Far, NearPlane and ScreenEdge were taken from builds made
before the depth fix. The fix cannot affect them: with culling enabled only
front faces are submitted and a convex cube's front faces never overlap, so no
depth comparison is ever decisive. This is confirmed directly, since the
baseline histogram is identical before and after the fix.

## Known limitations

- 640x480 32-bit only; no textures, lighting, MSAA or offscreen passes.
- Rotation is not implemented (static pose by design).
- Finite-frame runs end with nxdk's quick reboot after a five-second hold on
  the debug console; there is no controller input or menu.
- All rendering results above come from xemu. Real NV2A silicon may differ,
  particularly around depth-buffer tiling and Z compression, so the depth
  behaviour should be re-checked on hardware.
- The capture method cannot read the HAL debug console, so startup and
  shutdown diagnostics are not part of the recorded evidence.
