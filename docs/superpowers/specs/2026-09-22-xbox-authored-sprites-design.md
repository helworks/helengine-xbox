# Xbox Authored Sprite Rendering Design

## Goal

Render authored `ISpriteDrawable2D` sprites in the Xbox authored-scene runtime.
Use the cooked texture assets already packaged by the build and preserve the
engine's 2D queue order, sprite transform, source rectangle, size and tint.

This is the next authored-runtime increment after cooked textures became
loadable. The current `XboxRuntimeTexture` only records dimensions, and
`XboxRenderManager2D::DrawSprite` records the feature as unsupported.

## Scope

Included:

- Materialize the generic cooked texture formats emitted by the shared texture
  processor: `Rgba32`, `Rgba4444`, `Indexed4` and `Indexed8`. Convert them once
  during texture creation to an NV2A-sampleable representation. Reject
  `GxRgb5A3`, which is stored in a platform-specific tiled layout, with a named
  diagnostic.
- Keep each texture's actual extent separate from any padded GPU storage
  extent. Validate source payload lengths, dimensions and palette references
  before allocation or upload.
- Visit each camera's ordered 2D render queue from the Xbox 2D manager, honoring
  disabled entity hierarchies and routing sprite components through their
  existing `Draw()` method.
- Have the authored host call `render2D.Draw()` immediately after `core->Draw()`
  and before presentation. The generated `Core::Draw()` currently calls only
  `RenderManager3D.Draw()`, so the 2D pass must be explicitly sequenced by the
  platform host.
- Submit sprites as screen-space textured quads after the 3D draws. Use world
  position and scale, the sprite's explicit size or texture-size fallback,
  world Z rotation, normalized source rectangle, and RGBA tint.
- Enable alpha blending for the overlay, disable depth writes/tests for that
  pass, and re-establish the 3D pass state at the beginning of every frame.
- Keep GPU texture resources alive until the existing post-drain Core disposal
  path releases them. Include the Core's raw one-pixel fallback texture path so
  default `RenderManager2D` textures can be created.

Not included:

- Text, fonts, rounded rectangles, or generalized 2D commands/effects.
- Textured 3D materials, material cooker changes, or changes to the scene
  compatibility contract.
- Runtime texture-region updates. The existing Core API validates these calls;
  Xbox continues to reject them explicitly.
- New serialized texture formats or changes to shared engine/editor projects.
- Batching or a general-purpose texture cache beyond existing Core ownership.

## Design

### Texture materialization

`XboxRuntimeTexture` becomes the owner of the NV2A-visible storage and retains
the real width and height on the generated `RuntimeTexture` base. A small CPU
conversion helper validates and decodes each supported generic format to the
chosen NV2A pixel format and layout. The conversion also handles the shared
processor's alpha-precision rules and palette payloads. If the GPU layout needs
power-of-two padding, edge texels are extended and sprite UVs are scaled by the
ratio between the real and stored extents.

Texture creation has a clear failure result: unsupported format, malformed
payload, out-of-range palette index, unsupported extent, or contiguous-memory
allocation failure returns no texture with a specific reason. A failed load
must not leave an allocated GPU block behind. Upload occurs once at texture
creation; the frame path only binds and samples the resident texture.

The exact NV2A texture format, address layout, sampler registers, and upload
sequence must be derived from the pinned nxdk revision and recorded in
`docs/PlatformNotes.md`. The current checkout does not contain the nxdk tree at
the path cited by the older notes, so that source location must be resolved
before implementation relies on SDK examples.

### Sprite submission

After `core->Draw()` finishes the 3D pass, the authored host calls
`render2D.Draw()` before overlay text and `XboxVideo::EndFrame()`. The 2D
manager obtains the Core object manager's camera list and visits each camera's
`IRenderQueue2D` in its existing ordered traversal. It skips disabled owners
consistently with the engine visitor. Each sprite call resolves its Xbox
runtime texture and owner world transform, then submits a quad to the existing
Xbox raster renderer.

The quad uses top-left screen coordinates. Its size uses the positive authored
`Size` component where present, otherwise the real texture dimensions, then
multiplies by world X/Y scale. World Z rotation rotates the quad about its
center. The engine source rectangle is normalized against the real texture and
is remapped to the stored GPU extent. Tint multiplies sampled RGBA. Invalid or
foreign textures and invalid geometry are reported through the existing
bounded diagnostics and do not issue a partial draw.

The renderer pushes overlay state explicitly: one texture stage, alpha blend,
no depth test/write, and color writes enabled. Sprite draws preserve queue
order. The subsequent frame starts by pushing all 3D state again, so no 2D
state leaks into the authored 3D pass.

### Ownership and shutdown

The generated Core owns textures through `RenderManager2D`. The host already
drains the GPU before `core->Dispose()`; texture disposal releases NV2A-visible
memory only after that drain. Partial allocation/upload failures release any
allocation made before returning failure. No texture storage is allocated in
the per-frame draw path.

## Verification

The implementation should be checked against authored content in xemu with
known-color textures for every accepted source format, a full-texture sample,
an atlas source rectangle, alpha and tint, overlapping sprites in queue order,
entity rotation/scale, and an unchanged 3D scene behind the overlay. Confirm
malformed and unsupported texture assets fail with a named load diagnostic,
and confirm GPU textures remain allocated until the host's post-drain Core
disposal. Record build, emulator and physical-hardware evidence separately in
`docs/PlatformNotes.md`; do not treat a successful XBE build as visual proof.

## Expected files

- `src/platform/xbox/XboxRuntimeTexture.hpp/.cpp`
- A host-buildable texture conversion/layout helper under
  `src/platform/xbox/` if needed to isolate format math from nxdk headers
- `src/platform/xbox/XboxRenderManager2D.hpp/.cpp`
- `src/platform/xbox/XboxRasterRenderer.hpp/.cpp` and the relevant shader(s)
- `src/platform/xbox/XboxRuntimeHost.cpp` for explicit pass ordering
- `Makefile`, `README.md`, and `docs/PlatformNotes.md`

The material cooker, 3D runtime material/model path, and shared engine/editor
repositories are not part of this increment.
