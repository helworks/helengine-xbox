# Xbox HLSL to Cg compatibility gate — 2026-09-23

## Decision

**Compiler chain: PASS (four stages, two identical-output runs).**

**Authored COLOR0 interface: PASS with the separate reflected entry adapter.**

**Experimental pixel-stage transition: PASS with an engine-owned baseline.**
Cg alone still emits incomplete state. Production artifacts, runtime binding,
matrix/viewport/UV validation and emulator acceptance remain unimplemented.
Do not replace the working renderer on the strength of this host test.

No existing renderer, shared engine source, toolchain version, emulator
configuration or generated Core was changed by this gate.

## Approved baseline follow-up

### Artifact follow-up (ongoing integration)

Native `XboxShaderArtifact` now validates bounded HXS1 byte buffers before any
pixel register callback. A generic host extraction harness compiles each real
nxdk include, inserts the baseline for pixel stages and writes little-endian
`.hxsb` data. The 128-byte header records version, stage, ABI, word count,
non-cryptographic payload checksum and source/tool/entry SHA-256 identities.
These identities are provenance, not signatures or a complete production cache key.

Evidence: `.xbox/verification/hlsl-cg-artifacts-2` contains colour vertex/pixel
artifacts of 256/200 bytes and textured vertex/pixel artifacts of 272/216 bytes.
Actual deserialized pixel commands pass 120 colour/textured/colour sequences.
Malformed/truncated data, foreign versions/stages, forbidden methods and
incomplete baseline/active combiners are rejected; failure makes zero callback
writes. Permanent native tests cover this boundary in addition to 722 existing
checks and sprite/unlit transitions. Vertex validation covers instruction
framing/count/termination, not general microcode safety or GPU execution.

Builder registration, full dependency identity and reflected binding metadata,
viewport/UV ABI, runtime ownership and GPU binding remain work in progress.
The user requested continuous implementation; this is not the final backend.

### Reusable compiler and shared target follow-up

The frontend now lives in `shadercompiler/helengine.xbox.shadercompiler.csproj`;
the probe references that library. `XboxShaderToolchainCompiler` builds one
file or in-memory source request through real tools into a validated artifact.
It supports shaderc-selected includes, macros, active dependency identity,
process diagnostics, timeout/cancellation and uniquely owned Docker jobs.
Evidence: `hlsl-api-final` (four native stages, deterministic repeat and invalid
source), `hlsl-library-regression` (previous driver plus serialized replay),
and `hlsl-api-native-regression` (722 checks and both shader test suites).

The platform builder now contributes `XboxShaderBackend` to the shared registry.
Tests through the actual `ShaderCompileService` return typed matrix/interface/
texture metadata and native binary output; the backend opts out of the shared
root-only cache. Derivatives receive an explicit pre-Cg unsupported-operation
diagnostic. Evidence: `hlsl-backend-capabilities.log` and the compiled platform
contributor test.

### ABI2 and native binding progress

The typed vertex adapter now converts clip xyz to NV2A viewport coordinates,
preserves clip W and scales normalized UV into linear texture texels. Constants
c93/c94/c95 are reserved for extent/scale/offset. Native per-draw preparation
transposes the platform matrix and clears helper constants before setting these
reserved slots. Actual compiled vp20 instructions passed reference numerical
tests with a non-symmetric matrix, viewport/depth, colour/alpha and UV.

Artifact ABI is now 2; the reader rejects historical ABI1 data. Owned shader
pairs validate both stages before emitting GPU callbacks, invalidate failed
replacement and retain their bytes after source release. Both compiled fixtures
passed 120 pair-binding sequences. `BindCompiledShader` and its bounded pbkit
uploads compile in the real Xbox diagnostic XBE/ISO, at
`.xbox/builds/hlsl-native-binder-build`. That image still selects legacy drawing;
it is build evidence, not shader-backed rendering acceptance.

Physical resource-binding serialization, uniform-colour patching, cooked
material selection, draw-path activation and xemu validation remain incomplete.

Shared `ShaderCompileTarget.Xbox = 9` preserves existing values including
Xbox360 = 8. The editor now selects it explicitly for xbox/XBOX. Direct tests
of the actual compiled editor method passed after first observing DirectX11
as the incorrect result. The full editor test assembly could not compile:
existing `RuntimeCpuProfileProbeTests` refers to missing
`CoreInitializationOptions.CpuProfileSink` (four errors). Those files were not
modified; this limits the shared regression claim. The editor itself rebuilt
with zero errors and 317 warnings. Backend registration and runtime activation
are still pending, and no shader-backed emulator acceptance is claimed.

### Baseline evidence

`tools/shader-probe/native/XboxPixelShaderBaseline.hpp` provides an explicit
prelude for the initial pixel subset: clear all texture-stage programs and
dependent-stage input selectors, then apply unchanged compiler commands.
It does not modify texture objects, blend/depth state or renderer code.
This is reusable extraction/binding logic, not a serialized shader package
or a runtime consumer yet; combiner programming comes from the compiler.

The original 0/1/0 assertions now run on baseline plus compiler output.
The same test compiled with `XBOX_TEST_RAW_COMPILER_STATE` bypasses only the
baseline and retains the original failure as a negative control. The driver
requires both results and records the baseline hash in its summary.

Evidence: `.xbox/verification/hlsl-cg-baseline-1`:

```text
With baseline:    0x0 / 0x1 / 0x0 — PASS
Compiler alone:   0xffffffff / 0x1 / 0x1 — expected assertion failure
```

120 repeated host-side transitions also pass, with stage programs and input
selectors poisoned before each sequence. These are command-state checks, not
rendered frames. All four compiler stages and RGB/alpha interface checks pass.
Renderer regression remains 722 checks plus sprite/unlit transition PASS
(`hlsl-cg-baseline-regression/native-tests.log`). Author self-review only.

Next boundary: versioned data extraction/validation and runtime binding, with
the baseline included in the packaged command table. Actual GPU results remain
unproven; the baseline must not be mistaken for the complete runtime ABI.

## Approved emitter follow-up

The user approved preserving COLOR0. `XboxShaderEntryAdapter` now generates a
separate `.entry.cg` file from post-emission resource names, authored semantics
and reflected scalar/vector types. It includes the unchanged `.cg` and calls
its entry through explicit POSITION/COLOR0/TEXCOORD0 structs. Cg compiles the
new `XboxMain` entry; no generated shader text is rewritten and no library was
upgraded. The adapter is deliberately limited to the experimental interface.

Evidence: `.xbox/verification/hlsl-cg-adapter-2` and `hlsl-cg-adapter-3`.
All four core `.cg` files match the original chain byte-for-byte; all four
nxdk includes match across the two adapter runs.

- VS assembly writes colour to `o[COL0]` and UV to `o[TEX0]`.
- Colour PS reads primary colour RGB and alpha, without consuming a texture stage.
- Textured PS multiplies texture 0 by primary colour, including alpha.
- `shader_colour_interface_tests.cpp` executes the real generated includes and
  checks the register fields. It rejects the old chain's texture-backed colour
  and passes for the adapter. The driver now runs this test automatically.
- Unsupported TEXCOORD1 is explicitly rejected without publishing artifacts.
- Existing renderer: 722 checks and sprite/unlit transition suite pass again
  (`hlsl-cg-adapter-regression/native-tests.log`).

Before adding the baseline, the raw state-transition test reported:

```text
Stage programs colour/textured/colour: 0xffffffff / 0x1 / 0x1 (required 0 / 1 / 0)
Assertion failed: firstColour == 0
```

Unlike the earlier allocation mismatch, this shows omitted state: colour emits
only combiners, so it inherits texture-stage state. The baseline follow-up
above resolves this host transition without patching compiler output or
weakening the expected transition.
No runtime package/binder, ISO or emulator change was made in this follow-up.

The sections below retain the original gate findings and pre-adapter hashes.

## Reproduction

```powershell
./scripts/test-shader-compatibility.ps1 -OutputRoot "$PWD/.xbox/verification/hlsl-cg-chain-1"
./scripts/test-shader-compatibility.ps1 -OutputRoot "$PWD/.xbox/verification/hlsl-cg-chain-2"
```

These directories already contain evidence; choose a new directory to repeat.
Each records exact per-process commands, stdout/stderr, SPIR-V, emitted Cg,
reflection, vp20/fp20 text, nxdk-generated includes and compiler-chain.json.
The summary explicitly marks runtime compatibility as unproven.

Tool identities:

- .NET SDK observed: 10.0.401, probe target net9.0.
- Silk.NET.Shaderc and Silk.NET.SPIRV.Cross managed/native packages: 2.21.0.
- Cg: 3.1.0013, April 18 2012; Windows executable from local pinned nxdk.
- cgc SHA-256: `A0237CE50909FF01A1851E5EFC668AC64DB96E2A9B899C61D6624EF3782EB01A`.
- nxdk: `29638d0b001f179b73c3513489af10ddc2986216`.
- Docker: `sha256:acdb4a9fae971b5791b2732571fa0df72118301ef1760861e71892d36f80782a`.

## Observed translation behavior

Initial default legacy-HLSL emission retained a cbuffer declaration. cgc vp20
rejected its colon syntax. Applying CompilerFlattenBufferBlock through the
SPIRV-Cross API emitted `uniform float4 Transform[4]` and resolved that failure.
CompilerBuildCombinedImageSamplers was also necessary for legacy sampling.
Neither fix edits generated shader text.

The resulting four stages all compile with cgc and nxdk:

| Fixture | Stage | nxdk include SHA-256 |
| --- | --- | --- |
| colour | vertex | DB6580BD03C46CC05028D3F17E15BFBF9123D2B078965CAD565279BDFC2AE241 |
| colour | pixel | 5DB09C285D4A747FA9BDBEBC7A7353CEBCDBC71FD01FE7F2767863E540563F02 |
| textured | vertex | AF13FDD2B71588702CFE43863C1B40C0CE1089BDB3073265C52AD6C718F5A2F9 |
| textured | pixel | 0CF33D3DECD591FEC4189836A29C8F3ECFBDC4320A0E3F2978063DEA9CEB0C21 |

Both the emitted Cg and generated includes were byte-identical across the two
invocations. SPIR-V/debug assembly source paths are not asserted identical.

## Binding/state finding

The probe preserves VS input POSITION and COLOR0 using the native vertex
attribute remapping API. However, the legacy emitter maps VS output colour to
TEXCOORD0 and UV to TEXCOORD1. Its matching pixel inputs use those same channels.
Thus this is NOT evidence that VS and PS are unlinked: the pair agrees, but
its hardware allocation differs from the approved first-slice contract.

- Colour-only: texture stage 0 PASS_THROUGH; final combiner reads tex0.
- Textured: stage 0 PASS_THROUGH for colour, stage 1 2D_PROJECTIVE for the
  actual sample; combiner multiplies tex1 by tex0.
- Return to colour: stage 1 is cleared; stage 0 remains PASS_THROUGH.

Executing the actual nxdk-generated includes in the host register sink prints:

```text
Stage programs colour/textured/colour: 0x4 / 0x24 / 0x4 (required 0 / 1 / 0)
Assertion failed: firstColour == 0
```

This separate experimental test is intentionally red and is not added to the
working renderer's native suite. It captures a design-contract difference,
not proof of another xemu crash: PASS_THROUGH is not a texture-sampling mode.
Evidence is `.xbox/verification/hlsl-cg-chain-1/state-test.log`.

The pinned managed SPIRV-Cross API exposes vertex-attribute remapping but no
HlslUserSemantic option. Current upstream source has a user_semantic option;
support in a different version is not evidence that the pinned version has it.
No toolchain upgrade or unreviewed numeric-option call was attempted.

## Other ABI findings

- Colour VS uses c[0..3] for Transform and c[4] for injected gl_HalfPixel.
- Colour VS emits eight instructions; textured VS emits nine.
- Textured VS input UV remains vp20 v[8], which nxdk remaps to hardware v[9].
- Compiler-injected half-pixel correction must be explicitly initialized or
  deliberately removed through a supported emitter option in the runtime ABI.
- Authored normalized UVs remain unchanged by this experiment. The actual
  linear-texture texel-coordinate conversion is not implemented here.
- The transform assembly computes the weighted sum of matrix rows. The
  non-symmetric reference vector/matrix in the plan gives (75,86,97,108).
  No numerical GPU execution or viewport-equivalence claim is made.

## Verification and limitations

- Observed initial missing-implementation failure, then successful frontend
  emission and actual native tool compilation.
- Invalid HLSL reports missing_symbol with source line and publishes no shader.
- Missing cgc is rejected before creating output; existing output roots are
  rejected rather than overwritten.
- Four real compiler stages passed twice; output hashes compared equal.
- Experimental fixed-state contract test fails with the measured values above.
- Existing renderer regression: 722 native checks plus sprite/unlit shader
  transition test PASS (`hlsl-cg-renderer-regression/native-tests.log`).
- No new shader-backed ISO or xemu run; no hardware test.
- This is a self-contained-fixture probe, not an include/variant-capable backend.
- Timeout/process cleanup paths exist but have not received fault-injection
  coverage; do not promote the driver as a production process service.
- Author self-review only; no independent review authorized or launched.

## Original proposed revision (subsequently approved)

Prefer preserving COLOR0 in a Cg-capable emitter so colour does not consume
one of the four texture stages. Investigate a narrowly scoped emitter option
or extension, with pinned-tool compatibility tests, before implementing an
entire new translator. Do not patch generated text.

An alternative is to explicitly accept the measured reflected ABI for this
first slice: colour pass-through in stage 0, sampled texture in stage 1, and
state validation that distinguishes pass-through from sampling. That requires
changing the approved state contract and accounting for the extra consumed
stage; it must not be smuggled in by weakening the test.

References: [SPIRV-Cross HLSL emitter](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/spirv_hlsl.cpp),
[NVIDIA profiles](https://developer.nvidia.com/cg-profiles). The decision above
rests on the local pinned-tool outputs, not on assuming upstream matches them.

## Follow-up: physical vertex constant bindings

The reusable compiler now reads Cg's `#var` metadata without modifying its
source or assembly. It records each flattened uniform row and actual register,
including the half-pixel helper and reserved viewport/texture-extent constants.
Tests cover nonzero matrix placement, duplicate rows/registers, reserved-slot
collisions, malformed locations and missing viewport assignments.

HXS1 container format 2 retains the 128-byte header and ABI 2. Header offset 120
holds a constant-binding count (at most 96); offset 124 remains zero. After the
instruction/register-write payload, each 72-byte record contains a zero-padded
64-byte ASCII identifier, little-endian row index and physical register index.
The checksum covers both payload and binding records. The native reader checks
exact lengths, canonical identifiers, uniqueness, register ranges and reserved
ownership before exposing a view or writing GPU state. Pixel records are empty
until their separate combiner-constant contract is implemented.

Format 1 remains readable for historical compatibility probes, but contains no
physical binding map. It is not evidence of complete authored material support.
The compiler API supplies the binding table to the host extractor, which validates
the final format-2 data before publishing it. Native program lookup resolves a
uniform row by name; missing rows and invalidated programs return failure.

Native format validation passed with the 722 existing checks and shader-state
regressions (`.xbox/verification/hlsl-binding-final/native-tests.log`). The first
integrated format-2 run hit the existing 60-second Docker timeout after extractor
stdout reported a validated artifact; this is retained as a failed run, not
counted as a pass. Runtime draw/material activation and xemu acceptance remain
outstanding. Current detailed evidence is tracked in the ignored integration ledger.

The isolated retry passed the complete managed suite with actual Cg/nxdk tools
(`.xbox/verification/hlsl-binding-publication-retry.log`), including format-2
publication, both shader families, repeat determinism, backend reflection,
includes, invalid-source diagnostics and cancellation. New vertex artifacts are
896 bytes (colour) and 912 bytes (textured), including their physical bindings;
pixel artifacts remain 200/216 bytes with no constant table.

## Follow-up: package identity and native material state

Added the missing shared target descriptor (`xbox`, `HEL_API_XBOX`) after a test
reproduced `ShaderTargetNames.GetTargetName(Xbox)` throwing. The existing Xbox360
descriptor was preserved. Name lookup, case-insensitive parsing and API define
lookup pass in `hlsl-package-target-green.log`.

`XboxShaderMaterial` owns per-material named constant values and shares an
immutable owned shader program. It resolves matrix rows independently, rather
than assuming four adjacent registers, and stages all constants before publishing
any state. Missing uniforms, invalid dimensions, invalidated programs, nonfinite
values and reserved-register mutations fail closed. Its binder validates draw
constants before invoking the shader/GPU callbacks. `XboxRasterRenderer` now
exposes that material-aware binder, while existing draw selection stays unchanged.

`hlsl-material-bind-green/native-tests.log` passes the existing 722 checks and
shader/constant regressions plus two-material lifetime, isolated values,
noncontiguous matrix rows, 120 alternating preparations and zero-GPU-write failure
tests. A real Release diagnostic build including the new native material source
succeeded in `.xbox/builds/hlsl-material-binder-build`. This remains a legacy-draw
image, not shader-backed visual acceptance. Generated shader material loading,
draw selection and xemu acceptance are still pending.

## Experimental authored consumer build

The process opt-in `HELENGINE_XBOX_EXPERIMENTAL_SHADER_RUNTIME=1` now selects
the raw shader material contract; the default remains the legacy renderer.
The conditional generated-interface implementation loads native shader materials
and routes authored mesh draws through their validated binder. Unsupported
fragment uniforms and textured meshes currently reject explicitly; these remain
unfinished requirements.

The real generated-header interface check passes. A complete Release/Authored
Xbox build passed in `.xbox/builds/hlsl-authored-native5` after fixing legacy CPU
lighting calculations that were unused in shader mode. The post-fix native suite
passes all 722 checks and the additional shader/material regressions.

- XBE SHA-256: `e2a5c398814578c064e3ec1f502f270235c86c18b9aa16518aae0b73e0f83bb0`
- ISO SHA-256: `c0e5a5fd25d834f0c71cd538fc3754f274d6c9b4bb786d74b6080949624c7893`

This proves native compilation/linking, not shader-backed visual acceptance:
the copied scene still needs the custom authored HLSL material fixture, and no
new emulator result is claimed. Runtime-wrapper behavioral tests, physical
fragment/interface bindings, textured draws and xemu acceptance remain pending.

## Real authored fixture and plugin loading

The isolated scene now has an Xbox-only material override and real HLSL using
`TransformBuffer` and `BaseColorBuffer`. Actual editor serializers and file-based
loading verify its program selection and cyan material constants.

The first editor compilation exposed native-library lookup relative to the host
instead of the plugin. `XboxShaderNativeLibraries` now supplies assembly-relative
paths for both API loading and dependency hashing. A separate-host integration
test failed before this fix and now produces a real Xbox vertex artifact. The
full managed compiler suite also passes (`hlsl-plugin-regression.log`).

Packaging is still incomplete: although `shader-authored-colour1` built an ISO,
the new cooked-byte test finds a DX11 default shader and no custom Xbox package.
The cooked material itself has the correct authored selection. The Xbox builder
still needs the platform shader-artifact cook capability, and generated standard
material packaging must stop selecting the desktop shader for this profile.
`hlsl-cooked-package-red.log` records this failing gate; no emulator acceptance is
claimed for that image.

## Authored cook and first shader-image stability

`XboxShaderArtifactCooker` now implements the missing platform cook path through
the real backend. Its tests deserialize both Xbox programs, deduplicate repeated
dependencies, reject foreign target/variant/legacy requests, and verify that a
failed recompilation removes the requested stale package.

The shared packager no longer eagerly emits the unused desktop standard material
for the experimental Xbox raw profile. An actual reference to the unsupported
generated PBR material is rejected explicitly; other profiles are unchanged.

`shader-authored-colour2` built successfully with 37 artifacts. The cooked-byte
test now passes: the authored material selects `XboxFixtureColour`, both Xbox
stages are present, and no foreign shader target is staged. Fresh native and
profile regressions pass.

ISO SHA-256: `c4aa58cc863046e21ebab89837359f4f5447f4713c5ed393c263544159a36cdf`.
This exact image ran in xemu as Helena, with snapshot and isolated EEPROM/config.
The same process remained responsive after 67 seconds with no assertion in its
captured logs. Visual confirmation was requested; no screenshots were taken.
The serial log is empty, so this is stability evidence, not proof of rendered
geometry. Pixel uniforms, physical interface/texture bindings, textured draws
and the remaining runtime/visual verification gates are still incomplete.

## Uniform-colour compilation and native material binding

The real uniform-colour fixture now compiles through both stages, including a
pixel entry with no active inputs. Format 3 records its compiler-assigned global
combiner factors; the native reader rejects invalid factors and per-stage factor
mode. The existing formats remain compatible with shaders without PS bindings.

Native materials copy and validate unsigned colour values before any GPU callback,
then upload truncated ARGB8 to both general and final combiner constants. Tests
cover missing values, invalid floats, alternating materials and two global factors.
The actual Cg-generated pair also passes native binding with cyan `0xff33ccff`.
Evidence: `hlsl-pixel-actual-pair.log`, `hlsl-pixel-full-regression.log`, and the
native logs under `.xbox/verification/hlsl-pixel-*`.

A fresh diagnostic Xbox build succeeds (`hlsl-pixel-native-build`). It still
draws the legacy probe. The authored runtime material adapter now consumes bounded
unsigned float4 PS buffers and checks package metadata against physical factors.
A host test using the real generated engine classes passed defaults, changed
properties, invalid values, mismatched metadata and disposal
(`hlsl-runtime-pixel-final.log`).

`shader-authored-pixel-colour3` also completed the real editor cook and Xbox native
build. The cooked-byte test rejects the old VS-tinted image and confirms the new
PS has `BaseColorBuffer`, format 3, row 0, global factor 0
(`hlsl-pixel-package-old-red.log` → `hlsl-pixel-package-green.log`).
ISO SHA-256: `afbffe142c966a679d075b047d0cb8820d8c2ef17a5664397ba1f9d402dde5a4`.
This new image has not been launched; visual acceptance remains outstanding.

## Authored stage-pair validation

A real negative fixture previously cooked successfully despite the PS requiring
`TEXCOORD0` that its VS never wrote. The cooker now checks semantic identity,
index and exact type before publishing a package. It rejects missing/duplicate
varyings, invalid position/target signatures and mismatched stages, while
accepting reordered declarations and a constant-colour PS with no inputs.

Evidence: `hlsl-pair-cook-red3.log` → `hlsl-pair-cook-green.log`, valid cook
regression `hlsl-pair-valid-cook.log`, and full managed suite
`hlsl-pair-full-regression.log`. This is logical interface validation;
compiler-assigned physical attribute/varying/sampler maps and their runtime
consumption remain separate unfinished requirements.

## Physical texture and sampler identity

The frontend now retains SPIRV-Cross's actual image/sampler-to-combined mapping,
and the Cg reader resolves that combined symbol to its physical stage. A real
fixture with authored `t3`/`s5` verifies that both original names map to Cg stage 0.
Format 4 persists two bounded resource records after the constant table; the
native reader validates their names, kinds, stage and agreement with texture state.

The generated material adapter rejects texture/sampler metadata that disagrees
with those records. Evidence: `hlsl-texture-full-regression.log`,
`hlsl-texture-runtime-red.log` → `hlsl-texture-runtime-green.log`,
`hlsl-texture-native-final/native-tests.log`, and `hlsl-texture-real-pairs.log`.
The latter replays 120 actual textured/untextured transitions with owned artifacts.

An authored Xbox build also passed (`hlsl-texture-map-native.log`), using the
existing untextured scene package. This does not establish textured rendering or new xemu output.

## Textured mesh consumer follow-up

The authored model now copies finite normalized UVs into the 48-byte vertex
layout. The renderer supplies TEX0 at NV2A attribute9, as mapped by the pinned
vp20compiler. The compiled-material draw validates the texture descriptor and
resource presence before GPU callbacks, passes texture extents through c93,
binds the linear texture at stage0 and disables all texture units on untextured
draws. Missing UVs, missing/disposed textures and invalid allocations reject the
draw; there is no legacy shader substitution.

Evidence: `hlsl-model-vertex-uv-green.log`,
`hlsl-texture-consumer-final/native-tests.log` (722 checks plus shader/texture
suites), and `hlsl-texture-consumer-real.log` (actual Cg binaries with changed
texture extents). Both native modes compile: `hlsl-textured-draw-native.log`
and `hlsl-textured-draw-diagnostic.log`. These images still use the preceding
untextured content, so they are not visual acceptance.

The copied authored fixture now samples the existing checker-logo texture and
multiplies by cyan vertex colour. Its first full cook exposed an incorrect
fixture reference: hmeta authoring identity is distinct from the imported
texture ID. The fixture utility now imports through the actual GDI/editor
pipeline and checks the imported 64x64 texture identity before assigning it.
The corrected full cook passed (`shader-authored-textured2.log`), followed by
`hlsl-textured-package-green.log`: the runtime serializer reads the actual
64x64 RGBA32 texture, and both shader stages plus the physical resource map
are present. ISO SHA256:
`f92146df9cd70a72b165f1078502988185d36d82116ef60e99ea6c059fea4729`.
It was launched in xemu session1 with snapshot; visual confirmation is pending.

The literal audit found a real missing binding: the pinned Cg emits
`#const c[5] = 0.25 0.5 0.75 0.875` for `literal-colour.hlsl`, but the current
artifact/runtime path previously did not preserve that register. This is now
covered by format5 vertex records carrying the exact float4 bits and physical
register. The reader rejects nonfinite values, reserved registers, collisions,
bad padding and truncations before GPU callbacks. Both material and direct
program binding apply the immutable rows without treating them as user uniforms.

Evidence: `hlsl-literal-publication-red.log` →
`hlsl-literal-publication-green.log`, `hlsl-literal-runtime-red.log` →
`hlsl-literal-direct-green/native-tests.log`, and
`hlsl-literal-real-runtime.log` using an actual compiled artifact. The full
managed compiler regression and authored native build passed
(`hlsl-literal-full-regression.log`, `hlsl-literal-native.log`). The emulator
remains on the preceding textured image, not this newer native build.
These checkpoints are superseded by the integration results below.

## Final integration verification — 2026-09-24

### Runtime crash corrections (later on 2026-09-24)

Three real execution failures were isolated in sequence, without changing the
authored scene to avoid them:

1. Material defaults absent from the selected layout caused a core fatal.
   Xbox now applies defaults only to active layout slots, preserves the authored
   asset, and rejects malformed or duplicate active payloads before returning a
   runtime material. `hlsl-defaults-red.log` reproduced the abort;
   `hlsl-defaults-green.log` verifies the fix with generated runtime code.
2. The shader-backed resolver calls `BuildModelFromRaw` with a deserialized
   packaged model. That entry point was still unsupported. In the shader profile
   it now uses the same validated, copying `XboxRuntimeModel::Create` as the
   cooked path. Legacy behavior is unchanged.
3. Once the scene reached drawing, xemu asserted `Invalid back light type`.
   The upload emitted 64 incrementing words at method `0xb80`, crossing the
   constant window into back-light methods at `0xc00`. Both compiled-shader
   paths now use the same tested uploader with 32-word packets. The packet-level
   test uses the actual production emitter, checks the method aperture and all
   96 rows. RED: `hlsl-constant-upload-red2/native-tests.log`; full native GREEN:
   `hlsl-constant-upload-green/native-tests.log` (722 checks plus shader suites).

The corrected image is `.xbox/builds/hlsl-upload-fix/artifacts/game.iso`, SHA256
`b07136fd6a6d3328d8bcf927efef221ff065b48d60070a9aa08b331b277b136e`.
HelenUI OCR now reads the authored rendering HUD instead of the fatal screen:
`.xbox/emulator-upload-fix/ocr-1.json`. This is stronger evidence than process
liveness, but does not alone establish the appearance of the cube or sprites.
A second fresh HelenUI frame after 124.8 seconds also reads the authored HUD,
passes `scripts/test-runtime-ocr.ps1`, and differs from the first captured PNG.
The xemu stderr has no assertion/fatal matches. PID 148400 remains open in user
session 1. Both authored and legacy diagnostic builds passed; diagnostic evidence
is `hlsl-upload-diagnostic.log`. Final appearance still requires visual feedback.
The historical failure and earlier automated results follow below.

**Earlier runtime acceptance failed (user report confirmed with HelenUI OCR).** The earlier
image halts after `core-ready`: `Constant buffer binding 'RoughnessBuffer' was
not found on the material layout`, then `core fatal: halted`. Process liveness
and clean serial/stdout/stderr logs did not detect this on-screen engine fatal.
`XboxShaderRuntimeMaterial::Create` applies all authored constant defaults via
`ApplyConstantBufferDefaults`, including standard-material buffers absent from
the custom shader layout. The automated material fixtures did not reproduce that
real cooked-material combination. This is an unresolved runtime integration bug,
not merely pending visual confirmation. Evidence:
`.xbox/emulator-final-hlsl/helenui-ocr-result.json`.

HXS1 format 6 retains compiler-confirmed VS input/output and PS input/output
semantics, component counts and NV2A routes. The cooker checks the physical pair
before publishing; the native loader independently rejects incompatible pairs.
Resource consumers distinguish texture, sampler, vertex literal and interface
records. Legacy artifacts remain readable, but cannot be mixed with a format-6
stage in a pair. New compiler output uses format 6 for both stages.

The pixel-literal audit reproduced an overly restrictive method allowlist.
The reader now accepts only the two eight-stage combiner factor banks and the
two final factors in addition to its existing methods. The real pinned fp20
compiler/translator preserves RGBA `(0.2, 0.4, 0.6, 0.8)` as `0xCC336699`;
unaligned and out-of-range factor writes remain rejected. Compiler text is
neither patched nor interpreted by the runtime.

Verification logs under `.xbox/verification`:

- `hlsl-final-managed.log`: full compiler suite, actual Cg/nxdk, interfaces,
  literals, includes/defines, timeout/cancellation, physical texture mappings.
- `hlsl-final-default-path.log`: 722 native checks plus shader/material/texture
  suites. Default test output now stays in this repository's `.xbox` directory.
- `hlsl-final-runtime-material.log`: generated material defaults, changed
  constants, independent material disposal and shader invalidation with no
  GPU callbacks after invalidation; real v6 shader binaries.
- `hlsl-final-real-pairs.log`: 120 actual colour/textured/colour transitions
  after releasing source buffers.
- `hlsl-physical-cooker-real.log`: real cooking, duplicate dependencies,
  serialization and stale-package removal on failure.
- `shader-authored-physical6.log` and `hlsl-final-package.log`: complete editor
  cook of 38 artifacts, both stages, authored material and decoded 64x64 texture.
- `hlsl-final-diagnostic2.log` and `hlsl-final-authored.log`: final native images built.

Final authored image: `.xbox/builds/hlsl-final-authored/artifacts/game.iso`,
SHA256 `00bcae81a6efcb572c8b748091427d571dd4308e790a555b7004995ef9f5f0d6`.
Launched using PowerShell in user session 1, snapshot enabled, PID 159936;
launch evidence is `.xbox/emulator-final-hlsl/launch.json`.
The same process remained responsive after 157.86 seconds; serial/stdout/stderr
logs contained no assertion, fatal, panic or abort matches. This establishes
process stability, not visual correctness. Actual builder registration, editor
target selection and incompatible-pair cooking were also rerun successfully.

Acceptance remains pending user visual confirmation. The profile stays explicitly
opt-in; these checks do not establish physical Xbox compatibility. The full shared
editor test suite remains blocked by the previously recorded unrelated
`RuntimeCpuProfileProbeTests`/`CpuProfileSink` compilation errors; it is not claimed
green. General PBR/ForwardStandardShader and broader HLSL support are out of scope.
