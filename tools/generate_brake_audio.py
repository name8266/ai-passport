#!/usr/bin/env python3
"""Original robot announcer + procedural FX; no performer recording/voice clone.

Build eSpeak NG 1.52.0, set ESPEAK_BIN and ESPEAK_DATA, then run with numpy.
WAV sources: mono PCM16/16k. Flash: signed PCM8, expanded in 20 ms chunks.
"""
import json
import os
import subprocess
import tempfile
import wave
from pathlib import Path
import numpy as np

root = Path(__file__).resolve().parent.parent
dest = root/'assets/music/brake-hero'
voice = [('start','准备好，开始挑战！这次，遥遥领先！'), ('100','零百完成！'),
         ('tent','空气减速系统，启动！'), ('warn','注意力度！再踩就危险了！'),
         ('break','别踩断！哎！真断了！制动系统已经失效！'),
         ('near','前方距离不多了！'), ('win','漂亮！极限刹停！这次真是遥遥领先！'),
         ('lose','没刹住！挑战失败！')]
espeak = os.environ.get('ESPEAK_BIN', str(root/'.tooling/espeak-build/src/espeak-ng'))
data = os.environ.get('ESPEAK_DATA', str(root/'.tooling/espeak-build'))
clips = []


def save(name, a):
    a = np.clip(a, -.9, .9)
    pcm = (a*32767).astype('<i2')
    with wave.open(str(dest/(name+'.wav')), 'wb') as out:
        out.setnchannels(1); out.setsampwidth(2); out.setframerate(16000); out.writeframes(pcm.tobytes())
    clips.append((name, (pcm.astype(np.int32)//256).astype(np.int8).view(np.uint8)))


with tempfile.TemporaryDirectory() as td:
    for name, words in voice:
        raw = Path(td)/'raw.wav'
        subprocess.run([espeak, '--path='+data, '-v', 'cmn+m3', '-p', '28', '-s', '205', '-a', '170',
                        '-w', str(raw), words], check=True, stdout=subprocess.DEVNULL)
        with wave.open(str(raw)) as f:
            hz = f.getframerate(); a = np.frombuffer(f.readframes(f.getnframes()), dtype='<i2').astype(float)/32768
        a = np.interp(np.arange(round(len(a)*16000/hz))*hz/16000, np.arange(len(a)), a)
        t = np.arange(len(a))/16000
        # Low robotic ring modulation, light saturation, short synthetic echo.
        a = np.tanh(a*2.1)*(.83+.17*np.sin(2*np.pi*43*t))
        delay = 1040; echo = np.zeros(len(a)+delay)
        echo[:len(a)] += a*.75; echo[delay:] += a*.16
        if name == 'break':
            alarm = np.sin(2*np.pi*(680+220*np.sin(2*np.pi*9*t))*t)*.16
            echo[:len(a)] += alarm
        save(name, echo)

rng = np.random.default_rng(42524831)
for i, name in enumerate(['wall', 'cliff', 'truck', 'construction']):
    t = np.arange(14400)/16000
    noise = rng.normal(0, .2, len(t))
    if name == 'cliff': a = np.sin(2*np.pi*(320*t-130*t*t))*.4*np.exp(-t*2)+noise*.3
    elif name == 'truck': a = np.sin(2*np.pi*95*t)*np.exp(-t*6)*.6+noise*np.exp(-t*9)
    elif name == 'construction': a = noise*np.exp(-t*5)+np.sin(2*np.pi*750*t)*np.exp(-t*14)*.2
    else: a = noise*np.exp(-t*10)+np.sin(2*np.pi*52*t)*np.exp(-t*8)*.65
    save(name, a)

out = ['/* Generated PCM8. Original eSpeak robot processing and synthesized FX. */', '#include "brake_sound.h"']
for name, a in clips:
    out.append('static const uint8_t pcm_'+name+'[] = {')
    for i in range(0, len(a), 48): out.append(','.join(map(str, a[i:i+48]))+',')
    out.append('};')
out.append('const bh_clip_t bh_clips[SND_COUNT] = {')
for name, a in clips: out.append('{pcm_'+name+',sizeof(pcm_'+name+')},')
out.append('};')
(dest/'brake_audio.c').write_text('\n'.join(out)+'\n')
(dest/'lines.json').write_text(json.dumps({'engine': 'eSpeak NG 1.52.0', 'voice':'cmn+m3, pitch 28, rate 205',
    'source_format':'16000 Hz mono PCM16 WAV', 'flash_format':'signed PCM8',
    'voice_clone':False, 'lines':dict(voice), 'samples':{n:len(a) for n,a in clips}},ensure_ascii=False,indent=2)+'\n')
print('Generated', len(clips), 'real WAV/PCM clips;', sum(len(a) for _,a in clips), 'Flash bytes')
