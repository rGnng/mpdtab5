'use strict';
var T = window.Tab5, S = {}, C = {}, dirty = {}, pending = {};
var $ = function (id) { return document.getElementById(id); };
var configFields = ['volume','volume_steps','brightness','screen_off_minutes','theme','sound_mode',
  'pagination_size','debug_log','usb_mode','usb_net_ip','usb_net_client_ip','screen_flip','wifi_mode','ap_ssid'];
function toast(msg, bad) {
  $('toast').textContent = msg; $('toast').className = 'on' + (bad ? ' bad' : '');
  clearTimeout(toast.timer); toast.timer = setTimeout(function () { $('toast').className = ''; }, 4000);
}
var btPanel = T.bluetoothPanel($('bluetooth'), toast);
function value(id) { var e = $(id); return e.type === 'checkbox' ? e.checked : e.value; }
function same(a,b) { return String(a) === String(b); }
function setVal(id,v) {
  if (pending[id] !== undefined) {
    if (!same(v,pending[id])) return;
    delete pending[id];
  }
  if (dirty[id] || document.activeElement === $(id)) return;
  if ($(id).type === 'checkbox') $(id).checked = !!v; else $(id).value = v;
}
function markConfigDirty(id) {
  if (id==='brightness' && +value(id)<=0) { $('brightness').value=45; $('bri_v').textContent=45; }
  dirty[id] = true;
}
configFields.concat(['charge_en','qc_high']).forEach(function (id) {
  $(id).addEventListener('input', function () { markConfigDirty(id); });
  $(id).addEventListener('change', function () { markConfigDirty(id); });
});
['volume','brightness'].forEach(function (id) {
  $(id).addEventListener('input', function () { $(id === 'volume' ? 'vol_v' : 'bri_v').textContent = value(id); });
});
function saved(values) {
  Object.keys(values).forEach(function (id) {
    if (same(value(id), values[id])) { delete dirty[id]; pending[id] = values[id]; }
  });
}
function key(k) {
  return T.key(k).then(function () { setTimeout(refresh,350); })
    .catch(function (e) { toast(e.message,true); });
}
function renderArtwork(s,catalog) {
  var art=s.artwork || {}, total=Number(art.total || catalog.albums || 0);
  var cached=Math.min(total,Number(s.artwork_cached || 0));
  var checked=Math.min(total,Math.max(cached,Number(art.checked || 0)));
  var percent=total ? Math.floor(checked*100/total) : 0;
  var labels={caching:'Caching artwork',paused:'Paused while using Tab5',waiting:'Waiting for idle',
    retrying:'Waiting to retry',complete:'Cache pass complete',library:'Loading library',
    offline:'MPD offline',no_sd:'Insert SD card'};
  $('artwork_state').textContent=labels[art.state] || 'Waiting for library';
  $('artwork_progress').max=total || 1;
  $('artwork_progress').value=checked;
  var summary=cached.toLocaleString()+' / '+total.toLocaleString()+' albums cached · '+percent+'% checked';
  if(art.missing) summary+=' · '+Number(art.missing).toLocaleString()+' without supported artwork';
  if(art.failed) summary+=' · '+Number(art.failed).toLocaleString()+' failed';
  $('artwork_counts').textContent=summary;
}
function render(s,c,p) {
  S=s; C=c;
  btPanel.render(s.bluetooth);
  $('chips').innerHTML=T.chip('MPD',s.mpd_connected ? 'Connected' : s.mpd_host ? 'Connecting' : 'Setup',s.mpd_connected ? 'ok' : '') +
    T.chip('SD',s.sd_ready ? 'Ready' : 'Missing',s.sd_ready ? 'ok' : 'bad') + T.chip('WiFi',s.ip || '—','ok');
  $('np_title').textContent=s.title || 'No track'; $('np_artist').textContent=s.artist || '—'; $('np_album').textContent=s.album || '';
  $('play_state').textContent=['STOPPED','PLAYING','PAUSED'][s.play_status] || '—';
  $('btn_play').textContent=s.play_status===1 ? 'PAUSE' : 'PLAY';
  $('btn_star').textContent=s.starred ? '★' : '☆';
  [['btn_star',s.starred],['btn_shuffle',s.shuffle],['btn_repeat',s.repeat>0]].forEach(function (a) {
    $(a[0]).classList.toggle('active',!!a[1]); $(a[0]).setAttribute('aria-pressed',!!a[1]);
  });
  $('btn_repeat').textContent=s.repeat===1 ? 'REP 1' : 'REP';
  var cur=(s.elapsed_ms || 0)/1000, dur=(s.duration_ms || 0)/1000;
  $('np_cur').textContent=T.time(cur); $('np_dur').textContent=T.time(dur);
  $('np_bar').style.width=(dur ? Math.min(100,cur/dur*100) : 0)+'%';
  $('device_rows').innerHTML=T.row('MPD',s.mpd_status)+T.row('Server',s.mpd_host ? s.mpd_host+':'+s.mpd_port : 'Not configured')+
    T.row('Playback volume',s.mpd_volume>=0 ? s.mpd_volume+'%' : 'Mixer unavailable')+
    T.row('WiFi',s.wifi)+T.row('IP',s.ip)+T.row('SD card',s.sd_ready ? T.bytes(s.sd_used)+' / '+T.bytes(s.sd_total) : 'Missing')+
    T.row('Uptime',T.time(s.uptime_s))+T.row('Free heap',T.bytes(s.free_heap))+T.row('Free PSRAM',T.bytes(s.free_psram))+T.row('Last reset',T.reset(s.reset_reason));
  var usb=s.usb_network || {};
  $('device_rows').innerHTML+=T.row('USB-C network',usb.ready ? (usb.connected ? 'Connected · ' : 'Ready · ')+usb.ip : 'Inactive');
  var power=T.power(p,c);
  $('charge_state').textContent=power.state; $('charge_state').className='micro '+power.cls;
  $('power_rows').innerHTML=T.row('Battery voltage',power.voltage)+T.row('Current flow',power.flow,power.cls)+
    T.row('Charger output',p.chg_en===undefined ? 'Unavailable' : power.enabled ? 'Enabled' : 'Disabled')+
    T.row('USB 5 V output',p.usb5v_en===undefined ? 'Unavailable' : p.usb5v_en ? 'On' : 'Off');
  setVal('charge_en',c.charge_enabled); setVal('qc_high',c.qc_high ? 1 : 0);
  $('mpd_status').textContent=s.mpd_status || 'Not configured';
  $('btn_sync').disabled=!s.mpd_connected || s.mpd_syncing;
  var catalog=s.catalog || {};
  renderArtwork(s,catalog);
  $('library_rows').innerHTML=T.row('Albums',catalog.albums || 0)+T.row('Tracks',catalog.tracks || 0)+T.row('Storage',s.storage_root);
  $('sync_hint').textContent=s.mpd_syncing ? 'Importing the MPD library…' : s.mpd_connected ? 'Library ready. Resync after changing stored playlists; database changes refresh automatically.' : 'Enter your MPD server address. Music plays on the server’s audio output.';
  $('fw_rows').innerHTML=T.row('Firmware',s.firmware)+T.row('Build',s.build)+T.row('OTA slot',s.ota_partition)+T.row('USB mode',T.usb(s.usb_mode));
  $('clock_hint').textContent=(s.local_time || '—')+(s.rtc ? ' · RTC' : '');
  $('screen_state').textContent=s.screen_on===undefined ? '—' : s.screen_on ? 'Screen on' : 'Screen off';
  configFields.forEach(function (id) { setVal(id,c[id] === undefined ? (id==='volume_steps' ? 16 : ['screen_off_minutes','pagination_size'].includes(id) ? 0 : '') : c[id]); });
  $('vol_v').textContent=value('volume'); $('bri_v').textContent=value('brightness');
}
var refresh=T.poll(render,function (e) { $('chips').innerHTML=T.chip('Device',e.message,'bad'); });
function postConfig(body) { return T.api('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}); }
function saveConfig() {
  var body={}, snapshot={};
  configFields.forEach(function (id) {
    if (!dirty[id]) return;
    snapshot[id]=value(id); body[id]=$(id).type==='checkbox' ? value(id) : ['ap_ssid','usb_net_ip','usb_net_client_ip'].includes(id) ? value(id) : +value(id);
  });
  if (!Object.keys(body).length) { toast('No changes'); return; }
  postConfig(body).then(function (t) { saved(snapshot); toast(t); refresh(); }).catch(function(e) { toast(e.message,true); });
}
function saveCharge() {
  var snapshot={charge_en:value('charge_en'),qc_high:value('qc_high')};
  T.api('/api/charge?en='+(snapshot.charge_en ? 1 : 0)+'&qc='+snapshot.qc_high,{method:'POST'})
    .then(function () { saved(snapshot); toast('Charging settings saved'); refresh(); }).catch(function(e) { toast(e.message,true); });
}
function syncTime() {
  T.api('/api/time',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:Math.floor(Date.now()/1000)})})
    .then(function(t) { toast(t); refresh(); }).catch(function(e) { toast(e.message,true); });
}
function reboot() {
  T.api('/api/reboot',{method:'POST'}).then(function() { toast('Restarting…'); }).catch(function(e) { toast(e.message,true); });
}
function sync_media() {
  T.api('/api/mpd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action:'rescan'})})
    .then(function() { toast('MPD resync requested'); refresh(); }).catch(function(e) { toast(e.message,true); });
}
var mpdDirty=false;
['mpd_host','mpd_port','mpd_password','mpd_clear_password'].forEach(function(id) { $(id).addEventListener('input',function() { mpdDirty=true; }); });
T.json('/api/mpd').then(function(c) {
  if(!mpdDirty) { $('mpd_host').value=c.host || ''; $('mpd_port').value=c.port || 6600; }
}).catch(function(e) { toast(e.message,true); });
$('mpd_form').addEventListener('submit',async function(e) {
  e.preventDefault();
  var body={host:$('mpd_host').value.trim(),port:Number($('mpd_port').value)};
  if($('mpd_clear_password').checked) body.password='';
  else if($('mpd_password').value) body.password=$('mpd_password').value;
  $('mpd_save').disabled=true;
  try {
    await T.api('/api/mpd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    $('mpd_password').value=''; $('mpd_clear_password').checked=false;
    toast('MPD settings saved. Connecting…'); refresh();
  } catch(error) { toast(error.message,true); }
  finally { $('mpd_save').disabled=false; }
});
function upload(path,file,prog) {
  if (!file) { toast('Choose a file first',true); return; }
  var x=new XMLHttpRequest(); x.open('POST',path); x.setRequestHeader('Content-Type','application/octet-stream');
  prog.style.display='block'; prog.firstElementChild.style.width='0%';
  x.upload.onprogress=function(e) { if(e.lengthComputable) prog.firstElementChild.style.width=(e.loaded/e.total*100)+'%'; };
  x.onload=function() { toast(x.responseText || 'Upload complete',x.status<200 || x.status>=300); setTimeout(refresh,5000); };
  x.onerror=function() { toast('Upload failed',true); }; x.send(file);
}
function uploadFirmware() { upload('/api/ota',$('fw_file').files[0],$('fw_prog')); }
async function loadLog() {
  var cursor=0,text='';
  try {
    for(var i=0;i<64;i++) {
      var r=await fetch('/api/log?since='+cursor,{cache:'no-store'});
      if(!r.ok) throw new Error('HTTP '+r.status);
      var chunk=await r.text(), next=Number(r.headers.get('X-Log-Seq'));
      text=(text+chunk).slice(-30000);
      if(!chunk || !next || next===cursor) break;
      cursor=next;
    }
    $('log').textContent=text || 'No log entries';
  } catch(e) { $('log').textContent=e.message; }
}
