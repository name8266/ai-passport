#!/usr/bin/env python3
"""Local browser fixture using the actual C model, not an ESP32 Wi-Fi/NVS test.
All data is synthetic and in memory. No package installation is needed.
"""
import argparse
import ctypes as c
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
class Asset(c.Structure):
    _fields_ = [('id',c.c_uint32),('capacity',c.c_uint16),('cycles',c.c_uint16),
                ('soc',c.c_uint8),('health',c.c_uint8),('status',c.c_uint8),('chemistry',c.c_uint8),
                ('name',c.c_char*64),('location',c.c_char*48),('notes',c.c_char*96)]
class Event(c.Structure):
    _fields_ = [('epoch',c.c_int64),('asset_id',c.c_uint32),('sequence',c.c_uint32),
                ('action',c.c_uint8),('from_status',c.c_uint8),('to_status',c.c_uint8),('reserved',c.c_uint8)]
class Database(c.Structure):
    _fields_ = [('magic',c.c_uint32),('schema',c.c_uint32),('revision',c.c_uint32),
                ('next_id',c.c_uint32),('event_sequence',c.c_uint32),('count',c.c_uint16),
                ('event_count',c.c_uint16),('assets',Asset*16),('events',Event*48),('checksum',c.c_uint32)]
class Clock(c.Structure):
    _fields_ = [('synced',c.c_bool),('timezone',c.c_int16),('epoch',c.c_int64),('monotonic',c.c_int64)]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--seed',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='battery-web-preview-') as temp:
        lib_path=Path(temp)/'model.so'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'main'),str(ROOT/'main/battery_model.c'),'-o',str(lib_path)],check=True)
        lib=c.CDLL(str(lib_path))
        lib.bat_init.argtypes=[c.POINTER(Database)]
        lib.bat_upsert.argtypes=[c.POINTER(Database),c.POINTER(Asset),c.c_uint32,c.c_int64]
        lib.bat_action.argtypes=[c.POINTER(Database),c.c_uint32,c.c_int,c.c_uint32,c.c_int64]
        lib.bat_clock_sync.argtypes=[c.POINTER(Clock),c.c_int64,c.c_int,c.c_int64]
        lib.bat_clock_sync.restype=c.c_bool
        lib.bat_clock_now.argtypes=[c.POINTER(Clock),c.c_int64];lib.bat_clock_now.restype=c.c_int64
        db,clock=Database(),Clock();lib.bat_init(c.byref(db));lock=threading.Lock()
        def now():return lib.bat_clock_now(c.byref(clock),int(time.monotonic()*1000))
        def add(data,revision):
            asset=Asset()
            for name in ['id','capacity','cycles','soc','health','status','chemistry']:
                setattr(asset,name,data.get(name,0))
            for name,limit in [('name',63),('location',47),('notes',95)]:
                value=data.get(name,'').encode('utf-8')
                if len(value)>limit:return 1
                setattr(asset,name,value)
            return lib.bat_upsert(c.byref(db),c.byref(asset),revision,now())
        if args.seed:
            for i,(name,soc,health) in enumerate([('相机备用电池',86,98),('露营灯电池',72,94),('遥控器电池组',18,90),('录音设备电池',95,99),('随身风扇电池',42,76),('手电备用电池',66,92)]):
                add(dict(name=name,soc=soc,health=health,capacity=2000+i*200,location=['抽屉 A','工具柜','旅行包'][i%3],chemistry=i%3),db.revision)
            lib.bat_action(c.byref(db),2,2,db.revision,0)
            lib.bat_action(c.byref(db),4,4,db.revision,0)
            lib.bat_action(c.byref(db),5,6,db.revision,0)
        def snapshot():
            assets=[]
            for a in db.assets[:db.count]:
                assets.append({name:getattr(a,name).decode('utf-8') if name in ['name','location','notes'] else getattr(a,name)
                               for name in ['id','capacity','cycles','soc','health','status','chemistry','name','location','notes']})
            events=[dict(id=e.asset_id,sequence=e.sequence,epoch=e.epoch,action=e.action,**{'from':e.from_status,'to':e.to_status}) for e in db.events[:db.event_count]]
            return dict(revision=db.revision,limit=16,writable=True,epoch=now(),timezone=clock.timezone,assets=assets,events=events)
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*_):pass
            def send(self,code,data,kind='application/json'):
                content=json.dumps(data,ensure_ascii=False).encode() if kind=='application/json' else data
                self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(content)))
                self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(content)
            def do_GET(self):
                if self.path=='/':self.send(200,(ROOT/'main/web/index.html').read_bytes(),'text/html; charset=utf-8')
                elif self.path=='/api/state':
                    with lock:self.send(200,snapshot())
                else:self.send(404,dict(error='不存在'))
            def do_POST(self):
                try:
                    length=int(self.headers.get('Content-Length',0))
                    if length<1 or length>1536:raise ValueError('请求大小无效')
                    if self.headers.get('X-Passport-Client')!='battery-desk':raise ValueError('无效请求')
                    data=json.loads(self.rfile.read(length))
                    with lock:
                        if self.path=='/api/time':
                            result=0 if lib.bat_clock_sync(c.byref(clock),data['epoch'],data['timezone'],int(time.monotonic()*1000)) else 1
                        elif self.path=='/api/assets':result=add(data,data['revision'])
                        elif self.path=='/api/action':result=lib.bat_action(c.byref(db),data['id'],data['action'],data['revision'],now())
                        else:raise ValueError('不存在')
                        if result:self.send(409 if result in [3,4,5] else 400,dict(error={1:'字段无效',2:'资产不存在',3:'数据已改变，请刷新后重试',4:'已达到 16 个资产上限',5:'当前状态不能执行此操作'}[result]))
                        else:self.send(200,dict(ok=True))
                except (ValueError,KeyError,OverflowError) as exc:self.send(400,dict(error=str(exc)))
        print(f'Browser fixture: http://127.0.0.1:{args.port} (synthetic data; C model; no hardware)',flush=True)
        ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
if __name__=='__main__':main()
