#!/usr/bin/env python3
"""Label actual shared-renderer captures explicitly as desktop simulations."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parent.parent
folder = root/'build/brake-hero'
names = ['home', 'countdown', 'driving', 'warning', 'brake-fail', 'collision', 'result', 'leaderboard', 'help']
sheet = Image.new('RGB', (240*3, 344*3), '#dcdcdc')
for i, name in enumerate(names):
    im = Image.open(folder/(name+'.ppm'))
    labeled = Image.new('RGB', (240, 344), '#dcdcdc')
    labeled.paste(im, (0, 24))
    ImageDraw.Draw(labeled).text((7, 6), 'DESKTOP SIMULATOR: '+name, fill='black')
    labeled.save(folder/(name+'.png'))
    sheet.paste(labeled, (i % 3*240, i//3*344))
sheet.save(folder/'simulator-screens.png')
motion = []
for path in sorted(folder.glob('motion-*.ppm')):
    labeled = Image.new('RGB', (240, 344), '#dcdcdc')
    labeled.paste(Image.open(path), (0, 24))
    ImageDraw.Draw(labeled).text((6, 6), 'DESKTOP SIMULATOR - 15 FPS', fill='black')
    motion.append(labeled)
if motion:
    motion[0].save(folder/'driving-simulator.gif', save_all=True,
                   append_images=motion[1:], duration=67, loop=0, optimize=True)
print('Saved labeled simulator captures. These are not device photographs.')
