param([Parameter(Mandatory)][string]$FixtureRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$buildPath = Join-Path $PSScriptRoot '../build.ps1'
$buildText = [IO.File]::ReadAllText($buildPath)
if (-not $buildText.Contains('Get-XboxSdkImageIdentity')) { throw 'Build must resolve verified canonical/local SDK identities.' }
. (Join-Path $PSScriptRoot '../xbox-sdk-identity.ps1')
if (-not [IO.Path]::IsPathRooted($FixtureRoot)) { throw 'FixtureRoot must be absolute.' }
New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
$script:passed = 0
function Check([bool]$Value, [string]$Name) { if (-not $Value) { throw $Name }; $script:passed++ }
function Reject([scriptblock]$Action, [string]$Message) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike "*$Message*") { throw }
        $script:passed++; return
    }
    throw "Expected rejection: $Message"
}
function Write-JsonBytes($Name, $Value) {
    $path = Join-Path $FixtureRoot $Name
    [IO.File]::WriteAllText($path, ($Value | ConvertTo-Json -Depth 20 -Compress), [Text.UTF8Encoding]::new($false))
    return $path
}
function Digest($Path) { return 'sha256:' + (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$layerA = 'sha256:' + ('a' * 64); $layerB = 'sha256:' + ('b' * 64)
function Make-Proof([bool]$Indexed = $true, [bool]$Docker = $false) {
    $configPath = Write-JsonBytes 'config.json' @{os='linux';architecture='amd64';rootfs=@{type='layers';diff_ids=@($layerA,$layerB)}}
    $configMedia = if ($Docker) { 'application/vnd.docker.container.image.v1+json' } else { 'application/vnd.oci.image.config.v1+json' }
    $manifestMedia = if ($Docker) { 'application/vnd.docker.distribution.manifest.v2+json' } else { 'application/vnd.oci.image.manifest.v1+json' }
    $manifestPath = Write-JsonBytes 'manifest.json' @{schemaVersion=2;mediaType=$manifestMedia;config=@{mediaType=$configMedia;digest=(Digest $configPath);size=(Get-Item $configPath).Length};layers=@(@{digest=$layerA;size=1},@{digest=$layerB;size=2})}
    $pin = Digest $manifestPath
    if ($Indexed) {
        $indexMedia = if ($Docker) { 'application/vnd.docker.distribution.manifest.list.v2+json' } else { 'application/vnd.oci.image.index.v1+json' }
        $indexPath = Write-JsonBytes 'index.json' @{schemaVersion=2;mediaType=$indexMedia;manifests=@(@{mediaType=$manifestMedia;digest=$pin;size=(Get-Item $manifestPath).Length;platform=@{os='linux';architecture='amd64'}},@{mediaType=$manifestMedia;digest=('sha256:'+('c'*64));size=1;platform=@{os='unknown';architecture='unknown'}})}
        $pin = Digest $indexPath
    } elseif (Test-Path -LiteralPath (Join-Path $FixtureRoot 'index.json')) { Remove-Item -LiteralPath (Join-Path $FixtureRoot 'index.json') }
    return $pin
}
foreach ($indexed in @($false,$true)) { foreach ($docker in @($false,$true)) {
    $pin = Make-Proof $indexed $docker
    $proof = Read-XboxSdkImageProof $pin $FixtureRoot
    foreach ($id in @($pin,$proof.ConfigImageId)) {
        $actual = [pscustomobject]@{Id=$id;Os='linux';Architecture='amd64';RootFS=@{Type='layers';Layers=@($layerA,$layerB)}}
        $resolved = Resolve-XboxSdkImageIdentity $pin $proof $actual
        Check ($resolved.CanonicalImageId -ceq $pin -and $resolved.LocalImageId -ceq $id) 'Canonical/cache identity changed.'
    }
} }
$pin = Make-Proof
$proof = Read-XboxSdkImageProof $pin $FixtureRoot
$actual = [pscustomobject]@{Id=$proof.ConfigImageId;Os='linux';Architecture='amd64';RootFS=@{Type='layers';Layers=@($layerA,$layerB)}}
$actual.Id='sha256:'+('d'*64)
Reject { Resolve-XboxSdkImageIdentity $pin $proof $actual } 'unrelated'
$actual.Id=$proof.ConfigImageId; $actual.RootFS.Layers=@($layerB,$layerA)
Reject { Resolve-XboxSdkImageIdentity $pin $proof $actual } 'layers differ'
$actual.RootFS.Layers=@($layerA); Reject { Resolve-XboxSdkImageIdentity $pin $proof $actual } 'layer count'
$actual.RootFS.Layers=@($layerA,$layerB); $actual.Architecture='arm64'
Reject { Resolve-XboxSdkImageIdentity $pin $proof $actual } 'platform'
Reject { Read-XboxSdkImageProof $pin 'relative' } 'absolute'
Reject { Read-XboxSdkImageProof ('sha256:'+('e'*64)) $FixtureRoot } 'locked digest'
foreach ($file in @('index.json','manifest.json','config.json')) {
    $pin=Make-Proof; [IO.File]::AppendAllText((Join-Path $FixtureRoot $file),' ')
    $message = switch ($file) { 'index.json' {'locked digest'} 'manifest.json' {'manifest descriptor'} 'config.json' {'config bytes'} }
    Reject { Read-XboxSdkImageProof $pin $FixtureRoot } $message
    $pin=Make-Proof; Remove-Item -LiteralPath (Join-Path $FixtureRoot $file)
    $missingMessage = if ($file -eq 'index.json') { 'locked digest' } else { 'Could not find' }
    Reject { Read-XboxSdkImageProof $pin $FixtureRoot } $missingMessage
}
foreach ($mutation in @('schema','platform','duplicate','size','stringSize','media','digest')) {
    $pin=Make-Proof; $path=Join-Path $FixtureRoot 'index.json'; $index=[IO.File]::ReadAllText($path)|ConvertFrom-Json
    switch ($mutation) {
        'schema' {$index.schemaVersion=1}
        'platform' {$index.manifests[0].platform.architecture='arm64'}
        'duplicate' {$index.manifests=@($index.manifests[0],$index.manifests[0])}
        'size' {$index.manifests[0].size++}
        'stringSize' {$index.manifests[0].size=[string]$index.manifests[0].size}
        'media' {$index.manifests[0].mediaType='invalid'}
        'digest' {$index.manifests[0].digest='sha256:'+('f'*64)}
    }
    Write-JsonBytes 'index.json' $index | Out-Null; $pin=Digest $path
    $message=switch($mutation) {'schema' {'index'} 'platform' {'exactly one'} 'duplicate' {'exactly one'} default {'manifest descriptor'}}
    Reject { Read-XboxSdkImageProof $pin $FixtureRoot } $message
}
$pin=Make-Proof; $configPath=Join-Path $FixtureRoot 'config.json'
$config=[IO.File]::ReadAllText($configPath)|ConvertFrom-Json; $config.os='windows'; Write-JsonBytes 'config.json' $config|Out-Null
$manifestPath=Join-Path $FixtureRoot 'manifest.json'; $manifest=[IO.File]::ReadAllText($manifestPath)|ConvertFrom-Json
$manifest.config.digest=Digest $configPath; $manifest.config.size=(Get-Item $configPath).Length
Write-JsonBytes 'manifest.json' $manifest|Out-Null; Remove-Item -LiteralPath (Join-Path $FixtureRoot 'index.json')
Reject { Read-XboxSdkImageProof (Digest $manifestPath) $FixtureRoot } 'linux/amd64'
$pin=Make-Proof; $proof=Read-XboxSdkImageProof $pin $FixtureRoot
$script:inspectCount=0
function Invoke-CheckedNative {
    param($FilePath,$ArgumentList)
    $script:inspectCount++
    if ($ArgumentList[2] -cne 'locked-sdk-tag' -or $ArgumentList.Count -ne 5) { throw 'Legacy image inspection changed.' }
    return $pin
}
$resolved=Get-XboxSdkImageIdentity $pin -IdentityRoot '' -LockedTag 'locked-sdk-tag'
Check ($resolved.CanonicalImageId -ceq $pin -and $resolved.LocalImageId -ceq $pin -and $script:inspectCount -eq 1) 'No-proof mode must retain one legacy inspection.'
$script:inspectCount=0
function Invoke-CheckedNative { param($FilePath,$ArgumentList) $script:inspectCount++; throw 'Docker metadata failure' }
Reject { Get-XboxSdkImageIdentity $pin $FixtureRoot } 'Docker metadata failure'
Check ($script:inspectCount -eq 1) 'Daemon failure triggered fallback.'
function Invoke-CheckedNative {
    param($FilePath,$ArgumentList)
    if ($ArgumentList[2] -ceq $proof.ConfigImageId) { throw 'No such image: config' }
    return ([pscustomobject]@{Id=$pin;Os='linux';Architecture='amd64';RootFS=@{Type='layers';Layers=@($layerA,$layerB)}}|ConvertTo-Json -Depth 5)
}
$resolved=Get-XboxSdkImageIdentity $pin $FixtureRoot
Check ($resolved.LocalImageId -ceq $pin) 'Canonical store fallback failed.'
$pin=Make-Proof
[IO.File]::AppendAllText((Join-Path $FixtureRoot 'config.json'),' ')
Reject { Get-XboxSdkImageIdentity $pin $FixtureRoot } 'config bytes'
Write-Output "PASS: $script:passed Xbox SDK identity assertions (OCI/Docker indexes/manifests, canonical/classic stores and negatives)."
