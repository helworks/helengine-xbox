[CmdletBinding()]
param(
    [string]$NxdkPath = 'C:\dev\helworks\builds\helengine-xbox\dependencies\nxdk',
    [string]$DockerImage = 'helengine-xbox-sdk:v01',
    [string]$ExpectedNxdkCommit = '29638d0b001f179b73c3513489af10ddc2986216',
    [string]$LogRoot
)

# Builds the pinned nxdk toolchain image from the local nxdk checkout and
# regenerates toolchain-lock.txt with the resulting identities. Review the lock
# diff before committing it.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$NxdkPath = [IO.Path]::GetFullPath($NxdkPath)
$dockerfile = Join-Path $repoRoot 'docker\Dockerfile'
$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'
if ([string]::IsNullOrWhiteSpace($LogRoot)) {
    $LogRoot = Join-Path 'C:\dev\helworks\builds\helengine-xbox\v01' ('toolchain-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'))
}
$LogRoot = [IO.Path]::GetFullPath($LogRoot)

function Invoke-CheckedNative {
    param([string]$FilePath, [string[]]$ArgumentList)
    # Windows PowerShell 5.1 turns native stderr into terminating errors under
    # ErrorActionPreference=Stop, so keep stderr on its own stream here.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $FilePath @ArgumentList 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previous
    }
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $FilePath $($ArgumentList -join ' ')`n$($output -join [Environment]::NewLine)"
    }
    return @($output | Where-Object { $_ -isnot [Management.Automation.ErrorRecord] } | ForEach-Object { "$_" })
}

function Invoke-NativeToLog {
    # Runs a native command with stdout/stderr captured to LogPath (merged,
    # stdout first) and returns the exit code. Used for long docker runs whose
    # progress output lands on stderr.
    param([string]$FilePath, [string[]]$ArgumentList, [string]$LogPath)
    $stdoutPath = "$LogPath.stdout"
    $stderrPath = "$LogPath.stderr"
    $quoted = $ArgumentList | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
    $process = Start-Process -FilePath $FilePath -ArgumentList ($quoted -join ' ') -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $merged = @()
    if (Test-Path -LiteralPath $stdoutPath) { $merged += @('--- stdout ---'); $merged += @(Get-Content -LiteralPath $stdoutPath) }
    if (Test-Path -LiteralPath $stderrPath) { $merged += @('--- stderr ---'); $merged += @(Get-Content -LiteralPath $stderrPath) }
    Set-Content -LiteralPath $LogPath -Value $merged -Encoding UTF8
    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    return $process.ExitCode
}

if (-not (Test-Path -LiteralPath $dockerfile -PathType Leaf)) { throw "Dockerfile missing: $dockerfile" }
if (-not (Test-Path -LiteralPath $NxdkPath -PathType Container)) { throw "nxdk checkout does not exist: $NxdkPath" }
if (-not (Test-Path -LiteralPath (Join-Path $NxdkPath '.dockerignore') -PathType Leaf)) { throw "nxdk checkout has no .dockerignore; refusing to send an unfiltered context" }
if (Test-Path -LiteralPath $LogRoot) { throw "Refusing to reuse existing log directory: $LogRoot" }

$actualCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$NxdkPath", '-C', $NxdkPath, 'rev-parse', 'HEAD')) -join '').Trim()
if ($actualCommit -ne $ExpectedNxdkCommit) {
    throw "nxdk checkout is at $actualCommit, expected $ExpectedNxdkCommit"
}
$status = @(Invoke-CheckedNative 'git' @('-c', "safe.directory=$NxdkPath", '-C', $NxdkPath, 'status', '--porcelain', '--untracked-files=all'))
if ($status.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace(($status -join ''))) {
    throw "nxdk checkout is not clean:`n$($status -join [Environment]::NewLine)"
}
$submodules = @(Invoke-CheckedNative 'git' @('-c', "safe.directory=$NxdkPath", '-C', $NxdkPath, 'submodule', 'status', '--recursive'))
foreach ($line in $submodules) {
    if ($line.Length -gt 0 -and $line[0] -ne ' ') { throw "nxdk submodule is not at its recorded commit: $line" }
}
# The checkout is copied verbatim into a Linux image; CRLF working-tree files
# (core.autocrlf=true on Windows) break nxdk's sh wrappers and Makefiles.
$eols = @(Invoke-CheckedNative 'git' @('-c', "safe.directory=$NxdkPath", '-C', $NxdkPath, 'ls-files', '--eol', 'bin', 'Makefile', 'lib/Makefile'))
$crlf = @($eols | Where-Object { $_ -match 'w/crlf' })
if ($crlf.Count -gt 0) {
    throw "nxdk checkout has CRLF working-tree files; renormalize it with core.autocrlf=false (git rm --cached -r . && git reset --hard):`n$($crlf -join [Environment]::NewLine)"
}

New-Item -ItemType Directory -Path $LogRoot | Out-Null
$buildLog = Join-Path $LogRoot 'docker-image-build.log'
$dockerArgs = @('build', '--progress=plain', '-f', $dockerfile, '--build-arg', "NXDK_COMMIT=$ExpectedNxdkCommit", '-t', $DockerImage, $NxdkPath)
Set-Content -LiteralPath (Join-Path $LogRoot 'docker-image-command.txt') -Value ("docker " + ($dockerArgs -join ' ')) -Encoding UTF8
Write-Output "Building $DockerImage (log: $buildLog)"
$buildExit = Invoke-NativeToLog -FilePath 'docker' -ArgumentList $dockerArgs -LogPath $buildLog
if ($buildExit -ne 0) { throw "docker build failed with exit code $buildExit. See $buildLog" }

$imageId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $DockerImage, '--format', '{{.Id}}')) -join '').Trim()
$baseDigest = 'ghcr.io/xboxdev/nxdk-buildbase@sha256:b14cd17f59449c0c3bdc86dc04dde411cdd0eaaeb3539fbe85a598dec3846f52'
$probeScript = 'set -e; cat /opt/nxdk/HELENGINE_NXDK_COMMIT; clang --version | head -n1; ld.lld --version | head -n1; make --version | head -n1; cmake --version | head -n1; g++ --version | head -n1; . /etc/os-release; echo "$PRETTY_NAME"'
$probe = @(Invoke-CheckedNative 'docker' @('run', '--rm', '--entrypoint', '/bin/sh', $DockerImage, '-c', $probeScript))
if ($probe.Count -lt 7) { throw "Unexpected toolchain probe output:`n$($probe -join [Environment]::NewLine)" }
if ($probe[0].Trim() -ne $ExpectedNxdkCommit) { throw "Image records nxdk commit $($probe[0]) but expected $ExpectedNxdkCommit" }

$lockLines = @(
    '# Generated by scripts/prepare_toolchain.ps1. Review changes before use.',
    'LockFormat=1',
    "LocalImageTag=$DockerImage",
    "LocalImageId=$imageId",
    "UpstreamBaseImage=$baseDigest",
    'NxdkRepository=https://github.com/XboxDev/nxdk.git',
    "NxdkCommit=$ExpectedNxdkCommit",
    'NxdkDirInImage=/opt/nxdk',
    "CompilerC=$($probe[1].Trim())",
    "Linker=$($probe[2].Trim())",
    "Make=$($probe[3].Trim())",
    "CMake=$($probe[4].Trim())",
    "HostCxx=$($probe[5].Trim())",
    "ImageOs=$($probe[6].Trim())",
    'TargetTriple=i386-pc-win32',
    'TargetArch=-march=pentium3',
    'ShaderTools=vp20compiler (NV_vertex_program 1.1 text) + fp20compiler (RC1.0 register combiner text); NVIDIA cgc is not used',
    'Cxbe=/opt/nxdk/tools/cxbe/cxbe',
    'ExtractXiso=/opt/nxdk/tools/extract-xiso/build/extract-xiso'
)
Set-Content -LiteralPath $lockPath -Value $lockLines -Encoding UTF8
Write-Output "Toolchain image $DockerImage = $imageId"
Write-Output "Lock written: $lockPath"
Write-Output "Logs: $LogRoot"
