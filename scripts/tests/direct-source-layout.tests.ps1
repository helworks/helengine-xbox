param([Parameter(Mandatory)][string]$FixtureRoot, [string]$IdentityRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if (-not [IO.Path]::IsPathRooted($FixtureRoot)) { throw 'FixtureRoot must be absolute.' }
$repoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script:passed=0
function Check([bool]$Value,[string]$Message) { if(-not $Value){throw $Message};$script:passed++ }
New-Item -ItemType Directory -Force -Path $FixtureRoot,"$FixtureRoot/cache","$FixtureRoot/work"|Out-Null
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repoRoot 'scripts/build.ps1'),[ref]$tokens,[ref]$errors)
Check ($errors.Count -eq 0) 'Build script did not parse.'
foreach($name in @('Get-RelativePath','Get-SourceFingerprint')){
    $node=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
    . ([scriptblock]::Create($node.Extent.Text))
}
$fingerprint=Get-SourceFingerprint
foreach($sourceInput in @('Makefile','src/main.cpp','src/platform/xbox/XboxShaderConstantUpload.inl')){
    $hash=(Get-FileHash -LiteralPath (Join-Path $repoRoot $sourceInput) -Algorithm SHA256).Hash.ToLowerInvariant()
    Check ($fingerprint.Lines -ccontains "$sourceInput=$hash") "Fingerprint must read actual checkout bytes: $sourceInput"
}
$local=Join-Path $FixtureRoot 'local uncommitted input.cpp'
[IO.File]::WriteAllText($local,'first local contents')
$before=(Get-FileHash $local -Algorithm SHA256).Hash
[IO.File]::WriteAllText($local,'changed local contents')
Check ((Get-FileHash $local -Algorithm SHA256).Hash -cne $before) 'Local fixture mutation was not observed.'
Check ((Get-RelativePath -Root $FixtureRoot -Path $local) -ceq 'local uncommitted input.cpp') 'Host path fingerprint lost local input.'
$windowsHost=[IO.Path]::DirectorySeparatorChar -eq '\'
$probe="helengine-path-probe:`n`t@printf '%s\n' 'SOURCES=`$(SRCS)' 'OBJECTS=`$(OBJS)' 'SHADERS=`$(SHADER_OBJS)' 'DEPS=`$(DEPS)' 'FLAGS=`$(HELENGINE_CXXFLAGS)'`n"
if($windowsHost){
    $env:PATH='C:\Program Files\Git\usr\bin;'+$env:PATH
    $make=(Get-Command mingw32-make.exe).Source
    $source=$repoRoot.Replace('\','/');$cache="$FixtureRoot/cache".Replace('\','/');$work="$FixtureRoot/work".Replace('\','/')
    $nxdk=(Join-Path $repoRoot '.xbox/dependencies/nxdk').Replace('\','/')
    # This is make expansion only, selecting the SDK's Linux branch on Windows.
    # No compiler or Linux executable runs in this host fixture.
    $prefix=@('-f',"$source/Makefile",'UNAME_S=Linux','UNAME_M=x86_64',"NXDK_DIR=$nxdk")
}else{
    . (Join-Path $PSScriptRoot '../xbox-sdk-identity.ps1')
    function Invoke-CheckedNative {
        param($FilePath,$ArgumentList)
        $out=& $FilePath @ArgumentList 2>&1
        if($LASTEXITCODE -ne 0){throw ($out -join [Environment]::NewLine)}
        return @($out)
    }
    $identity=Get-XboxSdkImageIdentity 'sha256:acdb4a9fae971b5791b2732571fa0df72118301ef1760861e71892d36f80782a' $IdentityRoot
    $make='/usr/local/bin/docker';$source='/src';$cache='/nativecache';$work='/work'
    $prefix=@('run','--rm','--platform','linux/amd64','--memory','128m','--memory-swap','128m','--cpus','0.5','--network','none','--workdir','/work',
        '--mount',"type=bind,source=$repoRoot,target=/src,readonly",'--mount',"type=bind,source=$FixtureRoot/cache,target=/nativecache",'--mount',"type=bind,source=$FixtureRoot/work,target=/work",
        '--entrypoint','make',$identity.LocalImageId,'-f','/src/Makefile','NXDK_DIR=/opt/nxdk')
}
$common=@("SOURCE_ROOT=$source","NATIVE_OBJECT_ROOT=$cache/objects","GENERATED_SHADER_ROOT=$cache/generated-shaders","OUTPUT_DIR=$work/disc","GEN_XISO=$work/game.iso")
$output=@(& $make @prefix @common '--eval' $probe 'helengine-path-probe' '--no-print-directory' 2>&1)
Check ($LASTEXITCODE -eq 0) "GNUmake probe failed: $($output -join [Environment]::NewLine)"
$output|Set-Content -LiteralPath "$FixtureRoot/make-plan.log"
$values=@{}
foreach($line in $output){$text=[string]$line;$split=$text.IndexOf('=');if($split -gt 0){$values[$text.Substring(0,$split)]=$text.Substring($split+1)}}
$sources=@($values['SOURCES'].Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$objects=@($values['OBJECTS'].Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$deps=@($values['DEPS'].Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
$shaders=@($values['SHADERS'].Split(' ',[StringSplitOptions]::RemoveEmptyEntries))
Check ($sources.Count -gt 0 -and $sources.Count -eq $objects.Count -and $sources.Count -eq $deps.Count) 'Source/object/dependency counts differ.'
foreach($s in $sources){Check ($s.StartsWith("$source/src/",[StringComparison]::Ordinal)) 'Source must be read directly from actual checkout.'}
foreach($o in $objects){Check ($o.StartsWith("$cache/objects/",[StringComparison]::Ordinal)) 'Object must be outside the source checkout.'}
foreach($d in $deps){Check ($d.StartsWith("$cache/objects/",[StringComparison]::Ordinal)) 'Dependency must be outside the source checkout.'}
Check ($shaders.Count -eq 4) 'Expected exactly four shader headers.'
foreach($s in $shaders){Check ($s.StartsWith("$cache/generated-shaders/shaders/",[StringComparison]::Ordinal)) 'Generated shader header must be external.'}
foreach($flag in @('-std=c++17','-Wall','-Wextra','-Werror','-O0',"-I$source/src","-I$cache/generated-shaders")){
    Check (@($values['FLAGS'].Split(' ',[StringSplitOptions]::RemoveEmptyEntries)) -ccontains $flag) "Required compiler flag was changed: $flag"
}
# The dry run resolves the real SDK rule and actual source prerequisites, but
# executes no compiler/shader tool or recursive SDK build.
$dry=@(& $make @prefix @common '-n' $objects[0] '--no-print-directory' 2>&1)
Check ($LASTEXITCODE -eq 0) "GNUmake dry run failed: $($dry -join [Environment]::NewLine)"
$dry|Set-Content -LiteralPath "$FixtureRoot/make-dry-run.log"
$text=$dry -join [Environment]::NewLine
Check ($text.Contains("-c -o '$($objects[0])' '$($sources[0])'")) 'Compiler rule must consume the actual source and write an external object.'
Check ($text.Contains("-MF '$($deps[0])'")) 'Compiler dependency output must be external.'
Check ($text.Contains("$cache/generated-shaders/shaders/unlit_vsh.inl.input")) 'Normalized shader compiler input must be external.'
Check (-not (Test-Path -LiteralPath "$FixtureRoot/cache/source")) 'A source mirror was created.'
# Prove make's dependency invalidation with a new local fixture, not a product
# source edit or a Git export. Objects are dummy freshness markers; -q/-n never
# invoke a compiler. Both source and included-header changes must invalidate it.
$inputRoot=Join-Path $FixtureRoot 'dependency-input'
New-Item -ItemType Directory -Force -Path "$inputRoot/src","$FixtureRoot/cache/objects/src"|Out-Null
$localSource="$inputRoot/src/local.cpp";$localHeader="$inputRoot/src/local.hpp"
$localObject="$FixtureRoot/cache/objects/src/local.obj";$localDep="$FixtureRoot/cache/objects/src/local.cpp.d"
[IO.File]::WriteAllText($localSource,'#include "local.hpp"')
[IO.File]::WriteAllText($localHeader,'// first local header')
[IO.File]::WriteAllText($localObject,'freshness marker; not compiled')
$old=[DateTime]::UtcNow.AddMinutes(-2)
[IO.File]::SetLastWriteTimeUtc($localSource,$old);[IO.File]::SetLastWriteTimeUtc($localHeader,$old)
[IO.File]::SetLastWriteTimeUtc($localObject,$old.AddMinutes(1))
$fixturePrefix=@($prefix)
if($windowsHost){$inputMount=$inputRoot.Replace('\','/')}else{
    $inputMount='/fixture'
    $at=[Array]::IndexOf($fixturePrefix,'--entrypoint')
    $fixturePrefix=@($fixturePrefix[0..($at-1)])+@('--mount',"type=bind,source=$inputRoot,target=/fixture,readonly")+@($fixturePrefix[$at..($fixturePrefix.Count-1)])
}
$objectTarget="$cache/objects/src/local.obj"
[IO.File]::WriteAllText($localDep,"$objectTarget`: $inputMount/src/local.cpp $inputMount/src/local.hpp`n")
$fixtureArgs=@("SOURCE_ROOT=$inputMount","SRCS=$inputMount/src/local.cpp","NATIVE_OBJECT_ROOT=$cache/objects",'SHADER_OBJS=',"OUTPUT_DIR=$work/disc","GEN_XISO=$work/game.iso")
& $make @fixturePrefix @fixtureArgs '-q' $objectTarget '--no-print-directory' 2>&1|Out-Null
Check ($LASTEXITCODE -eq 0) 'Fresh local object was not up to date.'
[IO.File]::AppendAllText($localSource,'// uncommitted source mutation')
& $make @fixturePrefix @fixtureArgs '-q' $objectTarget '--no-print-directory' 2>&1|Out-Null
Check ($LASTEXITCODE -eq 1) 'Changed local source did not invalidate its external object.'
[IO.File]::SetLastWriteTimeUtc($localSource,$old)
[IO.File]::AppendAllText($localHeader,'// uncommitted header mutation')
& $make @fixturePrefix @fixtureArgs '-q' $objectTarget '--no-print-directory' 2>&1|Out-Null
Check ($LASTEXITCODE -eq 1) 'Included local header did not invalidate its external object.'
$fixtureDry=@(& $make @fixturePrefix @fixtureArgs '-n' $objectTarget '--no-print-directory' 2>&1)
Check ($LASTEXITCODE -eq 0 -and ($fixtureDry -join "`n").Contains("'$inputMount/src/local.cpp'")) 'Dependency rebuild plan lost its actual local source.'
Check ([IO.File]::ReadAllText($localObject) -ceq 'freshness marker; not compiled') 'A compiler ran during the dry run.'
Check ((Get-SourceFingerprint).Hash -ceq $fingerprint.Hash) 'Read-only product sources changed during the test.'
Write-Output "PASS: $script:passed direct-source layout assertions; make print/dry-run only, no compiler executed."
