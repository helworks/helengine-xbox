[CmdletBinding()]
param(
    # Unique output directory for the compiled test binary and its log.
    [string]$OutputRoot,
    [string]$DockerImage = 'helengine-xbox-sdk:v01'
)

# Compiles the CPU-only geometry/math/texture sources plus tests/native_contract_tests.cpp
# with the pinned container's host compiler (assertions forced on) and runs the
# resulting host executable. No nxdk headers and no Xbox binary are involved.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path 'C:\dev\helworks\builds\helengine-xbox\v01' ((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-native-tests')
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'
if (Test-Path -LiteralPath $OutputRoot) { throw "Refusing to reuse existing output directory: $OutputRoot" }
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

New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$logPath = Join-Path $OutputRoot 'native-tests.log'
$inner = 'set -e; ' +
    'g++ --version | head -n1; ' +
    'g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src ' +
    '/src/tests/native_contract_tests.cpp /src/src/platform/xbox/XboxCubeMesh.cpp /src/src/platform/xbox/XboxMath.cpp ' +
    '/src/src/platform/xbox/XboxContentPath.cpp /src/src/platform/xbox/XboxAuthoredTransform.cpp ' +
    '/src/src/platform/xbox/XboxTextureFormat.cpp ' +
    '-o /out/native_contract_tests; ' +
    '/out/native_contract_tests'
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
