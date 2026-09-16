[CmdletBinding()]
param(
    # Explicit XISO produced by scripts/build.ps1 (…/artifacts/game.iso).
    [Parameter(Mandatory = $true)]
    [string]$ArtifactPath,
    # xemu executable. No default location is guessed; set
    # HELENGINE_XBOX_XEMU_PATH or pass the path explicitly.
    [string]$EmulatorPath = $env:HELENGINE_XBOX_XEMU_PATH,
    # Optional explicit xemu.toml (passed as -config_path). Existing user
    # configuration is never modified by this script.
    [string]$ConfigPath,
    # When > 0, keep the emulator running this long, then stop it and report
    # whether it stayed alive; when 0, wait for the emulator to exit.
    [ValidateRange(0, 86400)]
    [int]$RunSeconds = 0,
    [string[]]$ExtraArguments = @()
)

# Launches the current build's XISO in xemu after validating it against the
# build record. Missing emulator, missing image, or hash mismatch fail with
# concrete diagnostics; no other artifact is ever substituted.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ArtifactPath = [IO.Path]::GetFullPath($ArtifactPath)
if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) { throw "Disc image does not exist: $ArtifactPath" }
$artifactItem = Get-Item -LiteralPath $ArtifactPath
if ($artifactItem.Length -le 0) { throw "Disc image is empty: $ArtifactPath" }
$artifactDir = Split-Path -Parent $ArtifactPath
$buildInfoPath = Join-Path $artifactDir 'build-info.json'
if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) { throw "Build record is missing beside the image; refusing to launch an unrecorded artifact: $buildInfoPath" }
$buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
$outputRoot = [string]$buildInfo.outputRoot
$expectedRelative = [string]$buildInfo.xiso
$expectedPath = [IO.Path]::GetFullPath((Join-Path $outputRoot $expectedRelative))
if (-not $expectedPath.Equals($ArtifactPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build record points at $expectedPath, not $ArtifactPath"
}
$record = $buildInfo.artifacts | Where-Object { $_.path -eq $expectedRelative } | Select-Object -First 1
if ($null -eq $record) { throw "Build record has no hash entry for $expectedRelative" }
$actualHash = (Get-FileHash -LiteralPath $ArtifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne ([string]$record.sha256).ToLowerInvariant()) {
    throw "game.iso SHA-256 mismatch; refusing to launch. Recorded $($record.sha256), found $actualHash."
}

if ([string]::IsNullOrWhiteSpace($EmulatorPath)) {
    throw 'No emulator path given. Pass -EmulatorPath <xemu.exe> or set HELENGINE_XBOX_XEMU_PATH. xemu is not part of the shared C:\dev\helworks\emus directory and no location is guessed.'
}
$EmulatorPath = [IO.Path]::GetFullPath($EmulatorPath)
if (-not (Test-Path -LiteralPath $EmulatorPath -PathType Leaf)) { throw "Emulator executable does not exist: $EmulatorPath" }
$arguments = @('-dvd_path', ('"' + $ArtifactPath + '"'))
if (-not [string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
    if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) { throw "Config file does not exist: $ConfigPath" }
    $arguments += @('-config_path', ('"' + $ConfigPath + '"'))
}
$arguments += $ExtraArguments

$launchRoot = Join-Path $outputRoot ('emulator-' + (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $launchRoot | Out-Null
$argumentText = $arguments -join ' '
Set-Content -LiteralPath (Join-Path $launchRoot 'emulator-command.txt') -Value ('"' + $EmulatorPath + '" ' + $argumentText) -Encoding UTF8
$startUtc = [DateTime]::UtcNow
Write-Output "Launching: $EmulatorPath $argumentText"
$process = Start-Process -FilePath $EmulatorPath -ArgumentList $argumentText -WorkingDirectory (Split-Path -Parent $EmulatorPath) -PassThru

$outcome = [ordered]@{
    launcherVersion = 1
    emulatorPath = $EmulatorPath
    emulatorSha256 = (Get-FileHash -LiteralPath $EmulatorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    arguments = $argumentText
    artifact = $ArtifactPath
    artifactSha256 = $actualHash
    buildRecord = $buildInfoPath
    processId = $process.Id
    startUtc = $startUtc.ToString('o')
    requestedRunSeconds = $RunSeconds
}
if ($RunSeconds -gt 0) {
    $exited = $process.WaitForExit($RunSeconds * 1000)
    $elapsed = ([DateTime]::UtcNow - $startUtc).TotalSeconds
    if ($exited) {
        $outcome.result = "emulator exited early after $([Math]::Round($elapsed, 1)) s with code $($process.ExitCode)"
        $outcome.exitCode = $process.ExitCode
        $outcome | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $launchRoot 'emulator-outcome.json') -Encoding UTF8
        throw $outcome.result
    }
    Stop-Process -Id $process.Id -Force
    $outcome.result = "emulator stayed alive for the requested $RunSeconds s and was stopped by the launcher"
}
else {
    $process.WaitForExit()
    $outcome.result = "emulator exited with code $($process.ExitCode)"
    $outcome.exitCode = $process.ExitCode
}
$outcome.endUtc = [DateTime]::UtcNow.ToString('o')
$outcome | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $launchRoot 'emulator-outcome.json') -Encoding UTF8
Write-Output $outcome.result
Write-Output "Launch record: $launchRoot"
Write-Output 'Note: a running or exited emulator process is not visual evidence; record what was observed on screen separately.'
