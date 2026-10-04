#!/usr/bin/env python3
"""Render a reproducible map asset from maintained geography and camera locations."""
from pathlib import Path
import json,struct
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
W,H=216,128
geo=json.loads((ROOT/'assets/maps/world.geo.json').read_text())
locations=json.loads((ROOT/'gateway/locations.json').read_text())
def project(point,w=W,h=H):return ((point[0]+180)/360*(w-1),(90-point[1])/180*(h-1))
image=Image.new('RGB',(W,H),'#071820');draw=ImageDraw.Draw(image)
paths=[]
for feature in geo['features']:
 g=feature['geometry'];polygons=[g['coordinates']] if g['type']=='Polygon' else g['coordinates']
 for polygon in polygons:
  ring=polygon[0];pts=[project(p) for p in ring];draw.polygon(pts,fill='#244552')
  svg=[project(p,1000,500) for p in ring];paths.append('M'+'L'.join(f'{x:.1f},{y:.1f}' for x,y in svg)+'Z')
for loc in locations:
 x,y=project([loc['lon'],loc['lat']]);draw.point((round(x),round(y)), fill='#4de4bd' if loc.get('camera_id') else '#c59d55')
pixels=bytearray()
for r,g,b in image.get_flattened_data():pixels.extend(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
rows=[', '.join(f'0x{b:02x}' for b in pixels[i:i+24]) for i in range(0,len(pixels),24)]
text='#include "lvgl.h"\nstatic const uint8_t map_pixels[] LV_ATTRIBUTE_MEM_ALIGN = {\n'+',\n'.join(rows)+'\n};\nconst lv_image_dsc_t worldcam_map = {.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=216,.h=128,.stride=432},.data_size=sizeof(map_pixels),.data=map_pixels};\n'
(ROOT/'assets/maps/world_map.c').write_text(text)
svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 500"><rect width="1000" height="500" fill="#071820"/><path fill="#244552" d="'+' '.join(paths)+'"/></svg>'
(ROOT/'gateway/world-map.svg').write_text(svg)
image.save(ROOT/'build/worldcam-map-preview.png')
print('World map:',len(pixels),'Flash bytes; markers:',len(locations))
