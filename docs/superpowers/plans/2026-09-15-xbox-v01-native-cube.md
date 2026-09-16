# Xbox v0.1 Native Cube Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox syntax. This handoff does not authorize additional agents or an independent review; follow Helena's current delegation rules if either becomes useful.

**Goal:** Build a native Original Xbox executable that initializes video and continuously renders a static unlit cube, without Core.

**Architecture:** A thin entry point calls an Xbox boot host. The host sequences video begin-frame, a small native cube renderer, and video end-frame/present. Diagnostic geometry, camera and transforms are native constants; no engine runtime or authored content is involved.

**Tech Stack:** C++17, nxdk, pbkit/NV2A, GNU Make, Docker, PowerShell, xemu for initial runtime verification.

**Spec:** `C:/dev/helworks/helengine-xbox/docs/superpowers/specs/2026-09-15-xbox-v01-handler-design.md`. Read it before executing; this plan implements that revised native-only scope.

## Global constraints

- Work in `C:/dev/helworks/helengine-xbox`. Follow applicable AGENTS.md files and `C:/Users/Helena/.codex/GRAPHITI.md`.
- No generated Core, Core adapters, platform builder/editor discovery, cooked content, scene loader, material schema or demodisc changes.
- One static indexed cube with distinct unlit face colors; a fixed camera and perspective transform; depth test/write and back-face culling enabled.
- One tested 640x480-class 4:3 video mode; no textures, lighting, MSAA or offscreen passes. Rotation is optional and outside the required result.
- Keep resource allocation outside the normal frame loop. No CPU reads from write-combined GPU storage. Retain resources while the GPU may reference them.
- Keep platform logic out of main; video owns clear/present; raster renderer owns geometry and draw commands.
- All agent-owned output, staged images, generated shader includes and logs stay under `C:/dev/helworks/builds/helengine-xbox/v01/<build-id>`. Do not use the system temporary directory.
- Preserve source provenance and license notices. Do not repair generated sources through text rewriting.
- No screen/window screenshots without Helena's explicit authorization. A process running is not proof of a visible cube.
- Do not expand the milestone to fix unrelated engine issues. Record concrete blockers and finish independent work.
- Use small relevant tests and real target execution; do not build a broad test framework or profiler.

## Read before implementation

1. Both source documents in `docs/`: `original_xbox_development_guide.md` and `Xbox_Rendering.md`.
2. GameCube native bootstrap precedent: `../helengine-gc/docs/superpowers/specs/2026-05-01-helengine-gamecube-bootstrap-design.md`.
3. Dreamcast native files: `../helengine-dc/src/platform/dreamcast/DreamcastBootHost.hpp`, `DreamcastVideo.hpp/.cpp`, `DreamcastCubeMesh.hpp/.cpp`, and its build/launch scripts. Use them as structural precedents, not GPU implementations to copy.
4. The chosen nxdk revision's mesh/triangle samples, pbkit headers/implementation, build rules and shader tools. Exact APIs and synchronization semantics come from that pinned checkout.

Research found Xbox revision `6fcbe79` and only source documentation plus the untracked draft. Recheck before editing. The shared `C:/dev/helworks/emus` listing did not contain an obvious xemu directory; this is not proof that xemu is absent elsewhere.

## File map and interfaces

Create only files needed by the following tasks:

| Files | Ownership |
| --- | --- |
| `docker/Dockerfile`, `toolchain-lock.txt` | SDK/compiler/image provenance |
| `Makefile`, `scripts/build.ps1` | Native build and fresh XBE/XISO artifacts |
| `.gitignore`, `README.md`, `docs/PlatformNotes.md` | Hygiene, commands and verified constraints |
| `src/main.cpp`, `src/platform/xbox/XboxBootHost.hpp/.cpp` | Lifecycle and frame sequencing |
| `src/platform/xbox/XboxVideo.hpp/.cpp` | pbkit startup, clear, present and drain/shutdown |
| `src/platform/xbox/XboxCubeMesh.hpp/.cpp` | CPU-side immutable cube definition |
| `src/platform/xbox/XboxMath.hpp/.cpp` | Small native matrix construction and CPU reference checks |
| `src/platform/xbox/XboxRasterRenderer.hpp/.cpp` | Vertex storage, shader/state setup and indexed submission |
| `src/platform/xbox/shaders/unlit.vsh`, `unlit.psh` | SDK-compatible vertex/combiner source, if using those build suffixes at the pinned revision |
| `tests/native_contract_tests.cpp`, `scripts/test-native.ps1` | CPU geometry/math checks independent of nxdk headers |
| `scripts/launch_in_emulator.ps1` | Explicit XISO and existing emulator/profile selection |

Public native contract (keep declarations in their owning headers; do not consolidate all classes into one file):

```cpp
namespace helengine::xbox {
enum class ProbeMode { Clear, Triangle, Cube };

struct BootConfig {
    ProbeMode Mode = ProbeMode::Cube;
    unsigned StopAfterFrames = 0; // zero means continue presenting
};

class XboxVideo {
public:
    bool Initialize();
    unsigned Width() const;
    unsigned Height() const;
    bool BeginFrame(unsigned clearArgb);
    bool EndFrame();
    bool Drain();
    void Shutdown();
};

class XboxRasterRenderer {
public:
    bool Initialize(unsigned width, unsigned height);
    bool Draw(ProbeMode mode);
    void Shutdown(); // called only after video/GPU drain
};

class XboxBootHost {
public:
    int Run(const BootConfig& config);
};
}
```

SDK operations that cannot report failure must not invent a success check. Return false for detected initialization/resource errors; emit phase/error diagnostics where detection occurs. If a GPU drain fails, never free resources still potentially in use; enter a terminal diagnostic state and report the failure.

## Task 1: Lock toolchain and produce a clear-screen native build

**Files:** Dockerfile, lock file, Makefile, build script, entry point, boot host, video class, initial README/PlatformNotes/.gitignore.
**Consumes:** supplied docs and pinned SDK APIs.
**Produces:** the video lifecycle above and a reproducible fresh `default.xbe` plus `game.iso`.

- [ ] Inspect status, instructions and available Docker/host tools. Preserve existing changes. Record `git status --short`, `git rev-parse HEAD`, `docker version` and relevant shell/tool versions in the workspace build directory.
- [ ] Select a concrete nxdk commit, initialize its submodules and record its full SHA. Use a pinned container base digest and record compiler/tool versions in `toolchain-lock.txt`. Do not leave floating `master` as the build input. Inspect the chosen SDK's Dockerfile and CI for supported prerequisites; build its tools before introducing local rendering code.
- [ ] Record an API worksheet in PlatformNotes: video mode call and return behavior; pbkit init result; back-buffer extent/format; depth format/range; clear/present sequence; vertex allocation/address rules; shader compiler input/output; index command layout; GPU drain and shutdown behavior. Include pinned source paths. This is a mandatory short source-inspection step, not permission to redesign the renderer.
- [ ] Implement `scripts/build.ps1` with `-OutputRoot`, `-Configuration Debug|Release`, `-Probe Clear|Triangle|Cube`, `-StopAfterFrames` and an optional documented diagnostic pose selector added in Task 3. Unsupported values fail before launching Docker. Use a unique output directory per invocation, staging under that directory, and quoted paths so spaces work.
- [ ] Make the final artifact directory contain `default.xbe`, `game.iso`, `build-info.json` and available map/symbol files. `build-info.json` records source commit plus dirty state, SDK SHA, image identity, configuration, probe, UTC build start, artifact relative paths and SHA-256 hashes. Publish this success record only after both artifacts pass validation. Failed commands propagate nonzero exit status.
- [ ] Follow the pinned SDK's actual Makefile rules for C++/shader support and packaging. Direct generated objects/includes into the invocation directory. Stage `default.xbe` at the disc root; do not package the whole checkout. Preserve SDK license notices for any reused code.
- [ ] Implement the entry and video-only host. The entry point must remain this small in structure:

```cpp
#include "platform/xbox/XboxBootHost.hpp"
int main() {
    helengine::xbox::XboxBootHost host;
    helengine::xbox::BootConfig config;
    config.Mode = static_cast<helengine::xbox::ProbeMode>(HELENGINE_XBOX_PROBE);
    config.StopAfterFrames = HELENGINE_XBOX_STOP_AFTER_FRAMES;
    return host.Run(config);
}
```

The Makefile maps Clear=0, Triangle=1, Cube=2 to `HELENGINE_XBOX_PROBE`; it defines `HELENGINE_XBOX_STOP_AFTER_FRAMES` from the validated nonnegative build argument. Default configuration is Cube and 0. Reject an unsupported mode before compilation.

- [ ] Implement video startup, extent checks and clear/present using the pinned pbkit lifecycle. Print `video-init`, `video-ready`, `first-present` and explicit errors. Do not add general input or an overlay system. Use the SDK's bounded diagnostics facilities.
- [ ] Build Clear mode. Validate a nonempty XBE with leading bytes `58 42 45 48` (`XBEH`), a nonempty XISO, and packaged root `default.xbe` using the SDK image tool. Preserve tool logs. Header magic is only structural validation, not boot proof.
- [ ] Test a nonexistent toolchain/container input and an invalid probe value. Both must fail without a success manifest. Do not delete an earlier successful build during these tests.
- [ ] Commit this bounded checkpoint after reviewing the diff and recording actual validation. If committing is blocked, leave the patch and explain why.

Expected build interface once created:

```powershell
$runRoot = 'C:/dev/helworks/builds/helengine-xbox/v01/clear-debug'
./scripts/build.ps1 -OutputRoot $runRoot -Configuration Debug -Probe Clear
if ($LASTEXITCODE -ne 0) { throw 'Clear build failed' }
```

The wrapper must refuse to overwrite an existing invocation directory. Callers select a fresh `-OutputRoot` for every build; never guess the latest artifact by directory scanning.

## Task 2: Establish an unlit triangle using the final renderer path

**Files:** raster renderer and shader sources; extend host/Makefile.
**Consumes:** initialized XboxVideo dimensions and pbkit context.
**Produces:** `Initialize`, `Draw(Triangle)` and `Shutdown` for a minimal GPU path.

- [ ] Inspect the pinned samples for vertex upload, program binding, combiner setup, indexed commands and required initial render state. Keep short source-location notes; do not copy textured/lighting sample systems into this milestone.
- [ ] Implement a vertex containing float position[3] and float color[4]. Float color deliberately avoids packed-color endianness/swizzle ambiguity in this first probe. Assert its 28-byte layout and offsets 0 and 12. Allocate GPU-visible vertex storage once using the pinned SDK's supported contiguous-memory API; check allocation and copy sequentially from ordinary CPU memory.
- [ ] Implement a minimal transform vertex program and unlit interpolated-color output. Disable texture sampling, blending, alpha testing, fog and lighting; enable color writes. Compile shader sources through the pinned toolchain. Document attribute slots and constant layout beside the upload code.
- [ ] Establish viewport/depth mapping once and document where perspective division and viewport conversion occur. Use one consistent path; do not apply viewport transforms in both CPU matrices and GPU state. Exact register values must come from the pinned API worksheet, not inferred desktop Direct3D constants.
- [ ] Draw one triangle through the same vertex/index/state path the cube will use. If indices are emitted into pbkit commands, keep the CPU index array in ordinary memory; it is not automatically a GPU DMA resource.
- [ ] Integrate host sequencing:

```text
initialize video
initialize renderer unless Clear mode
loop until requested diagnostic frame count is reached:
    video.BeginFrame(clear)
    renderer.Draw(mode) unless Clear mode
    video.EndFrame()
drain GPU
release renderer resources
shutdown video
```

- [ ] On partial startup failure, release only successfully initialized resources in reverse order. On normal exit, keep the pbkit context alive until renderer resources are safe to release. Continuous default mode does not require user-input exit support.
- [ ] Build Triangle mode and boot when emulator setup is available. Verify color and placement, plus unchanged Clear behavior. If runtime access is blocked, label target behavior unverified and continue CPU-side work; do not claim Task 2 runtime acceptance.
- [ ] Commit with logs/notes recording what was actually verified.

## Task 3: Add indexed cube, camera, projection, depth and culling

**Files:** cube mesh, math, renderer, CPU tests and test wrapper.
**Consumes:** working unlit triangle renderer.
**Produces:** default static Cube probe and deterministic geometry/math checks.

CPU-only data contract:

```cpp
struct CubeVertex { float Position[3]; float Color[4]; };
struct CubeGeometry {
    CubeVertex Vertices[24];
    unsigned short Indices[36];
};
struct Matrix4 { float Values[16]; }; // row-major storage
struct Vector4 { float X, Y, Z, W; };
CubeGeometry MakeCubeGeometry();
Matrix4 Multiply(const Matrix4& left, const Matrix4& right);
Vector4 Transform(const Matrix4& matrix, const Vector4& vector);
Matrix4 Identity();
Matrix4 Translation(float x, float y, float z);
Matrix4 RotationX(float radians);
Matrix4 RotationY(float radians);
bool TryPerspective(float verticalFovRadians, float aspect,
                    float nearPlane, float farPlane, Matrix4& output);
```

Place these in `helengine::xbox`; use fixed arrays and no heap allocation for these helpers. CPU convention: column-vector multiplication, `clip = Projection * View * Model * position`, right-handed view looking down negative Z, normalized clip depth [0,1]. Convert explicitly at the NV2A boundary if its chosen program/viewport path differs. This convention is local to the diagnostic; do not change shared engine math.

- [ ] Write tests before the math/mesh implementation. Check 24 vertices, 36 indices, index range, finite positions/colors, nondegenerate triangles and outward winding (`dot(cross(b-a,c-a), (a+b+c)/3) > 0`). Check exactly six face colors with opaque alpha and a uniform color on each face.
- [ ] Construct six faces as four unique vertices each. Give each face tangent vectors `u`, `v` with `cross(u,v)` equal to its outward normal. Emit corners `center-u-v`, `center+u-v`, `center+u+v`, `center-u+v`, and local indices `0,1,2,0,2,3`. Use unit half-extents and red, green, blue, yellow, cyan and magenta face colors.
- [ ] Implement deterministic math. For perspective use `f=1/tan(fov/2)`, entries `m00=f/aspect`, `m11=f`, `m22=far/(near-far)`, `m23=far*near/(near-far)`, `m32=-1`, all other entries zero. Reject nonfinite/invalid FOV, nonpositive aspect, nonpositive near and far<=near; `TryPerspective` returns false without changing its output on invalid input. The host logs the invalid configuration and stops. Use this bool/output contract consistently; this helper needs no exception support.
- [ ] Test identity, known translation and rotation, multiplication order, and near/far perspective mapping. Representative checks:

```cpp
Matrix4 projection{};
const bool valid = TryPerspective(1.0471975512f, 4.0f/3.0f, 0.1f, 100.0f, projection);
assert(valid);
const Vector4 nearClip = Transform(projection, {0, 0, -0.1f, 1});
const Vector4 farClip = Transform(projection, {0, 0, -100.0f, 1});
assert(std::abs(nearClip.Z / nearClip.W) < 0.0001f);
assert(std::abs(farClip.Z / farClip.W - 1.0f) < 0.0001f);
```

Tests must run in a configuration with assertions enabled; a Release build that removes assert is not test evidence. Test script compiles only CPU geometry/math and tests with the pinned container's host compiler, then runs the host executable. It must not attempt to execute an Xbox binary on the PC.

- [ ] Default pose: model `RotationY(30 degrees) * RotationX(-20 degrees)`, view `Translation(0,0,-5)`, vertical FOV 60 degrees, aspect from actual buffer dimensions, near 0.1, far 100. This is an explicit native test camera, not an authored camera policy.
- [ ] Reuse the final indexed submission path for all 12 cube triangles. Enable depth test/write, use a matching far-depth clear, and configure exterior-face culling. Adjust only the documented API-boundary conversion when GPU tests reveal winding/viewport differences.
- [ ] Add build-time diagnostic poses for baseline, farther cube, near-plane intersection and screen-edge intersection; plus reverse triangle order and disable culling switches if needed for verification. Keep these native probe settings, not general engine features.
- [ ] Run CPU tests and native Debug/Release builds. Boot Cube mode. Verify that a farther cube looks smaller, face colors remain unlit, reversed triangle order leaves the depth-correct result unchanged, and near/edge intersections clip without exploding geometry. With culling disabled, depth must still hide rear faces.
- [ ] Confirm no renderer allocation occurs during steady-state Draw; record resource allocation/release counts with simple diagnostic counters. Log once at initialization and at a bounded milestone, not per frame.
- [ ] Commit the cube checkpoint after relevant checks pass; record any runtime check still blocked.

## Task 4: Package, launch and record acceptance

**Files:** launch script, README, PlatformNotes, build provenance validation.
**Consumes:** current invocation's XISO and build-info record.
**Produces:** repeatable native cube workflow and an honest validation report.

- [ ] Inspect existing emulator paths/configuration. If xemu is installed, use the actual executable and supported command-line options verified by its help/docs. Allow explicit `-EmulatorPath` and `-ArtifactPath`; never guess firmware locations or download firmware. Keep existing user configuration intact.
- [ ] Implement launch validation: require the explicit image, matching current build record/hash and nonempty file. Missing emulator, missing image and hash mismatch must fail with concrete diagnostics. Do not launch a different artifact as fallback.
- [ ] Launch interactively only as part of runtime verification and document the actual visible execution path. Follow current host rules for visible windows. A background PID is not runtime visual evidence.
- [ ] If emulator/required boot configuration is missing, ask for that concrete missing setup while continuing builds/tests/docs. Do not stop for a broad permission question or silently switch emulators.
- [ ] Verify Clear, Triangle and Cube in order if not already verified. Let the default cube run at least 60 wall-clock seconds. Record frame progression and stable resource counters. A finite-frame shutdown probe should demonstrate GPU drain and reverse-order cleanup independently of the continuous default.
- [ ] Establish visual correctness through an allowed interactive observation or Helena's confirmation. Request screenshot permission only if a capture is needed; do not take screenshots by default. Record the method of verification.
- [ ] README must show exact working build/test/launch commands, output paths, default static behavior and required external emulator configuration. PlatformNotes records SDK identity, state/matrix conventions, GPU allocation rules, actual video/depth formats, tested emulator version and current limitations.
- [ ] Verify no Core/engine libraries or authored assets are linked into the target. Review the source list and map/dependency output. No changes outside the Xbox repo except named workspace build outputs.
- [ ] Finish with `git diff --check`, relevant native tests, fresh target build evidence, and a status/diff review. Do not run unrelated engine test suites. Commit scoped code/docs and report final commit/status.

## Handoff and success report

Deliver these facts, with real paths and commands:

- Exact source/SDK/image identities and dirty-state disclosure.
- Debug and Release build results and fresh XBE/XISO paths/hashes.
- CPU geometry/math tests and negative build/launch tests actually run.
- Emulator boot and visual cube verification method; 60-second stability result.
- Physical hardware result separately, or explicitly pending.
- Any incomplete checkpoint and its concrete blocker.

**Completion requires a rendered cube.** A compiled XBE, successful package or running emulator process alone is not completion. If runtime setup prevents observation, report implementation/build complete and runtime acceptance pending.

## Primary API references

- [nxdk source](https://github.com/XboxDev/nxdk): pin the chosen revision before deriving register commands or build rules.
- [nxdk mesh sample](https://github.com/XboxDev/nxdk/tree/master/samples/mesh): navigation aid for the GPU lifecycle; inspect its pinned equivalent, and retain only what the native unlit probe needs.
- [xemu disc images](https://xemu.app/docs/disc-images/): package an appropriate disc image for this launch flow.

## Suggested message to the implementing agent

> Implement `C:/dev/helworks/helengine-xbox/docs/superpowers/plans/2026-09-15-xbox-v01-native-cube.md` and its linked spec. The first target is native video plus a static unlit indexed cube, without Core or engine build integration. Follow existing handler conventions, work sequentially, keep build outputs under the documented workspace directory, and validate actual rendering. Report concrete missing emulator prerequisites without claiming runtime success. Do not expand the scope to authored scenes or change demodisc.

