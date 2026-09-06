$ErrorActionPreference = "Stop"
$BT     = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$Cmake  = "$BT\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$Ninja  = "$BT\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$VcVars = "$BT\VC\Auxiliary\Build\vcvarsall.bat"
$Root   = Split-Path -Parent $PSScriptRoot
$Build  = Join-Path $Root "build"

$configure = "`"$Cmake`" -S `"$Root`" -B `"$Build`" -G Ninja " +
             "-DCMAKE_MAKE_PROGRAM=`"$Ninja`" -DCMAKE_BUILD_TYPE=RelWithDebInfo"
$compile   = "`"$Cmake`" --build `"$Build`""

cmd /c "`"$VcVars`" x64 >nul && $configure && $compile"
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Host "build ok -> $Build"
