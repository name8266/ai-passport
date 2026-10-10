from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
import subprocess,hashlib,json
import argparse,tempfile
parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True);parser.add_argument('--converter',required=True)
args=parser.parse_args()
expected='a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da'
if hashlib.sha256(args.source.read_bytes()).hexdigest()!=expected: raise SystemExit('Source font SHA-256 mismatch')
r=Path(__file__).resolve().parents[1]; a=r/'assets/fonts'; m=r/'apps/notification-hub/firmware/main'
work=tempfile.TemporaryDirectory(prefix='passport-font-');regular=Path(work.name)/'Regular.ttf'
font=TTFont(args.source); instantiateVariableFont(font,{'wght':400},inplace=True).save(str(regular))
chars=set(chr(i) for i in range(32,127))
for hi in range(0xa1,0xf8):
 for lo in range(0xa1,0xff):
  try: chars.add(bytes([hi,lo]).decode('gb2312'))
  except UnicodeDecodeError: pass
fixed=set(c for c in (m/'app_main.c').read_text() if ord(c)>127)
chars |= fixed
cmap=font.getBestCmap(); missing=[c for c in chars if ord(c) not in cmap]; assert not missing,missing
symbols=''.join(sorted(chars,key=ord));(a/'notification_hub_glyphs.txt').write_text(symbols+'\n')
subprocess.run([args.converter,'--font',str(regular),'--size','16','--bpp','2','--format','lvgl','--no-compress','--no-kerning','--symbols',symbols,'--lv-font-name','notification_hub_16','--lv-include','lvgl.h','-o',str(a/'notification_hub_16.c')],check=True)
(a/'notification_hub_font.json').write_text(json.dumps({'font':'Noto Sans SC','weight':400,'size':16,'bpp':2,'converter':'lv_font_conv 1.5.3','source':'https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf','source_sha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),'glyph_count':len(chars)},indent=2)+'\n')
(m/'hub_ui_fixed_glyphs.inc').write_text(','.join(str(ord(c)) for c in sorted(fixed))+'\n')
print('Generated',len(chars),'glyphs; fixed Chinese UI:',len(fixed))
titles='通知中心应用通知阅读通知智能摘要网页设置与连接声音与提醒存档恢复'
title_symbols=''.join(sorted(set(titles+'0123456789 '),key=ord))
(a/'notification_hub_title_glyphs.txt').write_text(title_symbols+'\n')
subprocess.run([args.converter,'--font',str(regular),'--size','24','--bpp','2','--format','lvgl','--no-compress','--no-kerning','--symbols',title_symbols,'--lv-font-name','notification_hub_24','--lv-include','lvgl.h','-o',str(a/'notification_hub_24.c')],check=True)
meta=json.loads((a/'notification_hub_font.json').read_text())
meta['title_font']={'name':'notification_hub_24','size':24,'bpp':2,'glyph_count':len(title_symbols)}
(a/'notification_hub_font.json').write_text(json.dumps(meta,indent=2)+'\n')
work.cleanup()
