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
.warning{padding:12px;background:#fff4d9;border-radius:9px;font-size:14px}.status{font-size:14px;overflow-wrap:anywhere}#message{min-height:24px}#message.error,#enrollment.error{color:#a22723}
[hidden]{display:none!important}details{margin:18px 0}summary{cursor:pointer}.mode{font-weight:600}#link{font-size:14px}
</style><main><h1>TrainMeet TMBox</h1><p class="muted">Lokal testpanel · ESP8266</p>
<p id="login">Ansluter till boxen… Ingen kod behövs.</p>
<div id="controls" hidden><section class="panel"><div id="identity"></div><div id="link" class="muted"></div>
<p id="mode" class="mode">Webbtest avstängt</p><p class="warning">Knapparna påverkar den anslutna träffen på riktigt. Använd en testträff. Stationen tilldelas i TrainMeet Server.</p>
<div class="row"><button id="start" class="primary">Aktivera webbtest</button><button id="stop">Avsluta webbtest</button></div></section>
<button id="language" type="button">Språk / Language (#)</button>
<section class="box" aria-label="Virtuell TMBox"><div class="bezel"><div class="lcd" role="status" aria-live="polite"><div id="line1">                </div><div id="line2">                </div></div></div>
<div id="keys" class="keys" aria-label="Knappsats"></div><div class="brand">TRAINMEET · TMBOX</div></section>
<p id="status" class="status"></p><p id="hardware" class="status muted"></p>
<p class="muted">Boxen hittar automatiskt TrainMeet Server på samma nät. Visa enhetskoden för administratören, som tilldelar stationen på servern.</p>
<button id="logout">Koppla från telefonen</button><p class="muted"><small>Webbtest stängs av vid omstart, nätavbrott eller tio minuters inaktivitet. Ingen Cloud-anslutning behövs.</small></p></div>
<p id="message" role="alert"></p></main><script>
'use strict';
const $=id=>document.getElementById(id);let state=null,busy=false,online=false,closed=false,lastReply=0,entryContext=null,entryValue="",generation=0,pollController=null;
const buttons=[...'123A456B789C*0#D'].map(key=>{const b=document.createElement('button');b.textContent=key;b.type='button';b.disabled=true;b.setAttribute('aria-label','Tangent '+key);b.onclick=()=>press(key);$('keys').append(b);return b;});
function allowed(key){return !!state&&(state.allowedKeys.includes(key)||(state.serverDriven&&entryValue&&['#','*','B'].includes(key)))&&!(state.localEntry&&!state.serverDriven&&key==='#'&&!entryValue)&&!(state.localEntry&&/^[0-9]$/.test(key)&&entryValue.length>=5);}
function drawKeys(){for(const b of buttons)b.disabled=busy||!online||!state||!state.webTest||!state.ready||!allowed(b.textContent);}
function clearEntry(){entryContext=null;entryValue='';}
function drawEntry(){if(state&&state.localEntry&&state.webTest&&entryContext!==null){if(state.serverDriven){const lines=entryValue?state.entryLines:[state.line1,state.line2];$('line1').textContent=entryValue?[...lines[0]].slice(0,5).join('')+entryValue.padEnd(5,'_')+[...lines[0]].slice(10).join(''):lines[0];$('line2').textContent=lines[1];}else $('line2').textContent=((state.entryLabel||'Tag: ')+entryValue).padEnd(16,' ').slice(0,16);}}
function drawControls(){for(const el of document.querySelectorAll('input,button'))el.disabled=busy;
 $('start').disabled=busy||!online||!state||state.webTest||!state.canStart;$('stop').disabled=busy||!online||!state||!state.webTest;
 $('language').disabled=busy||!online||!state||!state.webTest||!state.ready||!state.languageAvailable||state.languageMenu;drawKeys();}
function show(s){
 if(!s.connected||!s.webTest||!s.localEntry)clearEntry();
 else if(entryContext!==s.entryContext){entryContext=s.entryContext;entryValue=s.entryValue||'';}
 state=s;online=true;lastReply=Date.now();$('login').hidden=true;$('controls').hidden=false;
 $('identity').textContent=s.deviceCode+' · '+s.firmware;$('link').textContent='Box: '+s.ip+' · Server: '+(s.server||'söker automatiskt')+' · Panel: '+(s.panel||'ej tilldelad');
 $('line1').textContent=s.line1;$('line2').textContent=s.line2;$('mode').textContent=s.webTest?'Webbtest aktivt':'Webbtest avstängt';
 $('status').textContent=!s.connected?'Söker eller återansluter till lokal server.':!s.panel?'Ansluten. Tilldela stationen i serverns admin.':s.waiting?'Väntar på serverkvittens…':!s.fresh?'Väntar på aktuell skärmbild från servern.':s.entryNeedsUpdate?'Uppdatera TrainMeet Server för lokal inmatning. Inga siffror skickas till servern.':s.webTest&&s.localEntry?'Skriv tågnumret. # bekräftar, * avbryter. Siffrorna stannar i telefonen tills du bekräftar.':s.webTest?'Serverns tillåtna tangenter är aktiva.':'Aktivera webbtest för att använda knapparna.';
 $('hardware').textContent='Display: '+(s.lcd?'ansluten':'saknas')+' · Knappsats: '+(s.keypad?'ansluten':'saknas');
 drawEntry();drawControls();}
function lost(){online=false;clearEntry();drawControls();$('status').textContent='Kontakten med boxen är bruten. Knapparna är spärrade.';}
function signedOut(){state=null;online=false;clearEntry();$('login').hidden=false;$('controls').hidden=true;drawControls();}
function feedback(id,message,error){$(id).textContent=message;$(id).classList.toggle('error',!!error);}
function handleError(e,id){feedback(id,e.message,true);if(e.status===401){feedback('message','Öppna webbpanelen igen. '+e.message,true);signedOut();}else if(!e.status)lost();}
async function request(path,body,timeout=5000,isPoll=false){const controller=new AbortController();let timedOut=false;if(isPoll)pollController=controller;
 const timer=setTimeout(()=>{timedOut=true;controller.abort();},timeout);
 try{const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),cache:'no-store',signal:controller.signal});
 let value;try{value=await r.json();}catch(e){if(timedOut)throw e;const invalid=Error('Boxen gav ett oläsbart svar. Läs aktuellt läge och försök igen.');invalid.status=r.status;throw invalid;}
 if(!r.ok){const e=Error(value.error||'Kunde inte utföra kommandot.');e.status=r.status;throw e;}return value;
 }catch(e){if(e.status)throw e;throw Error(timedOut?'Boxen svarade inte i tid. Ingen automatisk omsändning görs; kontrollera anslutningen och försök igen.':'Kunde inte nå boxen. Kontrollera telefonens Wi-Fi. Ingen automatisk omsändning görs.');}finally{clearTimeout(timer);if(pollController===controller)pollController=null;}}
async function refresh(){const version=generation;try{const s=await request('/api/status',undefined,5000,true);if(version===generation&&!busy)show(s);}catch(e){if(version===generation&&!busy){if(e.status===401){signedOut();await action("/api/session",{});}else if(!e.status)lost();}}}
function begin(message){if(busy)return false;busy=true;++generation;if(pollController)pollController.abort();feedback('message',message,false);drawControls();return true;}
function finish(){busy=false;drawControls();}
async function action(path,body){if(!begin('Kommandot skickas…'))return null;try{const result=await request(path,body);if(path==='/api/logout'){closed=true;signedOut();$('login').textContent='Telefonen är frånkopplad. Ladda om sidan för att ansluta igen.';}else show(result);feedback('message','',false);return result;}catch(e){handleError(e,'message');return null;}finally{finish();}}
async function press(key){if(!state||!state.ready||!state.webTest||!online||busy||!allowed(key))return;
 if(state.localEntry&&/^[0-9]$/.test(key)){entryValue+=key;drawEntry();drawKeys();return;}
 if(state.serverDriven&&entryValue){if(key==='*'){clearEntry();entryContext=state.entryContext;drawEntry();drawKeys();return;}if(key==='B'){entryValue=entryValue.slice(0,-1);drawEntry();drawKeys();return;}if(key==='A')clearEntry();else if(key!=='#')return;}
 const command={key,session:state.session,revision:state.revision};
 if(state.localEntry&&key==='#'&&(!state.serverDriven||entryValue)){command.train_number=entryValue;command.entryContext=entryContext;}
 if(key==='*')clearEntry();
 state.ready=false;const result=await action('/api/key',command);
 if(result&&command.train_number!==undefined&&result.serverDriven){clearEntry();entryContext=result.entryContext;drawEntry();drawKeys();}}
$('start').onclick=()=>action('/api/test',{enabled:true});$('stop').onclick=()=>action('/api/test',{enabled:false});
$('logout').onclick=()=>action('/api/logout',{});
$('language').onclick=()=>press('#');
// No overlapping polling or retries of commands. A timer only reads status.
async function poll(){if(!closed&&!busy&&!document.hidden)await refresh();setTimeout(poll,1000);}poll();
setInterval(()=>{if(!busy&&online&&Date.now()-lastReply>3000)lost();},500);
document.addEventListener('visibilitychange',()=>{if(document.hidden)lost();});
</script></html>)TMBOX";
