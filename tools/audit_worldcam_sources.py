#!/usr/bin/env python3
"""Verify maintained public image URLs and record explicit source failures."""
from concurrent.futures import ThreadPoolExecutor
from io import BytesIO
import importlib.util,json,time,urllib.request
from pathlib import Path
from PIL import Image,UnidentifiedImageError
R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('worldcam_gateway',R/'gateway/server.py');gateway=importlib.util.module_from_spec(spec);spec.loader.exec_module(gateway)

def check(c):
 result={'id':c['id'],'checked_at':int(time.time())}
 try:
  url=c['snapshot']
  if c.get('resolver')=='usap':
   with urllib.request.urlopen(url,timeout=12) as r:url=gateway.resolve_usap(r.read(4096).decode())
  with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'PassportWorldCamSourceCheck/2.0'}),timeout=12) as r:
   data=r.read(gateway.MAX_BYTES+1);result['last_modified']=r.headers.get('Last-Modified')
  if len(data)>gateway.MAX_BYTES:raise ValueError('Source too large')
  with Image.open(BytesIO(data)) as image:
   result['width'],result['height']=image.size
   result['format']=image.format
   result['progressive']=bool(image.info.get('progressive') or image.info.get('progression'))
   result['device_compatible']=image.format=='JPEG' and not result['progressive']
   image.load()
  result['status']='snapshot_verified';result['method']='Public URL fetch, image decoding, and ESP32 baseline-JPEG compatibility check; capture freshness not established'
 except Exception as e:result['status']='unavailable';result['error']='UnidentifiedImageError: source did not decode as an image' if isinstance(e,UnidentifiedImageError) else type(e).__name__+': '+str(e)[:180]
 return result
if __name__=='__main__':
 cameras=json.loads((R/'gateway/cameras.json').read_text());results=[]
 with ThreadPoolExecutor(max_workers=3) as pool:
  for i,result in enumerate(pool.map(check,cameras)):
   results.append(result)
   if (i+1)%50==0:print('Checked',i+1,'/',len(cameras),'images decoded:',sum(x['status']=='snapshot_verified' for x in results),flush=True)
 by_id={r['id']:r for r in results}
 for c in cameras:c['status']=by_id[c['id']]['status'];c['verified_at']=by_id[c['id']]['checked_at']
 (R/'gateway/cameras.json').write_text(json.dumps(cameras,ensure_ascii=False,indent=2)+'\n')
 (R/'gateway/source-audit.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
 coverage=json.loads((R/'gateway/capital-coverage.json').read_text())
 for c in coverage:
  c['verified_camera_ids']=[i for i in c['camera_ids'] if by_id[i]['status']=='snapshot_verified']
  c['status']='snapshot_verified' if c['verified_camera_ids'] else 'listed_but_unavailable' if c['camera_ids'] else 'no_verified_source'
 (R/'gateway/capital-coverage.json').write_text(json.dumps(coverage,ensure_ascii=False,indent=2)+'\n')
 print('Completed',len(results),'verified',sum(x['status']=='snapshot_verified' for x in results),'capitals with decoded images',sum(bool(c['verified_camera_ids']) for c in coverage),flush=True)
