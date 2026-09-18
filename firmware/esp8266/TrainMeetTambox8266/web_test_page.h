#pragma once
#include <Arduino.h>

// Kept in flash, with no CDN, fonts, framework or external network requests.
const char WEB_TEST_PAGE[] PROGMEM = R"TMBOX(<!doctype html>
<html lang="sv"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TrainMeet TMBox · Webbtest</title><style>
*{box-sizing:border-box}body{margin:0;background:#f3f4f6;color:#1d2430;font:16px/1.5 system-ui,sans-serif}
main{max-width:430px;margin:auto;padding:24px 18px}h1{font-size:24px;margin:0}h2{font-size:19px;margin:0 0 12px}
p{margin:10px 0}small,.muted{color:#596475}button,input{font:inherit;border:1px solid #c5cbd3;border-radius:10px;padding:12px}
button{cursor:pointer;background:white;color:#172d48}button:disabled{opacity:.38;cursor:default}button.primary{background:#2157bd;color:white;border-color:#2157bd}
input{width:100%;background:white;margin:8px 0}section{margin:18px 0}.panel{background:white;border:1px solid #d6dae0;border-radius:14px;padding:18px}
.box{padding:22px;border-radius:22px;background:#c8adb2;box-shadow:inset 0 0 0 1px #a4838b}.bezel{padding:13px;background:#202329;border-radius:9px}
.lcd{background:#153c94;color:#e6f2ff;border-radius:3px;padding:12px 8px;font:clamp(14px,4.7vw,19px)/1.8 ui-monospace,monospace;white-space:pre;letter-spacing:.03em}
.keys{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-top:22px}.keys button{font-size:22px;font-weight:600;min-height:55px;padding:8px;touch-action:manipulation}
.keys button:nth-child(4n){background:#2257ba;color:white}.keys button:active:not(:disabled){transform:translateY(2px);background:#dce7fb;color:#153c94}
.brand{font-size:12px;letter-spacing:.16em;text-align:center;margin-top:17px;color:#493840}.row{display:flex;gap:8px;flex-wrap:wrap}.row>*{flex:1}
.warning{padding:12px;background:#fff4d9;border-radius:9px;font-size:14px}.status{font-size:14px;overflow-wrap:anywhere}#message{min-height:24px;color:#a22723}
[hidden]{display:none!important}details{margin:18px 0}summary{cursor:pointer}.mode{font-weight:600}#link{font-size:14px}
</style><main><h1>TrainMeet TMBox</h1><p class="muted">Lokal testpanel · ESP8266</p>
<section id="login" class="panel"><h2>Parkoppla telefonen</h2><p>Ange den sexsiffriga webbtestkoden från kortets seriella monitor (115200 baud).</p>
<form id="pair"><label for="pin">Webbtestkod</label><input id="pin" inputmode="numeric" pattern="[0-9]{6}" maxlength="6" autocomplete="off" required>
<button class="primary" type="submit">Anslut till boxen</button></form><small>Koden byts när kortet startas om.</small></section>
<div id="controls" hidden><section class="panel"><div id="identity"></div><div id="link" class="muted"></div>
<p id="mode" class="mode">Webbtest avstängt</p><p class="warning">Knapparna påverkar den anslutna träffen på riktigt. Använd en testträff. Stationen tilldelas i TrainMeet Server.</p>
<div class="row"><button id="start" class="primary">Aktivera webbtest</button><button id="stop">Avsluta webbtest</button></div></section>
<section class="box" aria-label="Virtuell TMBox"><div class="bezel"><div class="lcd" role="status" aria-live="polite"><div id="line1">                </div><div id="line2">                </div></div></div>
<div id="keys" class="keys" aria-label="Knappsats"></div><div class="brand">TRAINMEET · TMBOX</div></section>
<p id="status" class="status"></p><p id="hardware" class="status muted"></p>
<section class="panel"><h2>Anslut till träffens server</h2><form id="enroll"><label for="servercode">Lokal anslutningskod</label>
<input id="servercode" inputmode="numeric" maxlength="7" autocomplete="off" placeholder="123456" required>
<button type="submit">Bekräfta kod hos servern</button></form><p id="enrollment" class="status muted">Använd koden från lokal TrainMeet Server, inte Cloud-koden. Administratören väljer station åt boxen.</p></section>
<details class="panel"><summary>Serveranslutning</summary><p>Tom adress söker automatiskt på samma lokala nät. Ange en server om flera hittas.</p>
<form id="settings"><label for="server">TrainMeet Server · IP eller namn</label><input id="server" maxlength="63" placeholder="Automatisk upptäckt">
<label for="port">MQTT-port</label><input id="port" type="number" min="1" max="65535" value="1883" required>
<label for="httpport">Serverns webbport (för anslutningskoden)</label><input id="httpport" type="number" min="1" max="65535" value="8787" required>
<button type="submit">Spara och återanslut</button></form><p class="muted">Detta ändrar serveradressen, inte boxens egen IP-adress.</p></details>
<button id="logout">Koppla från telefonen</button><p class="muted"><small>Webbtest stängs av vid omstart, nätavbrott eller tio minuters inaktivitet. Ingen Cloud-anslutning behövs.</small></p></div>
<p id="message" role="alert"></p></main><script>
'use strict';
const $=id=>document.getElementById(id);let state=null,busy=false,online=false,lastReply=0,configured=false;
const buttons=[...'123A456B789C*0#D'].map(key=>{const b=document.createElement('button');b.textContent=key;b.type='button';b.disabled=true;b.setAttribute('aria-label','Tangent '+key);b.onclick=()=>press(key);$('keys').append(b);return b;});
function drawKeys(){for(const b of buttons)b.disabled=busy||!online||!state||!state.webTest||!state.ready||!state.allowedKeys.includes(b.textContent);}
function show(s){state=s;online=true;lastReply=Date.now();$('login').hidden=true;$('controls').hidden=false;
 $('identity').textContent=s.deviceCode+' · '+s.firmware;$('link').textContent='Box: '+s.ip+' · Server: '+(s.server||'söker automatiskt')+' · Panel: '+(s.panel||'ej tilldelad');
 $('line1').textContent=s.line1;$('line2').textContent=s.line2;$('mode').textContent=s.webTest?'Webbtest aktivt':'Webbtest avstängt';
 $('start').disabled=busy||s.webTest||!s.canStart;$('stop').disabled=busy||!s.webTest;
 $('status').textContent=!s.connected?'Söker eller återansluter till lokal server.':!s.panel?'Ansluten. Tilldela stationen i serverns admin.':s.waiting?'Väntar på serverkvittens…':!s.fresh?'Väntar på aktuell skärmbild från servern.':s.webTest?'Serverns tillåtna tangenter är aktiva.':'Aktivera webbtest för att använda knapparna.';
 $('hardware').textContent='Display: '+(s.lcd?'ansluten':'saknas')+' · Knappsats: '+(s.keypad?'ansluten':'saknas');
 if(!configured){$('server').value=s.configuredServer;$('port').value=s.configuredPort;$('httpport').value=s.httpPort;configured=true;}drawKeys();}
function lost(){online=false;drawKeys();$('start').disabled=true;$('stop').disabled=true;$('status').textContent='Kontakten med boxen är bruten. Knapparna är spärrade.';}
async function request(path,body){const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),cache:'no-store',signal:AbortSignal.timeout(5000)});
 const value=await r.json();if(!r.ok){if(r.status===401){state=null;online=false;$('login').hidden=false;$('controls').hidden=true;drawKeys();}throw Error(value.error||'Kunde inte utföra kommandot.');}return value;}
async function refresh(){try{show(await request('/api/status'));}catch(e){lost();} }
async function action(path,body){if(busy)return;busy=true;drawKeys();$('message').textContent='';try{const result=await request(path,body);await refresh();return result;}catch(e){$('message').textContent=e.message;lost();}finally{busy=false;drawKeys();}}
async function press(key){if(!state||!state.ready||!state.webTest||!online||busy)return;
 const command={key,session:state.session,revision:state.revision};state.ready=false;await action('/api/key',command);}
$('pair').onsubmit=async e=>{e.preventDefault();await action('/api/login',{pin:$('pin').value});$('pin').value='';};
$('start').onclick=()=>action('/api/test',{enabled:true});$('stop').onclick=()=>action('/api/test',{enabled:false});
$('logout').onclick=async()=>{await action('/api/logout',{});state=null;online=false;configured=false;$('login').hidden=false;$('controls').hidden=true;drawKeys();};
$('settings').onsubmit=e=>{e.preventDefault();if(confirm('Byta server och avsluta webbtestet?'))action('/api/server',{host:$('server').value.trim(),port:Number($('port').value),httpPort:Number($('httpport').value)});};
$('enroll').onsubmit=async e=>{e.preventDefault();const result=await action('/api/enroll',{code:$('servercode').value});$('servercode').value='';if(result&&result.accepted)$('enrollment').textContent=result.awaitingStation?'Koden godkänd. Administratören tilldelar nu stationen i TrainMeet Server.':'Koden godkänd. Boxens befintliga stationstilldelning är kvar.';};
// No overlapping polling or retries of commands. A timer only reads status.
async function poll(){if(!busy&&!document.hidden)await refresh();setTimeout(poll,1000);}poll();
setInterval(()=>{if(online&&Date.now()-lastReply>3000)lost();},500);
document.addEventListener('visibilitychange',()=>{if(document.hidden)lost();});
</script></html>)TMBOX";
