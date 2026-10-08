# Xbox RoundedShape CPU texture fallback

## Scope and implementation

User approved CPU rasterization into bounded cached textures, rendered by the existing shared Textured SpriteBatch. Shared HLSL, PS3 sources, generated Core and shader package selection are unchanged. This does not add native fp20 support for the RoundedShape shader variant.

`XboxRoundedRaster` evaluates the shared rounded-distance, outer/inner coverage and straight-alpha fill/border math at pixel centers. It preserves corner selection and normalizes radius and border width. Each axis is capped at 512 texels; larger shapes retain their authored geometry and may show reduced detail when enlarged.

`XboxRoundedTextureCache` owns up to 64 textures within 4 MiB of page-rounded GPU allocations. Keys include dimensions, normalized radius/border, corner mask and both RGBA colors, not position, rotation or scissor. Hits avoid rasterization. Any texture requested in the current frame is pinned, including textures in unflushed batches. Old unpinned entries may be evicted; frame reset relies on RuntimeHost's preceding EndFrame GPU drain. Cache exhaustion or texture creation failure explicitly skips/reports the shape instead of freeing an in-flight texture. Cache disposal follows the host's shutdown drain.

`XboxRoundedBatch` uses the generated shared geometry/writer with white vertex tint and full texture UVs. The manager retains ordered queue traversal and ancestor clipping. Position, size and Rotation follow the shared rounded emitter contract (no new parent-scale convention). Text rendering remains unsupported.

## Tests and builds

- Raster and cache tests were first run against stubs and failed on missing behavior, then passed after implementation.
- `tests/rounded_raster_tests.cpp`: rounded coverage, selected square corner, straight alpha, antialiased pixels, solid border, border wider than shape, half-coverage color mixing, normalization, invalid/nonfinite inputs and bounded resolution.
- `tests/rounded_cache_tests.cpp`: reuse, page budget, frame pinning, safe eviction, failed factory, invalid/oversized requests and destruction.
- `tests/generated_rounded_batch_tests.cpp` through `scripts/test-rounded-batch.sh`: actual generated writer/geometry, rotation, UVs, white tint, scissors, flush order and invalid geometry. Exit 0 after the initial stub failed.
- Full native suite `.xbox/verification/rounded-final/native-tests.log`: exit 0, 722 existing checks plus shader/batch/texture tests and new rounded tests.
- Final authored native build `.xbox/verification/rounded-native-final-build.log`: exit 0. Existing nxdk TEX0 mapping/linker warnings remain.
- ISO `.xbox/builds/rounded-native-final/artifacts/game.iso`, SHA256 `a77b273d7187e51749f7e847f41872ce5a162f2b9d145eb31d4485693c18c78d`.
- XBE SHA256 `de5f82d4875c81b03d857123f32b34d9e076a916e20c1220a581a0de9d52dd8a`.

## Runtime debugging and acceptance

First native image did not expose a readable HUD. Restoring window clipping did not help, so that experiment was removed. The prior user-accepted sprite image remained readable in the same HelenUI setup, ruling out a general capture failure. A temporary checkpoint before rasterization showed the first shape dimensions (172 x 75); the checkpoint after texture creation was not reached.

Root cause: the pinned nxdk PDCLib `lround` implementation is an assert-only stub. Host libc tests alone did not expose this. Linking the raster test against the actual nxdk lround source reproduced the assertion. Pixel conversion now clamps and rounds nonnegative channels without lround. The native test runner retains the nxdk-linked regression, which now passes. All temporary fatal checkpoints and the unsuccessful clip experiment were removed.

Final xemu PID 3944, user session 1. HelenUI window-only capture `.xbox/emulator-shared-batch/rounded-final.png` / `rounded-final-ocr.json` reads 6 sprite calls, 8 batch submissions, 3 rounded shapes and `SKIP TEXT`, without a recognized fatal. These counters establish execution, not visual correctness. User acceptance of rounded borders, corners, colors and transparency remains pending. The emulator is left open. No independent review, commit or worktree is claimed.
