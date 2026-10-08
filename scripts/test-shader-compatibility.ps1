[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$OutputRoot,
    [string]$CgcPath
)

# Experimental compiler-chain evidence; this does not register a runtime backend.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw "Refusing reused output root: $OutputRoot" }
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.xbox')) + [IO.Path]::DirectorySeparatorChar
if (-not $OutputRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Probe output must be inside repository .xbox.' }
if ([string]::IsNullOrWhiteSpace($CgcPath)) { $CgcPath = Join-Path $repoRoot '.xbox/dependencies/nxdk/tools/cg/win/cgc.exe' }
$CgcPath = [IO.Path]::GetFullPath($CgcPath)
if (-not (Test-Path -LiteralPath $CgcPath -PathType Leaf)) { throw "Cg executable missing: $CgcPath" }
New-Item -ItemType Directory -Path $OutputRoot | Out-Null

function Invoke-ProbeTool {
    param([string]$Name, [string]$Executable, [string[]]$ToolArguments, [switch]$ExpectFailure)
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.WorkingDirectory = $repoRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    # PowerShell 5.1 lacks ProcessStartInfo.ArgumentList. Reject embedded quotes
    # and line breaks; double trailing backslashes before the closing quote.
    $quoted = foreach ($value in $ToolArguments) {
        if ($value -match '["\r\n]') { throw 'Unsupported quote or newline in tool argument.' }
        '"' + ($value -replace '(\\+)$', '$1$1') + '"'
    }
    $info.Arguments = $quoted -join ' '
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    $started = $false
    try {
        if (-not $process.Start()) { throw "Could not start $Name" }
        $started = $true
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit(60000)
        if ($timedOut) {
            $process.Kill()
            $process.WaitForExit()
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        [IO.File]::WriteAllText((Join-Path $OutputRoot "$Name.stdout.log"), $stdout)
        [IO.File]::WriteAllText((Join-Path $OutputRoot "$Name.stderr.log"), $stderr)
        [IO.File]::WriteAllText((Join-Path $OutputRoot "$Name.command.txt"), $Executable + ' ' + $info.Arguments)
        if ($timedOut) { throw "$Name timed out after 60 seconds." }
        if ($ExpectFailure) {
            if ($process.ExitCode -eq 0) { throw "$Name unexpectedly succeeded." }
        } elseif ($process.ExitCode -ne 0) {
            throw "$Name failed ($($process.ExitCode)): $stderr $stdout"
        }
        return $stdout
    } finally {
        if ($started -and -not $process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}

$lock = @{}
foreach ($line in Get-Content (Join-Path $repoRoot 'toolchain-lock.txt')) {
    if ($line -match '^([^#=]+)=(.*)$') { $lock[$matches[1]] = $matches[2] }
}
$nxdk = Join-Path $repoRoot '.xbox/dependencies/nxdk'
$revision = Invoke-ProbeTool 'nxdk-revision' 'git' @('-C', $nxdk, 'rev-parse', 'HEAD')
if ($revision.Trim() -ne $lock.NxdkCommit) { throw 'NXDK revision mismatch.' }
$imageId = Invoke-ProbeTool 'image-id' 'docker' @('image', 'inspect', $lock.LocalImageTag, '--format', '{{.Id}}')
if ($imageId.Trim() -ne $lock.LocalImageId) { throw 'Docker image mismatch.' }
$version = Invoke-ProbeTool 'cgc-version' $CgcPath @('-v')
$dotnetRoot = Join-Path $OutputRoot 'dotnet'
$null = Invoke-ProbeTool 'build-probe' 'dotnet' @('build', 'tools/shader-probe/helengine.xbox.shader-probe.csproj', '--artifacts-path', $dotnetRoot, '-v', 'quiet')
$dll = Join-Path $dotnetRoot 'bin/helengine.xbox.shader-probe/debug/helengine.xbox.shader-probe.dll'
$invalidPrefix = Join-Path $OutputRoot 'invalid'
$null = Invoke-ProbeTool 'invalid' 'dotnet' @($dll, '--source', 'tests/shaders/invalid.hlsl', '--entry', 'PSMain', '--stage', 'pixel', '--output', $invalidPrefix) -ExpectFailure
foreach ($extension in @('.spv', '.cg', '.entry.cg', '.reflection.txt')) {
    if (Test-Path ($invalidPrefix + $extension)) { throw 'Failed translation published an artifact.' }
}
$unsupportedPrefix = Join-Path $OutputRoot 'unsupported-interface'
$null = Invoke-ProbeTool 'unsupported-interface' 'dotnet' @($dll, '--source', 'tests/shaders/unsupported-interface.hlsl', '--entry', 'PSMain', '--stage', 'pixel', '--output', $unsupportedPrefix) -ExpectFailure
if ((Get-Content (Join-Path $OutputRoot 'unsupported-interface.stderr.log') -Raw) -notmatch 'Unsupported Xbox interface semantic: TEXCOORD1') {
    throw 'Unsupported interface failed for an unexpected reason.'
}
foreach ($extension in @('.spv', '.cg', '.entry.cg', '.reflection.txt')) {
    if (Test-Path ($unsupportedPrefix + $extension)) { throw 'Unsupported interface published an artifact.' }
}

$records = @()
foreach ($fixture in @('colour', 'textured')) {
    foreach ($stage in @('vertex', 'pixel')) {
        $name = "$fixture-$stage"
        $prefix = Join-Path $OutputRoot $name
        $entry = if ($stage -eq 'vertex') { 'VSMain' } else { 'PSMain' }
        $profile = if ($stage -eq 'vertex') { 'vp20' } else { 'fp20' }
        $translation = Invoke-ProbeTool "$name-translate" 'dotnet' @($dll, '--source', "tests/shaders/$fixture.hlsl", '--entry', $entry, '--stage', $stage, '--output', $prefix)
        if ($translation -notmatch '(?m)^EntryPoint=(\w+)\s*$') { throw 'Missing emitted entry point.' }
        $emittedEntry = $matches[1]
        $null = Invoke-ProbeTool "$name-cgc" $CgcPath @('-profile', $profile, '-entry', $emittedEntry, '-o', "$prefix.$profile", "$prefix.entry.cg")
        if ((Get-Item "$prefix.$profile").Length -eq 0) { throw 'Empty Cg output.' }
        $compiler = if ($stage -eq 'vertex') { 'vp20compiler' } else { 'fp20compiler' }
        $mount = "type=bind,source=$OutputRoot,target=/probe"
        $generated = Invoke-ProbeTool "$name-nxdk" 'docker' @('run', '--rm', '--mount', $mount, '--entrypoint', "/opt/nxdk/tools/$compiler/$compiler", $lock.LocalImageTag, "/probe/$name.$profile")
        if ([string]::IsNullOrWhiteSpace($generated)) { throw 'Empty nxdk output.' }
        [IO.File]::WriteAllText("$prefix.inl", $generated)
        $records += [ordered]@{ fixture=$fixture; stage=$stage; entry=$emittedEntry; profile=$profile; sourceSha256=(Get-FileHash "$prefix.cg").Hash; adapterSha256=(Get-FileHash "$prefix.entry.cg").Hash; outputSha256=(Get-FileHash "$prefix.inl").Hash }
        Write-Output "$name : shaderc -> SPIRV-Cross -> cgc -> nxdk passed"
    }
}
$null = Invoke-ProbeTool 'colour-interface' 'docker' @('run', '--rm', '--mount', "type=bind,source=$repoRoot,target=/src,readonly", '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', 'sh', $lock.LocalImageTag, '-c', 'set -e; g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/opt/nxdk/lib -I/out /src/tests/shader_colour_interface_tests.cpp -o /out/shader_colour_interface_tests; /out/shader_colour_interface_tests')
$null = Invoke-ProbeTool 'pixel-state-build' 'docker' @('run', '--rm', '--mount', "type=bind,source=$repoRoot,target=/src,readonly", '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', 'sh', $lock.LocalImageTag, '-c', 'set -e; g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/opt/nxdk/lib -I/out /src/tests/shader_pair_state_tests.cpp -o /out/pixel-state; g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -DXBOX_TEST_RAW_COMPILER_STATE -I/opt/nxdk/lib -I/out /src/tests/shader_pair_state_tests.cpp -o /out/pixel-state-raw')
$null = Invoke-ProbeTool 'pixel-state' 'docker' @('run', '--rm', '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', '/out/pixel-state', $lock.LocalImageTag)
$null = Invoke-ProbeTool 'pixel-state-raw' 'docker' @('run', '--rm', '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', '/out/pixel-state-raw', $lock.LocalImageTag) -ExpectFailure
if ((Get-Content (Join-Path $OutputRoot 'pixel-state-raw.stdout.log') -Raw) -notmatch '0xffffffff / 0x1 / 0x1') {
    throw 'Raw state negative control failed for an unexpected reason.'
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $toolIdentity = $revision.Trim() + $imageId.Trim() + (Get-FileHash $CgcPath).Hash + (Get-FileHash (Join-Path $repoRoot 'shadercompiler/helengine.xbox.shadercompiler.csproj')).Hash
    $toolsHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($toolIdentity))).Replace('-', '')
    $entryHash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes('XboxMain'))).Replace('-', '')
} finally { $sha.Dispose() }
foreach ($fixture in @('colour', 'textured')) {
    foreach ($stage in @('vertex', 'pixel')) {
        $name = "$fixture-$stage"
        $extractRoot = Join-Path $OutputRoot "$name-extraction"
        New-Item -ItemType Directory -Path $extractRoot | Out-Null
        Copy-Item -LiteralPath (Join-Path $OutputRoot "$name.inl") -Destination (Join-Path $extractRoot 'shader.inl')
        $define = if ($stage -eq 'vertex') { '-DXBOX_EXTRACT_VERTEX' } else { '' }
        $null = Invoke-ProbeTool "$name-extractor-build" 'docker' @('run', '--rm', '--mount', "type=bind,source=$repoRoot,target=/src,readonly", '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', 'sh', $lock.LocalImageTag, '-c', "set -e; g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG $define -I/src/src -I/opt/nxdk/lib -I/out/$name-extraction /src/tools/shader-probe/native/ExtractShaderArtifact.cpp /src/src/platform/xbox/XboxShaderArtifact.cpp -o /out/extract-$name")
        $sourceHash = (Get-FileHash (Join-Path $repoRoot "tests/shaders/$fixture.hlsl")).Hash
        $null = Invoke-ProbeTool "$name-extract" 'docker' @('run', '--rm', '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', "/out/extract-$name", $lock.LocalImageTag, "/out/$name.hxsb", $sourceHash, $toolsHash, $entryHash)
    }
}
$null = Invoke-ProbeTool 'shader-artifacts' 'docker' @('run', '--rm', '--mount', "type=bind,source=$repoRoot,target=/src,readonly", '--mount', "type=bind,source=$OutputRoot,target=/out", '--entrypoint', 'sh', $lock.LocalImageTag, '-c', 'set -e; g++ -std=c++17 -Wall -Wextra -Werror -UNDEBUG -I/src/src -I/opt/nxdk/lib /src/tests/shader_artifact_tests.cpp /src/src/platform/xbox/XboxShaderArtifact.cpp /src/src/platform/xbox/XboxShaderProgram.cpp -o /out/shader-artifact-tests; /out/shader-artifact-tests /out/colour-vertex.hxsb /out/colour-pixel.hxsb /out/textured-vertex.hxsb /out/textured-pixel.hxsb')
$artifactRecords = @(Get-ChildItem -LiteralPath $OutputRoot -Filter '*.hxsb' | ForEach-Object { [ordered]@{ name=$_.Name; size=$_.Length; sha256=(Get-FileHash $_.FullName).Hash } })
$summary = [ordered]@{ compilerChainPassed=$true; artifactValidationPassed=$true; pixelBaselineTransitionPassed=$true; rawCompilerStateComplete=$false; runtimeCompatibilityProven=$false; baselineSha256=(Get-FileHash (Join-Path $repoRoot 'tools/shader-probe/native/XboxPixelShaderBaseline.hpp')).Hash; nxdkCommit=$revision.Trim(); image=$imageId.Trim(); cgcSha256=(Get-FileHash $CgcPath).Hash; stages=$records; artifacts=$artifactRecords }
$summary | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $OutputRoot 'compiler-chain.json') -Encoding UTF8
Write-Output "Compiler evidence: $OutputRoot (runtime compatibility still requires interface/state validation)."
