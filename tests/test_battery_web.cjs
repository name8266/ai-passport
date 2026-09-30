/* Run with Playwright available and system Chromium. Starts a model-backed local
 * fixture; it does not validate the ESP32 HTTP transport, RF or NVS hardware. */
const { chromium } = require('playwright');
const { spawn } = require('node:child_process');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root=path.resolve(__dirname,'..');
const server=spawn('python3',[path.join(__dirname,'battery_web_preview.py'),'--port','8766','--seed']);
const url='http://127.0.0.1:8766';
let browser;
(async()=>{
  await new Promise((resolve,reject)=>{server.stdout.once('data',resolve);server.once('error',reject);server.once('exit',code=>reject(new Error('Fixture exited '+code)));});
  browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true});
  const page=await browser.newPage({viewport:{width:1440,height:1000},acceptDownloads:true});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===6);
  const get=async()=>await (await page.request.get(url+'/api/state')).json();
  const post=async(endpoint,data)=>await page.request.post(url+endpoint,{data,headers:{'X-Passport-Client':'battery-desk'}});
  let data=await get();assert(data.epoch>1704067200);assert(Math.abs(data.epoch-Date.now()/1000)<5);
  const out=path.join(root,'build','previews');fs.mkdirSync(out,{recursive:true});
  await page.screenshot({path:path.join(out,'battery-desktop.png'),fullPage:true});
  await page.locator('#search').fill('遥控器');await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===1);
  await page.locator('#search').fill('');await page.locator('#filter').selectOption('attention');await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===2);
  await page.locator('#filter').selectOption('');await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===6);
  await page.locator('#add').click();
  await page.locator('[name=name]').fill('测试相机电池');await page.locator('[name=location]').fill('测试抽屉');
  await page.locator('[name=soc]').fill('60');await page.locator('#save').click();
  await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===7);
  data=await get();const id=data.assets.find(a=>a.name==='测试相机电池').id;
  async function act(action,status){
    await page.locator(`[data-detail="${id}"]`).first().click();
    await page.locator(`[data-action="${action}"]`).click();await page.locator('#confirmAction').click();
    await page.waitForFunction(()=>!document.getElementById('detail').open&&!document.getElementById('confirmation').open);
    const actual=await get();assert.equal(actual.assets.find(a=>a.id===id).status,status);
    await page.waitForFunction(rev=>document.getElementById('connectionStatus').textContent.includes('版本 '+rev+' '),actual.revision);
  }
  await act(2,1);await act(3,0);await act(4,2);await act(5,0);data=await get();
  assert.equal(data.assets.find(a=>a.id===id).cycles,1);assert.equal(data.assets.find(a=>a.id===id).soc,100);
  await act(6,3);await act(9,0);
  await page.locator(`[data-detail="${id}"]`).first().click();await page.locator('#edit').click();
  await page.locator('[name=name]').fill('已编辑测试电池');await page.locator('#save').click();
  await page.waitForFunction(()=>!document.getElementById('editor').open);
  data=await get();assert.equal(data.assets.find(a=>a.id===id).name,'已编辑测试电池');
  await page.locator(`[data-detail="${id}"]`).first().click();await page.locator('#edit').click();
  await page.locator('[name=notes]').fill('草稿不会被轮询覆盖');
  data=await get();await post('/api/action',{id,action:2,revision:data.revision});
  await page.locator('#save').click();await page.waitForFunction(()=>document.getElementById('toast').textContent.includes('数据已改变'));
  assert(await page.locator('#editor').evaluate(e=>e.open));assert.equal(await page.locator('[name=notes]').inputValue(),'草稿不会被轮询覆盖');
  await page.locator('#editor [data-close]').first().click();
  await page.locator('#refresh').click();await page.waitForFunction(()=>!document.getElementById('refresh').disabled);
  await page.locator('#add').click();await page.locator('[name=name]').fill('电'.repeat(22));await page.locator('#save').click();
  await page.waitForFunction(()=>document.getElementById('toast').textContent.includes('超过 63 字节'));assert(await page.locator('#editor').evaluate(e=>e.open));
  await page.locator('#editor [data-close]').first().click();
  await page.locator('#add').click();await page.locator('[name=name]').fill('<img src=x onerror=alert(1)>');await page.locator('#save').click();
  await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===8);assert.equal(await page.locator('#rows img').count(),0);
  const downloadWait=page.waitForEvent('download');await page.locator('#export').click();const csv=await downloadWait;
  const csvPath=path.join(out,'test-export.csv');await csv.saveAs(csvPath);assert(fs.readFileSync(csvPath,'utf8').includes('已编辑测试电池'));
  await page.locator('[data-view=history]').click();assert(await page.locator('#historySection').isVisible());
  assert((await page.locator('#historyList').textContent()).includes('未校时'));
  const backupWait=page.waitForEvent('download');await page.locator('#backup').click();const backup=await backupWait;
  await backup.saveAs(path.join(out,'test-export.json'));const dump=JSON.parse(fs.readFileSync(path.join(out,'test-export.json')));assert.equal(dump.assets.length,8);
  await page.locator('[data-view=overview]').click();
  for(const width of [390,320]){
    await page.setViewportSize({width,height:844});
    assert(await page.evaluate(()=>document.documentElement.scrollWidth===innerWidth),'No horizontal page overflow at '+width);
    await page.screenshot({path:path.join(out,`battery-mobile-${width}.png`),fullPage:true});
  }
  await page.setViewportSize({width:1440,height:1000});
  data=await get();const xp=data.pet.xp;await page.locator('#pet').click();assert.equal((await get()).pet.xp,xp,'Patting does not create rewards');assert(xp>=20,'Care actions earn growth');
  await page.locator('[data-view=care]').click();assert(await page.locator('#careSection').isVisible());
  await page.locator('#careSettings [name=muted]').selectOption('1');await page.locator('#careSettings [name=volume]').fill('25');
  await page.locator('#careSettings button[type=submit]').click();await page.waitForFunction(()=>document.getElementById('careStatus').textContent.includes('已静音'));
  data=await get();assert.equal(data.pet.muted,1);assert.equal(data.pet.volume,25);
  await page.locator('#snooze').click();await page.waitForFunction(()=>document.getElementById('careStatus').textContent.includes('已稍后提醒'));assert((await get()).pet.snooze_until>Date.now()/1000);
  await page.screenshot({path:path.join(out,'battery-reminders.png'),fullPage:true});
  for(let i=0;i<25;i++){
    data=await get();const response=await post('/api/assets',{id:0,revision:data.revision,name:'分页电池 '+i,location:'储物盒',notes:'',capacity:2200,cycles:0,soc:10,health:95,status:0,chemistry:0,remind_at:Math.floor(Date.now()/1000)-60,charge_minutes:1});assert(response.ok());
  }
  await page.locator('[data-view=assets]').click();await page.locator('#refresh').click();await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===16);
  assert.equal((await get()).total,33);assert(!(await page.locator('#add').isDisabled()),'Asset creation stays available beyond 16');
  await page.locator('#nextPage').click();await page.waitForFunction(()=>document.getElementById('tableCount').textContent.includes('本页 16')&&!document.getElementById('previousPage').disabled);
  const secondIds=await page.locator('#rows [data-detail]').evaluateAll(nodes=>nodes.map(n=>Number(n.dataset.detail)));assert(Math.min(...secondIds)>16);
  await page.locator('#nextPage').click();await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===1);assert(await page.locator('#nextPage').isDisabled());
  await page.locator('#previousPage').click();await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===16);
  await page.locator('#search').fill('分页电池 24');await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===1);assert((await page.locator('#rows').textContent()).includes('分页电池 24'));
  const allCsvWait=page.waitForEvent('download');await page.locator('#export').click();const allCsv=await allCsvWait;await allCsv.saveAs(path.join(out,'test-all-assets.csv'));assert.equal(fs.readFileSync(path.join(out,'test-all-assets.csv'),'utf8').split('\r\n').length,34);
  for(let i=0;i<6;i++)assert((await post('/api/settings',{volume:25,muted:1,quiet_start:22+i%2,quiet_end:8})).ok());
  const full=await (await page.request.get(url+'/api/export')).json();assert.equal(full.assets.length,33);assert.equal(full.events.length,full.revision);assert(full.events.length>48);assert.equal(full.pet.xp,xp);
  await page.locator('#search').fill('');await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===16);
  await page.locator('[data-view=care]').click();await page.locator('#allReminders').click();await page.waitForFunction(()=>document.getElementById('filter').value==='due'&&document.querySelectorAll('#rows tr').length===16);
  await page.locator('#nextPage').click();await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===11);
  await page.locator('#filter').selectOption('');await page.locator('[data-view=overview]').click();await page.waitForFunction(()=>document.querySelectorAll('#rows tr').length===16);
  for(const width of [390,320]){await page.setViewportSize({width,height:844});assert(await page.evaluate(()=>document.documentElement.scrollWidth===innerWidth));}
  await page.route('**/api/state*',route=>route.abort());await page.locator('#refresh').click();
  await page.waitForFunction(()=>!document.getElementById('banner').hidden);
  assert(await page.locator('#add').isDisabled());await page.unroute('**/api/state*');
  await page.locator('#refresh').click();await page.waitForFunction(()=>document.getElementById('banner').hidden);
  assert.deepEqual(errors,[]);
  console.log('Browser tests: PASS (33 assets, global search and pagination, all-history export, pet rewards and no click farming, reminder settings/snooze, time sync, CRUD, conflicts, XSS, offline recovery, desktop/390/320)');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();server.kill('SIGTERM');});
