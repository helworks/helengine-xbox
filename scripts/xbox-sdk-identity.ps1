# Prove a Docker classic config address against the pinned manifest/index.
# Only LocalImageId is used for execution; canonical IDs remain cache identities.
function Get-XboxSdkBytesDigest {
    param([byte[]]$Bytes)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return 'sha256:' + ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Read-XboxSdkImageProof {
    param([string]$CanonicalImageId, [string]$IdentityRoot)
    if ($CanonicalImageId -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'Invalid canonical SDK digest.' }
    if (-not [IO.Path]::IsPathRooted($IdentityRoot)) { throw 'SDK identity proof requires an absolute root.' }
    # Each file is read once. Hashing, descriptor sizes and parsing share that snapshot.
    $manifestBytes = [IO.File]::ReadAllBytes((Join-Path $IdentityRoot 'manifest.json'))
    $configBytes = [IO.File]::ReadAllBytes((Join-Path $IdentityRoot 'config.json'))
    $manifestDigest = Get-XboxSdkBytesDigest $manifestBytes
    $configDigest = Get-XboxSdkBytesDigest $configBytes
    $utf8 = [Text.UTF8Encoding]::new($false,$true)
    $indexDigest = $null
    $indexPath = Join-Path $IdentityRoot 'index.json'
    if (Test-Path -LiteralPath $indexPath -PathType Leaf) {
        $indexBytes = [IO.File]::ReadAllBytes($indexPath)
        $indexDigest = Get-XboxSdkBytesDigest $indexBytes
        if ($indexDigest -cne $CanonicalImageId) { throw 'SDK index bytes do not match the locked digest.' }
        $index = $utf8.GetString($indexBytes) | ConvertFrom-Json
        if (($index.schemaVersion -isnot [int] -and $index.schemaVersion -isnot [long]) -or $index.schemaVersion -ne 2 -or $index.mediaType -cnotin @('application/vnd.oci.image.index.v1+json','application/vnd.docker.distribution.manifest.list.v2+json')) { throw 'Unsupported SDK image index.' }
        $candidates = @($index.manifests | Where-Object {
            $null -ne $_.PSObject.Properties['platform'] -and
            $null -ne $_.platform.PSObject.Properties['os'] -and
            $null -ne $_.platform.PSObject.Properties['architecture'] -and
            $_.platform.os -ceq 'linux' -and $_.platform.architecture -ceq 'amd64'
        })
        if ($candidates.Count -ne 1) { throw 'SDK index must contain exactly one linux/amd64 manifest.' }
        $descriptor = $candidates[0]
        if ($descriptor.mediaType -cnotin @('application/vnd.oci.image.manifest.v1+json','application/vnd.docker.distribution.manifest.v2+json') -or
            ($descriptor.size -isnot [int] -and $descriptor.size -isnot [long]) -or
            $descriptor.size -ne $manifestBytes.LongLength -or $descriptor.digest -cne $manifestDigest) { throw 'SDK bytes do not match the index manifest descriptor.' }
    } elseif ($manifestDigest -cne $CanonicalImageId) { throw 'SDK manifest bytes do not match the locked digest.' }
    $manifest = $utf8.GetString($manifestBytes) | ConvertFrom-Json
    if (($manifest.schemaVersion -isnot [int] -and $manifest.schemaVersion -isnot [long]) -or $manifest.schemaVersion -ne 2 -or $manifest.mediaType -cnotin @('application/vnd.oci.image.manifest.v1+json','application/vnd.docker.distribution.manifest.v2+json')) { throw 'Unsupported SDK image manifest.' }
    if ($null -ne $indexDigest -and $descriptor.mediaType -cne $manifest.mediaType) { throw 'SDK manifest descriptor media type differs.' }
    if ($manifest.config.mediaType -cnotin @('application/vnd.oci.image.config.v1+json','application/vnd.docker.container.image.v1+json')) { throw 'Unsupported SDK config media type.' }
    if (($manifest.config.size -isnot [int] -and $manifest.config.size -isnot [long]) -or $configDigest -cne $manifest.config.digest -or $configBytes.LongLength -ne $manifest.config.size) { throw 'SDK config bytes do not match the manifest descriptor.' }
    $config = $utf8.GetString($configBytes) | ConvertFrom-Json
    if ($config.os -cne 'linux' -or $config.architecture -cne 'amd64' -or $config.rootfs.type -cne 'layers') { throw 'SDK config is not a linux/amd64 layered image.' }
    $layers = @($config.rootfs.diff_ids)
    if ($layers.Count -eq 0 -or @($manifest.layers).Count -ne $layers.Count) { throw 'SDK manifest and config layer counts differ or are empty.' }
    foreach ($layer in $layers) { if ($layer -cnotmatch '^sha256:[0-9a-f]{64}$') { throw 'Invalid SDK filesystem digest.' } }
    foreach ($layer in $manifest.layers) {
        if ($layer.digest -cnotmatch '^sha256:[0-9a-f]{64}$' -or ($layer.size -isnot [int] -and $layer.size -isnot [long]) -or $layer.size -lt 0) { throw 'Invalid SDK compressed layer descriptor.' }
    }
    return [pscustomobject]@{CanonicalImageId=$CanonicalImageId; ConfigImageId=$configDigest; Layers=$layers; IndexSha256=$(if ($null -eq $indexDigest) {$null} else {$indexDigest.Substring(7)}); ManifestSha256=$manifestDigest.Substring(7); ConfigSha256=$configDigest.Substring(7)}
}

function Resolve-XboxSdkImageIdentity {
    param([string]$CanonicalImageId, $Proof, $InspectedImage)
    if ($Proof.CanonicalImageId -cne $CanonicalImageId) { throw 'SDK proof belongs to another pinned image.' }
    if ($InspectedImage.Id -cnotin @($CanonicalImageId,$Proof.ConfigImageId)) { throw 'Local SDK image is unrelated to the locked image.' }
    if ($InspectedImage.Os -cne 'linux' -or $InspectedImage.Architecture -cne 'amd64' -or $InspectedImage.RootFS.Type -cne 'layers') { throw 'Local SDK platform differs from the manifest.' }
    $actualLayers = @($InspectedImage.RootFS.Layers)
    if ($actualLayers.Count -ne $Proof.Layers.Count) { throw 'Local SDK filesystem layer count differs.' }
    for ($i=0; $i -lt $actualLayers.Count; $i++) { if ($actualLayers[$i] -cne $Proof.Layers[$i]) { throw 'Local SDK filesystem layers differ.' } }
    return [pscustomobject]@{CanonicalImageId=$CanonicalImageId; LocalImageId=[string]$InspectedImage.Id; IndexSha256=$Proof.IndexSha256; ManifestSha256=$Proof.ManifestSha256; ConfigSha256=$Proof.ConfigSha256}
}

function Get-XboxSdkImageIdentity {
    param([string]$CanonicalImageId, [string]$IdentityRoot = $env:HELENGINE_XBOX_SDK_IDENTITY_ROOT, [string]$LockedTag = $CanonicalImageId)
    if ([string]::IsNullOrWhiteSpace($IdentityRoot)) {
        $actual = ((Invoke-CheckedNative 'docker' @('image','inspect',$LockedTag,'--format','{{.Id}}')) -join '').Trim()
        if ($actual -cne $CanonicalImageId) { throw "SDK image ID mismatch. Expected $CanonicalImageId, found $actual." }
        return [pscustomobject]@{CanonicalImageId=$CanonicalImageId; LocalImageId=$actual; IndexSha256=$null; ManifestSha256=$null; ConfigSha256=$null}
    }
    $proof = Read-XboxSdkImageProof $CanonicalImageId $IdentityRoot
    $metadata = $null
    foreach ($candidate in @($proof.ConfigImageId,$CanonicalImageId)) {
        try {
            $metadata = ((Invoke-CheckedNative 'docker' @('image','inspect',$candidate)) -join [Environment]::NewLine) | ConvertFrom-Json
            break
        } catch { if ($_.Exception.Message -notmatch '(?i)No such image:') { throw } }
    }
    if ($null -eq $metadata) { throw 'Neither the verified SDK config nor its canonical image is installed locally.' }
    $images = @($metadata)
    if ($images.Count -ne 1) { throw 'Expected exactly one SDK image.' }
    return Resolve-XboxSdkImageIdentity $CanonicalImageId $proof $images[0]
}
