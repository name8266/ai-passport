'use strict';
const $ = id => document.getElementById(id);
let catalogue, probe, renderPending = false, lastMessage = '', runMode = '';
const names = { 'image_loaded': '可显示', 'load_error': '加载失败', 'timeout': '超时', 'unsupported': '无法测试' };
const providerNames = { 'Foto-Webcam.eu': 'Foto-Webcam.eu', 'SkylineWebcams': 'SkylineWebcams', 'Webcam Galore': 'Webcam Galore', 'National Park Service': '美国国家公园', 'Hong Kong Observatory': '香港天文台', 'GeoNet': 'GeoNet', 'US Antarctic Program': '美国南极科考' };
function textNode(tag, value, className) { const e = document.createElement(tag); e.textContent = value; if (className) e.className = className; return e; }
function notify(value) { lastMessage = value; $('notice').textContent = value; }
function report() {
  if (!probe || !probe.rows.length) return null;
  return {
    schema_version: 1, method: 'Phone browser direct HTTPS Image load, no CORS fetch or image proxy. Success requires natural image dimensions. Browser HTTP statuses unavailable.',
    started_at: probe.startedAt, ended_at: probe.endedAt,
    run_status: probe.running ? probe.paused ? 'paused' : 'running' : probe.queue.length ? 'stopped_partial' : 'completed',
    sample: runMode, catalogue_total: catalogue.cameras.length,
    network_label: probe.options.network, physical_egress_verified: false,
    note: 'Country, VPN, capture freshness and censorship are not established. Quick sample cannot estimate total camera usability; image queries include a cache buster when enabled.',
    browser: navigator.userAgent, application_origin: location.origin,
    cache_bust: probe.options.cacheBust, concurrency: probe.concurrency, timeout_ms: probe.timeout,
    catalogue_generated_at: catalogue.generated_at,
    summary: WorldCamProbe.summarize(probe.rows, catalogue.capitals),
    results: probe.rows.map(({ id, name, country, provider, url, source_page, resolution, status, attempts, interrupted, checked_at, elapsed_ms, width, height }) =>
      ({ id, name, country, provider, url, source_page, resolution, status, attempts, interrupted, checked_at, elapsed_ms, width, height }))
  };
}
function render() {
  renderPending = false;
  const data = report(); if (!data) return;
  const s = data.summary;
  $('passed').textContent = s.passed; $('failed').textContent = s.failed;
  $('bar').style.width = (100 * s.completed / Math.max(1, s.total)) + '%';
  const progress = document.querySelector('[role=progressbar]');
  progress.setAttribute('aria-valuemax', s.total); progress.setAttribute('aria-valuenow', s.completed);
  $('progress-text').textContent = `已完成 ${s.completed} / ${s.total}` + (s.skipped ? ` · 无法测试 ${s.skipped}` : '');
  $('capital-text').textContent = `首都有画面 ${s.capitals_with_image} / ${s.capital_registry_total}`;
  $('status').textContent = probe.running ? probe.paused ? '已暂停，点击继续可接着测' : '正在用手机加载图片…' : probe.queue.length ? '本次已结束，保留已测结果' : '检测完成';
  $('pause').textContent = probe.paused ? '继续检测' : '暂停检测';
  $('pause').disabled = $('stop').disabled = !probe.running;
  $('all').disabled = $('quick').disabled = probe.running;
  $('network').disabled = $('cache').disabled = probe.running;
  for (const id of ['copy', 'download', 'share']) $(id).disabled = false;
  const tbody = document.createDocumentFragment();
  for (const [provider, p] of Object.entries(s.providers)) {
    const tr = document.createElement('tr'); tr.append(textNode('td', providerNames[provider] || provider, 'source-name'));
    for (const value of [p.passed, p.failed, p.passed + p.failed + p.skipped]) tr.append(textNode('td', value));
    tbody.append(tr);
  }
  $('providers').replaceChildren(tbody);
  const list = document.createDocumentFragment();
  const done = probe.rows.filter(r => names[r.status]).sort((a, b) => (b.checked_at || '').localeCompare(a.checked_at || '')).slice(0, 20);
  for (const r of done) {
    const li = document.createElement('li'); const place = textNode('span', r.name, 'place');
    place.append(textNode('small', `${r.country} · ${providerNames[r.provider] || r.provider}`));
    li.append(place, textNode('span', names[r.status], r.status === 'image_loaded' ? 'pass' : r.status === 'unsupported' ? 'pending' : 'fail')); list.append(li);
  }
  if (!done.length) list.append(textNode('li', '等待第一张图片…', 'pending'));
  $('results').replaceChildren(list);
  if ($('report-text').closest('details').open) $('report-text').value = JSON.stringify(data, null, 2);
}
function scheduleRender() { if (!renderPending) { renderPending = true; requestAnimationFrame(render); } }
function begin(mode) {
  if (!catalogue || probe.running) return;
  runMode = mode; notify(''); $('preview').style.display = 'none'; $('image').replaceChildren();
  const cameras = mode === 'all' ? catalogue.cameras : WorldCamProbe.quickSample(catalogue.cameras);
  probe.start(cameras, { cacheBust: $('cache').checked, network: $('network').value });
}
function summaryText() {
  const r = report(), s = r.summary;
  return `摄像头手机直连检测\n${runMode === 'all' ? '全量' : '快测样本'}：已完成 ${s.completed}/${s.total}\n成功 ${s.passed}，失败/超时 ${s.failed}，无法测试 ${s.skipped}\n首都有画面 ${s.capitals_with_image}/${s.capital_registry_total}\n网络标注：${{ wifi: 'Wi-Fi', cellular: '手机流量', unspecified: '未标注' }[r.network_label]}\n测试时间：${r.started_at}\n出口国家和 VPN 状态未经验证；手机浏览器结果。`;
}
$('all').onclick = () => begin('all'); $('quick').onclick = () => begin('provider-sample-30');
$('pause').onclick = () => { notify(''); probe.paused ? probe.resume() : probe.pause(); };
$('stop').onclick = () => { probe.stop(); scheduleRender(); };
document.addEventListener('visibilitychange', () => {
  if (document.hidden && probe && probe.running && !probe.paused) {
    probe.pause(); notify('页面进入后台，已暂停。回到前台后点击「继续检测」。');
  }
});
$('copy').onclick = async () => {
  const text = summaryText();
  try { await navigator.clipboard.writeText(text); notify('摘要已复制，可以粘贴回聊天。'); }
  catch (_) { const box = $('report-text'); box.closest('details').open = true; box.value = text; box.focus(); box.select(); notify('请长按文字复制摘要。'); }
};
function reportFile() { return new File([JSON.stringify(report(), null, 2)], 'WorldCam-phone-report.json', { type: 'application/json' }); }
$('download').onclick = () => {
  const url = URL.createObjectURL(reportFile()); const a = document.createElement('a');
  a.href = url; a.download = 'WorldCam-phone-report.json'; document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60000); notify('报告已生成。如果浏览器没有保存，请展开下方报告复制。');
};
$('share').onclick = async () => {
  const file = reportFile();
  try {
    if (navigator.canShare && navigator.canShare({ files: [file] })) await navigator.share({ files: [file], title: '摄像头手机检测结果' });
    else if (navigator.share) await navigator.share({ text: summaryText(), title: '摄像头手机检测结果' });
    else { notify('此浏览器不支持分享，请复制摘要或下载报告。'); return; }
  } catch (e) { if (e.name !== 'AbortError') notify('分享未成功，请复制摘要或下载报告。'); }
};
document.querySelector('details').addEventListener('toggle', e => { if (e.target.open) $('report-text').value = report() ? JSON.stringify(report(), null, 2) : '尚未开始检测'; });
fetch('catalogue.json', { cache: 'no-store' }).then(r => { if (!r.ok) throw new Error('catalogue'); return r.json(); }).then(data => {
  catalogue = data;
  probe = new WorldCamProbe.Probe({ ImageClass: Image, onChange: scheduleRender, onImage: (image, row) => {
    $('image').replaceChildren(image); $('preview').style.display = 'block';
    $('caption').textContent = `${row.country} · ${row.name}（不代表拍摄时间实时）`;
    const link = textNode('a', ' 查看官方来源'); link.href = row.source_page;
    link.target = '_blank'; link.rel = 'noopener noreferrer'; $('caption').append(link);
  }});
  $('all').disabled = $('quick').disabled = false; $('status').textContent = `目录就绪 · ${data.cameras.length} 个图片源`;
  const tbody = document.createDocumentFragment();
  for (const p of [...new Set(data.cameras.map(c => c.provider))]) {
    const tr = document.createElement('tr'); tr.append(textNode('td', providerNames[p] || p, 'source-name'));
    for (let i = 0; i < 3; i++) tr.append(textNode('td', '—')); tbody.append(tr);
  }
  $('providers').replaceChildren(tbody);
}).catch(() => { $('status').textContent = '目录加载失败，请刷新页面重试。'; });
