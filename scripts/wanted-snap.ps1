# 수배 컴포넌트를 통째로 떠서 파일로 남긴다. **A/B 로 견주려고 만든 것이다.**
#
#   .\scripts\wanted-snap.ps1 on      # 수배가 켜진 판에서
#   .\scripts\wanted-snap.ps1 off     # 게임 안에서 수배를 푼 뒤
#   .\scripts\wanted-snap.ps1 -Diff   # 둘을 견준다
#
# 왜 필요한가 — **벌금 금액과 수배 상태는 다른 것이고, 상태가 어디 있는지
# 모른다.** `WantedRegionData` 는 100.00/현상수배 때와 0/벌금 때가 `+0x30`
# 말고 한 바이트도 안 다르다. 그래서 상태는 그 레코드 밖에 있다.
#
# `+0x30`(벌금)을 찾은 방법이 바로 이 A/B 였다. 같은 방법을 상태에 쓴다 -
# **바이트를 눈으로 훑으며 추측하지 않는다.** 2026-09-19 에 그렇게 하다가
# 두 번 틀린 원인을 적었다(STATUS 1.24 의 정정 둘).
#
# 주소는 로그에서 읽는다(`수배 컴포넌트: 0x…`). 실행마다 바뀌므로 박지 않는다.

param(
    [string]$Tag = "",
    [switch]$Diff,
    [string]$OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) "shots\wanted")
)

$ErrorActionPreference = "Stop"
$Root  = Split-Path -Parent $PSScriptRoot
$Probe = Join-Path $Root "build\cdtb_probe.exe"
$Log   = "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CDToybox.log"

if (-not (Test-Path $Probe)) { throw "probe 가 없습니다: $Probe (build.ps1 먼저)" }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force $OutDir | Out-Null }

if ($Diff) {
    $a = Join-Path $OutDir "on.txt"
    $b = Join-Path $OutDir "off.txt"
    foreach ($f in @($a, $b)) {
        if (-not (Test-Path $f)) { throw "없습니다: $f  (on 과 off 를 둘 다 떠야 합니다)" }
    }
    # 주소는 판마다 다르므로 **주소 열은 빼고** 값만 견준다.
    $strip = { param($p) Get-Content $p | ForEach-Object { $_ -replace '^0x[0-9A-Fa-f]+\s+', '' } }
    $la = & $strip $a
    $lb = & $strip $b
    Write-Host "on  $($la.Count)줄   off $($lb.Count)줄"
    $n = [Math]::Min($la.Count, $lb.Count)
    $diffs = 0
    for ($i = 0; $i -lt $n; $i++) {
        if ($la[$i] -ne $lb[$i]) {
            $diffs++
            Write-Host ("줄 {0,3}" -f $i) -ForegroundColor Yellow
            Write-Host "   on  $($la[$i])"
            Write-Host "   off $($lb[$i])"
        }
    }
    Write-Host "다른 줄 $diffs 개" -ForegroundColor Cyan
    if ($diffs -eq 0) {
        Write-Host "한 줄도 안 다릅니다 - 상태는 이 컴포넌트 밖입니다." -ForegroundColor Cyan
    }
    return
}

if ($Tag -ne "on" -and $Tag -ne "off") { throw "사용법: wanted-snap.ps1 on | off | -Diff" }

if (-not (Get-Process CrimsonDesert -ErrorAction SilentlyContinue)) {
    throw "게임이 실행 중이어야 합니다."
}

# 컴포넌트 주소는 **로그에서** 읽는다. 없으면 창에서 [다시 찾기] 를 눌러야 한다.
$line = Select-String -Path $Log -Pattern '수배 컴포넌트: 0x([0-9A-Fa-f]+)' |
        Select-Object -Last 1
if (-not $line) { throw "로그에 `수배 컴포넌트: 0x…` 가 없습니다 - 창에서 [다시 찾기] 를 누르십시오." }
$comp = [Convert]::ToUInt64($line.Matches[0].Groups[1].Value, 16)
Write-Host ("컴포넌트 0x{0:X} (로그에서 읽음)" -f $comp)

# **주소가 아직 그 객체인지 확인한다.** 로그의 주소는 죽는다 - 세이브를 다시
# 부르거나 지역을 옮기면 그 자리에 다른 객체가 들어앉는다(wanted.h). 실제로
# 2026-09-19 에 안 거르고 떴다가 쓰레기를 스냅숏으로 남길 뻔했다.
$what = (& $Probe whatis ("0x{0:X}" -f $comp) 2>&1) -join "`n"
if ($what -notmatch 'ClientSelfWantedActorComponent') {
    Write-Host $what
    throw "그 주소는 더 이상 수배 컴포넌트가 아닙니다. 게임 창의 플레이어 치트 > 범죄수치에서 [다시 찾기] 를 누른 뒤 다시 돌리십시오."
}
Write-Host "확인: $what"

$out = Join-Path $OutDir "$Tag.txt"
"# 수배 컴포넌트 스냅숏 ($Tag)  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" | Set-Content $out
("# comp 0x{0:X}" -f $comp) | Add-Content $out

# 컴포넌트 머리 0x100. 여기에 벡터 머리들이 들어 있다.
& $Probe dump ("0x{0:X}" -f $comp) 256 2>&1 | Add-Content $out

# 벡터 넷(+0x30 · +0x40 · +0x50 · +0x70)의 데이터도 같이 뜬다.
# 크기는 안 믿고 고정 폭만 본다 - A/B 는 **같은 폭**이어야 견줄 수 있다.
foreach ($off in 0x30, 0x40, 0x50, 0x70) {
    $head = & $Probe dump ("0x{0:X}" -f ($comp + $off)) 16 2>&1
    $hex = ($head | Select-String '^0x' | Select-Object -First 1).ToString()
    # "0xADDR  b0 b1 ..." 의 첫 8바이트가 데이터 포인터다.
    $bytes = ($hex -split '\s+')[1..8]
    if ($bytes -contains $null) { continue }
    $ptr = 0UL
    for ($i = 7; $i -ge 0; $i--) { $ptr = ($ptr -shl 8) -bor [Convert]::ToUInt64($bytes[$i], 16) }
    ("# vec +0x{0:X} -> 0x{1:X}" -f $off, $ptr) | Add-Content $out
    if ($ptr -eq 0) { continue }
    & $Probe dump ("0x{0:X}" -f $ptr) 512 2>&1 | Add-Content $out
}

Write-Host "떴습니다 -> $out"
Write-Host "이제 게임 안에서 수배를 반대 상태로 만든 뒤 다른 태그로 한 번 더 뜨십시오."
