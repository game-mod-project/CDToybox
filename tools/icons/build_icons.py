"""아이템 아이콘 아틀라스를 만든다. 한 번만 돌리면 된다.

    python tools/icons/build_icons.py --table <itemtable.bin> --out <icons.bin>

`itemtable.bin` 은 `cdtb_probe items save <파일>` 로 만든다.

왜 이렇게 하는가
----------------
아이콘은 crimsondb.gg 에서 한 번 받아 로컬 아틀라스로 굽는다. 모드는
그 파일만 읽으므로 **사이트가 바뀌거나 닫혀도 계속 동작한다.** 런타임에
네트워크를 쓰지 않는다.

원시 RGBA 로 굽는 이유는 모드에 이미지 디코더를 넣지 않기 위해서다.
D3D12 텍스처도 한 장이면 된다.

산출물은 저장소에 커밋하지 않는다(20MB 안팎). 이 스크립트로 언제든
다시 만든다.

파일 형식 (리틀엔디언)
----------------------
    u32 magic 'CDIC'    u32 version=1
    u32 cell            한 칸의 픽셀 (정사각)
    u32 cols  u32 rows  아틀라스 격자
    u32 count           매핑 개수
    count * { u32 아이템키, u32 칸번호 }
    RGBA 픽셀 cols*cell * rows*cell * 4
"""
import argparse
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.request
from collections import OrderedDict

SITE = 'https://crimsondb.gg'
UA = ('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 '
      '(KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36')

# 아이템이 있는 목록 쪽. /ko/items 에서 확인했다.
CATEGORIES = ['weapons', 'armor', 'shields', 'accessories', 'mount-gear',
              'tools', 'consumables', 'materials', 'ammunition',
              'miscellaneous', 'abyss-gears']

CARD_RE = re.compile(
    r'<a href="(?P<href>/ko/[^"]+)"[^>]*class="item-card".*?'
    r'(?:<span class="tier-badge"[^>]*>(?P<tier>T\d)</span>)?.*?'
    # 디렉터리에 밑줄이 섞인다(mount_gear). 하이픈만 허용하면 그
    # 카테고리가 통째로 빠진다 - 실제로 114개를 놓쳤다.
    r'images/items/(?P<dir>[a-z_-]+)/(?P<hash>[0-9a-f]+)\.webp.*?'
    r'class="card-name"[^>]*>(?P<name>[^<]+)<'
    r'(?:/span><span class="card-type"[^>]*>(?P<type>[^<]*)<)?', re.S)


def fetch(url, tries=3):
    for i in range(tries):
        try:
            req = urllib.request.Request(
                url, headers={'User-Agent': UA, 'Accept-Language': 'ko'})
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except (urllib.error.URLError, TimeoutError) as e:
            if i + 1 == tries:
                raise
            time.sleep(1.5 * (i + 1))
    return None


def scrape():
    """{이름: (디렉터리, 해시)} 를 모은다. 같은 이름은 처음 것을 쓴다."""
    found = OrderedDict()
    for cat in CATEGORIES:
        page, seen_here = 1, 0
        while True:
            url = '%s/ko/%s?page=%d' % (SITE, cat, page)
            html = fetch(url).decode('utf-8', 'replace')
            cards = list(CARD_RE.finditer(html))
            if not cards:
                break
            for m in cards:
                name = m.group('name').strip()
                if name and name not in found:
                    found[name] = (m.group('dir'), m.group('hash'),
                                   (m.group('type') or '').strip())
            seen_here += len(cards)
            page += 1
            time.sleep(0.2)
            if page > 60:      # 안전장치
                break
        print('  %-14s %4d개 (누적 %d)' % (cat, seen_here, len(found)))
        sys.stdout.flush()
    return found


def download_icons(found, cache_dir, cell):
    os.makedirs(cache_dir, exist_ok=True)
    got, failed = {}, 0
    for i, (name, (d, h, _t)) in enumerate(found.items(), 1):
        path = os.path.join(cache_dir, '%s_%s.png' % (d, h))
        if not os.path.exists(path):
            url = '%s/_ipx/f_png&s_%dx%d/images/items/%s/%s.webp' % (
                SITE, cell, cell, d, h)
            try:
                data = fetch(url)
            except Exception:
                failed += 1
                continue
            with open(path, 'wb') as f:
                f.write(data)
            time.sleep(0.05)
        got[name] = path
        if i % 200 == 0:
            print('  받는 중 %d/%d' % (i, len(found)))
            sys.stdout.flush()
    return got, failed


def load_item_table(path):
    with open(path, 'rb') as f:
        buf = f.read()
    magic, count, recsz = struct.unpack_from('<III', buf, 0)
    if magic != 0x49544443:
        raise SystemExit('itemtable.bin 이 아닙니다')
    out, o = [], 12
    for _ in range(count):
        key, _nk, nlen = struct.unpack_from('<IQI', buf, o)
        o += 16
        name = buf[o:o + nlen].decode('utf-8', 'replace')
        o += nlen + recsz
        out.append((key, name))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--table', required=True, help='cdtb_probe items save 산출물')
    ap.add_argument('--out', required=True, help='만들 아틀라스 경로')
    ap.add_argument('--cell', type=int, default=32)
    ap.add_argument('--cache', default=None, help='내려받은 PNG 보관 폴더')
    ap.add_argument('--types-out', default=None,
                    help='"유형 TAB 이름" TSV 경로 (분류 라벨링용)')
    ap.add_argument('--types-only', action='store_true',
                    help='아이콘은 건너뛰고 유형만 모은다')
    args = ap.parse_args()

    from PIL import Image   # 아틀라스를 구울 때만 필요하다

    cache = args.cache or os.path.join(os.path.dirname(args.out), 'icon_cache')

    print('1) 사이트에서 아이콘 목록을 모읍니다')
    found = scrape()
    print('   이름 %d개' % len(found))

    if args.types_out:
        with open(args.types_out, 'w', encoding='utf-8') as f:
            for name, (_d, _h, t) in found.items():
                if t:
                    f.write(t + '\t' + name + '\n')
        print('   유형 TSV -> %s' % args.types_out)
    if args.types_only:
        return

    print('2) 아이콘을 내려받습니다 (%dx%d PNG, 이미 있으면 건너뜁니다)'
          % (args.cell, args.cell))
    got, failed = download_icons(found, cache, args.cell)
    print('   받음 %d개, 실패 %d개' % (len(got), failed))

    print('3) 아이템 표와 이름으로 맞춥니다')
    items = load_item_table(args.table)
    # 같은 이름의 아이템이 여럿이면 전부 같은 아이콘을 가리킨다.
    name_to_cell, mapping = {}, []
    for key, name in items:
        if not name or name not in got:
            continue
        if name not in name_to_cell:
            name_to_cell[name] = len(name_to_cell)
        mapping.append((key, name_to_cell[name]))
    print('   아이템 %d개에 아이콘을 붙였습니다 (칸 %d개)'
          % (len(mapping), len(name_to_cell)))

    print('4) 아틀라스를 굽습니다')
    cell = args.cell
    cols = max(1, 2048 // cell)
    rows = (len(name_to_cell) + cols - 1) // cols
    atlas = Image.new('RGBA', (cols * cell, rows * cell), (0, 0, 0, 0))
    for name, idx in name_to_cell.items():
        try:
            img = Image.open(got[name]).convert('RGBA')
        except Exception:
            continue
        if img.size != (cell, cell):
            img = img.resize((cell, cell), Image.LANCZOS)
        atlas.paste(img, ((idx % cols) * cell, (idx // cols) * cell))

    with open(args.out, 'wb') as f:
        f.write(struct.pack('<IIIIII', 0x43494443, 1, cell, cols, rows,
                            len(mapping)))
        for key, idx in mapping:
            f.write(struct.pack('<II', key, idx))
        f.write(atlas.tobytes())
    size = os.path.getsize(args.out)
    print('   %s  %dx%d  %.1f MB' % (args.out, atlas.width, atlas.height,
                                     size / 1048576))


if __name__ == '__main__':
    main()
