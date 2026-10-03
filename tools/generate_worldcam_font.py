#!/usr/bin/env python3
"""Create a bounded, licensed CJK subset for the application's real text inventory."""
import argparse,json,re,subprocess,sys
from pathlib import Path
from fontTools.ttLib import TTFont
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--converter',required=True);args=p.parse_args()
locations=json.loads((R/'gateway/locations.json').read_text())
source=(R/'main/main.c').read_text()
chars=set(chr(i) for i in range(32,127))
# Non-ASCII application strings, plus actual dynamic names and countries.
chars.update(c for c in source if ord(c)>=128 and c.isprintable())
for loc in locations:
 for key in ['name_zh','country_zh']:chars.update(c for c in loc[key] if c.isprintable())
inventory=''.join(sorted(chars,key=ord));(R/'assets/fonts/worldcam_chars.txt').write_text(inventory)
subprocess.run([sys.executable,str(R/'tools/fetch_worldcam_font_source.py')],check=True)
font=TTFont(R/'assets/fonts/NotoSansCJKsc-Regular.otf');cmap=font.getBestCmap();latin=TTFont(R/'assets/fonts/NotoSans-Regular.ttf').getBestCmap();extra=''.join(c for c in inventory if ord(c) not in cmap);missing=[f'U+{ord(c):04X}' for c in extra if ord(c) not in latin]
if missing:raise SystemExit('Source font missing: '+','.join(missing))
subprocess.run([args.converter,'--font',str(R/'assets/fonts/NotoSansCJKsc-Regular.otf'),'--symbols',''.join(c for c in inventory if ord(c) in cmap),'--font',str(R/'assets/fonts/NotoSans-Regular.ttf'),'--symbols',extra,'--size','16','--bpp','4','--format','lvgl','--no-compress','--lv-font-name','worldcam_font_16','--lv-include','lvgl.h','--output',str(R/'assets/fonts/worldcam_font_16.c')],check=True)
(R/'assets/fonts/worldcam_font_manifest.json').write_text(json.dumps({'font':'Noto Sans CJK SC Regular','source':'https://github.com/notofonts/noto-cjk','license':'SIL Open Font License 1.1','additional_sources':[{'font':'Noto Sans Regular','source':'https://github.com/notofonts/noto-fonts','license':'SIL Open Font License 1.1','characters':extra}],'converter':'lv_font_conv 1.5.3','size':16,'bpp':4,'compressed':False,'glyph_count':len(chars),'codepoints':[ord(c) for c in inventory]},indent=2)+'\n')
print('Font subset:',len(chars),'verified source glyphs; 16px, 4bpp, uncompressed')
