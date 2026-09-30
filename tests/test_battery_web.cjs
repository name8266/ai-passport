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
  await page.locator('#search').fill('遥控器');assert.equal(await page.locator('#rows tr').count(),1);
  await page.locator('#search').fill('');await page.locator('#filter').selectOption('attention');assert.equal(await page.locator('#rows tr').count(),2);
  await page.locator('#filter').selectOption('');
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
  await page.route('**/api/state',route=>route.abort());await page.locator('#refresh').click();
  await page.waitForFunction(()=>!document.getElementById('banner').hidden);
  assert(await page.locator('#add').isDisabled());await page.unroute('**/api/state');
  await page.locator('#refresh').click();await page.waitForFunction(()=>document.getElementById('banner').hidden);
  assert.deepEqual(errors,[]);
  console.log('Browser tests: PASS (time sync, create/edit, 6 transitions, conflict draft, UTF-8 limit, XSS, filter, CSV/JSON, offline recovery, desktop/390/320)');
})().catch(e=>{console.error(e);process.exitCode=1;}).finally(async()=>{if(browser)await browser.close();server.kill('SIGTERM');});
