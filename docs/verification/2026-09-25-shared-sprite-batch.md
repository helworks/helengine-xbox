# Shared Textured SpriteBatch integration

User subsequently confirmed sprites are visible after the projection fix. The next milestone adds a CPU-textured RoundedShape fallback; see [rounded fallback verification](2026-09-25-rounded-shape-fallback.md). Scope/remaining-acceptance notes below describe the earlier Textured-only milestone.

## Follow-up: user rejected the first image (cube only)

The previous HUD was 3D-only and was not sprite evidence. Added per-frame 2D queue, sprite-entry and successful batch-submission counters. HelenUI OCR of `sprite-diag2` reads `SPRITES PRESENT 0006` and `BATCHES PRESENT 0006`: emission reaches the native submission boundary.

The actual shared Cg output (`.xbox/shader-jobs/a87b04176e3a490f8c9091df3b193d9a/shader.cg` and `shader.asm`) computes DP4 against column-major buffer columns. SetCamera transposed the Core matrix, then the material transposed it again, uploading rows instead of columns. This corrupts clip coordinates including W. `XboxBatch2DProjection.hpp` now preserves Core rows for the material's single packing transpose. The regression in `batch2d_submission_tests.cpp` failed with the old conversion and passes with the correction (pixel 160,120 -> clip -.5,.5,0,1).

Full native suite passed again: `.xbox/verification/sprite-projection-native/native-tests.log`. Native build `.xbox/verification/sprite-projection-fix.log` exited 0. Current image is `.xbox/builds/sprite-projection-fix/artifacts/game.iso`, xemu PID 51856 in user session 1. HelenUI `projection-ocr.json` reads six sprite calls, six successful batch submissions and `SKIP ROUNDED RECT` (known unsupported scope). Visual sprite correctness still awaits user acceptance; OCR counters do not establish visible pixels.

## Scope

Xbox reuses Core's `Batch2DWriter`, `Batch2DDrawableEmitter`, camera projection and ancestor clip-chain builder. The platform only loads the selected shader package and submits its borrowed batches to NV2A. The canonical `SpriteBatchShader.hlsl` is unchanged. `RoundedShape` and text rendering are not enabled by this milestone. Legacy material selection remains unchanged.

The shader profile declares only `SpriteBatchShader.vs` / `SpriteBatchShader.ps`, variant `Textured`, through the shared renderer-dependency provider. Cooking resolves the shared variant catalog, retains the selected variant in the package, and rejects unsupported requests before mutating valid output. POSITION0 is normalized to the NV2A POSITION route; position and packed texture inputs retain float4 widths through native artifact validation.

The Xbox codegen contract now requests the existing `HELENGINE_CODEGEN_FATAL_ONLY` shared branch, matching its disabled C++ exceptions. No shared repository or generated source was edited.

Runtime uses the typed `ShaderAssetBinarySerializer` on `cooked/shaders/spritebatchshader.xbox.hasset`, copies the native programs before releasing the deserialized asset, and binds the shared camera buffer. Submission copies the six-float4 vertex layout to persistent GPU-visible storage. Indices are copied into the pushbuffer. Runs retain source order, straight-alpha blending and integer scissors. The existing 512-quad frame limit remains explicit; the writer flushes chunks of 128 quads. EndFrame drains the GPU before next-frame storage reuse.

## Verification

- Real shared shader compilation and physical pair validation: `.xbox/verification/sprite-batch-full-compiler.log`, exit 0. Includes the existing compiler suite.
- Real cooker test (`--cook-sprite`): serialized Textured package, duplicate dependency collapse, unsupported variants rejected without package mutation, shader-only dependency declaration and legacy exclusion; exit 0. Output `.xbox/verification/sprite-cook-51264812aba4440f98749c160c43b910`.
- Existing custom-material cook and failure/legacy regression: `.xbox/verification/sprite-default-cook-regression.log`, exit 0.
- Full native suite: `.xbox/verification/sprite-batch-submission-full/native-tests.log`, exit 0; 722 existing contracts plus shader, texture, upload and new batch validation tests.
- Actual Xbox-generated shared writer test: `tests/generated_batch2d_writer_tests.cpp`, run using `.xbox/verification/run-generated-batch-test.sh`; exit 0. Checks copied vertex values, capacity flush, camera-change ordering, exact indices and submission ABI.
- Fresh editor/codegen/cook: `.xbox/verification/sprite-shared-contract2.log`, exit 0, 39 packaged artifacts. The preceding run demonstrated the missing fatal-only symbol via CPP1001; corrected profile test and regeneration passed.
- Final native build: `.xbox/verification/sprite-shared-native4.log`, exit 0. ISO `.xbox/builds/sprite-shared-native4/artifacts/game.iso`, SHA256 `b46ece3b2c462590bb8f783520822ae3fda8bf1b12ba147c817b701424d82e7e`. XBE SHA256 `08ec0ed5e5ab0e052deb7ad85ad3a540bc6b0ff2bc8b1de155181b324ff24f0f`.

## Runtime evidence and remaining acceptance

HelenUI OCR exposed and preserved two integration failures: the missing `cooked/` path prefix (`ocr1.json`) and generic deserializer rejection (`ocr4.json`). Both were corrected at the reader boundary, without changing the package format.

Final image launched in user session 1, xemu PID 52036, 2026-09-25 14:24:09 local. HelenUI-only capture/OCR `.xbox/emulator-shared-batch/frame5.png` / `ocr5.json` shows the authored HUD without fatal. The OCR runtime guard passes. A PowerShell array-pipeline mistake in target discovery caused false window-absence reports; corrected enumeration uses a variable before filtering individual targets.

This proves execution reached the scene HUD, not visual correctness of sprite textures, tint, alpha or clipping. User confirmation remains required. No whole-branch or independent review is claimed. Native builds retain the existing nxdk translator/linker warnings; the initial managed dependency build reported existing shared warnings and unavailable NuGet vulnerability data.

A second HelenUI capture (`frame6.png` / `ocr6.json`) again shows the authored HUD and passes the fatal guard. Frame hashes differ: frame5 `DB60FA328F6B9D8892C8F3D4B2B778D2F939CB2B49EB452C790EA0A1C9125AB6`, frame6 `A275CFE9D440EDE4866950B5989E23DEB30AB0CDFA5FFC6BE3B852422D4A7E01`. The test-owned HelenUI session/service is cleaned up; xemu is left open for user acceptance.
