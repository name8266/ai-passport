"""Check delivered LVGL cmaps, not just the source TTF's coverage."""
from pathlib import Path
import re
r=Path(__file__).resolve().parents[3]
s=(r/'assets/fonts/notification_hub_16.c').read_text()
arrays={name:[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+',body)]
        for name,body in re.findall(r'static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};',s,re.S)}
covered=set()
section=s.split('static const lv_font_fmt_txt_cmap_t cmaps[] =')[1].split('};')[0]
for block in re.findall(r'\{([^{}]+)\}',section):
 fields=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)',block))
 start=int(fields['range_start']);length=int(fields['range_length']);kind=fields['type']
 if kind.endswith('FORMAT0_TINY'): offsets=range(length)
 elif kind.endswith('FORMAT0_FULL'): offsets=[i for i,n in enumerate(arrays[fields['glyph_id_ofs_list']]) if i==0 or n!=0]
 elif kind.endswith('SPARSE_TINY'): offsets=arrays[fields['unicode_list']]
 else: raise AssertionError(kind)
 covered.update(start+i for i in offsets)
inventory={ord(c) for c in (r/'assets/fonts/notification_hub_glyphs.txt').read_text().rstrip('\n')}
assert inventory==covered,(len(inventory),len(covered),inventory-covered)
for file in ('app_main.c','hub_ui.c'):
 fixed={ord(c) for c in (r/'apps/notification-hub/firmware/main'/file).read_text() if ord(c)>127}
 assert fixed<=covered, sorted(fixed-covered)
assert ord('😀') not in covered and 0x10ffff not in covered
print(f'Font cmaps: {len(covered)} glyphs; all Chinese UI text covered; emoji correctly unsupported: PASS')

small_covered=covered.copy()
s=(r/'assets/fonts/notification_hub_24.c').read_text()
arrays={name:[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\d+',body)] for name,body in re.findall(r'static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};',s,re.S)}
covered=set()
section=s.split('static const lv_font_fmt_txt_cmap_t cmaps[] =')[1].split('};')[0]
for block in re.findall(r'\{([^{}]+)\}',section):
 fields=dict(re.findall(r'\.(\w+)\s*=\s*(\w+)',block))
 start=int(fields['range_start']);length=int(fields['range_length']);kind=fields['type']
 if kind.endswith('FORMAT0_TINY'): offsets=range(length)
 elif kind.endswith('FORMAT0_FULL'): offsets=[i for i,n in enumerate(arrays[fields['glyph_id_ofs_list']]) if i==0 or n!=0]
 elif kind.endswith('SPARSE_TINY'): offsets=arrays[fields['unicode_list']]
 else: raise AssertionError(kind)
 covered.update(start+i for i in offsets)
inventory={ord(c) for c in (r/'assets/fonts/notification_hub_title_glyphs.txt').read_text().rstrip('\n')}
assert inventory==covered
assert set(map(ord,'通知中心应用通知阅读通知智能摘要网页设置与连接声音与提醒存档恢复'))<=covered
print(f'Title font: {len(covered)} glyphs at 24px, all headings covered: PASS')
