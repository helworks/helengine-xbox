[CmdletBinding()]
param(
    # XISO produced by scripts/build.ps1 (…/artifacts/game.iso).
    [Parameter(Mandatory = $true)][string]$ArtifactPath,
    # Fresh directory for the capture; must not already exist.
    [Parameter(Mandatory = $true)][string]$RunDir,
    [string]$EmulatorPath = $env:HELENGINE_XBOX_XEMU_PATH,
    # Isolated xemu config naming the MCPX ROM, BIOS and HDD image. The user's
    # own configuration is never read or written by this script.
    [Parameter(Mandatory = $true)][string]$ConfigPath,
    [ValidateRange(1024, 65535)][int]$QmpPort = 4511,
    # Seconds to let the title run before dumping emulated RAM.
    [ValidateRange(1, 3600)][int]$DumpAtSeconds = 40,
    [ValidateRange(1, 3600)][int]$TotalSeconds = 45
)

# Boots the XISO in xemu and captures the EMULATED CONSOLE's video memory by
# dumping emulated guest RAM through the QEMU monitor (pmemsave). Nothing
# belonging to the host desktop is read or captured: the dump contains only the
# emulated Xbox's own 64 MiB address space. tools/decode_framebuffer.py turns
# the dump into a PNG of the console's framebuffer.
#
# xemu strips QEMU's screendump from both QMP and HMP, so pmemsave plus an
# offline decode is the available programmatic route to the rendered frame.
#
# -snapshot is always passed so the hard-disk image cannot be modified.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ArtifactPath = [IO.Path]::GetFullPath($ArtifactPath)
$ConfigPath = [IO.Path]::GetFullPath($ConfigPath)
if (-not (Test-Path -LiteralPath $ArtifactPath -PathType Leaf)) { throw "Disc image does not exist: $ArtifactPath" }
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) { throw "xemu config does not exist: $ConfigPath" }
if (Test-Path -LiteralPath $RunDir) { throw "Refusing to reuse existing capture directory: $RunDir" }
if ([string]::IsNullOrWhiteSpace($EmulatorPath)) {
    throw 'No emulator path given. Pass -EmulatorPath <xemu.exe> or set HELENGINE_XBOX_XEMU_PATH.'
}
$EmulatorPath = [IO.Path]::GetFullPath($EmulatorPath)
if (-not (Test-Path -LiteralPath $EmulatorPath -PathType Leaf)) { throw "Emulator executable does not exist: $EmulatorPath" }
if ($DumpAtSeconds -ge $TotalSeconds) { throw 'DumpAtSeconds must be smaller than TotalSeconds.' }

$buildInfoPath = Join-Path (Split-Path -Parent $ArtifactPath) 'build-info.json'
if (-not (Test-Path -LiteralPath $buildInfoPath -PathType Leaf)) { throw "Build record is missing beside the image: $buildInfoPath" }
$buildInfo = Get-Content -LiteralPath $buildInfoPath -Raw | ConvertFrom-Json
$record = $buildInfo.artifacts | Where-Object { $_.path -eq $buildInfo.xiso } | Select-Object -First 1
if ($null -eq $record) { throw "Build record has no hash entry for $($buildInfo.xiso)" }
$actualHash = (Get-FileHash -LiteralPath $ArtifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actualHash -ne ([string]$record.sha256).ToLowerInvariant()) {
    throw "game.iso SHA-256 mismatch; refusing to launch. Recorded $($record.sha256), found $actualHash."
}

New-Item -ItemType Directory -Path $RunDir | Out-Null

function Send-Qmp {
    param($Stream, [string]$Json, [int]$SettleMs = 700)
    $bytes = [Text.Encoding]::UTF8.GetBytes($Json + "`r`n")
    $Stream.Write($bytes, 0, $bytes.Length); $Stream.Flush(); Start-Sleep -Milliseconds $SettleMs
    $buffer = New-Object byte[] 262144; $sb = New-Object Text.StringBuilder
    while ($Stream.DataAvailable) {
        $n = $Stream.Read($buffer, 0, $buffer.Length)
        [void]$sb.Append([Text.Encoding]::UTF8.GetString($buffer, 0, $n))
        Start-Sleep -Milliseconds 150
    }
    return $sb.ToString()
}

$arguments = @(
    '-config_path', "`"$ConfigPath`"",
    '-snapshot',
    '-dvd_path', "`"$ArtifactPath`"",
    '-qmp', "tcp:127.0.0.1:$QmpPort,server=on,wait=off"
)
Set-Content -LiteralPath (Join-Path $RunDir 'emulator-command.txt') -Value "$EmulatorPath $($arguments -join ' ')" -Encoding UTF8
$proc = Start-Process -FilePath $EmulatorPath -ArgumentList $arguments -PassThru `
    -RedirectStandardOutput (Join-Path $RunDir 'stdout.log') -RedirectStandardError (Join-Path $RunDir 'stderr.log')
Write-Output "xemu pid $($proc.Id); QMP 127.0.0.1:$QmpPort"

$log = [Collections.Generic.List[string]]::new()
$log.Add("Artifact=$ArtifactPath")
$log.Add("ArtifactSha256=$actualHash")
$log.Add("Emulator=$EmulatorPath")
$dumpPath = Join-Path $RunDir 'guest-ram.bin'
try {
    $client = New-Object Net.Sockets.TcpClient
    $deadline = (Get-Date).AddSeconds(45)
    while ($true) {
        try { $client.Connect('127.0.0.1', $QmpPort); break }
        catch {
            if ((Get-Date) -gt $deadline) { throw 'xemu never opened its QMP port' }
            Start-Sleep -Milliseconds 500
        }
    }
    $stream = $client.GetStream(); Start-Sleep -Milliseconds 800
    $scratch = New-Object byte[] 65536
    while ($stream.DataAvailable) { [void]$stream.Read($scratch, 0, $scratch.Length) }
    [void](Send-Qmp -Stream $stream -Json '{"execute":"qmp_capabilities"}')

    $start = Get-Date
    Start-Sleep -Seconds $DumpAtSeconds
    $status = Send-Qmp -Stream $stream -Json '{"execute":"query-status"}'
    $log.Add("StatusAtDump=$($status.Trim())")
    $escaped = $dumpPath.Replace('\', '\\')
    $reply = Send-Qmp -Stream $stream -SettleMs 15000 `
        -Json ('{"execute":"human-monitor-command","arguments":{"command-line":"pmemsave 0 0x4000000 ' + $escaped + '"}}')
    if (-not (Test-Path -LiteralPath $dumpPath)) { throw "pmemsave produced no dump. Monitor said: $reply" }
    $dumpBytes = (Get-Item -LiteralPath $dumpPath).Length
    if ($dumpBytes -ne 0x4000000) { throw "Guest RAM dump is $dumpBytes bytes, expected 67108864." }
    $log.Add("DumpBytes=$dumpBytes")
    Write-Output "dumped emulated RAM: $dumpPath ($dumpBytes bytes)"

    $remaining = $TotalSeconds - ((Get-Date) - $start).TotalSeconds
    if ($remaining -gt 0) { Start-Sleep -Milliseconds ([int]($remaining * 1000)) }
    $alive = -not $proc.HasExited
    $final = Send-Qmp -Stream $stream -Json '{"execute":"query-status"}'
    $log.Add("AliveAfter${TotalSeconds}s=$alive")
    $log.Add("FinalStatus=$($final.Trim())")
    Write-Output "alive after $TotalSeconds s: $alive"
    if (-not $alive) { throw "xemu exited early with code $($proc.ExitCode)" }
}
finally {
    if ($client) { $client.Close() }
    if ($proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Set-Content -LiteralPath (Join-Path $RunDir 'capture.log') -Value $log -Encoding UTF8
}
Write-Output "Capture complete: $RunDir"
Write-Output "Decode with: python tools/decode_framebuffer.py `"$dumpPath`" `"$RunDir/frame`""
