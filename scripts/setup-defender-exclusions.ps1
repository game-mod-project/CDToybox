# Windows Defender 제외 경로 등록.
#
# 2026-09-17, Defender 가 배포된 bin64\xinput1_4.dll 을
# Trojan:Win32/Bearfoos.A!ml 로 격리해 모드가 아예 안 실렸다. `!ml` 은
# 머신러닝 휴리스틱이고, 서명 없는 DLL + API 후킹 + 힙 전수 탐색은
# 인젝터와 특징이 같다. 빌드마다 해시가 바뀌므로 다시 걸릴 수 있다.
# TROUBLESHOOTING.md 6.25 참고.
#
# 게임 폴더를 통째로 빼지 않는다 - 그 안에 무엇이 떨어져도 검사를 안 하게
# 된다. 배포 대상 DLL 하나와 각 워크트리의 build 폴더만 뺀다.
#
# 워크트리를 새로 만든 뒤 다시 실행하면 그 build 폴더가 더해진다. 이미
# 등록된 경로는 Defender 가 알아서 무시하므로 몇 번 돌려도 무해하다.

[CmdletBinding()]
param(
    # 배포 대상. deploy.ps1 의 것과 같아야 한다.
    [string] $GameBin = "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64",
    # 무엇이 등록될지만 보고 끝낸다. 관리자 권한이 필요 없다.
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot

# --- 제외할 경로를 모은다 -------------------------------------------------

$paths = [System.Collections.Generic.List[string]]::new()
$paths.Add((Join-Path $GameBin "xinput1_4.dll"))

# 워크트리마다 build 가 따로 있고, 배포 전 산출물이 거기서 먼저 잡힌다.
# `git worktree list --porcelain` 은 "worktree <절대경로>" 줄로 시작한다.
$worktrees = @(git -C $repo worktree list --porcelain |
               Where-Object { $_ -like "worktree *" } |
               ForEach-Object { $_.Substring(9) })

if (-not $worktrees) { throw "워크트리를 못 읽었습니다. git 저장소가 맞습니까? ($repo)" }

foreach ($w in $worktrees) {
    $paths.Add((Join-Path ($w -replace '/', '\') "build"))
}

Write-Host "등록할 경로:" -ForegroundColor Cyan
$paths | ForEach-Object { Write-Host "  $_" }
Write-Host ""

if ($DryRun) {
    Write-Host "-DryRun 이므로 등록하지 않고 끝냅니다." -ForegroundColor Yellow
    return
}

# --- 관리자 권한 ----------------------------------------------------------

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())

if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "관리자 권한이 필요합니다. 승격된 창을 엽니다 (UAC 확인)." -ForegroundColor Yellow
    # $args 는 자동 변수다 - 다른 이름을 쓴다.
    $relaunch = @('-NoProfile', '-ExecutionPolicy', 'Bypass',
                  '-File', $PSCommandPath, '-GameBin', $GameBin)
    Start-Process powershell -Verb RunAs -ArgumentList $relaunch
    return
}

# --- 등록하고 되읽어 확인한다 ---------------------------------------------

Add-MpPreference -ExclusionPath $paths

$now = @((Get-MpPreference).ExclusionPath)
$missing = @($paths | Where-Object { $now -notcontains $_ })

Write-Host "현재 제외 목록:" -ForegroundColor Cyan
$now | ForEach-Object { Write-Host "  $_" }
Write-Host ""

if ($missing) {
    Write-Host "등록되지 않은 경로가 있습니다:" -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  $_" }
    # 승격된 창은 끝나면 사라진다 - 실패를 볼 수 있게 붙잡는다.
    if ($Host.Name -eq 'ConsoleHost') { Read-Host "엔터를 누르면 닫습니다" }
    exit 1
}

Write-Host "$($paths.Count) 개 경로가 모두 등록됐습니다." -ForegroundColor Green
Write-Host "이미 격리된 파일은 되살아나지 않습니다. deploy.ps1 로 다시 까십시오." -ForegroundColor Gray
if ($Host.Name -eq 'ConsoleHost') { Read-Host "엔터를 누르면 닫습니다" }
