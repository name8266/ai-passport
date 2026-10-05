#!/usr/bin/env python3
"""Generate GBK conversion and the font for supported Tencent stock names."""
import argparse,json,subprocess,re
from pathlib import Path
from fontTools.ttLib import TTFont
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--converter',required=True);a=p.parse_args()
font=R/'assets/fonts/NotoSansCJKsc-Regular.otf';cmap=TTFont(font).getBestCmap();latin=R/'assets/fonts/NotoSans-Regular.ttf';latinmap=TTFont(latin).getBestCmap()
table=[];chars=set(chr(i) for i in range(32,127))
for lead in range(0x81,0xff):
 for trail in range(0x40,0xff):
  try:u=ord(bytes([lead,trail]).decode('gbk'))
  except (UnicodeDecodeError,TypeError):u=0
  if u and u not in cmap and u not in latinmap:
   if 0x4e00 <= u <= 0x9fff:raise SystemExit(f'Font missing Chinese GBK U+{u:04X}')
   u=ord('?') # Unsupported non-name symbols are explicit replacement characters.
  table.append(u)
  if u:chars.add(chr(u))
for file in ['main/stock_app.c','main/stock_network.c']:
 chars.update(c for c in (R/file).read_text() if ord(c)>=128 and c.isprintable())
missing=[c for c in chars if ord(c) not in cmap and ord(c) not in latinmap]
if missing:raise SystemExit('Missing '+repr(missing))
inventory=''.join(sorted(chars,key=ord));(R/'assets/fonts/stock_chars.txt').write_text(inventory)
text='#include <stdint.h>\n/* Python GBK codec, indexed by (lead-0x81)*191+trail-0x40. */\nconst uint16_t stock_gbk_unicode[126 * 191] = {\n'
text+='\n'.join('    '+','.join(str(v) for v in table[i:i+24])+',' for i in range(0,len(table),24))+'\n};\n';(R/'main/stock_gbk.c').write_text(text)
subprocess.run([a.converter,'--font',str(font),'--symbols',''.join(c for c in inventory if ord(c) in cmap),'--font',str(latin),'--symbols',''.join(c for c in inventory if ord(c) not in cmap),'--size','16','--bpp','2','--format','lvgl','--no-compress','--lv-font-name','stock_font_16','--lv-include','lvgl.h','--output',str(R/'assets/fonts/stock_font_16.c')],check=True)
(R/'assets/fonts/stock_font_manifest.json').write_text(json.dumps({'font':'Noto Sans CJK SC / Noto Sans Regular','license':'SIL OFL 1.1','converter':'lv_font_conv 1.5.3','size':16,'bpp':2,'compressed':False,'codepoints':[ord(c) for c in inventory]},indent=2)+'\n')
print('Stock font:',len(chars),'verified glyphs')

# Compact UI font keeps metadata readable without shrinking the stock-name font.
ui=set(chr(i) for i in range(32,127))
for file in ['main/stock_app.c','main/stock_network.c']:
 ui.update(c for c in (R/file).read_text() if ord(c)>=128 and c.isprintable())
ui_inventory=''.join(sorted(ui,key=ord))
subprocess.run([a.converter,'--font',str(font),'--symbols',ui_inventory,'--size','12','--bpp','2','--format','lvgl','--no-compress','--lv-font-name','stock_font_12','--lv-include','lvgl.h','--output',str(R/'assets/fonts/stock_font_12.c')],check=True)
(R/'assets/fonts/stock_ui_manifest.json').write_text(json.dumps({'font':'Noto Sans CJK SC','license':'SIL OFL 1.1','converter':'lv_font_conv 1.5.3','size':12,'bpp':2,'compressed':False,'codepoints':[ord(c) for c in ui_inventory]},indent=2)+'\n')
