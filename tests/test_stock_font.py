import json,re,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
class StockFontTests(unittest.TestCase):
 def test_coverage(self):
  points=set(json.loads((R/'assets/fonts/stock_font_manifest.json').read_text())['codepoints'])
  text=(R/'assets/fonts/stock_font_16.c').read_text()
  arrays={name:[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b',body)] for name,body in re.findall(r'static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};',text,re.S)}
  offsets={name:[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b',body)] for name,body in re.findall(r'static const uint8_t (glyph_id_ofs_list_\d+)\[\] = \{(.*?)\};',text,re.S)}
  cmaps=re.search(r'static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{(.*?)\n\};',text,re.S)[1];actual=set()
  for block in re.findall(r'\{(.*?)\}',cmaps,re.S):
   start=int(re.search(r'\.range_start = (\d+)',block)[1]);length=int(re.search(r'\.range_length = (\d+)',block)[1]);name=re.search(r'\.unicode_list = (\w+)',block)[1]
   ofs=re.search(r'\.glyph_id_ofs_list = (\w+)',block)[1]
   if name!='NULL':actual.update(start+v for v in arrays[name])
   elif ofs!='NULL':actual.update(start+i for i,v in enumerate(offsets[ofs]) if i==0 or v!=0)
   else:actual.update(range(start,start+length))
  self.assertEqual(actual,points)
  for file in ['stock_app.c','stock_network.c']:
   for c in (R/'main'/file).read_text():
    if c.isprintable() and ord(c)>=128:self.assertIn(ord(c),points)
  for a in range(0x81,0xff):
   for b in range(0x40,0xff):
    try:c=bytes([a,b]).decode('gbk')
    except UnicodeDecodeError:continue
    if 0x4e00<=ord(c)<=0x9fff:self.assertIn(ord(c),points)
 def test_bitmap_bounds(self):
  t=(R/'assets/fonts/stock_font_16.c').read_text();raw=re.search(r'glyph_bitmap\[\].*?=\s*\{(.*?)\};',t,re.S)[1];raw=re.sub(r'/\*.*?\*/','',raw,flags=re.S);size=len(re.findall(r'0x[0-9a-fA-F]+',raw))
  for index,w,h in re.findall(r'\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+)',t):self.assertLessEqual(int(index)+(int(w)*int(h)+3)//4,size)
if __name__=='__main__':unittest.main()
