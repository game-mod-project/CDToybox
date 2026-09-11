# 게임을 포그라운드로 가져오지 않고 오버레이를 캡처·클릭한다.
#
#   . .\scripts\overlay-check.ps1        # 함수와 [CDClick] 을 현재 세션에 싣는다
#   Toggle                               # Insert 를 게임 창에 보낸다(오버레이 켜고 끄기)
#   Cap "shot-01"                        # PrintWindow 캡처 -> .\shots\shot-01.png
#   ClickAt 1118 680                     # 클라이언트 좌표 클릭(700ms 대기)
#   [CDClick]::Cursor()                  # 실제 커서 위치와 그 아래 hwnd
#
# 왜 이렇게 하나:
# - PrintWindow(hwnd, hdc, 2) 는 DWM 이 합성한 내용이라 창이 가려져 있어도 찍힌다.
# - ImGui Win32 백엔드는 WM_MOUSEMOVE 에 TrackMouseEvent 를 걸어 실제 커서가 창 밖이면
#   곧바로 WM_MOUSELEAVE 가 와 MousePos 가 리셋된다. 그래서 MOUSEMOVE 와 버튼 메시지
#   사이에 지연을 두면 안 된다.
# - 탭·Selectable(AllowOverlap) 은 직전 프레임에 호버돼 있어야 눌린다. WM_CHAR(1) 을
#   사이에 끼우면 ImGui 트리클이 거기서 끊어 pos 와 down 을 다른 프레임에 처리한다.
#   InputText 는 문자 1 을 거른다.
# - Esc 는 모달이 아니면 게임으로 새어 메뉴가 열린다. 팝업은 빈 곳 클릭으로 닫는다.
# - SetForegroundWindow / keybd_event / SendInput 은 쓰지 않는다 - 사용자의 창을 건드린다.
# - 게임 메모리에 쓰는 버튼과 고르기 팝업의 [적용] 은 누르지 않는다.
param([string]$OutDir = (Join-Path (Get-Location) "shots"))

$src = @'
using System; using System.Runtime.InteropServices;
public static class CDClick {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out long p);
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(long p);
  public static IntPtr LP(int x, int y) { return (IntPtr)(((long)y << 16) | (long)(x & 0xFFFF)); }
  public static void Move(IntPtr h, int x, int y) { PostMessage(h, 0x200, IntPtr.Zero, LP(x,y)); }
  public static void ClickOverlap(IntPtr h, int x, int y) { var l = LP(x,y); PostMessage(h, 0x200, IntPtr.Zero, l); PostMessage(h, 0x102, (IntPtr)1, IntPtr.Zero); PostMessage(h, 0x201, (IntPtr)1, l); PostMessage(h, 0x202, IntPtr.Zero, l); }
  public static void Key(IntPtr h, int vk) { PostMessage(h, 0x100, (IntPtr)vk, (IntPtr)1); PostMessage(h, 0x101, (IntPtr)vk, new IntPtr(0xC0000001L)); }
  public static string Cursor() { long p; GetCursorPos(out p); int x = (int)(p & 0xFFFFFFFF); int y = (int)(p >> 32); return x + "," + y + " hwnd=" + WindowFromPoint(p); }
}
'@
if (-not ("CDClick" -as [type])) { Add-Type -TypeDefinition $src }
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null
$script:dir = $OutDir
$script:h = (Get-Process CrimsonDesert -ErrorAction Stop).MainWindowHandle
function Cap($name) {
  $bmp = New-Object System.Drawing.Bitmap 1920, 1080
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $g.GetHdc(); [CDClick]::PrintWindow($script:h, $hdc, 2) | Out-Null; $g.ReleaseHdc($hdc)
  $bmp.Save((Join-Path $script:dir "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose(); "$name saved"
}
function ClickAt($x, $y, $ms = 700) { [CDClick]::ClickOverlap($script:h, $x, $y); Start-Sleep -Milliseconds $ms }
function Toggle() { [CDClick]::Key($script:h, 0x2D); Start-Sleep -Milliseconds 1200 }
"game hwnd=$script:h  shots -> $script:dir"
