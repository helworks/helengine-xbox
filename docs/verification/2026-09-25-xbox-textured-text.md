# Xbox text rendering through shared SpriteBatch

`XboxRenderManager2D::DrawText` now validates the drawable, hierarchy and font atlas, then calls the generated shared `Batch2DDrawableEmitter::EmitText` with the current camera scissor and atlas texture. The shared emitter supplies glyph layout, line spacing, alignment, wrapping, shadow/outline passes, atlas UVs and vertex colors. Glyph quads enter the same ordered Textured runs as sprites/shapes and use the existing NV2A texture path. Empty strings and disabled hierarchy emit nothing; absent, foreign or disposed atlases are reported and skipped.

The HUD reports per-frame nonempty text drawables alongside sprite, batch and rounded counts.

Build: `.xbox/verification/text-native2-build.log`, exit 0, authored ISO `.xbox/builds/text-native2/artifacts/game.iso` (SHA256 `35d02672bfc571cfe30be939fbe51190e530723499580f9f916afc8ab15180e6`). The first compile caught the missing complete `ITextDrawable2D` type; adding its generated header resolved the compiler errors. No tests were added or run in this pass.

Final xemu PID 40692, session 1. HelenUI OCR capture `.xbox/emulator-shared-batch/text-final.png` / `text-final-ocr.json` reads 6 sprites, 3 rounded shapes, 8 nonempty text drawables, 42 batches and the text “Cube Test”; no fatal is recognized. The emulator remains open for direct inspection.
