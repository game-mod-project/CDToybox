$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src  = Join-Path $Root "build\xinput1_4.dll"
$Dst  = "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64"

if (-not (Test-Path $Src)) { throw "빌드 산출물이 없습니다: $Src" }
Copy-Item $Src (Join-Path $Dst "xinput1_4.dll") -Force
Write-Host "배포 완료 -> $Dst\xinput1_4.dll"
