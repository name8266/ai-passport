(function (root) {
  'use strict';
  const TERMINAL = new Set(['image_loaded', 'load_error', 'timeout', 'unsupported']);
  function summarize(rows, capitals = []) {
    const passed = new Set(rows.filter(r => r.status === 'image_loaded').map(r => r.id));
    const providers = {};
    for (const r of rows) {
      const p = providers[r.provider] ||= { total: 0, passed: 0, failed: 0, skipped: 0, pending: 0 };
      p.total++;
      if (r.status === 'image_loaded') p.passed++;
      else if (r.status === 'load_error' || r.status === 'timeout') p.failed++;
      else if (r.status === 'unsupported') p.skipped++;
      else p.pending++;
    }
    return {
      total: rows.length, passed: passed.size,
      failed: rows.filter(r => r.status === 'load_error' || r.status === 'timeout').length,
      skipped: rows.filter(r => r.status === 'unsupported').length,
      completed: rows.filter(r => TERMINAL.has(r.status)).length,
      capitals_with_image: capitals.filter(c => c.camera_ids.some(id => passed.has(id))).length,
      capital_registry_total: capitals.length, providers
    };
  }
  function quickSample(cameras, limit = 30) {
    const groups = [...new Set(cameras.map(c => c.provider))].map(p => cameras.filter(c => c.provider === p));
    const picked = []; let offset = 0;
    while (picked.length < Math.min(limit, cameras.length)) {
      for (const group of groups) if (group[offset] && picked.length < limit) picked.push(group[offset]);
      offset++;
    }
    return picked;
  }
  class Probe {
    constructor({ ImageClass, timeout = 10000, concurrency = 3, onChange = () => {}, onImage = () => {}, clock = Date.now, setTimer = (fn, ms) => setTimeout(fn, ms), clearTimer = id => clearTimeout(id) }) {
      Object.assign(this, { ImageClass, timeout, concurrency, onChange, onImage, clock, setTimer, clearTimer });
      this.active = new Map(); this.rows = []; this.queue = []; this.running = false; this.paused = false;
    }
    start(cameras, options = {}) {
      this.stop(); this.runId = String(this.clock()) + '-' + Math.random().toString(36).slice(2, 8);
      this.startedAt = new Date(this.clock()).toISOString(); this.endedAt = null;
      this.options = options; this.paused = false; this.running = true;
      this.rows = cameras.map(c => ({ ...c, status: 'not_tested', attempts: 0, interrupted: 0 }));
      this.queue = [...this.rows]; this.onChange(); this.pump();
    }
    pause() {
      if (!this.running) return;
      this.paused = true;
      for (const cancel of [...this.active.values()]) cancel();
      this.onChange();
    }
    resume() {
      if (!this.running) return;
      this.paused = false; this.pump(); this.onChange();
    }
    stop() {
      this.paused = true;
      for (const cancel of [...this.active.values()]) cancel();
      this.running = false; this.endedAt = new Date(this.clock()).toISOString();
    }
    pump() {
      if (!this.running || this.paused) return;
      while (this.active.size < this.concurrency && this.queue.length) {
        const row = this.queue.shift();
        try {
          const url = new URL(row.url);
          if (url.protocol !== 'https:' || url.username || url.password) throw new Error('unsupported');
          if (this.options.cacheBust !== false) url.searchParams.set('_wc_phone_test', this.runId + '-' + (row.attempts + 1));
          this.launch(row, url.href);
        } catch (_) { row.status = 'unsupported'; this.onChange(); }
      }
      if (!this.queue.length && !this.active.size) {
        this.running = false; this.endedAt = new Date(this.clock()).toISOString(); this.onChange();
      }
    }
    launch(row, url) {
      const image = new this.ImageClass(); let finished = false, timer;
      const began = this.clock(); row.attempts++; row.status = 'loading'; row.request_url = url;
      const finish = (status) => {
        if (finished) return;
        finished = true; this.clearTimer(timer); image.onload = null; image.onerror = null;
        this.active.delete(row.id);
        if (status === 'interrupted') {
          row.interrupted++; row.status = 'not_tested'; this.queue.unshift(row);
        } else {
          row.status = status; row.elapsed_ms = this.clock() - began;
          row.checked_at = new Date(this.clock()).toISOString();
          if (status === 'image_loaded') {
            row.width = image.naturalWidth; row.height = image.naturalHeight;
            this.onImage(image, row);
          }
        }
        if (status !== 'image_loaded') image.removeAttribute('src');
        this.onChange(); queueMicrotask(() => this.pump());
      };
      this.active.set(row.id, () => finish('interrupted'));
      image.referrerPolicy = 'no-referrer'; image.alt = row.name;
      image.onload = () => finish(image.naturalWidth > 0 && image.naturalHeight > 0 ? 'image_loaded' : 'load_error');
      image.onerror = () => finish('load_error');
      timer = this.setTimer(() => finish('timeout'), this.timeout);
      image.src = url; this.onChange();
    }
  }
  const api = { Probe, summarize, quickSample };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.WorldCamProbe = api;
})(typeof window !== 'undefined' ? window : globalThis);
