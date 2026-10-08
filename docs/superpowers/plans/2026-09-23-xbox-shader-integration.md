# Xbox shader integration implementation plan

> Execute inline using executing-plans and TDD. User requests continuous work;
> do not pause for routine checkpoints. No worktrees, commits, independent
> reviewers or computer-use. Preserve dirty changes and legacy rendering.

**Goal:** Finish the approved HLSL/Cg backend through authored runtime and xemu acceptance.

**Spec:** ../specs/2026-09-23-xbox-hlsl-cg-backend-design.md

**Architecture:** Preserve the verified frontend and typed adapter. Extract
compiler-owned C output with a host harness into bounded versioned data; native
consumers validate the entire artifact before GPU writes. Backend registration
and runtime activation remain separate until the latter is verified.

**Tech stack:** .NET 9, pinned shaderc/SPIRV-Cross/Cg/nxdk, C++17, PowerShell.

**Current checkpoint (2026-09-24):** compiler, artifact, cooker and runtime work
is implemented and scoped automated suites pass. Diagnostic/authored builds pass;
the exact final authored ISO runs in user-session xemu and remained responsive
for 157 seconds without assertion/fatal log matches. User visual confirmation
is the remaining acceptance gate. Full shared editor tests have the separately
documented unrelated CpuProfileSink compile blocker. Detailed evidence is in
`.xbox/verification/hlsl-integration-progress.md` and the compatibility report;
the original checklist below is retained as the implementation brief.

## Constraints and review focus

- Build evidence stays in ignored repository .xbox; no compiler text rewriting.
- Malformed length, version, stage, method or incomplete baseline must fail closed.
- A failed compile must not reuse an old binary; include/define closure is part of identity.
- Row-vector matrices, reserved viewport constants and normalized UV conversion need numerical tests.
- Shader errors do not silently select legacy rendering; legacy is an explicit option.
- Acceptance includes native diagnostic/authored builds, real xemu run and user visual feedback.

## 1. Artifact and extraction boundary

- [ ] Add native XboxShaderArtifact header/source for little-endian HXS1 data.
  Header: magic/version/stage/payload word count/ABI/checksum, followed by 32-byte
  source, toolchain and entry hashes, and two zero reserved words (128 bytes).
  Pixel payload is method/value pairs including the verified baseline; vertex
  payload is complete 16-byte instructions (maximum 136).
- [ ] Add host tests for truncated headers/every payload truncation, wrong version,
  foreign stage, oversized counts, checksum errors, forbidden GPU methods,
  missing baseline and valid round trips. Observe failure before implementation.
- [ ] Add a host extraction harness using actual generated includes, never a C parser.
  Pass immutable hashes from the driver. Validate before publishing .hxsb files.
- [ ] Run actual four-stage compilation, extracted artifact tests and existing native suite.

## 2. Shared target and builder backend

- [ ] Inspect indexed shared interfaces and coverage; read exact current sources.
- [ ] Add Xbox target preserving existing numeric values, registry contributor and
  explicit editor target resolution. Test target selection and registration.
- [ ] Move reusable compilation into builder/shaders with include/define resolution,
  dependency identity, process cancellation/timeout and named unsupported-feature diagnostics.
- [ ] Test valid/invalid requests and deterministic binary results with actual tools.

## 3. Runtime ABI and authored integration

- [ ] Add typed viewport/UV adapter ABI, pin reflected registers and test a non-symmetric
  transform (2,3,5,1) -> (75,86,97,108), viewport mapping and texture extents.
- [ ] Load validated artifacts into native shader ownership; bind constants/attributes,
  textures and shader commands per draw, with no partial GPU writes on malformed data.
- [ ] Wire cooked shader materials through authored rendering and explicit profile
  selection while retaining the existing legacy profile. Test alternating shaders,
  changed constants/textures and shared model/material ownership.
- [ ] Build diagnostic and authored images under .xbox; run native/shared regression suites.

## 4. Acceptance audit

- [ ] Launch exact new ISO hash in user's session using PowerShell snapshot mode.
- [ ] Verify live emulator/log state for at least 60 seconds without the prior assertion.
- [ ] Request visual confirmation only when necessary; do not capture desktop.
- [ ] Audit every spec requirement; record remaining gaps, never call host compilation
  runtime proof. Keep the goal active until all required work is verified.
