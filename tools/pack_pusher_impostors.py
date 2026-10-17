#!/usr/bin/env python3
"""Pack exact-renderer coin views into indexed-color, per-pixel-depth spans."""
from pathlib import Path
import math,struct,sys
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
raw=Path(sys.argv[1]).read_bytes()
POSES=97*16
assert len(raw)==POSES*1600
def rgb(c):return ((c>>11)*255//31,((c>>5)&63)*255//63,(c&31)*255//31)
colors=[];views=[]
for i in range(POSES):
    color=struct.unpack_from('<400H',raw,i*1600)
    depth=struct.unpack_from('<400h',raw,i*1600+800)
    views.append((color,depth))
    colors.extend(rgb(c) for c,d in zip(color,depth) if d!=32767)
image=Image.new('RGB',(len(colors),1));image.putdata(colors)
quant=image.quantize(colors=32,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
pal=quant.getpalette()[:96]
palette=[tuple(pal[k:k+3]) for k in range(0,96,3)]
native=[(r>>3)<<11|(g>>2)<<5|(b>>3) for r,g,b in palette]
lookup={}
def index(c):
    if c not in lookup:
        source=rgb(c);lookup[c]=min(range(32),key=lambda k:sum((a-b)**2 for a,b in zip(source,palette[k])))
    return lookup[c]
words=[];offsets=[];bounds=[]
for color,depth in views:
    offsets.append(len(words));spans=[];pixels=[];xs=[];ys=[]
    for y in range(20):
        visible=[x for x in range(20) if depth[y*20+x]!=32767]
        if not visible:spans.append(0);continue
        left,right=min(visible),max(visible);xs+=visible;ys.append(y)
        spans.append(left|((right-left+1)<<5))
        for x in range(left,right+1):
            at=y*20+x;d=depth[at]
            if d==32767:pixels.append(0);continue
            delta=round(d/2);assert -128<=delta<=127
            pixels.append(0x8000|((delta+128)<<5)|index(color[at]))
    assert xs and ys
    bounds.append([min(xs),min(ys),max(xs),max(ys)])
    words+=spans+pixels
offsets.append(len(words))
binary=struct.pack('<%dH'%len(words),*words)
(ROOT/'main/pusher_coin_impostors.bin').write_bytes(binary)
header='#pragma once\n#include <stdint.h>\n/* Rebuild: tools/bake_pusher_impostors.sh. Real 3D geometry, indexed lighting, 2-unit depth. */\n#define CP_ATLAS_NORMALS 97\n#define CP_ATLAS_POSES 1552\n#define CP_ATLAS_BYTES '+str(len(binary))+'\n'
def array(name,data,typ,columns=16):
    return 'static const %s %s[%d]={\n%s\n};\n'%(typ,name,len(data),',\n'.join(','.join(str(x) for x in data[i:i+columns]) for i in range(0,len(data),columns)))
header+=array('cp_atlas_offsets',offsets,'uint32_t')
header+=array('cp_atlas_palette',native,'uint16_t')
header+='static const uint8_t cp_atlas_bounds[1552][4]={\n'+',\n'.join('{'+','.join(map(str,b))+'}' for b in bounds)+'\n};\n'
header+='typedef struct {int16_t n[3],u[3],v[3];} cp_atlas_basis_t;\nstatic const cp_atlas_basis_t cp_atlas_basis[97]={\n'
normals=[]
for tilt in range(7):
    for az in range(16 if tilt else 1):
        theta=tilt*math.pi/12;phi=az*2*math.pi/16
        normal=(math.sin(theta)*math.cos(phi),math.sin(theta)*math.sin(phi),math.cos(theta))
        u=(math.cos(theta)*math.cos(phi),math.cos(theta)*math.sin(phi),-math.sin(theta))
        v=(-math.sin(phi),math.cos(phi),0)
        normals.append('{'+','.join('{'+','.join(str(round(x*4096)) for x in axis)+'}' for axis in (normal,u,v))+'}')
header+=',\n'.join(normals)+'\n};\n'
header+=array('cp_atlas_cos',[round(math.cos(k*2*math.pi/16)*4096) for k in range(16)],'int16_t')
header+=array('cp_atlas_sin',[round(math.sin(k*2*math.pi/16)*4096) for k in range(16)],'int16_t')
(ROOT/'main/pusher_coin_impostors.h').write_text(header)
print('Coin atlas:',POSES,'poses,',len(binary),'Flash bytes; no runtime framebuffer/allocation.')
