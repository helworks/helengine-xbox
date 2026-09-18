# Helengine Original Xbox Handler

Native Original Xbox host for Helengine. Version 0.1 is a standalone native
graphics bootstrap: it initializes video through the pinned nxdk/pbkit stack and
continuously renders a static, unlit, indexed cube with perspective, depth
testing/writing and back-face culling. No generated Core, engine runtime,
authored scenes, cooked assets, editor discovery or platform-builder integration
is involved yet; the cube geometry and camera are native diagnostic constants.

## Prerequisites

- Docker Desktop (Linux containers). The whole compiler/SDK lives in the image.
- The pinned nxdk checkout at `C:\dev\helworks\builds\helengine-xbox\dependencies\nxdk`
  (commit `29638d0b001f179b73c3513489af10ddc2986216`, submodules initialized,
  working tree normalized to LF: `core.autocrlf=false`). It is the Docker build
  context for the toolchain image and is verified before every build.
- Windows PowerShell 5.1 or later for the scripts.
- xemu for runtime verification, supplied by you (see "Run in emulator").

## Toolchain image

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\prepare_toolchain.ps1
```

Builds `helengine-xbox-sdk:v01` from `docker/Dockerfile` (base
`ghcr.io/xboxdev/nxdk-buildbase` by digest, nxdk tools and libraries built at
the pinned commit) and regenerates `toolchain-lock.txt` with the image ID and
tool versions. Builds refuse to run if the image ID or the checkout commit differ
from the lock.

## Build

```powershell
$runRoot = 'C:\dev\helworks\builds\helengine-xbox\v01\cube-debug-01'
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -OutputRoot $runRoot -Configuration Debug -Probe Cube
if ($LASTEXITCODE -ne 0) { throw 'Cube build failed' }
```

Parameters:

| Parameter | Values | Default |
| --- | --- | --- |
| `-OutputRoot` | fresh directory (must not exist) | `...\v01\<utc-stamp>-<probe>-<config>` |
| `-Configuration` | `Debug` (`-O0 -g`, PDB) or `Release` (`-O2`) | `Debug` |
| `-Probe` | `Clear`, `Triangle`, `Cube` | `Cube` |
| `-StopAfterFrames` | nonnegative integer; `0` = present forever | `0` |
| `-Pose` | `Baseline`, `Far`, `NearPlane`, `ScreenEdge` | `Baseline` |
| `-DisableCulling`, `-ReverseTriangleOrder` | diagnostic switches | off |

Unsupported values fail during parameter binding, before Docker starts. The
script never overwrites an existing invocation directory.

Output layout under `-OutputRoot`:

```text
build-info.json            success record (written last; absent on failure)
artifacts/default.xbe      XBE (validated: nonempty, magic XBEH)
artifacts/game.iso         XISO with default.xbe at the disc root (validated with extract-xiso -l)
artifacts/main.map         linker map; main.exe (PE input to cxbe); main.pdb (Debug)
artifacts/generated-shaders/unlit_vsh.inl, unlit_psh.inl
artifacts/build-info.json  copy of the record for the launcher
work/                      staged sources, objects and disc root used by make
logs/                      environment.txt, docker-build.log, xiso-listing.txt, source hashes
```

`build-info.json` records the source commit and dirty state, nxdk commit, image
identity, configuration, probe, pose, UTC start/end, artifact paths and SHA-256
hashes.

## Authored builds

`-Mode Authored` links an editor-generated Core and packages a cooked content
tree onto the disc, instead of building the native probe:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 `
  -OutputRoot C:\dev\helworks\builds\helengine-xbox\v02\authored-01 `
  -Configuration Release -Mode Authored `
  -GeneratedCoreRoot <generated core root> `
  -ContentRoot <cooked content tree> `
  [-CatalogRoot <scene catalog sources>]
```

| Parameter | Meaning |
| --- | --- |
| `-GeneratedCoreRoot` | Generated Core C++ root; must contain `generated_unity.cpp` and `Core.hpp` |
| `-ContentRoot` | Cooked content tree copied onto the disc beside `default.xbe`; must not be empty |
| `-CatalogRoot` | Optional scene catalog sources compiled alongside Core |

Both roots are mounted read-only into the container, so a build cannot modify
what it was handed. The generated Core compiles as one unity translation unit at
C++20 with warnings off, RTTI on and exceptions off, matching a Core generated
with `codegen-use-exceptions=false`; `__RTDynamicCast` comes from
`src/platform/xbox/XboxCxxAbi.cpp`.

Authored builds reject the diagnostic probe selectors (`-Probe`, `-Pose`,
`-StopAfterFrames`, `-DisableCulling`, `-ReverseTriangleOrder`) rather than
accepting them with no effect, and `-Mode Diagnostic` rejects the authored roots
for the same reason.

The editor drives this through `builder/`, which the engine discovers via
`user_settings/platforms.json`.

## Test (CPU geometry and math)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-native.ps1 `
  -OutputRoot C:\dev\helworks\builds\helengine-xbox\v01\native-tests-01
```

Compiles `tests/native_contract_tests.cpp` with the geometry/math sources using
the container's host `g++` with assertions forced on, then runs it. No nxdk
headers and no Xbox binary are involved.

## Run in emulator

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch_in_emulator.ps1 `
  -ArtifactPath C:\dev\helworks\builds\helengine-xbox\v01\cube-debug-01\artifacts\game.iso `
  -EmulatorPath C:\path\to\xemu.exe -RunSeconds 60
```

The launcher requires the explicit image, its sibling `build-info.json`, a
matching SHA-256, and an existing emulator executable (`-EmulatorPath` or
`HELENGINE_XBOX_XEMU_PATH`). It never substitutes another artifact and never
touches your xemu configuration. It passes `-dvd_path <iso>` (and `-config_path`
only if you give `-ConfigPath`).

xemu 0.8.136 is installed at
`C:\dev\helworks\emus\xemu-0.8.136-windows-x86_64\xemu.exe`, but these scripts do
not download or configure it. xemu needs, in its own `xemu.toml`, an MCPX boot
ROM and an Xbox-compatible BIOS that you must obtain from hardware you own, plus
a hard-disk image (xemu offers a blank, copyright-free one on first run). The
firmware is not provided or located by this repository, and the launcher never
edits your xemu configuration.

## What the probe shows

- `Clear`: solid dark blue-grey frame with a two-line pbkit text overlay
  (`helengine-xbox v0.1 Clear frame N`, last diagnostic line).
- `Triangle`: one red/green/blue triangle drawn through the same vertex,
  index and state path as the cube.
- `Cube` (default): a static cube, half-extent 1, model rotation
  `RotationY(30°) * RotationX(-20°)`, camera at distance 5 looking down -Z,
  60° vertical FOV, near 0.1, far 100. Faces: +Z red, -Z green, +X blue,
  -X yellow, +Y cyan, -Y magenta, unlit. Overlay shows frame count and the
  renderer's allocation/release/draw counters (expected: alloc 1, free 0
  while running).

With `-StopAfterFrames N` the host presents N frames, drains the GPU, releases
the renderer allocation, stops pbkit, shows the debug console for five seconds
and returns; nxdk's runtime then reboots to the dashboard.

## Verify the rendered frame

xemu has no working `screendump`, so the frame is recovered from emulated
memory rather than from the host screen:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\capture_emulator_frame.ps1 `
  -ArtifactPath C:\dev\helworks\builds\helengine-xbox\v01\cube-debug-01\artifacts\game.iso `
  -RunDir C:\dev\helworks\builds\helengine-xbox\v01\capture-01 `
  -EmulatorPath C:\dev\helworks\emus\xbox\xemu-0.8.136-windows-x86_64\xemu.exe `
  -ConfigPath C:\dev\helworks\builds\helengine-xbox\v01\xemu-setup\xemu.toml

python tools\decode_framebuffer.py `
  C:\dev\helworks\builds\helengine-xbox\v01\capture-01\guest-ram.bin `
  C:\dev\helworks\builds\helengine-xbox\v01\capture-01\frame --expect-cube
```

The capture boots with `-snapshot`, so the hard-disk image is never modified,
and reads only the emulated console's own memory. `--expect-cube` asserts that
the three camera-facing colours are present and their opposites absent, which
fails loudly if winding, culling or depth regress.

## Verification status

See `docs/PlatformNotes.md` for the API worksheet, conventions and the current
verification state (build/test evidence versus emulator and hardware evidence).
