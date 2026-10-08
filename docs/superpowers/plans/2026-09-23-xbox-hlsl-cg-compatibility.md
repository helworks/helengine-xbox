# Xbox HLSL to Cg Compatibility Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. No worktrees, subagents or independent reviewers are authorized.

**Goal:** Establish with actual compiler output whether the approved Xbox backend can translate its initial HLSL subset through SPIR-V and legacy HLSL emission into Cg vp20/fp20 accepted by nxdk.

**Architecture:** An isolated, explicitly experimental host probe compiles checked-in HLSL fixtures through shaderc and SPIRV-Cross. A PowerShell driver invokes the pinned Windows Cg compiler and Docker-hosted nxdk translators, records exact outputs, and runs a native command-capture test. This gate does not advertise a working engine shader backend.

**Tech Stack:** .NET 9, Silk.NET.Shaderc/SPIRV.Cross 2.21.0, PowerShell, Cg 3.1.0013, pinned nxdk and Docker, C++17 host tests.

**Spec:** `docs/superpowers/specs/2026-09-23-xbox-hlsl-cg-backend-design.md` (approved).

## Global Constraints

- Work in the main checkouts, never worktrees.
- Preserve unrelated dirty changes in both repositories.
- Do not modify generated Core sources, capture host windows, use computer-use, or launch independent review agents.
- Keep dependencies and new backend scratch output under the ignored .xbox directory.
- No commits or toolchain upgrades are part of this specification.
- Pin nxdk to `29638d0b001f179b73c3513489af10ddc2986216`, Docker to the image identity in toolchain-lock.txt, and cgc to the executable inside that checkout.
- No regex/string replacement of generated shader source. Configure compiler emission and semantic mapping through APIs. Unsupported output is evidence to revise the design, not permission to rewrite it.
- No shared-engine or runtime product changes in this gate. The user authorized them for the subsequent integration, but their implementation depends on the gate result.
- Keep the working sprite/3D path untouched; do not close the user's emulator.
- The probe remains experimental. It is not silently promoted into a production compiler.

## Review Focus

1. A successful compile with the wrong VS/PS semantic mapping is failure: inspect paired interfaces and compiled input assignments (Tasks 1 and 2).
2. Identity matrices hide transposition errors: compile the non-symmetric transform fixture and record row/column conventions (Task 1); visual equivalence remains a later runtime gate.
3. A failed tool run must not reuse an old output: unique job directories, missing/empty-output tests and no publication after failure (Task 2).
4. Constant-colour pixel programs can leave texture sampling active: capture colour -> textured -> colour register transitions with the real compiled output (Task 3).
5. Text that looks like a shader is not acceptance: use actual shaderc, SPIRV-Cross, cgc and nxdk processes; preserve native diagnostics and artifact hashes (Tasks 1-3).

## File boundaries

Create only these gate-owned files:

- `tools/shader-probe/helengine.xbox.shader-probe.csproj`: isolated net9.0 executable, unsafe enabled; four Silk.NET packages at 2.21.0, matching the PS3 compiler. No dependency on PS3 builder types.
- `tools/shader-probe/Program.cs`: parse explicit source, stage, entry and output arguments and return nonzero on failure.
- `tools/shader-probe/XboxShaderCompatibilityProbe.cs`: shaderc/SPIRV-Cross ownership and native API calls; one class per file, substantive XML comments on all members.
- `tools/shader-probe/XboxShaderProbeOutput.cs`: immutable result containing SPIR-V bytes, emitted source, emitted entry point and reflected interface report.
- `tests/shaders/colour.hlsl`, `tests/shaders/textured.hlsl`, `tests/shaders/invalid.hlsl`: small authored fixtures below.
- `scripts/test-shader-compatibility.ps1`: pinned-tool preflight, isolated jobs, process diagnostics and final result summary.
- `tests/shader_pair_state_tests.cpp`: consume real generated pixel shader commands and verify state transitions.
- `docs/verification/2026-09-23-xbox-hlsl-cg-compatibility.md`: compact gate result with command lines, hashes, limitations and decision.

All bin/obj, SPIR-V, Cg, assembly, generated includes and logs go below a unique `.xbox/verification/hlsl-cg-<timestamp>` invocation root. Pass `--artifacts-path` to dotnet; do not redirect agent-owned output to TEMP.

## Task 1: Translate representative HLSL fixtures without rewriting output

**Interfaces:**

`XboxShaderCompatibilityProbe.Translate(string sourcePath, string entryPoint, bool vertexStage)` returns `XboxShaderProbeOutput` with `byte[] Spirv`, `string Source`, `string EntryPoint`, `string InterfaceReport`. It throws a diagnostic exception for frontend/emitter failure. This probe accepts self-contained fixtures only; include support belongs to the production backend.

- [ ] Add `tests/shaders/colour.hlsl` with this actual shader pair:

```hlsl
cbuffer Transform : register(b0) { row_major float4x4 WorldViewProjection; };
struct VSInput { float3 position : POSITION; float4 colour : COLOR0; };
struct VSOutput { float4 position : SV_Position; float4 colour : COLOR0; };
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.colour = input.colour;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target { return input.colour; }
```

- [ ] Add `tests/shaders/textured.hlsl`:

```hlsl
cbuffer Transform : register(b0) { row_major float4x4 WorldViewProjection; };
Texture2D Albedo : register(t0);
SamplerState AlbedoSampler : register(s0);
struct VSInput {
    float3 position : POSITION;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
};
struct VSOutput {
    float4 position : SV_Position;
    float4 colour : COLOR0;
    float2 uv : TEXCOORD0;
};
VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.colour = input.colour;
    output.uv = input.uv;
    return output;
}
float4 PSMain(VSOutput input) : SV_Target {
    return Albedo.Sample(AlbedoSampler, input.uv) * input.colour;
}
```

- [ ] Add `invalid.hlsl` containing `float4 PSMain() : SV_Target { return missing_symbol; }`. The probe must exit nonzero, retain the original diagnostic and publish no result for this input.
- [ ] Establish the project and CLI boundary. Run the colour vertex command against an initially unimplemented `Translate` method; require a missing-implementation failure, not a package/tool failure, before implementing translation.

```powershell
dotnet run --project tools/shader-probe/helengine.xbox.shader-probe.csproj --artifacts-path .xbox/verification/hlsl-cg-red/dotnet -- --source tests/shaders/colour.hlsl --entry VSMain --stage vertex --output .xbox/verification/hlsl-cg-red/colour-vs
```

- [ ] Implement shaderc translation with SourceLanguage.Hlsl, Vulkan 1.0 target, HLSL IO mapping, automatic locations/bindings, preserved binding names and debug information. Dispose compiler/options/result in finally blocks; reject empty/non-word-aligned output. Use the current PS3 source compiler as an API reference, not a source dependency.
- [ ] Create a SPIRV-Cross HLSL backend using the same parsed module. Set legacy shader model 30 and use native semantic-remapping APIs to preserve the reflected input/output contract. Record active inputs, outputs, uniforms and sampled resources before emission. Do not rename/rewrite emitted text afterwards.
- [ ] Implement CLI output only after successful translation. Write `<output>.spv`, `<output>.cg`, and `<output>.reflection.txt` and print the actual emitted entry point. Explicitly reject unknown stages or missing input files.
- [ ] Run both fixtures for both stages plus invalid input. Require four translations and one expected failure. Verify that the emitted texture declaration is acceptable legacy syntax, that pixel colour and UV match VS outputs, and that source matrices preserve row-vector multiplication. Record a non-symmetric reference matrix with rows `(1,2,3,4)`, `(5,6,7,8)`, `(9,10,11,12)`, `(13,14,15,16)`: input `(2,3,5,1)` must transform to `(75,86,97,108)` before viewport conversion. This is a reference value for later execution tests, not a claim that source inspection proves numerical runtime behavior.

## Task 2: Exercise the pinned Cg and nxdk chain

**Interfaces:** `test-shader-compatibility.ps1 -OutputRoot <fresh path>` owns one invocation and reports success only if all required fixtures complete. It consumes Task 1's CLI output and emitted entry point. It produces `.vp20`/`.fp20`, `.inl`, stdout/stderr logs and a machine-readable summary with source/tool hashes and exit codes.

- [ ] Add process-boundary tests through the driver: a nonexistent cgc path and `invalid.hlsl` must each return nonzero without publishing a successful summary. A reused output directory must be rejected before invoking any tools.
- [ ] Preflight the nxdk git revision, Docker image identity and executable existence. Query cgc version and record SHA-256. The Windows Cg executable is `.xbox/dependencies/nxdk/tools/cg/win/cgc.exe`; use argument arrays, never interpolated shell command strings containing authored source.
- [ ] Invoke cgc for each emitted stage using its reported entry point:

```powershell
& $CgcPath -profile vp20 -entry $VertexEntry -o $VertexAssembly $VertexCg
& $CgcPath -profile fp20 -entry $PixelEntry -o $PixelAssembly $PixelCg
```

Check each exit code immediately and require a nonempty output. Capture stdout/stderr, including warnings. Bound execution to 60 seconds per process and stop only processes belonging to this invocation when cancelled/timed out.

- [ ] Mount the invocation root into the pinned Docker image and run `/opt/nxdk/tools/vp20compiler/vp20compiler` and `/opt/nxdk/tools/fp20compiler/fp20compiler` on the generated assembly. Redirect tool-generated includes into that root. Verify nonzero output length and propagate translator diagnostics unchanged.
- [ ] Repeat the four-stage build in a fresh invocation. Compare normalized machine payload and reflection identities, not temporary filenames or debug-source paths. Record any nondeterministic field rather than concealing it.
- [ ] If any supported fixture cannot compile without generated-source rewriting, stop the gate. Report the exact source construct, compiler diagnostic and whether an emitter API can address it; do not register Xbox or proceed to runtime work on that assumption.

## Task 3: Verify compiled state transitions and record the decision

**Interfaces:** The host test receives generated `colour_ps.inl` and `textured_ps.inl`. Its `pb_push1(uint32_t*, uint32_t, uint32_t)` sink records register writes from actual nxdk-generated code, without pretending to emulate rasterization.

- [ ] Add the command-capture test with the following checks, using nxdk register definitions and correctly sized local push buffers:

```cpp
PushColour();
assert(stageProgram == 0);
PushTextured();
assert(stageProgram == 1);
PushColour();
assert(stageProgram == 0);
```

Initialize `stageProgram` to `0xffffffff` so missing initialization cannot pass. Implement each Push function by including its actual generated header with a local `uint32_t* p`. The first run must expose absent state if the compiler does not emit it; never hand-author an expected header to make the test pass.
- [ ] Compile and run with the pinned Docker host g++, `-std=c++17 -Wall -Wextra -Werror -UNDEBUG`, the nxdk include directory and invocation-generated includes. If the compiler emits partial state, record that the artifact packager must supply a complete initial baseline in the next design/plan; retain the failure evidence rather than changing compiler output.
- [ ] Check compiled VP attributes/constants and PS texture/combiner assignments against reflected interfaces. Record actual register usage and any unexpected helpers, semantics or profile limits in the report. Shader compile success does not waive an interface mismatch.
- [ ] Run `scripts/test-native.ps1 -OutputRoot <fresh .xbox path>` to confirm the working renderer's 722 checks and sprite/unlit transition test remain green. No emulator relaunch is needed for this compiler-only gate.
- [ ] Write `docs/verification/2026-09-23-xbox-hlsl-cg-compatibility.md`: exact tool identities, commands, fixture outcomes, artifact hashes, interface mapping, state completeness, matrix/UV caveats and a PASS/BLOCKED decision with the failing stage if blocked.
- [ ] Run `git diff --check` and inspect scoped changes. Do not stage or commit unrelated edits. Do not label this gate a finished runtime backend.

## Coverage and next delivery boundary

This plan implements delivery gate 1 of the approved spec, including the
toolchain/emitter risk that determines the following ABI. Gates 2-4 (shared
target integration, binary artifacts, runtime binding and xemu acceptance)
are intentionally not implementation steps in this plan. Their executable
plan is produced from this gate's evidence, retaining the approved spec.

On PASS, present the measured binding/state contract and the next integration
plan. On BLOCKED, present a concrete emitter revision for approval. Do not
claim either that an untested Cg dialect works or that the entire backend is
complete. No independent review is launched without the user's approval.

## Self-review

- Scope is the approved compatibility gate, not a reduction of the overall goal.
- Product code and working renderer stay unchanged.
- Fixture source, command shapes, output contracts and failure policy are explicit.
- Every Review Focus item has an owner and a check above.
- Remaining runtime numerical/visual checks are explicitly unproven here.
- Execution recommendation: inline in this session, one agent, because all
  three tasks depend on the same compiler and binding evidence.
