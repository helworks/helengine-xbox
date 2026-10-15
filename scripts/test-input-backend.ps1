param(
    [Parameter(Mandatory=$true)][string]$GeneratedCoreRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$Compiler='g++'
)
$ErrorActionPreference='Stop'
$Repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
New-Item -ItemType Directory -Force -Path $OutputRoot|Out-Null
$Sources=@('InputFrameState.cpp','InputGamepadState.cpp','InputPointerState.cpp','InputTouchState.cpp','InputTextState.cpp')|ForEach-Object {Join-Path $GeneratedCoreRoot $_}
$Executable=Join-Path $OutputRoot 'input_backend_tests.exe'
& $Compiler -std=c++20 -Wall -Wextra -Werror -UNDEBUG -DHELENGINE_XBOX_HAS_GENERATED_CORE ("-I"+(Join-Path $Repo 'tests/input_backend_stubs')) ("-I"+(Join-Path $Repo 'src')) ("-I"+$GeneratedCoreRoot) (Join-Path $Repo 'tests/input_backend_tests.cpp') (Join-Path $Repo 'src/platform/xbox/XboxInputBackend.cpp') @Sources -o $Executable
if($LASTEXITCODE -ne 0){throw "Strict input backend test compilation failed: $LASTEXITCODE"}
& $Executable
if($LASTEXITCODE -ne 0){throw "Input backend contract test failed: $LASTEXITCODE"}
