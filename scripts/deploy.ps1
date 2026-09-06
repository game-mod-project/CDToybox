$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src  = Join-Path $Root "build\xinput1_4.dll"
$Dst  = "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64"

if (-not (Test-Path $Src)) { throw "빌드 산출물이 없습니다: $Src" }

# 빌드가 실패해도 이전 산출물이 남아 있어 낡은 DLL을 배포할 수 있다.
# 소스보다 오래된 산출물은 거부한다. 게임 재시작 한 번이 비싸다.
$newestSrc = Get-ChildItem (Join-Path $Root "src") -Recurse -File |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
$dll = Get-Item $Src
if ($newestSrc -and $dll.LastWriteTime -lt $newestSrc.LastWriteTime) {
    throw ("산출물이 소스보다 오래되었습니다. 빌드가 실패했을 수 있습니다.`n" +
           "  DLL : $($dll.LastWriteTime)`n" +
           "  소스: $($newestSrc.LastWriteTime)  ($($newestSrc.Name))")
}

$proc = Get-Process CrimsonDesert -ErrorAction SilentlyContinue
if ($proc) { throw "게임이 실행 중입니다 (PID $($proc.Id)). 종료 후 다시 시도하세요." }

Copy-Item $Src (Join-Path $Dst "xinput1_4.dll") -Force
Write-Host "deployed -> $Dst\xinput1_4.dll  ($($dll.LastWriteTime))"
