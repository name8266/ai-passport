#!/usr/bin/env python3
"""Bake original, object-space coin relief into tiny metal intensity maps."""
from pathlib import Path
from math import hypot

DIGITS = {'1': [4,12,4,4,4,4,14], '3': [14,1,1,6,1,1,14]}

def material(digit):
    mask = set()
    for y, row in enumerate(DIGITS[digit]):
        for x in range(4):
            if row & (1 << (3-x)):
                for dy in range(2):
                    for dx in range(2):
                        mask.add((12+x*2+dx,9+y*2+dy))
    result=[]
    for y in range(32):
        for x in range(32):
            nx,ny=(x-15.5)/15.5,(y-15.5)/15.5
            r=hypot(nx,ny); lit=(-nx-ny)*.7071
            level=8+lit*1.4
            if r>.94: level=3+max(0,lit)*4
            elif r>.84: level=10+lit*4
            elif .70<r<.77: level=4+lit*2
            elif .65<r<=.70: level=11+lit*2
            if (x,y) in mask: level=5
            elif (x+1,y+1) in mask: level=14
            elif (x-1,y-1) in mask: level=3
            result.append(max(0,min(15,round(level))))
    return result

out=['/* Generated original coin relief; run tools/bake_pusher_coins.py. */', '#pragma once', '#include <stdint.h>']
for name,digit in [('copper','1'),('gold','3')]:
    values=material(digit)
    out.append('static const uint8_t cp_metal_'+name+'[1024]={')
    out.extend('    '+','.join(map(str,values[i:i+32]))+',' for i in range(0,1024,32))
    out.append('};')
Path(__file__).resolve().parents[1].joinpath('main/pusher_coin_texture.h').write_text('\n'.join(out)+'\n')
