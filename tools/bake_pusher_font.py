#!/usr/bin/env python3
"""Export checked, fixed bitmap glyphs for the native renderer (not LVGL)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont
import re
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'assets/fonts/coin-pusher/NotoSansSC-game-subset.ttf'
characters = sorted(set((ROOT/'assets/fonts/coin-pusher/ui-characters.txt').read_text())-{'\n'})
used=set(re.findall(r'[\u3400-\u9fff]', ''.join((ROOT/p).read_text() for p in ('main/pusher_render.c','main/pusher_rogue.c','main/pusher_tactics.c'))))
assert used<=set(characters), 'UI characters absent from inventory: '+repr(used-set(characters))
cmap = TTFont(SOURCE).getBestCmap()
missing = [c for c in characters if ord(c) not in cmap]
assert not missing, 'Missing glyphs: '+repr(missing)
fonts = [ImageFont.truetype(str(SOURCE),size) for size in (12,20)]
rows = []
for char in characters:
    glyphs=[]
    advances=[]
    for size,font in zip((12,20),fonts):
        image=Image.new('L',(size,size));ImageDraw.Draw(image).text((0,size-1 if size==12 else 18),char,font=font,fill=255,anchor='ls')
        advance=max(1,round(font.getlength(char)))
        assert advance<=size
        glyphs.append([sum((1<<x) for x in range(size) if image.getpixel((x,y))>=96) for y in range(size)])
        advances.append(advance)
    rows.append('{%d,%d,%d,{%s},{%s}}'%(ord(char),*advances,','.join(hex(v) for v in glyphs[0]),','.join(hex(v) for v in glyphs[1])))
header='#pragma once\n#include <stdint.h>\n/* Noto Sans SC, SIL OFL 1.1; see assets/fonts/coin-pusher/OFL.txt. */\ntypedef struct {uint32_t code;uint8_t small_advance,large_advance;uint16_t small[12];uint32_t large[20];} cp_glyph_t;\nstatic const cp_glyph_t cp_glyphs[]={\n'+',\n'.join(rows)+'\n};\n#define CP_GLYPH_COUNT '+str(len(rows))+'\n'
(ROOT/'main/pusher_font.h').write_text(header)
print('Glyph coverage PASS:',len(rows),'characters, 12/20 px, 1 bpp; no fallback boxes.')
