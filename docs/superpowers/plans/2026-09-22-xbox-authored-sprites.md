# Xbox Authored Sprite Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Draw authored 2D sprites with cooked Xbox textures in the Original Xbox authored-scene runtime.

**Architecture:** Decode the shared cooked texture formats into a host-testable pixel/layout representation, then upload each texture once into GPU-visible memory owned by `XboxRuntimeTexture`. Extend the existing raster renderer with a screen-space textured-quad pass; explicitly invoke the 2D manager after `Core::Draw()` so it traverses camera queues and submits sprites before presentation.

**Tech Stack:** C++17, nxdk/pbkit/NV2A, existing `vp20compiler` and `fp20compiler`, GNU Make/Docker, PowerShell, xemu, and the existing CPU native-contract harness.

**Spec:** `docs/superpowers/specs/2026-09-22-xbox-authored-sprites-design.md`

## Global Constraints

- Work in `C:/dev/helworks/helengine-xbox` on the main checkout; do not create or use worktrees.
- Preserve the clean starting tree apart from this plan/spec and the scoped sprite implementation.
- Keep native platform code at `-std=c++17 -Wall -Wextra -Werror` with exceptions disabled.
- Accept `Rgba32`, `Rgba4444`, `Indexed4` and `Indexed8`; reject `GxRgb5A3` with a specific diagnostic.
- Allocate texture storage during texture creation, never in the sprite/frame submission loop.
- Keep textures alive through GPU drain; release them through Core disposal after the host has drained.
- Implement sprites only; text, fonts, rounded rectangles, textured 3D materials and runtime texture updates remain unsupported.
- Do not modify shared engine/editor repositories or generated Core sources.
- Record actual NV2A texture format, addressing, sampling and upload behavior in `docs/PlatformNotes.md` from the pinned nxdk sources.
- Do not claim visual success from a successful XBE build; record emulator and physical-hardware evidence separately.

## Review Focus

- Dimension/payload arithmetic overflow or zero extent must reject an asset before allocation. Pin this in Task 1's invalid-input checks.
- Indexed4 stores the first texel in the low nibble; odd pixel counts must not read a nonexistent second index. Pin this in Task 1's packed-index checks.
- Indexed palette indices outside the available palette must reject the texture rather than sample arbitrary memory. Pin this in Task 1's palette checks.
- Non-power-of-two texture extents and source rectangles must map to the real texel bounds if the GPU layout pads them. Pin this in Task 1's layout checks and Task 3's UV checks.
- NaN, infinity, non-positive sprite size/scale, and a foreign or disposed runtime texture must produce no draw and a bounded diagnostic. Pin input geometry checks in Task 3 and renderer validation in Task 4.

---

### Task 1: Decode shared texture source formats

**Files:**
- Create: `src/platform/xbox/XboxTextureFormat.hpp`
- Create: `src/platform/xbox/XboxTextureFormat.cpp`
- Modify: `tests/native_contract_tests.cpp`
- Modify: `scripts/test-native.ps1`

**Interfaces:**
- Consumes: generated `TextureAsset` scalar fields and byte arrays, copied into a generated-Core-independent view.
- Produces: `XboxTextureSourceView`, `XboxRgbaPixel`, `XboxTextureStatus`, and `DecodeTextureToRgba8(const XboxTextureSourceView&, XboxRgbaPixel*, size_t)`. The decoder has no nxdk/generated-Core dependency, allocates nothing, and is `noexcept`.

- [x] **Step 1: Declare generated-Core-independent source and decoded-pixel types.** In `XboxTextureFormat.hpp`, define `XboxTextureColorFormat { Rgba32=0, Rgba4444=1, Indexed4=2, Indexed8=3 }`, `XboxTextureAlphaPrecision { Opaque=0, Binary=1, A4=2, A8=3 }`, `XboxTextureSourceView { uint32_t Width, Height; XboxTextureColorFormat ColorFormat; XboxTextureAlphaPrecision AlphaPrecision; const uint8_t* Colors; size_t ColorsLength; const uint8_t* PaletteColors; size_t PaletteLength; }`, `XboxRgbaPixel { uint8_t R, G, B, A; }`, and `XboxTextureStatus { Ok, InvalidExtent, SizeOverflow, UnsupportedFormat, MalformedPayload, InvalidPalette, InsufficientOutput }`. Declare `XboxTextureStatus DecodeTextureToRgba8(const XboxTextureSourceView& source, XboxRgbaPixel* output, size_t outputPixelCapacity) noexcept`.
- [x] **Step 2: Add failing decoder checks.** In `native_contract_tests.cpp`, construct source views for one RGBA32 texel, one little-endian RGBA4444 texel, an Indexed8 lookup, and Indexed4 low-nibble-first order. Assert exact output channels and status. Add alpha checks for `Opaque`, `Binary`, `A4` and `A8`; rejection checks for odd Indexed4 short input, short RGBA input, short palette, invalid palette index, zero dimensions, unsupported format and size overflow. For example:

```cpp
const std::uint8_t rgba[] = {0x12, 0x34, 0x56, 0x78};
const XboxTextureSourceView source{1, 1, XboxTextureColorFormat::Rgba32,
    XboxTextureAlphaPrecision::A8, rgba, sizeof(rgba), nullptr, 0};
XboxRgbaPixel output[1]{};
CHECK(DecodeTextureToRgba8(source, output, 1) == XboxTextureStatus::Ok);
CHECK(output[0].R == 0x12 && output[0].G == 0x34 &&
      output[0].B == 0x56 && output[0].A == 0x78);
```

- [x] **Step 3: Run the CPU contract harness and confirm the new assertions fail for the missing decoder.** Run `scripts/test-native.ps1` with a fresh output directory under `C:\dev\helworks\builds\helengine-xbox\v02\sprites-native-red`; the expected red result is a link failure naming the declared but not-yet-defined decoder. A Docker, compiler-setup or output-directory error is not the expected red result.
- [x] **Step 4: Implement payload validation and source decoding.** Check positive dimensions, overflow-safe pixel/byte counts, output capacity and source spans before reading. Decode RGBA32 channels directly; expand each RGBA4444 nibble by multiplying by 17; decode Indexed8 bytes directly; decode Indexed4 low nibble before high nibble. Palette entries are four RGBA bytes each. Apply alpha precision (`Opaque` to 255, `Binary` threshold 128, `A4` nibble replication, `A8` unchanged) to decoded alpha. Reject the platform-specific `GxRgb5A3` format and out-of-range palette references with distinct statuses.
- [x] **Step 5: Compile and run the host harness.** Update `test-native.ps1` to compile `XboxTextureFormat.cpp`; run it at a fresh output root and require every known-color, alpha and rejection check to pass.
- [x] **Step 6: Commit the CPU texture decoding checkpoint.** Stage only `XboxTextureFormat.hpp/.cpp`, native-contract test changes and the test-script source list; commit as `Add Xbox cooked texture decoding`.

### Task 2: Give Xbox runtime textures GPU ownership

**Files:**
- Create: `src/platform/xbox/XboxTextureUpload.hpp`
- Create: `src/platform/xbox/XboxTextureUpload.cpp`
- Modify: `src/platform/xbox/XboxRuntimeTexture.hpp`
- Modify: `src/platform/xbox/XboxRuntimeTexture.cpp`
- Modify: `src/platform/xbox/XboxRenderManager2D.cpp`
- Modify: `tests/native_contract_tests.cpp`
- Modify: `scripts/test-native.ps1`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `XboxTextureSourceView` and `DecodeTextureToRgba8(...)` from Task 1; borrowed generated `TextureAsset` data.
- Produces: `XboxRuntimeTexture::Create(const TextureAsset&, const char*& reason)`, `uint32_t GpuOffset() const`, `const XboxTextureLayout& Layout() const`, and idempotent GPU-memory release through `Dispose()`.

- [x] **Step 1: Resolve the pinned nxdk sources.** Current inspection found `$env:NXDK_DIR` unset, `nxdk-cxx` unavailable and the documented nxdk path absent. Search the known Xbox build/dependency roots for pinned commit `29638d0b001f179b73c3513489af10ddc2986216`; if it is not available, stop here and ask Helena for its location. Do not download or substitute another SDK.
- [x] **Step 2: Record the actual texture hardware contract.** Inspect the pinned texture samples/headers and document legal extents, pixel format, swizzle/address calculation, sampler clamp/filter registers, upload constraints and allocation alignment in `docs/PlatformNotes.md` with source paths.
- [x] **Step 3: Add failing upload-layout checks.** Extend `native_contract_tests.cpp` to verify the pinned texture dimensions and pixel-address mapping, required padding edge extension, byte count, and short-output/unsupported-extent statuses using synthetic pixel grids.
- [x] **Step 4: Implement the SDK-independent upload layout.** In `XboxTextureUpload.hpp/.cpp`, define `XboxTextureLayout { uint32_t RealWidth, RealHeight, StorageWidth, StorageHeight, BytesPerTexel, HardwareFormat; size_t ByteCount; }`, add `UnsupportedExtent` to `XboxTextureStatus`, and declare `BuildXboxTextureLayout(uint32_t,uint32_t,XboxTextureLayout&)` plus `ConvertTextureToNv2a(const XboxTextureSourceView&, const XboxRgbaPixel*, const XboxTextureLayout&, uint8_t*, size_t)`. Write the target order/address into ordinary caller memory, overflow-check every size, and do not assume power-of-two padding unless the pinned SDK requires it.
- [x] **Step 5: Run the CPU contract harness and confirm layout checks pass.** Add `XboxTextureUpload.cpp` to `test-native.ps1`; run at a fresh output root and require format decode, layout, padding and failure cases to pass.
- [x] **Step 6: Validate before GPU allocation.** Adapt generated `TextureAsset` fields into `XboxTextureSourceView`, allocate temporary decoded/output CPU memory with a non-throwing allocation path, run decode/layout/conversion, and return a reason before GPU allocation for every invalid source.
- [x] **Step 7: Implement GPU storage creation using the pinned API.** Allocate the final layout in GPU-visible contiguous memory with the reviewed alignment/range/flags, then copy the prepared hardware payload sequentially. Retain real/stored extents, hardware format and GPU offset; set generated RuntimeTexture dimensions to the real extent.
- [x] **Step 8: Make partial failure cleanup explicit.** Hold the new texture in a locally owned pointer until decode, conversion, allocation and upload setup succeed. Any failed step frees only the allocation already acquired; successful construction transfers ownership to the caller.
- [x] **Step 9: Release GPU memory after drain.** Make `Dispose()` idempotently release the contiguous block and then call `RuntimeTexture::Dispose()`. The existing host order is `video.Drain()` then `core->Dispose()`; retain that order.
- [x] **Step 10: Route cooked and raw texture creation through the same constructor.** `BuildTextureFromCooked` continues to deserialize `TextureAsset`, creates the runtime texture while the borrowed asset is alive, then destroys the asset. Implement `BuildTextureFromRaw` through the same decoder so Core's one-pixel fallback texture works.
- [ ] **Step 11: Compile the authored XBE against a fresh generated Core/content pair.** Use `scripts/build.ps1 -Mode Authored` with explicit roots supplied by the active editor build, write artifacts beneath a new `C:\dev\helworks\builds\helengine-xbox\v02\sprites-texture-debug` directory, and require the normal XBE/XISO validations plus a clean native warning build.
- [ ] **Step 12: Commit the runtime texture checkpoint.** Stage the two upload files, runtime texture files, Makefile source list and PlatformNotes texture-upload findings; commit as `Upload cooked Xbox textures to NV2A memory`.

### Task 3: Resolve sprite quads in CPU screen space

**Files:**
- Create: `src/platform/xbox/XboxSpriteGeometry.hpp`
- Create: `src/platform/xbox/XboxSpriteGeometry.cpp`
- Modify: `tests/native_contract_tests.cpp`
- Modify: `scripts/test-native.ps1`

**Interfaces:**
- Consumes: generated sprite values copied into a compact native view, plus the runtime texture real/stored extents from Task 2.
- Produces: `XboxSpriteInput { float X, Y, Width, Height, RotationRadians; float SourceX, SourceY, SourceWidth, SourceHeight; uint32_t RealTextureWidth, RealTextureHeight, StoredTextureWidth, StoredTextureHeight; uint8_t Tint[4]; }`, `XboxSpriteVertex { float X, Y, U, V, R, G, B, A; }`, `XboxSpriteQuad { XboxSpriteVertex Vertices[4]; }`, `XboxSpriteStatus`, and `TryBuildXboxSpriteQuad(const XboxSpriteInput&, XboxSpriteQuad&)`, all without generated Core or nxdk headers.

- [x] **Step 1: Define the plain sprite input/output records.** Input carries screen-space position, destination size, Z rotation, normalized source rectangle, tint bytes, and real/stored texture dimensions. Output is four vertices in stable top-left, top-right, bottom-right, bottom-left order with screen coordinates, normalized stored-texture UVs and float RGBA tint.
- [x] **Step 2: Add failing quad-resolution checks.** Checked a 16x8 texture at `(10,20)` with destination `32x16`, identity rotation, full source rect, and white tint; half-source rect against actual stored extent; 90-degree center rotation and already-scaled dimensions.
- [x] **Step 3: Add invalid-input checks.** Tests reject zero extents, non-finite transform/UV values, out-of-range/empty source rectangles and non-positive destination dimensions; failure does not modify output.
- [x] **Step 4: Implement `TryBuildXboxSpriteQuad(...)`.** Resolves corners around the destination center, rotates, remaps UVs by real/stored extent, converts tint channels to `[0,1]`, and assigns outputs only on success.
- [x] **Step 5: Compile and run the host harness.** Added `XboxSpriteGeometry.cpp` to `test-native.ps1`; fresh run passed all 713 native contract checks.
- [ ] **Step 6: Commit the CPU quad checkpoint.** Stage the new geometry helper and native-contract harness changes; commit as `Resolve Xbox authored sprite quads`.

### Task 4: Submit textured sprite quads through NV2A

**Files:**
- Modify: `src/platform/xbox/XboxRasterRenderer.hpp`
- Modify: `src/platform/xbox/XboxRasterRenderer.cpp`
- Create: `src/platform/xbox/shaders/sprite.vsh`
- Create: `src/platform/xbox/shaders/sprite.psh`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `XboxSpriteVertex[4]`, the GPU texture descriptor from Task 2, and initialized video dimensions.
- Produces: `XboxRasterRenderer::DrawSprite(const XboxSpriteQuad&, const XboxRuntimeTexture&)` and a one-time `BeginSpritePass()` state setup.

- [x] **Step 1: Define the sprite vertex fetch layout.** Use two screen-space position floats, two texture-coordinate floats and four tint floats; the vertex program supplies z=0 and w=1. Add compile-time size/offset assertions and map each attribute slot to the sprite vertex shader.
- [x] **Step 2: Add the sprite vertex program.** Pass screen-space positions through directly (already in pbkit's viewport coordinate convention), set z/w constants, and pass UV and tint to the pixel stage. Compiled with the pinned `vp20compiler`.
- [x] **Step 3: Add the single-stage pixel combiner.** Sample the bound texture and multiply by interpolated tint; preserve sampled alpha for source-alpha blending. Compiled with the pinned `fp20compiler`; Makefile generates the shader includes in the staged source tree.
- [x] **Step 4: Add explicit sprite-pass state.** Bind the single texture stage and sampler, enable source-alpha / one-minus-source-alpha blending, disable depth test/write and culling, and enable full color writes. Register values are from Task 2's pinned-source worksheet.
- [x] **Step 5: Add no-allocation draw submission.** Bind the texture GPU address and format, point vertex attributes at persistent quad storage, submit two triangles in stable order and update renderer draw/triangle counters. The authored renderer uses a bounded 512-quad GPU vertex buffer and allocates no per-sprite resource.
- [x] **Step 6: Reissue full 3D state on the next frame.** `BeginAuthoredFrame()` restores the 3D vertex program/combiner and render state; 3D vertex attributes are rebound before each authored mesh.
- [ ] **Step 7: Build Diagnostic and Authored modes.** Run one fresh Debug authored build and one existing native probe build to prove the added shader path compiles without changing the cube path; preserve the build logs under their invocation roots.
- [ ] **Step 8: Commit the raster checkpoint.** Stage only raster/shader/Makefile/PlatformNotes files; commit as `Add Xbox textured sprite raster pass`.

### Task 5: Traverse 2D queues and sequence the host pass

**Files:**
- Modify: `src/platform/xbox/XboxRenderManager2D.hpp`
- Modify: `src/platform/xbox/XboxRenderManager2D.cpp`
- Modify: `src/platform/xbox/XboxRuntimeHost.cpp`

**Interfaces:**
- Consumes: `XboxRasterRenderer::BeginSpritePass()` / `DrawSprite(...)`, `TryBuildXboxSpriteQuad(...)`, and the generated `Core`, camera and 2D drawable contracts.
- Produces: an active `XboxRenderManager2D::Draw()` that visits camera `IRenderQueue2D` entries with `VisitOrdered`, filters disabled owners, and dispatches sprite drawables to the raster renderer.

- [x] **Step 1: Install the shared raster renderer on the 2D manager.** Added `InstallRenderer(XboxRasterRenderer*)`; a missing renderer is reported once only when a camera queue is nonempty.
- [x] **Step 2: Implement the 2D drawable visitor.** Visits each camera queue with `VisitOrdered`, skips null/disabled owners and calls enabled `IDrawable2D::Draw()` without reordering or adding camera/layer policy.
- [x] **Step 3: Resolve each sprite into the CPU quad.** Validates owner/native texture state; uses world position, scale and Z-projected orientation, explicit positive size or real texture-size fallback, source rectangle and tint. Invalid geometry/texture reasons are bounded to one diagnostic per distinct error.
- [x] **Step 4: Run the pass after Core's 3D draw.** Installs the shared renderer on both managers and invokes `render2D.Draw()` after `core->Draw()` but before overlay/presentation.
- [ ] **Step 5: Build a fresh authored Debug image.** Confirm the generated Core links, cooked sprites load, the XISO includes the content tree, and all warnings remain errors. Use the active editor's generated-Core and cooked-content roots explicitly.
- [ ] **Step 6: Commit queue traversal and host sequencing.** Stage the 2D manager, runtime host and PlatformNotes changes; commit as `Draw authored Xbox sprites after 3D`.

### Task 6: Verify the rendered overlay and document support

**Files:**
- Modify: `README.md`
- Modify: `docs/PlatformNotes.md`

**Interfaces:**
- Consumes: the authored sprite build from Task 5 and the repository's explicit xemu capture/launch scripts.
- Produces: recorded build/runtime evidence, supported-format list and explicit unsupported 2D feature list.

- [ ] **Step 1: Run the authored image in the configured xemu profile.** Use the exact fresh ISO path, current matching build-info record and isolated config; do not modify the shared xemu config.
- [ ] **Step 2: Inspect the emulated framebuffer for known-color sprite content.** Confirm top-left UV orientation, full-image sampling, atlas crop, tint/alpha blending, overlap order, position/scale/rotation, and unchanged 3D background. Use the repository guest-memory capture path; do not capture the host desktop.
- [ ] **Step 3: Exercise source payload formats in authored content.** Render one known-color texture for each accepted source format and confirm `GxRgb5A3` plus malformed palette/payloads fail at load with the named diagnostic.
- [ ] **Step 4: Check shutdown ownership.** Use the finite-frame authored run to confirm the GPU drain precedes Core texture release and that renderer allocations/release counters balance after shutdown.
- [ ] **Step 5: Update `README.md` and `PlatformNotes.md`.** Document exact source format support, build invocation, sprite transforms/UV convention, NV2A texture settings, alpha state, memory ownership, xemu evidence and physical-hardware status.
- [ ] **Step 6: Review final repository state.** Inspect `git diff --check`, `git status --short --branch`, and the scoped diff. Confirm no worktree was created and no generated-Core/shared-engine file changed.
- [ ] **Step 7: Commit the documentation and evidence checkpoint.** Stage only README, PlatformNotes, and any evidence metadata intentionally stored in-repo; commit as `Document Xbox authored sprite support`.

## Handoff

The nxdk checkout is absent from the location currently recorded in
`docs/PlatformNotes.md`. Task 1 Step 1 must locate the already pinned checkout
before any NV2A format or register work; if none is available locally, stop at
that step and ask Helena for the SDK location. Do not download a different
revision or infer register values from desktop Direct3D.
