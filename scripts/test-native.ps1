[CmdletBinding()]
param(
    # Reusable output directory for the compiled test binaries and their log.
    [string]$OutputRoot,
    [string]$DockerImage = 'helengine-xbox-sdk:v01'
)

# Compiles the CPU-only geometry/math/texture/sprite sources plus tests/native_contract_tests.cpp
# with the pinned container's host compiler (assertions forced on) and runs the
# resulting host executable. Also compiles the sprite shader with fp20compiler
# and verifies its emitted register writes using nxdk register definitions.
# Both tests run on the host; no Xbox binary is executed.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path (Join-Path $repoRoot '.xbox\verification') 'native-tests'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { throw "Toolchain lock is missing: $lockPath" }

$lockImageId = ''
$lockImageTag = ''
foreach ($line in Get-Content -LiteralPath $lockPath) {
    if ($line.StartsWith('LocalImageId=')) { $lockImageId = $line.Substring('LocalImageId='.Length) }
    if ($line.StartsWith('LocalImageTag=')) { $lockImageTag = $line.Substring('LocalImageTag='.Length) }
}
if ($DockerImage -ne $lockImageTag) { throw "Docker image tag '$DockerImage' is not the locked tag '$lockImageTag'." }
$previous = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$actualImageId = (& docker image inspect $DockerImage --format '{{.Id}}' 2>&1 | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] }) -join ''
$inspectExit = $LASTEXITCODE
$ErrorActionPreference = $previous
if ($inspectExit -ne 0 -or $actualImageId.Trim() -ne $lockImageId) { throw "Toolchain image ID mismatch or image missing. Expected $lockImageId, found '$actualImageId'." }

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$logPath = Join-Path $OutputRoot 'native-tests.log'
$inner = 'set -e; ' +
    'g++ --version | head -n1; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/material_format_tests.cpp -o /out/material_format_tests; /out/material_format_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/native_contract_tests.cpp /src/src/platform/xbox/XboxCubeMesh.cpp /src/src/platform/xbox/XboxMath.cpp ' +
    '/src/src/platform/xbox/XboxContentPath.cpp /src/src/platform/xbox/XboxAuthoredTransform.cpp ' +
    '/src/src/platform/xbox/XboxTextureFormat.cpp /src/src/platform/xbox/XboxTextureUpload.cpp ' +
    '/src/src/platform/xbox/XboxLighting.cpp ' +
    '/src/src/platform/xbox/XboxSpriteGeometry.cpp ' +
    '-o /out/native_contract_tests; ' +
    '/out/native_contract_tests; ' +
    '/opt/nxdk/tools/fp20compiler/fp20compiler /src/src/platform/xbox/shaders/sprite.psh > /out/sprite_psh.inl; ' +
    '/opt/nxdk/tools/fp20compiler/fp20compiler /src/src/platform/xbox/shaders/unlit.psh > /out/unlit_psh.inl; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/opt/nxdk/lib -I/out ' +
    '/src/tests/sprite_shader_contract_tests.cpp -o /out/sprite_shader_contract_tests; ' +
    '/out/sprite_shader_contract_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src -I/opt/nxdk/lib ' +
    '/src/tests/shader_artifact_tests.cpp /src/src/platform/xbox/XboxShaderArtifact.cpp /src/src/platform/xbox/XboxShaderProgram.cpp ' +
    '-o /out/shader_artifact_tests; /out/shader_artifact_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/shader_constant_tests.cpp /src/src/platform/xbox/XboxShaderConstants.cpp ' +
    '-o /out/shader_constant_tests; /out/shader_constant_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src -I/opt/nxdk/lib ' +
    '/src/tests/shader_constant_upload_tests.cpp -o /out/shader_constant_upload_tests; /out/shader_constant_upload_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/batch2d_submission_tests.cpp -o /out/batch2d_submission_tests; /out/batch2d_submission_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/rounded_raster_tests.cpp /src/src/platform/xbox/XboxRoundedRaster.cpp -o /out/rounded_raster_tests; /out/rounded_raster_tests; ' +
    'gcc -c /opt/nxdk/lib/pdclib/platform/xbox/functions/math/lround.c -o /out/nxdk_lround.o; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/rounded_raster_tests.cpp /src/src/platform/xbox/XboxRoundedRaster.cpp /out/nxdk_lround.o ' +
    '-o /out/rounded_nxdk_math_tests; /out/rounded_nxdk_math_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/rounded_cache_tests.cpp /src/src/platform/xbox/XboxRoundedRaster.cpp -o /out/rounded_cache_tests; /out/rounded_cache_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src -I/opt/nxdk/lib ' +
    '/src/tests/shader_material_tests.cpp /src/src/platform/xbox/XboxShaderMaterial.cpp ' +
    '/src/src/platform/xbox/XboxShaderTextureBinding.cpp /src/src/platform/xbox/XboxTextureStageState.cpp ' +
    '/src/src/platform/xbox/XboxTextureUpload.cpp ' +
    '/src/src/platform/xbox/XboxShaderArtifact.cpp /src/src/platform/xbox/XboxShaderProgram.cpp ' +
    '/src/src/platform/xbox/XboxShaderConstants.cpp -o /out/shader_material_tests; /out/shader_material_tests; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/texture_stage_state_tests.cpp /src/src/platform/xbox/XboxTextureStageState.cpp ' +
    '/src/src/platform/xbox/XboxTextureUpload.cpp -I/opt/nxdk/lib -o /out/texture_stage_state_tests; /out/texture_stage_state_tests'
$dockerArgs = @(
    'run', '--rm',
    '--mount', "type=bind,source=$repoRoot,target=/src,readonly",
    '--mount', "type=bind,source=$OutputRoot,target=/out",
    '--entrypoint', '/bin/sh', $DockerImage, '-c', $inner
)
$quoted = $dockerArgs | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
$process = Start-Process -FilePath 'docker' -ArgumentList ($quoted -join ' ') -NoNewWindow -Wait -PassThru `
    -RedirectStandardOutput "$logPath.stdout" -RedirectStandardError "$logPath.stderr"
$merged = @('--- stdout ---') + @(Get-Content -LiteralPath "$logPath.stdout") + @('--- stderr ---') + @(Get-Content -LiteralPath "$logPath.stderr")
Set-Content -LiteralPath $logPath -Value $merged -Encoding UTF8
Remove-Item -LiteralPath "$logPath.stdout", "$logPath.stderr" -Force
$merged | ForEach-Object { Write-Output $_ }
if ($process.ExitCode -ne 0) { throw "Native contract tests failed with exit code $($process.ExitCode). See $logPath" }
Write-Output "Native contract tests passed. Log: $logPath"
