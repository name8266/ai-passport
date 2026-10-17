#!/usr/bin/env python3
"""Original synthesized arcade effects, mono 16-bit/16 kHz PCM in Flash."""
from pathlib import Path
import math, random, struct, wave
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'assets/music/coin-pusher'
OUT.mkdir(parents=True,exist_ok=True)
SR=16000
def export(name,samples):
    data=struct.pack('<%dh'%len(samples),*samples)
    with wave.open(str(OUT/(name+'.wav')),'wb') as f:
        f.setparams((1,2,SR,0,'NONE','not compressed'));f.writeframes(data)
    values=[str(v) for v in samples]
    return 'static const int16_t cp_sound_%s[%d]={\n%s\n};\n'%(name,len(samples),',\n'.join(','.join(values[i:i+24]) for i in range(0,len(values),24)))
def tune(notes,step):
    result=[]
    for note in notes:
        frequency=440*2**((note-69)/12)
        for i in range(round(step*SR)):
            t=i/SR;env=min(1,t/.004)*math.exp(-t/(step*.45))
            v=(math.sin(2*math.pi*frequency*t)+.23*math.sin(4*math.pi*frequency*t))*.25*env
            result.append(round(v*32767))
    return result
rng=random.Random(8266);coin=[]
for i in range(round(.16*SR)):
    t=i/SR;env=min(1,t/.001)*math.exp(-t*38)
    v=(.28*math.sin(2*math.pi*2850*t)+.17*math.sin(2*math.pi*4310*t)+.06*(rng.random()*2-1))*env
    coin.append(round(v*32767))
header='#pragma once\n#include <stdint.h>\n/* Original generated PCM. Rebuild: python3 tools/bake_pusher_audio.py */\n'
header+=export('coin',coin)
header+=export('prize',tune([76,79,83,88],.13))
header+=export('jackpot',tune([72,76,79,84,76,79,84,88,79,84,88,91],.16))
header+=export('event',tune([79,84,88,91,88,96],.10))
spin=[]
for step in range(24):
    tone=84+(step%3)*3
    spin+=tune([tone],.06)
header+=export('spin',spin)
hole=[]
for i in range(SR):
    t=i/SR;frequency=70+1200*(1-t)**3
    value=(math.sin(2*math.pi*(70*t+300*(1-(1-t)**4)))+.18*(rng.random()*2-1))*math.sin(math.pi*t)*.24
    hole.append(round(value*32767))
header+=export('hole',hole)
(ROOT/'main/pusher_sounds.h').write_text(header)
print('Exported coin, prize, jackpot, reels, event and siphon effects: 16 kHz, 16-bit mono.')
