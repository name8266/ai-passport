"""Verify the shipped font cmap and glyph bitmap bounds against actual UI text."""
import json,re,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
class FontTests(unittest.TestCase):
 def test_shipped_glyph_coverage(self):
  text=(R/'assets/fonts/worldcam_font_16.c').read_text()
  arrays={name:[int(v,0) for v in re.findall(r'0x[0-9a-fA-F]+|\b\d+\b',body)] for name,body in re.findall(r'static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};',text,re.S)}
  cmaps=re.search(r'static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{(.*?)\n\};',text,re.S)[1]
  points=set()
  for block in re.findall(r'\{(.*?)\}',cmaps,re.S):
   start=int(re.search(r'\.range_start = (\d+)',block)[1]);length=int(re.search(r'\.range_length = (\d+)',block)[1]);name=re.search(r'\.unicode_list = (\w+)',block)[1]
   if name=='NULL':points.update(range(start,start+length))
   else:
    offsets=arrays[name];self.assertEqual(len(offsets),int(re.search(r'\.list_length = (\d+)',block)[1]));points.update(start+v for v in offsets)
  required=set(json.loads((R/'assets/fonts/worldcam_font_manifest.json').read_text())['codepoints'])
  self.assertEqual(points,required)
  for loc in json.loads((R/'gateway/locations.json').read_text()):
   for key in ['name_zh','country_zh']:self.assertTrue(all(ord(c) in points for c in loc[key] if c.isprintable()),loc[key])
  self.assertTrue(all(ord(c) in points for c in (R/'main/main.c').read_text() if ord(c)>=128 and c.isprintable()))
  self.assertNotIn(0x9f98,points) # Known absent glyph: negative coverage check.
  self.assertIn('worldcam_font_16', (R/'main/main.c').read_text())
 def test_bitmap_bounds(self):
  text=(R/'assets/fonts/worldcam_font_16.c').read_text()
  bitmap=re.search(r'glyph_bitmap\[\].*?=\s*\{(.*?)\};',text,re.S)[1]
  bitmap=re.sub(r'/\*.*?\*/','',bitmap,flags=re.S)
  size=len(re.findall(r'0x[0-9a-fA-F]+',bitmap))
  glyphs=re.findall(r'\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+)',text)
  self.assertGreater(len(glyphs),500)
  for index,w,h in glyphs:self.assertLessEqual(int(index)+(int(w)*int(h)+1)//2,size)
if __name__=='__main__':unittest.main()
