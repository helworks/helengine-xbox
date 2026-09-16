# Original Xbox handler v0.1 — native video and unlit cube

**Status:** Revised draft for discussion. Planning only; implementation has not started.
**Goal:** Build a native Xbox executable, initialize video, and render an unlit cube without Core.

## Scope

The first milestone is a standalone native graphics bootstrap. Follow the existing handlers' repository, build, ownership and validation conventions. Generated Core and authored-scene integration belong to a later milestone.

Use native diagnostic cube geometry, a fixed camera and explicit perspective matrices. A static angled cube with distinct unlit face colors is sufficient. Enable depth testing/writing and back-face culling. Rotation can follow once the static cube works; it is optional for this version.

Use pinned nxdk with pbkit/NV2A. Start with one tested 640x480-class, 4:3 output mode, color buffers and a depth buffer. Validate mode support rather than assuming progressive output. Keep geometry and GPU resources persistent across frames, and respect GPU allocation and in-flight resource lifetimes.

## Sources and handler standards

Both supplied documents were read in full:

- `docs/original_xbox_development_guide.md`: nxdk, pbkit, XBE/XISO, video bring-up, resource constraints and deployment.
- `docs/Xbox_Rendering.md`: indexed geometry, shared memory, submission and synchronization. Its performance estimates are not acceptance targets; its exported citation tokens do not resolve locally.

The closest precedent is `../helengine-gc/docs/superpowers/specs/2026-05-01-helengine-gamecube-bootstrap-design.md`: reproducible Docker build, thin entry point, dedicated native host, explicit failures and no generated-Core dependency.

Also inspected the GameCube README, Dreamcast boot/video/render components, later authored-scene designs, and the active engine builder and camera contracts. Those later contracts inform future integration, not v0.1 completion.

Observed revisions during research: engine `85533277`, Dreamcast `d1cd2ef`, GameCube `1b0791a`, Xbox `6fcbe79`. Recheck working-tree state before execution.

## Native structure

| Proposed file | Responsibility |
| --- | --- |
| `docker/Dockerfile`, `toolchain-lock.txt` | Reproducible, pinned nxdk/compiler environment |
| `Makefile`, `scripts/build.ps1` | Compile/link, XBE generation and XISO packaging |
| `src/main.cpp` | Construct boot host and return its result |
| `src/platform/xbox/XboxBootHost.hpp/.cpp` | Initialization order, diagnostic frame loop and shutdown |
| `src/platform/xbox/XboxVideo.hpp/.cpp` | Video/pbkit setup, clear and presentation |
| `src/platform/xbox/XboxCubeMesh.hpp/.cpp` | Native indexed cube and face-color data |
| `src/platform/xbox/XboxRasterRenderer.hpp/.cpp` | GPU resources, transforms, unlit state and draws |
| `scripts/launch_in_emulator.ps1` | Explicit artifact selection and xemu launch |
| `README.md`, `docs/PlatformNotes.md` | Reproducible build/run steps and current limits |

Keep platform logic out of main. Give presentation one owner: the boot host sequences video begin-frame, raster draw and video end-frame/present. Reuse existing math conventions where practical, with small native transform code if necessary; Core is not a dependency. Do not build a general renderer abstraction or scene system for this probe.

Use `C:/dev/helworks/builds/helengine-xbox/v01/<build-id>` for agent-owned outputs, staged images and logs. Preserve explicit working/output-root parameters. Do not direct these artifacts into the system temporary directory or accept stale outputs as successful builds.

## Implementation checkpoints

### 1. Toolchain and video

Pin nxdk and the Docker build environment. Build a minimal native host, initialize video and pbkit, and repeatedly clear/present a solid color. Produce a fresh `default.xbe` and a bootable XISO for the xemu test flow.

**Pass:** the current build boots to a stable clear screen. Record the selected output mode, buffer formats/counts and allocations. Initialization failures stop with a readable diagnostic.

### 2. Minimal GPU draw

Follow the pinned nxdk samples to establish vertex layout, a minimal vertex program, unlit combiner state and command submission. Use a temporary triangle probe if it helps isolate graphics setup.

**Pass:** known geometry renders through one clear/draw/present cycle. This is an intermediate diagnostic checkpoint.

### 3. Unlit cube

Add the indexed native cube, fixed camera and perspective transform. Bind persistent GPU-readable geometry and per-face colors. Enable depth and culling; verify winding and clip-space conventions rather than assuming desktop API behavior. Allocate outside the normal frame loop and wait for GPU consumption before releasing resources.

**Pass:** a static angled cube displays correct perspective, visible faces and occlusion. Check alternate cube/camera poses and near/far clipping behavior. No Core, cooked content or scene loader is involved.

### 4. Repeatable verification

Document native build and launch commands, verify artifact freshness, and boot the packaged result. Run for at least 60 seconds with presentation progress and stable native resource ownership. Use focused geometry/index and transform checks where useful, plus the actual target build and runtime check.

**Pass:** the documented flow reproduces the cube. Build/setup failures are explicit and cannot launch stale output. Report emulator and real-hardware verification separately. No screenshots of the user's screen without explicit authorization.

## Deferred to subsequent versions

- Generated Core and engine initialization/update/draw adapters.
- Platform builder discovery, editor installation and normal project-build integration.
- Authored scenes/cameras, cooked assets, material schemas and startup catalogs.
- Any changes to demodisc or its startup scene.
- Textures, lighting, audio, general input, UI, physics and gameplay.
- Batching, LOD, general memory arenas or broad profiling/optimization infrastructure.

After native graphics is proven, a later milestone can replace diagnostic data with normal engine submissions while preserving the established handler contracts.

## Choices to verify during execution

Record the exact SDK commit, image version, tested video mode, buffer formats and NV2A command sequence during bring-up. Inspect the local emulator configuration before implementing its launcher. These are bounded implementation choices, not additional milestone requirements.

Primary references: [nxdk](https://github.com/XboxDev/nxdk) and [xemu disc images](https://xemu.app/docs/disc-images/). The supplied research guides architecture; the pinned SDK supplies the exact API reference.

**Definition of done:** a fresh native Xbox build boots, initializes video, and continuously displays an unlit cube with correct perspective, depth and culling, without Core.
