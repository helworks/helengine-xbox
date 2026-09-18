[CmdletBinding()]
param(
    # Unique invocation directory. Must not exist yet. Defaults to a fresh
    # timestamped directory under the canonical workspace build root.
    [string]$OutputRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    # Diagnostic builds the native probe from constants in this repository.
    # Authored links an editor-generated Core and packages cooked content.
    [ValidateSet('Diagnostic', 'Authored')]
    [string]$Mode = 'Diagnostic',
    # Authored mode inputs. Both roots are mounted read-only into the container.
    [string]$GeneratedCoreRoot,
    [string]$ContentRoot,
    [string]$CatalogRoot,
    [ValidateSet('Clear', 'Triangle', 'Cube', 'AbiChecks')]
    [string]$Probe = 'Cube',
    [ValidateRange(0, 2147483647)]
    [int]$StopAfterFrames = 0,
    # Diagnostic cube/camera pose (Task 3 verification aid).
    [ValidateSet('Baseline', 'Far', 'NearPlane', 'ScreenEdge')]
    [string]$Pose = 'Baseline',
    [switch]$DisableCulling,
    [switch]$ReverseTriangleOrder,
    [string]$DockerImage = 'helengine-xbox-sdk:v01',
    [string]$NxdkPath = 'C:\dev\helworks\builds\helengine-xbox\dependencies\nxdk',
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

# Builds default.xbe and game.iso for the native Xbox v0.1 probe inside the
# pinned toolchain container, validates both artifacts and writes
# build-info.json only after validation succeeds. Every failure propagates as
# a nonzero exit status and leaves no build-info.json behind.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$buildStartUtc = [DateTime]::UtcNow
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$defaultBuildRoot = 'C:\dev\helworks\builds\helengine-xbox\v01'
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $defaultBuildRoot ($buildStartUtc.ToString('yyyyMMdd-HHmmss') + '-' + $Probe.ToLowerInvariant() + '-' + $Configuration.ToLowerInvariant())
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$NxdkPath = [IO.Path]::GetFullPath($NxdkPath)
$lockPath = Join-Path $repoRoot 'toolchain-lock.txt'

function Invoke-CheckedNative {
    param([string]$FilePath, [string[]]$ArgumentList)
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

function Read-KeyValueFile {
    param([string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { throw "Malformed key/value line in ${Path}: $line" }
        $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
    }
    return $values
}

function Get-RelativePath {
    param([string]$Root, [string]$Path)
    $rootPrefix = $Root.TrimEnd('\', '/') + '\'
    if (-not $Path.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw "$Path is not under $Root" }
    return $Path.Substring($rootPrefix.Length).Replace('\', '/')
}

function Get-SourceFingerprint {
    $files = @(
        Get-Item -LiteralPath (Join-Path $repoRoot 'Makefile')
        Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src') -File -Recurse | Where-Object { $_.Extension -ne '.inl' }
    )
    $lines = @(
        foreach ($file in $files) {
            $relative = Get-RelativePath -Root $repoRoot -Path $file.FullName
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$relative=$hash"
        }
    ) | Sort-Object
    $text = $lines -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    $digest = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)))).Replace('-', '').ToLowerInvariant()
    return [PSCustomObject]@{ Lines = @($lines); Hash = $digest }
}

# --- Validation before anything is created ----------------------------------
$repoPrefix = $repoRoot.TrimEnd('\') + '\'
if ($OutputRoot.Equals($repoRoot, [StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutputRoot must be outside the source worktree: $OutputRoot"
}
if (Test-Path -LiteralPath $OutputRoot) { throw "Refusing to reuse existing invocation directory: $OutputRoot" }
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { throw "Toolchain lock is missing: $lockPath (run scripts/prepare_toolchain.ps1)" }
if (-not (Test-Path -LiteralPath $NxdkPath -PathType Container)) { throw "nxdk checkout does not exist: $NxdkPath" }

# --- Mode validation ---------------------------------------------------------
$diagnosticSelectors = @('Probe', 'StopAfterFrames', 'Pose', 'DisableCulling', 'ReverseTriangleOrder')
if ($Mode -eq 'Authored') {
    if ([string]::IsNullOrWhiteSpace($GeneratedCoreRoot)) { throw 'Authored mode requires -GeneratedCoreRoot.' }
    if ([string]::IsNullOrWhiteSpace($ContentRoot)) { throw 'Authored mode requires -ContentRoot.' }
    $GeneratedCoreRoot = [IO.Path]::GetFullPath($GeneratedCoreRoot)
    $ContentRoot = [IO.Path]::GetFullPath($ContentRoot)
    if (-not (Test-Path -LiteralPath $GeneratedCoreRoot -PathType Container)) { throw "Generated Core root does not exist: $GeneratedCoreRoot" }
    if (-not (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot 'generated_unity.cpp') -PathType Leaf)) {
        throw "Generated Core root does not contain generated_unity.cpp: $GeneratedCoreRoot"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $GeneratedCoreRoot 'Core.hpp') -PathType Leaf)) {
        throw "Generated Core root does not contain Core.hpp: $GeneratedCoreRoot"
    }
    if (-not (Test-Path -LiteralPath $ContentRoot -PathType Container)) { throw "Content root does not exist: $ContentRoot" }
    if (@(Get-ChildItem -LiteralPath $ContentRoot -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1).Count -eq 0) {
        throw "Content root contains no files: $ContentRoot"
    }
    if (-not [string]::IsNullOrWhiteSpace($CatalogRoot)) {
        $CatalogRoot = [IO.Path]::GetFullPath($CatalogRoot)
        if (-not (Test-Path -LiteralPath $CatalogRoot -PathType Container)) { throw "Catalog root does not exist: $CatalogRoot" }
    }
    # The probe selectors describe the native diagnostic scene, which an authored
    # build does not draw. Accepting them silently would imply they had an effect.
    $suppliedSelectors = @($diagnosticSelectors | Where-Object { $PSBoundParameters.ContainsKey($_) })
    if ($suppliedSelectors.Count -gt 0) {
        throw "Diagnostic selectors cannot be used in Authored mode: $($suppliedSelectors -join ', ')"
    }
}
else {
    foreach ($authoredOnly in @('GeneratedCoreRoot', 'ContentRoot', 'CatalogRoot')) {
        if ($PSBoundParameters.ContainsKey($authoredOnly)) {
            throw "-$authoredOnly requires -Mode Authored."
        }
    }
}

$lock = Read-KeyValueFile $lockPath
foreach ($key in @('LocalImageTag', 'LocalImageId', 'NxdkCommit', 'NxdkDirInImage')) {
    if (-not $lock.ContainsKey($key) -or [string]::IsNullOrWhiteSpace($lock[$key])) { throw "Toolchain lock lacks $key" }
}
if ($DockerImage -ne $lock['LocalImageTag']) { throw "Docker image tag '$DockerImage' is not the locked tag '$($lock['LocalImageTag'])'." }
$actualImageId = ((Invoke-CheckedNative 'docker' @('image', 'inspect', $DockerImage, '--format', '{{.Id}}')) -join '').Trim()
if ($actualImageId -ne $lock['LocalImageId']) { throw "Toolchain image ID mismatch. Expected $($lock['LocalImageId']), found $actualImageId. Run scripts/prepare_toolchain.ps1." }
$expectedNxdkCommit = $lock['NxdkCommit']
$actualNxdkCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$NxdkPath", '-C', $NxdkPath, 'rev-parse', 'HEAD')) -join '').Trim()
if ($actualNxdkCommit -ne $expectedNxdkCommit) { throw "nxdk checkout commit mismatch. Expected $expectedNxdkCommit, found $actualNxdkCommit." }
$nxdkDir = $lock['NxdkDirInImage']

# --- Invocation directory ----------------------------------------------------
New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$workRoot = Join-Path $OutputRoot 'work'
$artifactRoot = Join-Path $OutputRoot 'artifacts'
$logRoot = Join-Path $OutputRoot 'logs'
New-Item -ItemType Directory -Path $workRoot, $artifactRoot, $logRoot | Out-Null

$gitCommit = ((Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'rev-parse', 'HEAD')) -join '').Trim()
$gitStatus = @(Invoke-CheckedNative 'git' @('-c', "safe.directory=$repoRoot", '-C', $repoRoot, 'status', '--porcelain', '--untracked-files=all'))
$gitDirty = ($gitStatus | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count -gt 0
$environmentLines = @(
    "BuildStartUtc=$($buildStartUtc.ToString('o'))",
    "SourceRoot=$repoRoot",
    "GitCommit=$gitCommit",
    "GitDirty=$gitDirty",
    '--- git status --porcelain --untracked-files=all ---'
) + $gitStatus + @('--- docker version ---') + @(Invoke-CheckedNative 'docker' @('version')) + @(
    '--- powershell ---',
    "PSVersion=$($PSVersionTable.PSVersion)",
    "PSEdition=$($PSVersionTable.PSEdition)",
    "OS=$([Environment]::OSVersion.VersionString)"
)
Set-Content -LiteralPath (Join-Path $logRoot 'environment.txt') -Value $environmentLines -Encoding UTF8

$fingerprintBefore = Get-SourceFingerprint
Set-Content -LiteralPath (Join-Path $logRoot 'source-hashes-before.txt') -Value $fingerprintBefore.Lines -Encoding UTF8

# Stage the build inputs so nxdk's beside-the-source object files land in the
# invocation directory and the repository worktree stays untouched.
Copy-Item -LiteralPath (Join-Path $repoRoot 'Makefile') -Destination (Join-Path $workRoot 'Makefile')
Copy-Item -LiteralPath (Join-Path $repoRoot 'src') -Destination (Join-Path $workRoot 'src') -Recurse
Get-ChildItem -LiteralPath (Join-Path $workRoot 'src') -Recurse -File -Filter '*.inl' | Remove-Item -Force
# GNU make and the nxdk shader compilers run on Linux and treat a trailing CR
# as content; normalize only the staged copies (never the repository files).
$lineEndingSensitive = @(Get-Item -LiteralPath (Join-Path $workRoot 'Makefile')) +
    @(Get-ChildItem -LiteralPath (Join-Path $workRoot 'src') -Recurse -File | Where-Object { $_.Extension -in @('.vsh', '.psh') })
foreach ($file in $lineEndingSensitive) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    if ($bytes -contains 13) {
        $text = [Text.Encoding]::UTF8.GetString($bytes).Replace("`r`n", "`n").Replace("`r", "`n")
        [IO.File]::WriteAllText($file.FullName, $text, (New-Object Text.UTF8Encoding($false)))
    }
}

# --- Container build ---------------------------------------------------------
$disableCullingValue = if ($DisableCulling) { '1' } else { '0' }
$reverseValue = if ($ReverseTriangleOrder) { '1' } else { '0' }
$makeArguments = @(
    "NXDK_DIR=$nxdkDir",
    "MODE=$Mode",
    "CONFIGURATION=$Configuration",
    "PROBE=$Probe",
    "STOP_AFTER_FRAMES=$StopAfterFrames",
    "POSE=$Pose",
    "DISABLE_CULLING=$disableCullingValue",
    "REVERSE_TRIANGLES=$reverseValue",
    'OUTPUT_DIR=/work/disc',
    'GEN_XISO=/work/game.iso',
    'V=1'
)
if ($Mode -eq 'Authored') {
    $makeArguments += 'GENERATED_CORE_ROOT=/generated'
    $makeArguments += 'CONTENT_ROOT=/content'
    if (-not [string]::IsNullOrWhiteSpace($CatalogRoot)) { $makeArguments += 'CATALOG_ROOT=/catalog' }
}
$innerCommand = 'set -e; ' +
    'recorded="$(cat ' + $nxdkDir + '/HELENGINE_NXDK_COMMIT)"; ' +
    'test "$recorded" = "' + $expectedNxdkCommit + '" || { echo "image nxdk commit $recorded != ' + $expectedNxdkCommit + '" >&2; exit 90; }; ' +
    'cd /work; ' +
    'make ' + ($makeArguments -join ' ') + ' print-config; ' +
    'make -j' + $Jobs + ' ' + ($makeArguments -join ' ') + ' all; ' +
    'ls -l /work/disc /work/game.iso'
$dockerArgs = @(
    'run', '--rm', '--workdir', '/work',
    '--mount', "type=bind,source=$workRoot,target=/work"
)
if ($Mode -eq 'Authored') {
    # Read-only: the generated Core and cooked content are build inputs, and the
    # build must not be able to edit what it was handed.
    $dockerArgs += @('--mount', "type=bind,source=$GeneratedCoreRoot,target=/generated,readonly")
    $dockerArgs += @('--mount', "type=bind,source=$ContentRoot,target=/content,readonly")
    if (-not [string]::IsNullOrWhiteSpace($CatalogRoot)) {
        $dockerArgs += @('--mount', "type=bind,source=$CatalogRoot,target=/catalog,readonly")
    }
}
$dockerArgs += @(
    '--entrypoint', '/bin/sh',
    $DockerImage,
    '-c', $innerCommand
)
$buildLog = Join-Path $logRoot 'docker-build.log'
Set-Content -LiteralPath (Join-Path $logRoot 'docker-build-command.txt') -Value @("docker $($dockerArgs -join ' ')", '', 'inner shell command:', $innerCommand) -Encoding UTF8
Write-Output "Building $Probe/$Configuration in $OutputRoot"
$buildExit = Invoke-NativeToLog -FilePath 'docker' -ArgumentList $dockerArgs -LogPath $buildLog
if ($buildExit -ne 0) { throw "Container build failed with exit code $buildExit. See $buildLog" }

# --- Artifact validation -----------------------------------------------------
$xbePath = Join-Path $workRoot 'disc\default.xbe'
$isoPath = Join-Path $workRoot 'game.iso'
if (-not (Test-Path -LiteralPath $xbePath -PathType Leaf)) { throw "Build did not produce $xbePath" }
if (-not (Test-Path -LiteralPath $isoPath -PathType Leaf)) { throw "Build did not produce $isoPath" }
$xbeItem = Get-Item -LiteralPath $xbePath
$isoItem = Get-Item -LiteralPath $isoPath
if ($xbeItem.Length -le 0) { throw "default.xbe is empty" }
if ($isoItem.Length -le 0) { throw "game.iso is empty" }
$stream = [IO.File]::OpenRead($xbePath)
try {
    $magic = New-Object byte[] 4
    $read = $stream.Read($magic, 0, 4)
}
finally { $stream.Dispose() }
if ($read -ne 4 -or $magic[0] -ne 0x58 -or $magic[1] -ne 0x42 -or $magic[2] -ne 0x45 -or $magic[3] -ne 0x48) {
    throw ("default.xbe does not start with XBEH (got {0:X2} {1:X2} {2:X2} {3:X2})" -f $magic[0], $magic[1], $magic[2], $magic[3])
}
$discEntries = @(Get-ChildItem -LiteralPath (Join-Path $workRoot 'disc') -Force)
if ($Mode -eq 'Diagnostic') {
    if ($discEntries.Count -ne 1 -or $discEntries[0].Name -ne 'default.xbe') {
        throw "Diagnostic disc staging root must contain exactly default.xbe; found: $(($discEntries | ForEach-Object { $_.Name }) -join ', ')"
    }
}
else {
    if (-not ($discEntries | Where-Object { $_.Name -eq 'default.xbe' })) {
        throw "Authored disc staging root is missing default.xbe; found: $(($discEntries | ForEach-Object { $_.Name }) -join ', ')"
    }
    # An authored image whose only entry is the executable means the cooked tree
    # never reached the disc, which would boot to a runtime that cannot find its scene.
    if ($discEntries.Count -lt 2) {
        throw "Authored disc staging root carries no content beside default.xbe; the cooked tree was not staged."
    }
}

$listLog = Join-Path $logRoot 'xiso-listing.txt'
$listExit = Invoke-NativeToLog -FilePath 'docker' -ArgumentList @(
    'run', '--rm', '--mount', "type=bind,source=$workRoot,target=/work,readonly",
    '--entrypoint', "$nxdkDir/tools/extract-xiso/build/extract-xiso", $DockerImage, '-l', '/work/game.iso'
) -LogPath $listLog
if ($listExit -ne 0) { throw "extract-xiso -l failed with exit code $listExit. See $listLog" }
$listing = Get-Content -LiteralPath $listLog
if (-not ($listing | Where-Object { $_ -match 'default\.xbe' })) { throw "XISO listing does not contain default.xbe. See $listLog" }

$fingerprintAfter = Get-SourceFingerprint
if ($fingerprintBefore.Hash -ne $fingerprintAfter.Hash) {
    throw "Source inputs changed during the build; refusing to attribute artifacts to one snapshot."
}
Set-Content -LiteralPath (Join-Path $logRoot 'source-hashes.txt') -Value $fingerprintAfter.Lines -Encoding UTF8

# --- Publish artifacts -------------------------------------------------------
Copy-Item -LiteralPath $xbePath -Destination (Join-Path $artifactRoot 'default.xbe')
Copy-Item -LiteralPath $isoPath -Destination (Join-Path $artifactRoot 'game.iso')
foreach ($optional in @('main.map', 'main.exe', 'main.pdb')) {
    $candidate = Join-Path $workRoot $optional
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { Copy-Item -LiteralPath $candidate -Destination (Join-Path $artifactRoot $optional) }
}
$shaderOut = Join-Path $artifactRoot 'generated-shaders'
New-Item -ItemType Directory -Path $shaderOut | Out-Null
Get-ChildItem -LiteralPath (Join-Path $workRoot 'src\platform\xbox\shaders') -File -Filter '*.inl' | Copy-Item -Destination $shaderOut

$artifactRecords = @(
    foreach ($file in Get-ChildItem -LiteralPath $artifactRoot -File -Recurse) {
        [ordered]@{
            path = Get-RelativePath -Root $OutputRoot -Path $file.FullName
            bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
)
$buildInfo = [ordered]@{
    schema = 'helengine-xbox-build-info/1'
    buildStartUtc = $buildStartUtc.ToString('o')
    buildEndUtc = [DateTime]::UtcNow.ToString('o')
    outputRoot = $OutputRoot
    source = [ordered]@{
        root = $repoRoot
        gitCommit = $gitCommit
        gitDirty = $gitDirty
        inputSha256 = $fingerprintAfter.Hash
        inputHashManifest = 'logs/source-hashes.txt'
    }
    toolchain = [ordered]@{
        nxdkCommit = $expectedNxdkCommit
        nxdkRepository = $lock['NxdkRepository']
        dockerImage = $DockerImage
        dockerImageId = $actualImageId
        upstreamBaseImage = $lock['UpstreamBaseImage']
        compiler = $lock['CompilerC']
        linker = $lock['Linker']
    }
    mode = $Mode
    authored = [ordered]@{
        generatedCoreRoot = $GeneratedCoreRoot
        contentRoot = $ContentRoot
        catalogRoot = $CatalogRoot
    }
    configuration = $Configuration
    probe = $Probe
    stopAfterFrames = $StopAfterFrames
    pose = $Pose
    disableCulling = [bool]$DisableCulling
    reverseTriangleOrder = [bool]$ReverseTriangleOrder
    makeArguments = $makeArguments
    xbe = 'artifacts/default.xbe'
    xiso = 'artifacts/game.iso'
    artifacts = $artifactRecords
    logs = [ordered]@{
        environment = 'logs/environment.txt'
        dockerBuild = 'logs/docker-build.log'
        xisoListing = 'logs/xiso-listing.txt'
    }
}
$buildInfoJson = $buildInfo | ConvertTo-Json -Depth 6
# Publish the success record last, both at the invocation root and beside the artifacts.
Set-Content -LiteralPath (Join-Path $OutputRoot 'build-info.json') -Value $buildInfoJson -Encoding UTF8
Set-Content -LiteralPath (Join-Path $artifactRoot 'build-info.json') -Value $buildInfoJson -Encoding UTF8

Write-Output "Built $(Join-Path $artifactRoot 'default.xbe')"
Write-Output "Built $(Join-Path $artifactRoot 'game.iso')"
foreach ($record in $artifactRecords) { Write-Output ("{0}  {1}  {2} bytes" -f $record.sha256, $record.path, $record.bytes) }
