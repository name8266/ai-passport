#!/usr/bin/env python3
"""Local browser fixture using the actual C model, not an ESP32 Wi-Fi/NVS test.
All data is synthetic; the production file archive runs on a temporary host filesystem. No package installation is needed.
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
from urllib.parse import urlparse, parse_qs

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

class CareAsset(c.Structure):
    _fields_=[('asset',Asset),('updated_at',c.c_int64),('remind_at',c.c_int64),('charge_until',c.c_int64),('snooze_until',c.c_int64),('reward_day',c.c_int32),('reward_mask',c.c_uint32),('charge_minutes',c.c_uint16),('reserved',c.c_uint16),('checksum',c.c_uint32)]
class Pet(c.Structure):
    _fields_=[('xp',c.c_uint32),('streak',c.c_uint32),('last_day',c.c_int32),('reward_day',c.c_int32),('daily_xp',c.c_uint16),('volume',c.c_uint8),('quiet_start',c.c_uint8),('quiet_end',c.c_uint8),('muted',c.c_uint8),('snooze_until',c.c_int64)]
class Reminder(c.Structure):
    _fields_=[('id',c.c_uint32),('due_at',c.c_int64),('reason',c.c_uint8)]
class Info(c.Structure):
    _fields_=[('total',c.c_uint32),('counts',c.c_uint32*5),('due_count',c.c_uint32),('attention',c.c_uint32),('revision',c.c_uint32),('next_cursor',c.c_uint32),('page_after',c.c_uint32),('pet',Pet),('reminders',Reminder*8),('reminder_count',c.c_uint8),('storage_used',c.c_uint64),('storage_total',c.c_uint64)]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--seed',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='battery-web-preview-') as temp:
        lib_path=Path(temp)/'model.so'
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror',
                        '-I'+str(ROOT/'main'),*[str(ROOT/'main'/name) for name in ['battery_model.c','battery_care.c','battery_archive.c']],'-o',str(lib_path)],check=True)
        lib=c.CDLL(str(lib_path))
        lib.bat_init.argtypes=[c.POINTER(Database)]
        lib.bat_upsert.argtypes=[c.POINTER(Database),c.POINTER(Asset),c.c_uint32,c.c_int64]
        lib.bat_action.argtypes=[c.POINTER(Database),c.c_uint32,c.c_int,c.c_uint32,c.c_int64]
        lib.bat_clock_sync.argtypes=[c.POINTER(Clock),c.c_int64,c.c_int,c.c_int64]
        lib.bat_clock_sync.restype=c.c_bool
        lib.bat_clock_now.argtypes=[c.POINTER(Clock),c.c_int64];lib.bat_clock_now.restype=c.c_int64
        lib.battery_archive_open.argtypes=[c.c_char_p];lib.battery_archive_open.restype=c.c_bool
        lib.battery_archive_page.argtypes=[c.POINTER(Database),c.POINTER(Info),c.c_uint32,c.c_char_p,c.c_int,c.c_int64];lib.battery_archive_page.restype=c.c_bool
        lib.battery_archive_get.argtypes=[c.c_uint32,c.POINTER(CareAsset)];lib.battery_archive_get.restype=c.c_bool
        lib.battery_archive_upsert.argtypes=[c.POINTER(Asset),c.c_int64,c.c_uint,c.c_uint32,c.c_int64,c.c_int]
        lib.battery_archive_action.argtypes=[c.c_uint32,c.c_int,c.c_uint32,c.c_int64,c.c_int]
        lib.battery_archive_snooze.argtypes=[c.c_int64];lib.battery_archive_snooze.restype=c.c_bool
        lib.battery_archive_settings.argtypes=[c.c_uint,c.c_uint,c.c_uint,c.c_bool];lib.battery_archive_settings.restype=c.c_bool
        lib.battery_archive_history.argtypes=[c.POINTER(Event),c.c_uint,c.c_uint32,c.c_uint32]
        lib.care_due.argtypes=[c.POINTER(CareAsset),c.c_int64,c.POINTER(c.c_int64)]
        archive=Path(temp)/'assets';archive.mkdir();assert lib.battery_archive_open(str(archive).encode())
        db,clock,info=Database(),Clock(),Info();lock=threading.Lock()
        def update(after=0,q='',status=-1):
            assert lib.battery_archive_page(c.byref(db),c.byref(info),after,q.encode(),status,now())
        def action(id,act,rev):
            return lib.battery_archive_action(id,act,rev,now(),clock.timezone)
        def now():return lib.bat_clock_now(c.byref(clock),int(time.monotonic()*1000))
        def add(data,revision):
            asset=Asset()
            for name in ['id','capacity','cycles','soc','health','status','chemistry']:
                setattr(asset,name,data.get(name,0))
            for name,limit in [('name',63),('location',47),('notes',95)]:
                value=data.get(name,'').encode('utf-8')
                if len(value)>limit:return 1
                setattr(asset,name,value)
            return lib.battery_archive_upsert(c.byref(asset),data.get('remind_at',0),data.get('charge_minutes',120),revision,now(),clock.timezone)
        update()
        if args.seed:
            for i,(name,soc,health) in enumerate([('相机备用电池',86,98),('露营灯电池',72,94),('遥控器电池组',18,90),('录音设备电池',95,99),('随身风扇电池',42,76),('手电备用电池',66,92)]):
                add(dict(name=name,soc=soc,health=health,capacity=2000+i*200,location=['抽屉 A','工具柜','旅行包'][i%3],chemistry=i%3),db.revision);update()
            action(2,2,db.revision);update()
            action(4,4,db.revision);update()
            action(5,6,db.revision);update()
        def asset_json(a):
            data={name:getattr(a,name).decode('utf-8') if name in ['name','location','notes'] else getattr(a,name)
                  for name in ['id','capacity','cycles','soc','health','status','chemistry','name','location','notes']}
            care=CareAsset();assert lib.battery_archive_get(a.id,c.byref(care));due=c.c_int64()
            data.update({k:getattr(care,k) for k in ['updated_at','remind_at','charge_until','charge_minutes']})
            data['charge_minutes']=data['charge_minutes'] or 120
            data['reason']=lib.care_due(c.byref(care),now(),c.byref(due));data['due_at']=due.value
            return data
        def event_json(e):return dict(id=e.asset_id,sequence=e.sequence,epoch=e.epoch,action=e.action,**{'from':e.from_status,'to':e.to_status})
        def snapshot(after=0,q='',status=-1):
            update(after,q,status)
            pet={k:getattr(info.pet,k) for k in ['xp','streak','daily_xp','volume','muted','quiet_start','quiet_end','snooze_until']};pet['stage']=min(3,pet['xp']//60)
            if now():
                day=(now()+clock.timezone*60)//86400
                if info.pet.reward_day!=day:pet['daily_xp']=0
                if info.pet.last_day<day-1:pet['streak']=0
            return dict(revision=db.revision,total=info.total,counts=list(info.counts),attention=info.attention,due_count=info.due_count,page_after=after,next_cursor=info.next_cursor,page_size=16,
                        storage_total=0x4f0000,storage_used=sum(f.stat().st_size for f in archive.iterdir()),writable=True,epoch=now(),timezone=clock.timezone,pet=pet,speaker_available=True,
                        reminders=[dict(id=r.id,reason=r.reason,due_at=r.due_at) for r in info.reminders[:info.reminder_count]],assets=[asset_json(a) for a in db.assets[:db.count]],events=[event_json(e) for e in db.events[:db.event_count]])
        def export():
            data=snapshot();assets=list(data['assets']);cursor=data['next_cursor']
            while cursor:
                page=snapshot(cursor);assets.extend(page['assets']);cursor=page['next_cursor']
            events=[];after=0
            while after<data['revision']:
                batch=(Event*16)();n=lib.battery_archive_history(batch,16,after,data['revision']);assert n>0
                events.extend(event_json(e) for e in batch[:n]);after+=n
            return dict(schema=2,revision=data['revision'],pet=data['pet'],assets=assets,events=events)
        class Handler(BaseHTTPRequestHandler):
            def log_message(self,*_):pass
            def send(self,code,data,kind='application/json'):
                content=json.dumps(data,ensure_ascii=False).encode() if kind=='application/json' else data
                self.send_response(code);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(content)))
                self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(content)
            def do_GET(self):
                parts=urlparse(self.path);qs=parse_qs(parts.query)
                if parts.path=='/':self.send(200,(ROOT/'main/web/index.html').read_bytes(),'text/html; charset=utf-8');return
                with lock:
                    if parts.path=='/api/state':self.send(200,snapshot(int(qs.get('after',['0'])[0]),qs.get('q',[''])[0],int(qs.get('status',['-1'])[0])))
                    elif parts.path=='/api/export':self.send(200,export())
                    elif parts.path=='/api/asset':
                        care=CareAsset();id=int(qs.get('id',['0'])[0])
                        if lib.battery_archive_get(id,c.byref(care)):self.send(200,asset_json(care.asset))
                        else:self.send(404,dict(error='资产不存在'))
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
                        elif self.path=='/api/action':result=action(data['id'],data['action'],data['revision'])
                        elif self.path=='/api/snooze':result=0 if lib.battery_archive_snooze(now()) else 1
                        elif self.path=='/api/settings':result=0 if lib.battery_archive_settings(data['volume'],data['quiet_start'],data['quiet_end'],bool(data['muted'])) else 1
                        else:raise ValueError('不存在')
                        if result:self.send(409 if result in [3,4,5] else 400,dict(error={1:'字段无效',2:'资产不存在',3:'数据已改变，请刷新后重试',4:'存储不足或事务未完成',5:'当前状态不能执行此操作'}[result]))
                        else:self.send(200,dict(ok=True))
                        update()
                except (ValueError,KeyError,OverflowError) as exc:self.send(400,dict(error=str(exc)))
        print(f'Browser fixture: http://127.0.0.1:{args.port} (synthetic data; C model; no hardware)',flush=True)
        ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()
if __name__=='__main__':main()
