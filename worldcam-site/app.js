'use strict';
const $=id=>document.getElementById(id);
let catalogue=null, filtered=[], selected=null, selectedIndex=0, favorites=new Set(JSON.parse(localStorage.getItem('worldcam-favorites')||'[]')), favoriteOnly=false, refreshTimer=null, loadToken=0;

function safeUrl(raw){
  try{const u=new URL(raw);return u.protocol==='https:'?u:null}catch{return null}
}
function esc(s){return String(s??'')}
function uniq(arr){return [...new Set(arr)].sort((a,b)=>a.localeCompare(b,'zh-CN'))}
function setOptions(el,values,label){el.innerHTML='<option value="">'+label+'</option>'+values.map(v=>'<option value="'+esc(v).replace(/"/g,'&quot;')+'">'+esc(v)+'</option>').join('')}
function saveFavs(){localStorage.setItem('worldcam-favorites',JSON.stringify([...favorites]))}
function isFav(id){return favorites.has(id)}
function toggleFav(id){
  if(!id)return;
  isFav(id)?favorites.delete(id):favorites.add(id);saveFavs();
  $('favorite').textContent=isFav(id)?'★':'☆';
  if(favoriteOnly)applyFilters(); else renderList();
}
function cameraText(c){return (c.name+' '+c.country+' '+c.provider).toLowerCase()}

function applyFilters(){
  if(!catalogue)return;
  const q=$('query').value.trim().toLowerCase(), country=$('country').value, provider=$('provider').value;
  filtered=catalogue.cameras.filter(c=>safeUrl(c.url)&&(!q||cameraText(c).includes(q))&&(!country||c.country===country)&&(!provider||c.provider===provider)&&(!favoriteOnly||isFav(c.id)));
  $('resultCount').textContent=filtered.length+' 个结果';
  if(!filtered.length){selected=null;renderList();clearViewer('没有符合当前条件的摄像头');return}
  const keep=selected&&filtered.find(c=>c.id===selected.id);
  selectCamera(keep||filtered[0],false);
}
function renderList(){
  const box=$('list');box.replaceChildren();
  if(!filtered.length){const e=document.createElement('div');e.className='camera';e.textContent='没有结果';box.append(e);return}
  const frag=document.createDocumentFragment();
  filtered.forEach((c,i)=>{
    const b=document.createElement('button');b.className='camera'+(selected?.id===c.id?' active':'');b.type='button';b.setAttribute('role','option');b.setAttribute('aria-selected',String(selected?.id===c.id));
    const left=document.createElement('div'),name=document.createElement('b'),meta=document.createElement('span'),provider=document.createElement('small'),fav=document.createElement('span');
    name.textContent=c.name;meta.textContent=c.country;provider.textContent=c.provider;fav.className='fav';fav.textContent=isFav(c.id)?'★':'';
    left.append(name,meta,provider);b.append(left,fav);b.onclick=()=>selectCamera(c,true);frag.append(b);
  });
  box.append(frag);
  box.querySelector('.active')?.scrollIntoView({block:'nearest'});
}
function clearViewer(message){
  loadToken++;clearTimeout(refreshTimer);$('name').textContent=message;$('meta').textContent='—';$('frame').removeAttribute('src');$('frame').style.display='none';$('loading').hidden=true;$('error').hidden=true;$('status').textContent='无画面';$('source').href='#';$('favorite').textContent='☆'
}
function selectCamera(c,load=true){
  selected=c;selectedIndex=Math.max(0,filtered.findIndex(x=>x.id===c.id));$('name').textContent=c.name;$('meta').textContent=c.country+' · '+c.provider+(c.resolution?' · '+c.resolution:'');$('source').href=c.source_page||c.url;$('favorite').textContent=isFav(c.id)?'★':'☆';renderList();if(load)loadFrame()
}
function withBust(raw){
  const u=new URL(raw);u.searchParams.set('_wc_view',Date.now().toString());return u.href
}
function loadFrame(){
  if(!selected)return;
  const url=safeUrl(selected.url);if(!url){showError('来源不是 HTTPS，浏览器已跳过');return}
  const token=++loadToken;clearTimeout(refreshTimer);$('loading').hidden=false;$('error').hidden=true;$('frame').style.display='none';$('status').textContent='正在获取画面…';
  const img=new Image();img.referrerPolicy='no-referrer';img.alt=selected.country+' '+selected.name+' 公开摄像头画面';
  const began=performance.now();
  const timer=setTimeout(()=>{if(token!==loadToken)return;img.src='';showError('连接超时')},12000);
  img.onload=()=>{
    if(token!==loadToken)return;clearTimeout(timer);
    $('frame').src=img.src;$('frame').alt=img.alt;$('frame').style.display='block';$('loading').hidden=true;$('error').hidden=true;
    $('status').textContent='刷新成功 · '+new Date().toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit',second:'2-digit'})+' · '+Math.round(performance.now()-began)+' ms';
    refreshTimer=setTimeout(loadFrame,60000);
  };
  img.onerror=()=>{if(token!==loadToken)return;clearTimeout(timer);showError('来源暂时不可用')};
  img.src=withBust(url.href);
}
function showError(msg){
  $('loading').hidden=true;$('frame').style.display='none';$('error').hidden=false;$('error').querySelector('strong').textContent=msg;$('status').textContent='加载失败 · '+new Date().toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'});
  refreshTimer=setTimeout(loadFrame,60000);
}
function move(n){
  if(!filtered.length)return;selectedIndex=(selectedIndex+n+filtered.length)%filtered.length;selectCamera(filtered[selectedIndex],true)
}
function random(){
  if(!filtered.length)return;selectedIndex=Math.floor(Math.random()*filtered.length);selectCamera(filtered[selectedIndex],true)
}
async function toggleFullscreen(){
  const el=$('viewer');try{document.fullscreenElement?await document.exitFullscreen():await el.requestFullscreen()}catch{}
}

$('query').addEventListener('input',applyFilters);$('country').onchange=applyFilters;$('provider').onchange=applyFilters;
$('random').onclick=$('randomTop').onclick=random;$('prev').onclick=()=>move(-1);$('next').onclick=()=>move(1);$('refresh').onclick=$('retry').onclick=loadFrame;
$('fullscreen').onclick=$('fullscreenTop').onclick=toggleFullscreen;$('favorite').onclick=()=>toggleFav(selected?.id);
$('favoriteFilter').onclick=()=>{favoriteOnly=!favoriteOnly;$('favoriteFilter').textContent=favoriteOnly?'★':'☆';$('favoriteFilter').title=favoriteOnly?'显示全部':'只看收藏';applyFilters()};
document.addEventListener('keydown',e=>{if(e.target.matches('input,select'))return;if(e.key==='ArrowLeft')move(-1);if(e.key==='ArrowRight')move(1);if(e.key.toLowerCase()==='r')random();if(e.key==='Enter')loadFrame()});
document.addEventListener('visibilitychange',()=>{if(document.hidden)clearTimeout(refreshTimer);else if(selected)loadFrame()});

fetch('./catalogue.json',{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('catalogue');return r.json()}).then(data=>{
  catalogue=data;catalogue.cameras=catalogue.cameras.filter(c=>safeUrl(c.url));
  $('cameraCount').textContent=catalogue.cameras.length.toLocaleString('zh-CN');$('countryCount').textContent=uniq(catalogue.cameras.map(c=>c.country)).length;$('providerCount').textContent=uniq(catalogue.cameras.map(c=>c.provider)).length;
  setOptions($('country'),uniq(catalogue.cameras.map(c=>c.country)),'全部国家 / 地区');setOptions($('provider'),uniq(catalogue.cameras.map(c=>c.provider)),'全部发布方');
  applyFilters();if(filtered.length)loadFrame();
}).catch(()=>clearViewer('摄像头目录加载失败，请刷新页面'));
