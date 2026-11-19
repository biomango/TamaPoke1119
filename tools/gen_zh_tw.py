#!/usr/bin/env python3
"""Generates i18n_zh.h (Traditional Chinese game data tables) and cjk_font.h
(subsetted 12x12 + 16x16 1-bit CJK bitmap fonts covering every non-ASCII
character used in the Traditional Chinese firmware).

Data sources:
  - PokeAPI official zh-Hant CSVs (pokemon_species_names, move_names,
    type_names, region_names)
  - Fusion Pixel 12px zh_tw BDF (OFL-1.1) for crisp 12x12 / 24x24 glyphs
  - GNU Unifont 16px OTF (GPL-2.0+FE / OFL-1.1) for crisp 16x16 / 32x32 glyphs

Usage:
  python3 tools/gen_zh_tw.py \
    --csv-dir /tmp/pokeapi_zh \
    --bdf12 /tmp/fonts_test/fusion-pixel-12px-monospaced-zh_tw.bdf \
    --otf16 /tmp/fonts_test/unifont-16.0.04.otf
"""
import argparse
import csv
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Official Traditional Chinese (zh-Hant / Taiwan) Gym Leaders, Elite Four,
# Champions, and towns/islands matching trainers.h exactly (7 regions x 13).
TRAINERS_ZH = [
    # 0: KANTO
    [
        ("小剛", "深灰市"), ("小霞", "華藍市"), ("馬志士", "枯葉市"),
        ("莉佳", "玉虹市"), ("阿桔", "淺紅市"), ("娜姿", "金黃市"),
        ("夏伯", "紅蓮島"), ("坂木", "常磐市"),
        ("科拿", "四天王"), ("希巴", "四天王"), ("菊子", "四天王"),
        ("阿渡", "四天王"), ("勁敵", "冠軍"),
    ],
    # 1: JOHTO
    [
        ("阿速", "桔梗市"), ("阿筆", "檜皮鎮"), ("小茜", "滿金市"),
        ("松葉", "圓朱市"), ("阿四", "湛藍市"), ("阿蜜", "淺蔥市"),
        ("柳伯", "卡吉鎮"), ("小椿", "煙墨市"),
        ("一樹", "四天王"), ("阿桔", "四天王"), ("希巴", "四天王"),
        ("梨花", "四天王"), ("阿渡", "冠軍"),
    ],
    # 2: HOENN
    [
        ("杜娟", "卡那茲市"), ("藤樹", "武鬥鎮"), ("鐵旋", "紫堇市"),
        ("亞莎", "釜炎鎮"), ("千里", "橙華市"), ("娜琪", "茵鬱市"),
        ("小楓", "綠嶺市"), ("亞當", "琉璃市"),
        ("花月", "四天王"), ("芙蓉", "四天王"), ("波妮", "四天王"),
        ("源治", "四天王"), ("米可利", "冠軍"),
    ],
    # 3: SINNOH
    [
        ("瓢太", "黑金市"), ("菜種", "百代市"), ("梅麗莎", "緣之市"),
        ("阿李", "帷幕市"), ("吉憲", "野原市"), ("東瓜", "水脈市"),
        ("小菘", "切鋒市"), ("電次", "濱海市"),
        ("阿柳", "四天王"), ("菊野", "四天王"), ("大葉", "四天王"),
        ("悟松", "四天王"), ("竹蘭", "冠軍"),
    ],
    # 4: UNOVA
    [
        ("黑連", "檜扇市"), ("霍米加", "立湧市"), ("亞堤", "飛雲市"),
        ("小菊兒", "雷文市"), ("菊老大", "帆巴市"), ("風露", "吹寄市"),
        ("夏卡", "雙龍市"), ("西子伊", "青海波市"),
        ("婉龍", "四天王"), ("越橘", "四天王"), ("嘉德麗雅", "四天王"),
        ("連武", "四天王"), ("艾莉絲", "冠軍"),
    ],
    # 5: KALOS
    [
        ("紫羅蘭", "白檀市"), ("查克洛", "遙香市"), ("可爾妮", "娑羅市"),
        ("福爺", "海翼市"), ("希特隆", "密阿雷市"), ("瑪繡", "香薰市"),
        ("葛吉花", "百刻市"), ("得撫", "映雪市"),
        ("帕琦拉", "四天王"), ("志米", "四天王"), ("雁鎧", "四天王"),
        ("朵拉塞娜", "四天王"), ("卡露妮", "冠軍"),
    ],
    # 6: ALOLA
    [
        ("伊利馬", "美樂美樂島"), ("水蓮", "阿卡拉島"), ("卡奇", "阿卡拉島"),
        ("瑪奧", "阿卡拉島"), ("馬瑪內", "烏拉烏拉島"), ("茉莉", "波尼島"),
        ("默丹", "烏拉烏拉島"), ("哈普烏", "波尼島"),
        ("哈拉", "四天王"), ("麗姿", "四天王"), ("阿塞蘿拉", "四天王"),
        ("卡希麗", "四天王"), ("庫庫伊", "冠軍"),
    ],
]

AVATARS_ZH = [
    "赤紅", "青綠", "女孩", "精英男", "精英女", "登山男", "水手", "短褲小子"
]

MOVE_ALIASES = {
    "HIJUMPKICK": "HIGHJUMPKICK",
    "DAZZLEGLEAM": "DAZZLINGGLEAM",
}

TYPE_ORDER = [
    "NORMAL", "FIRE", "WATER", "ELECTRIC", "GRASS", "ICE", "FIGHTING",
    "POISON", "GROUND", "FLYING", "PSYCHIC", "BUG", "ROCK", "GHOST",
    "DRAGON", "DARK", "STEEL", "FAIRY",
]


def c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def load_pokeapi_data(csv_dir: str):
    # 1. Species 1..809
    sp_zh = {}
    with open(os.path.join(csv_dir, "pokemon_species_names.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["local_language_id"] == "4":
                sp_zh[int(r["pokemon_species_id"])] = r["name"]
    with open(os.path.join(ROOT, "dex.h"), encoding="utf-8") as f:
        m = re.search(r"#define\s+DEX_COUNT\s+(\d+)", f.read())
        dex_count = int(m.group(1))
    species = ["?"]
    for i in range(1, dex_count + 1):
        if i not in sp_zh:
            sys.exit(f"Missing zh-Hant species name for dex #{i}")
        species.append(sp_zh[i])

    # 2. Moves matching MOVE_TBL order in moves.h
    move_en_to_id = {}
    move_id_to_zh = {}
    with open(os.path.join(csv_dir, "move_names.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["local_language_id"] == "9":
                norm = re.sub(r"[^A-Z0-9]", "", r["name"].upper())
                move_en_to_id[norm] = int(r["move_id"])
            elif r["local_language_id"] == "4":
                move_id_to_zh[int(r["move_id"])] = r["name"]
    with open(os.path.join(ROOT, "moves.h"), encoding="utf-8") as f:
        tbl = f.read().split("static const MoveEntry MOVE_TBL[MOVE_COUNT] = {")[1].split("};")[0]
    move_en_list = re.findall(r'\{\s*"([^"]+)"', tbl)
    moves = []
    for idx, en in enumerate(move_en_list):
        if idx == 0:
            moves.append("-")
            continue
        norm = re.sub(r"[^A-Z0-9]", "", en.upper())
        norm = MOVE_ALIASES.get(norm, norm)
        mid = move_en_to_id.get(norm)
        if not mid or mid not in move_id_to_zh:
            sys.exit(f"Missing zh-Hant move name for #{idx} {en}")
        moves.append(move_id_to_zh[mid])

    # 3. Types matching PkType order
    type_en_to_id = {}
    type_id_to_zh = {}
    with open(os.path.join(csv_dir, "type_names.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["local_language_id"] == "9":
                type_en_to_id[r["name"].upper()] = int(r["type_id"])
            elif r["local_language_id"] == "4":
                type_id_to_zh[int(r["type_id"])] = r["name"]
    types = [type_id_to_zh[type_en_to_id[t]] for t in TYPE_ORDER]

    # 4. Regions 1..7 + ALL
    reg_id_to_zh = {}
    with open(os.path.join(csv_dir, "region_names.csv"), encoding="utf-8") as f:
        for r in csv.DictReader(f):
            if r["local_language_id"] == "4":
                reg_id_to_zh[int(r["region_id"])] = r["name"]
    regions = [reg_id_to_zh[i] for i in range(1, 8)] + ["全部"]

    return species, moves, types, regions


def emit_i18n_zh_h(species, moves, types, regions):
    out_path = os.path.join(ROOT, "i18n_zh.h")
    lines = [
        "#pragma once",
        "// GENERATED by tools/gen_zh_tw.py -- do not edit by hand.",
        "// Official Traditional Chinese (zh-Hant) names for species, moves, types,",
        "// regions, trainers, places, and player avatars.",
        "",
        f"static const char *const DEX_NAMES_ZH[{len(species)}] = {{",
    ]
    for i in range(0, len(species), 6):
        chunk = ", ".join(c_str(s) for s in species[i:i + 6])
        lines.append(f"  {chunk},")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const MOVE_NAMES_ZH[{len(moves)}] = {{")
    for i in range(0, len(moves), 6):
        chunk = ", ".join(c_str(s) for s in moves[i:i + 6])
        lines.append(f"  {chunk},")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const TYPE_NAMES_ZH[{len(types)}] = {{")
    lines.append("  " + ", ".join(c_str(s) for s in types) + ",")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const REGION_NAMES_ZH[{len(regions)}] = {{")
    lines.append("  " + ", ".join(c_str(s) for s in regions) + ",")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const TRAINER_NAMES_ZH[{len(TRAINERS_ZH)}][13] = {{")
    for reg in TRAINERS_ZH:
        lines.append("  { " + ", ".join(c_str(n) for n, _ in reg) + " },")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const TRAINER_PLACES_ZH[{len(TRAINERS_ZH)}][13] = {{")
    for reg in TRAINERS_ZH:
        lines.append("  { " + ", ".join(c_str(p) for _, p in reg) + " },")
    lines.append("};")
    lines.append("")

    lines.append(f"static const char *const AVATAR_NAMES_ZH[{len(AVATARS_ZH)}] = {{")
    lines.append("  " + ", ".join(c_str(s) for s in AVATARS_ZH) + ",")
    lines.append("};")
    lines.append("")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"wrote {out_path} ({len(species) - 1} species, {len(moves)} moves, {len(TRAINERS_ZH)*13} trainers)")


def collect_non_ascii_codepoints():
    cps = set()
    sources = [
        os.path.join(ROOT, "i18n.cpp"),
        os.path.join(ROOT, "i18n_zh.h"),
    ]
    for path in sources:
        with open(path, encoding="utf-8") as f:
            for ch in f.read():
                cp = ord(ch)
                if cp > 0x7F:
                    if cp > 0xFFFF:
                        sys.exit(f"Non-BMP character U+{cp:X} ({ch}) in {path}")
                    cps.add(cp)
    # Extra UI labels outside i18n.cpp (e.g., LANG_CODES "繁中")
    for ch in "繁中":
        cps.add(ord(ch))
    return sorted(cps)


def load_bdf_glyphs(bdf_path: str, want_cps: set):
    glyphs = {}
    with open(bdf_path, encoding="utf-8", errors="ignore") as f:
        cur_enc = None
        bbx = None
        in_bm = False
        rows = []
        for line in f:
            s = line.strip()
            if s.startswith("ENCODING "):
                enc = int(s.split()[1])
                cur_enc = enc if enc in want_cps else None
            elif cur_enc is not None and s.startswith("BBX "):
                bbx = tuple(map(int, s.split()[1:]))
            elif cur_enc is not None and s == "BITMAP":
                in_bm = True
                rows = []
            elif in_bm:
                if s == "ENDCHAR":
                    glyphs[cur_enc] = (bbx, rows)
                    cur_enc = None
                    in_bm = False
                else:
                    rows.append(int(s, 16))
    return glyphs


def rasterize_bdf12(bbx, rows):
    """Pack a Fusion Pixel 12px BDF glyph into 12x12 (18 bytes, MSB-first)."""
    bw, bh, bx, by = bbx
    nb = ((bw + 7) // 8) * 8
    y0 = 10 - bh - by  # Fusion Pixel 12px ascent=10, descent=2 -> 12px box
    grid = [[0] * 12 for _ in range(12)]
    for ry, val in enumerate(rows):
        dy = y0 + ry
        if 0 <= dy < 12:
            for rx in range(bw):
                dx = bx + rx
                if 0 <= dx < 12 and (val & (1 << (nb - 1 - rx))):
                    grid[dy][dx] = 1
    bits = [grid[y][x] for y in range(12) for x in range(12)]
    out = bytearray(18)
    for i, b in enumerate(bits):
        if b:
            out[i >> 3] |= 0x80 >> (i & 7)
    return bytes(out)


def rasterize_unifont16(font, cp):
    """Pack a GNU Unifont 16px OTF glyph into 16x16 (32 bytes, MSB-first)."""
    ch = chr(cp)
    img = Image.new("1", (16, 16), 0)
    ImageDraw.Draw(img).text((0, 0), ch, font=font, fill=1)
    # Check if blank (missing in Unifont)
    has_px = False
    out = bytearray(32)
    for y in range(16):
        hi = 0
        lo = 0
        for x in range(8):
            if img.getpixel((x, y)):
                hi |= 0x80 >> x
                has_px = True
        for x in range(8):
            if img.getpixel((x + 8, y)):
                lo |= 0x80 >> x
                has_px = True
        out[y * 2] = hi
        out[y * 2 + 1] = lo
    return bytes(out), has_px


def rasterize_noto_fallback(cp, size):
    """Fallback 1-bit rasterization from Noto Sans CJK TC if a rare glyph is missing."""
    noto = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    font = ImageFont.truetype(noto, size - 1, index=3)
    img = Image.new("L", (size, size), 0)
    ImageDraw.Draw(img).text((0, -2), chr(cp), font=font, fill=255)
    nbytes = (size * size + 7) // 8
    out = bytearray(nbytes)
    idx = 0
    for y in range(size):
        for x in range(size):
            if img.getpixel((x, y)) >= 110:
                out[idx >> 3] |= 0x80 >> (idx & 7)
            idx += 1
    return bytes(out)


def emit_cjk_font_h(cps, bdf12_path, otf16_path):
    want = set(cps)
    bdf_glyphs = load_bdf_glyphs(bdf12_path, want)
    font16 = ImageFont.truetype(otf16_path, 16)

    fb12 = []
    fb16 = []
    bm12 = []
    bm16 = []

    for cp in cps:
        if cp in bdf_glyphs:
            bm12.append(rasterize_bdf12(*bdf_glyphs[cp]))
        else:
            fb12.append(cp)
            bm12.append(rasterize_noto_fallback(cp, 12))

        b16, ok16 = rasterize_unifont16(font16, cp)
        if ok16:
            bm16.append(b16)
        else:
            fb16.append(cp)
            bm16.append(rasterize_noto_fallback(cp, 16))

    out_path = os.path.join(ROOT, "cjk_font.h")
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        "",
        "// GENERATED by tools/gen_zh_tw.py -- do not edit by hand.",
        "// Subsetted 1-bit CJK bitmap fonts for Traditional Chinese UI & game data:",
        "//   - CJK_12: 12x12 (18 bytes/glyph) from Fusion Pixel 12px zh_tw (OFL-1.1)",
        "//   - CJK_16: 16x16 (32 bytes/glyph) from GNU Unifont 16px (GPL-2.0+FE / OFL-1.1)",
        f"#define CJK_COUNT {len(cps)}",
        "",
        "static const uint16_t CJK_CP[CJK_COUNT] = {",
    ]
    for i in range(0, len(cps), 12):
        lines.append("  " + ", ".join(f"0x{cp:04X}" for cp in cps[i:i + 12]) + ",")
    lines.append("};")
    lines.append("")

    lines.append("static const uint8_t CJK_12[CJK_COUNT][18] = {")
    for cp, data in zip(cps, bm12):
        hexs = ",".join(f"0x{b:02X}" for b in data)
        lines.append(f"  {{{hexs}}}, // U+{cp:04X} {chr(cp)}")
    lines.append("};")
    lines.append("")

    lines.append("static const uint8_t CJK_16[CJK_COUNT][32] = {")
    for cp, data in zip(cps, bm16):
        hexs = ",".join(f"0x{b:02X}" for b in data)
        lines.append(f"  {{{hexs}}}, // U+{cp:04X} {chr(cp)}")
    lines.append("};")
    lines.append("")

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    total_bytes = len(cps) * (2 + 18 + 32)
    print(f"wrote {out_path}: {len(cps)} unique glyphs, {total_bytes / 1024:.1f} KB flash")
    print(f"  12x12 fallbacks: {len(fb12)} {[chr(c) for c in fb12]}")
    print(f"  16x16 fallbacks: {len(fb16)} {[chr(c) for c in fb16]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv-dir", default="/tmp/pokeapi_zh")
    ap.add_argument("--bdf12", default="/tmp/fonts_test/fusion-pixel-12px-monospaced-zh_tw.bdf")
    ap.add_argument("--otf16", default="/tmp/fonts_test/unifont-16.0.04.otf")
    args = ap.parse_args()

    species, moves, types, regions = load_pokeapi_data(args.csv_dir)
    emit_i18n_zh_h(species, moves, types, regions)
    cps = collect_non_ascii_codepoints()
    emit_cjk_font_h(cps, args.bdf12, args.otf16)


if __name__ == "__main__":
    main()
