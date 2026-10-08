# Original Xbox HLSL to Cg shader backend

Status: initial supported backend implemented and automated integration verified;
final xemu visual acceptance pending (2026-09-24). See the compatibility report.

## Intent and constraints

Keep authored HLSL as the source of truth and add an Original Xbox backend
using NVIDIA Cg vp20/fp20 and the pinned nxdk translators. The first delivery
supports untextured colour and a single 2D texture multiplied by colour.
Preserve the currently working cube_test/sprite renderer until the new path
has passed compiler, native-contract and emulator checks.

Work in the main checkouts, never worktrees. User has authorized minimal
integration changes in the sibling helengine repository. Preserve unrelated
dirty changes in both repositories. Do not modify generated Core sources,
capture host windows, use computer-use, or launch independent review agents.
Keep dependencies and new backend scratch output under the ignored .xbox
directory. No commits or toolchain upgrades are part of this specification.

## Verified starting point

- helengine exposes ShaderCompileRequest, IShaderBackend, compile results,
  includes, dependency hashing and IShaderBackendRegistryContributor.
- ShaderCompileTarget currently has no Original Xbox value. Add Xbox at the
  end, preserving every existing serialized numeric value.
- EditorCliBuildRunner.ResolveShaderCompileTarget currently selects PsVita
  or DirectX11. Xbox must resolve explicitly, not inherit the desktop target.
- The PS3 source compiler uses shaderc HLSL -> SPIR-V -> SPIRV-Cross GLSL.
  This is a reusable architectural pattern, not an existing shared Cg emitter.
- The Xbox builder currently disables the shaders feature and rejects all
  builder-owned cook work. Compiler registration alone is not runtime support.
- nxdk commit 29638d0b001f179b73c3513489af10ddc2986216 contains vp20compiler,
  fp20compiler and Cg. The Windows cgc reports version 3.1.0013.
- Current fp20compiler output is C pushbuffer calls, not a ready-made binary
  shader package. Vertex output is microcode expressed as C initializer data.
- Explicit TS1.0 state is necessary for both textured and untextured passes.
  The latter must disable sampling after the sprite pass; this already has a
  regression test in tests/sprite_shader_contract_tests.cpp.

## Chosen approach and alternatives

Use the existing HLSL frontend technology, retain SPIR-V for validation and
reflection, and emit Cg-compatible legacy source before invoking cgc.

Do not replace tokens in authored HLSL, silently substitute built-in shaders,
or create a microcode compiler. Direct HLSL-to-Cg text rewriting would lose
include, type and binding information. A new general-purpose IR/compiler is
unnecessary for the first delivery.

The first implementation gate is a real toolchain compatibility test of
SPIRV-Cross legacy HLSL output with cgc vp20/fp20. Configure emission and
interface mappings through compiler APIs, not regex patches to generated
source. If the pinned emitter cannot express the required Cg dialect without
rewriting its output, stop and revise the emitter design before implementing
the rest. An engine-owned typed emitter is a possible revision, not an
unreviewed fallback or a promise that all HLSL works.

## Pipeline and ownership

1. Resolve the requested entry point, active includes and defines through the
   shared request/resolver contract. Record the actual dependency closure.
2. Compile HLSL to SPIR-V with the same matrix convention as the engine.
3. Validate the selected entry point and reflect active resources/interfaces.
4. Emit Cg-compatible source with explicit bindings and stage semantics.
5. Invoke pinned cgc with vp20 or fp20 and the exact generated entry point.
6. Invoke pinned vp20compiler or fp20compiler. Retain diagnostics and generated
   text as build evidence; do not feed arbitrary source to a runtime compiler.
7. Convert compiler-owned output into a versioned Xbox shader artifact and
   validate it before publication. The engine runtime consumes data, not Cg
   source or C code.

Xbox-specific implementation belongs in builder/shaders and the native Xbox
platform code. Shared changes are limited to target selection/registration,
package compatibility checks and tests required by those contracts. Do not
refactor PS3/Vita or extend global capabilities merely to describe Xbox.

## Initial supported contract

Vertex programs: position transform using float4x4 constants, colour and one
texture coordinate varying. Pixel programs: interpolated colour, uniform
colour, or one ordinary 2D sample multiplied by colour. Cover both legacy
semantics and modern HLSL position/target semantics through typed mapping.

Reject unsupported stages, storage resources, texture arrays, derivatives,
discard, dynamic resource indexing, dependent texture reads and surviving
loops/conditional control flow in this first slice. These are initial backend
restrictions, not claims that all such operations are impossible on NV2A.
Preprocessor-selected variants remain supported. Unsupported expressions
produce a named diagnostic instead of rendering a replacement shader.

Use a backend-owned capability descriptor. Distinguish the initial supported
subset, cgc profile limits, and hardware limits. In particular, do not expose
all 192 Xbox constants as usable through vp20 without proving the compiler
and runtime mapping. Check final instruction/register usage after cgc.
Four texture stages and eight general combiner stages are hardware/profile
ceilings, not a promise that arbitrary operations within those counts fit.

## Binding and rendering ABI

Record VS input attributes, VS-to-PS varyings, constant registers and texture
stage assignments explicitly. Validate the VS/PS pair before packaging;
locations alone must not silently map colours onto texture coordinates.
Account for nxdk's VP input remapping (TEX0 v[8] maps to hardware attribute 9).

Pin matrix layout, multiplication order, clip/depth conventions and the
viewport conversion at the runtime boundary. Test with a non-symmetric matrix,
not identity alone. Reserve runtime viewport constants so they cannot collide
with material constants assigned by the compiler.

Preserve normalized authored UVs. Current NV2A linear textures require texel
coordinates; pass texture extents through an explicit generated-stage ABI.
Do not globally change authored HLSL sampling semantics to pixel coordinates.

Pixel state must be complete: texture-shader programs, dependent-stage input
selection and combiner configuration. Binding an untextured shader clears the
texture shader stages. Blend, depth, culling and texture objects remain owned
by render/material state, rather than arbitrary shader register writes.

## Artifact and tool execution contract

Define an Xbox-owned little-endian versioned container containing stage,
profile, entry point identity, source/toolchain hashes, resource binding map
and either VS microcode or a bounded PS register-write table. Consumers reject
wrong versions/stages, malformed lengths, invalid register ranges and unknown
methods before touching the GPU. Never serialize native pointers.

For nxdk C output, use a controlled host-side extraction harness that compiles
only compiler-generated includes and captures emitted data/register writes.
Do not parse general C or evaluate strings supplied by content as commands.
Allowed shader register methods and constant patch locations must be checked
against the pinned headers and final compiler output before serialization.

Tool invocation uses argument arrays, explicit executable paths, cancellation,
timeouts, captured stdout/stderr and unique workspace-owned job directories.
Record hashes of cgc, translators, frontend libraries, profiles and options.
Missing tools, timeouts, empty output and nonzero exits fail the build with
the responsible stage and original diagnostic. Never reuse a previous artifact
after failure. Cache identity includes active dependencies and ABI version.

## Delivery gates

1. Compiler compatibility: compile real HLSL fixtures through SPIR-V, Cg and
   nxdk for both supported shader families; verify reflected bindings.
2. Backend integration: register Xbox through the platform contributor and
   select it in relevant editor/CLI build paths. Keep shader runtime support
   disabled until its consumer is implemented, and report that distinction.
3. Artifact/runtime integration: package and bind both shader stages with
   validated constants, textures and attributes; retain an explicit legacy
   renderer selection for comparison, not automatic fallback on errors.
4. End-to-end acceptance: enable the shader-backed profile only after the
   supported fixtures cook, build and render correctly in xemu.

## Required verification

- Frontend: includes, defines, entry points, inactive unsupported code and
  diagnostics with source locations where frontend information is available.
- Translation: colour-only and texture-times-colour, matrix convention,
  sampler mapping, UV extent ABI, deterministic output and rejected features.
- Tool integration: execute actual cgc and nxdk tools; process mocks only test
  failure/timeout/cancellation paths, never stand in for compiler acceptance.
- Artifacts: binary round trips, bounds, truncated/corrupt payloads, foreign
  stages/versions, resource collisions and shader-method allowlist.
- Runtime: colour/texture/alpha, changing constants and textures between draws,
  alternating textured/untextured passes across frames, and two objects sharing
  shader/model resources without per-draw state leaking.
- Regression: existing 722 native checks and shader transition tests, scoped
  shared-engine target/registry tests and both diagnostic/authored native builds.
- xemu: exact artifact hash, user-session launch with snapshot, at least 60
  seconds without the prior assertion, and user-confirmed visual results.
  No physical-hardware success claim without hardware evidence.

## Not in this delivery

General ForwardStandardShader/PBR compatibility, new lighting models, normal
mapping, shadows, multipass emulation, text/rounded rectangles, dynamic shader
compilation on Xbox, or a universal HLSL-to-NV2A translator.

## References

- Local: builder/XboxPlatformAssetBuilder.cs and XboxPlatformDefinitionFactory.cs.
- Shared: helengine.shader/shaders/compilation and shaders/runtime/ShaderCompileTarget.cs.
- Existing frontend pattern: helengine-ps3/builder/shaders/Ps3ShaderSourceCompiler.cs.
- Pinned nxdk: Makefile, tools/cg, tools/vp20compiler, tools/fp20compiler, lib/pbkit.
- NVIDIA profiles: https://developer.nvidia.com/cg-profiles
- SPIRV-Cross: https://github.com/KhronosGroup/SPIRV-Cross
