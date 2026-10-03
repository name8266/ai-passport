const assert = require('node:assert/strict');
const { Probe, summarize, quickSample } = require('../dist/probe.js');
const catalogue = require('../dist/catalogue.json');
const cameras = catalogue.cameras.slice(0, 4);
function fixture() {
  const images = [], timers = new Map(); let next = 0;
  class Image {
    constructor() { images.push(this); }
    removeAttribute() { this.removed = true; }
  }
  const probe = new Probe({ ImageClass: Image, concurrency: 2, clock: () => 100000,
    setTimer: fn => { timers.set(++next, fn); return next; }, clearTimer: id => timers.delete(id) });
  const pass = image => { image.naturalWidth = 300; image.naturalHeight = 200; image.onload(); };
  return { probe, images, timers, pass };
}
async function run() {
  let f = fixture(); f.probe.start(cameras);
  assert.equal(f.probe.active.size, 2);
  assert.match(f.images[0].src, /_wc_phone_test=/);
  assert.equal(f.images[0].referrerPolicy, 'no-referrer');
  assert.equal(f.images[0].crossOrigin, undefined);
  const late = f.images[0].onerror;
  f.pass(f.images[0]); late(); f.images[1].onerror(); await Promise.resolve();
  assert.deepEqual([summarize(f.probe.rows).passed, summarize(f.probe.rows).failed], [1, 1]);
  f.pass(f.images[2]); f.pass(f.images[3]); await Promise.resolve();
  assert.equal(f.probe.running, false); assert.equal(f.timers.size, 0);
  assert.equal(summarize(f.probe.rows).completed, 4);

  f = fixture(); f.probe.start(cameras);
  const latePass = f.images[0].onload; f.probe.pause(); latePass(); await Promise.resolve();
  assert.equal(f.probe.active.size, 0); assert.equal(f.timers.size, 0);
  assert.equal(summarize(f.probe.rows).completed, 0); assert.equal(f.probe.queue.length, 4);
  assert.equal(f.probe.rows[0].interrupted, 1);
  f.probe.resume(); assert.equal(f.probe.active.size, 2);
  for (const timeout of [...f.timers.values()]) timeout(); await Promise.resolve();
  assert.equal(summarize(f.probe.rows).failed, 2);
  f.probe.stop(); const stoppedRows = f.probe.rows;
  assert.equal(summarize(stoppedRows).completed, 2); assert.equal(f.probe.active.size, 0);
  f.probe.start([cameras[0]], { cacheBust: false }); f.pass(f.images.at(-1)); await Promise.resolve();
  assert.equal(summarize(f.probe.rows).passed, 1); assert.equal(f.probe.rows.length, 1);
  assert.equal(f.images.at(-1).src, cameras[0].url);

  f = fixture(); f.probe.start([{ ...cameras[0], url: 'http://example.com/image.jpg' }]);
  assert.equal(f.probe.rows[0].status, 'unsupported'); assert.equal(f.probe.running, false);
  assert.equal(summarize(f.probe.rows).skipped, 1);
  const sample = quickSample(catalogue.cameras);
  assert.equal(sample.length, 30); assert.equal(new Set(sample.map(c => c.id)).size, 30);
  assert.equal(new Set(sample.map(c => c.provider)).size, 7);
  assert.equal(summarize([{...cameras[0], status:'image_loaded'}], [{camera_ids:[cameras[0].id]}, {camera_ids:[]}]).capitals_with_image, 1);
  console.log('PASS: actual success counts, late callbacks, timeout, pause/resume, partial stop, fresh run, sample and capital aggregation');
}
run().catch(e => { console.error(e); process.exitCode = 1; });
