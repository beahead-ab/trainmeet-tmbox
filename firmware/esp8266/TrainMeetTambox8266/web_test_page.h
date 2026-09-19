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
<section id="login" class="panel"><h2>Parkoppla telefonen</h2><p>Ange den sexsiffriga webbtestkoden från kortets seriella monitor (115200 baud).</p>
<form id="pair"><label for="pin">Webbtestkod</label><input id="pin" inputmode="numeric" pattern="[0-9]{6}" maxlength="6" autocomplete="off" required>
<button class="primary" type="submit">Anslut till boxen</button></form><small>Koden byts när kortet startas om.</small></section>
<div id="controls" hidden><section class="panel"><div id="identity"></div><div id="link" class="muted"></div>
<p id="mode" class="mode">Webbtest avstängt</p><p class="warning">Knapparna påverkar den anslutna träffen på riktigt. Använd en testträff. Stationen tilldelas i TrainMeet Server.</p>
<div class="row"><button id="start" class="primary">Aktivera webbtest</button><button id="stop">Avsluta webbtest</button></div></section>
<section class="box" aria-label="Virtuell TMBox"><div class="bezel"><div class="lcd" role="status" aria-live="polite"><div id="line1">                </div><div id="line2">                </div></div></div>
<div id="keys" class="keys" aria-label="Knappsats"></div><div class="brand">TRAINMEET · TMBOX</div></section>
<p id="status" class="status"></p><p id="hardware" class="status muted"></p>
<section class="panel"><h2>Anslut till träffens server</h2><form id="enroll">
<label for="server">TrainMeet Server · IP eller namn</label><input id="server" maxlength="95" autocomplete="off" placeholder="192.168.0.160 eller automatisk upptäckt">
<p class="muted"><small>Tom adress söker automatiskt på samma lokala nät. Du kan också klistra in http://192.168.0.160:8787/.</small></p>
<label for="servercode">Lokal anslutningskod</label><input id="servercode" inputmode="numeric" maxlength="7" autocomplete="off" placeholder="123456" required>
<p class="muted"><small>Använd koden från lokal TrainMeet Server, inte Cloud-koden. Administratören väljer station åt boxen.</small></p>
<button id="connect" class="primary" type="submit">Anslut till servern</button><p id="enrollment" class="status" role="alert" aria-live="polite" aria-atomic="true"></p>
<details><summary>Avancerade serverinställningar</summary>
<label for="port">MQTT-port</label><input id="port" type="number" min="1" max="65535" value="1883" required>
<label for="httpport">Serverns webbport (för anslutningskoden)</label><input id="httpport" type="number" min="1" max="65535" value="8787" required>
<button id="saveaddress" type="button">Spara adress utan kod</button><p class="muted"><small>Detta ändrar serveradressen, inte boxens egen IP-adress. Byte av server avslutar webbtestet.</small></p></details></form></section>
<button id="logout">Koppla från telefonen</button><p class="muted"><small>Webbtest stängs av vid omstart, nätavbrott eller tio minuters inaktivitet. Ingen Cloud-anslutning behövs.</small></p></div>
<p id="message" role="alert"></p></main><script>
'use strict';
const $=id=>document.getElementById(id);let state=null,busy=false,online=false,lastReply=0,configured=false,settingsEdited=false,generation=0,pollController=null;
const buttons=[...'123A456B789C*0#D'].map(key=>{const b=document.createElement('button');b.textContent=key;b.type='button';b.disabled=true;b.setAttribute('aria-label','Tangent '+key);b.onclick=()=>press(key);$('keys').append(b);return b;});
function drawKeys(){for(const b of buttons)b.disabled=busy||!online||!state||!state.webTest||!state.ready||!state.allowedKeys.includes(b.textContent);}
function drawControls(){for(const el of document.querySelectorAll('input,button'))el.disabled=busy;
 $('start').disabled=busy||!online||!state||state.webTest||!state.canStart;$('stop').disabled=busy||!online||!state||!state.webTest;drawKeys();}
function settingsOf(s){return {host:s.configuredServer,port:Number(s.configuredPort),httpPort:Number(s.httpPort)};}
function sameSettings(a,b){return a.host===b.host&&a.port===b.port&&a.httpPort===b.httpPort;}
function fillSettings(s,force){if(force||(!configured&&!settingsEdited)){const v=settingsOf(s);$('server').value=v.host;$('port').value=v.port;$('httpport').value=v.httpPort;}configured=true;if(force)settingsEdited=false;}
function show(s){state=s;online=true;lastReply=Date.now();$('login').hidden=true;$('controls').hidden=false;
 $('identity').textContent=s.deviceCode+' · '+s.firmware;$('link').textContent='Box: '+s.ip+' · Server: '+(s.server||'söker automatiskt')+' · Panel: '+(s.panel||'ej tilldelad');
 $('line1').textContent=s.line1;$('line2').textContent=s.line2;$('mode').textContent=s.webTest?'Webbtest aktivt':'Webbtest avstängt';
 $('status').textContent=!s.connected?'Söker eller återansluter till lokal server.':!s.panel?'Ansluten. Tilldela stationen i serverns admin.':s.waiting?'Väntar på serverkvittens…':!s.fresh?'Väntar på aktuell skärmbild från servern.':s.webTest?'Serverns tillåtna tangenter är aktiva.':'Aktivera webbtest för att använda knapparna.';
 $('hardware').textContent='Display: '+(s.lcd?'ansluten':'saknas')+' · Knappsats: '+(s.keypad?'ansluten':'saknas');
 fillSettings(s,false);drawControls();}
function lost(){online=false;drawControls();$('status').textContent='Kontakten med boxen är bruten. Knapparna är spärrade.';}
function signedOut(){state=null;online=false;configured=false;settingsEdited=false;$('login').hidden=false;$('controls').hidden=true;drawControls();}
function feedback(id,message,error){$(id).textContent=message;$(id).classList.toggle('error',!!error);}
function handleError(e,id){feedback(id,e.message,true);if(e.status===401){feedback('message','Telefonen behöver parkopplas med boxen igen. '+e.message,true);signedOut();}else if(!e.status)lost();}
async function request(path,body,timeout=5000,isPoll=false){const controller=new AbortController();let timedOut=false;if(isPoll)pollController=controller;
 const timer=setTimeout(()=>{timedOut=true;controller.abort();},timeout);
 try{const r=await fetch(path,{method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'Content-Type':'application/json'},body:body===undefined?undefined:JSON.stringify(body),cache:'no-store',signal:controller.signal});
 let value;try{value=await r.json();}catch(e){if(timedOut)throw e;const invalid=Error('Boxen gav ett oläsbart svar. Läs aktuellt läge och försök igen.');invalid.status=r.status;throw invalid;}
 if(!r.ok){const e=Error(value.error||'Kunde inte utföra kommandot.');e.status=r.status;throw e;}return value;
 }catch(e){if(e.status)throw e;throw Error(timedOut?'Boxen svarade inte i tid. Ingen automatisk omsändning görs; kontrollera anslutningen och försök igen.':'Kunde inte nå boxen. Kontrollera telefonens Wi-Fi. Ingen automatisk omsändning görs.');}finally{clearTimeout(timer);if(pollController===controller)pollController=null;}}
async function refresh(){const version=generation;try{const s=await request('/api/status',undefined,5000,true);if(version===generation&&!busy)show(s);}catch(e){if(version===generation&&!busy){if(e.status===401)signedOut();else if(!e.status)lost();}}}
function begin(message){if(busy)return false;busy=true;++generation;if(pollController)pollController.abort();feedback('message',message,false);drawControls();return true;}
function finish(){busy=false;drawControls();}
async function action(path,body){if(!begin('Kommandot skickas…'))return null;try{const result=await request(path,body);if(path==='/api/logout')signedOut();else show(result);feedback('message','',false);return result;}catch(e){handleError(e,'message');return null;}finally{finish();}}
async function press(key){if(!state||!state.ready||!state.webTest||!online||busy||!state.allowedKeys.includes(key))return;
 const command={key,session:state.session,revision:state.revision};state.ready=false;await action('/api/key',command);}
$('pair').onsubmit=async e=>{e.preventDefault();if(await action('/api/login',{pin:$('pin').value}))$('pin').value='';};
$('start').onclick=()=>action('/api/test',{enabled:true});$('stop').onclick=()=>action('/api/test',{enabled:false});
$('logout').onclick=()=>action('/api/logout',{});
for(const id of ['server','port','httpport'])$(id).addEventListener('input',()=>{settingsEdited=true;});
async function waitForServer(target){const deadline=Date.now()+25000;
 while(Date.now()<deadline){const s=await request('/api/status',undefined,Math.min(5000,deadline-Date.now()));show(s);
  if(!sameSettings(settingsOf(s),target)){const e=Error('Serverinställningarna har ändrats. Kontrollera adressen och försök igen. Ingen kod skickades.');e.status=409;throw e;}
  if(s.connected&&s.enrollmentReady)return;
  feedback('enrollment',s.connected?'Serveranslutning finns. Väntar på en ny bekräftelse från TrainMeet Server…':'Ansluter till servern… Koden skickas när servern har bekräftat boxen.',false);
  await new Promise(resolve=>setTimeout(resolve,Math.min(500,Math.max(0,deadline-Date.now()))));}
 const e=Error('Servern bekräftade inte boxen inom 25 sekunder. Kontrollera IP-adress, MQTT-port och att TrainMeet Server körs. Ingen kod skickades. Försök igen.');e.status=408;throw e;}
async function connectServer(addressOnly){if(busy)return;
 const entered={host:$('server').value.trim(),port:Number($('port').value),httpPort:Number($('httpport').value)},code=$('servercode').value;
 if(!Number.isInteger(entered.port)||entered.port<1||entered.port>65535||!Number.isInteger(entered.httpPort)||entered.httpPort<1||entered.httpPort>65535){feedback('enrollment','Ange portar mellan 1 och 65535.',true);return;}
 if(!addressOnly&&!code.trim()){feedback('enrollment','Ange den lokala serverns anslutningskod.',true);return;}
 if(!begin('Serveranslutning pågår…'))return;
 feedback('enrollment','Kontrollerar serverinställningarna…',false);let codeSent=false;
 try{let target=entered;
  if(!state||!sameSettings(entered,settingsOf(state))){feedback('enrollment','Sparar serveradressen… Webbtestet avslutas vid serverbyte.',false);const saved=await request('/api/server',entered);show(saved);fillSettings(saved,true);target=settingsOf(saved);}
  if(addressOnly){feedback('enrollment','Serveradressen är sparad. Ingen anslutningskod har skickats eller godkänts.',false);return;}
  feedback('enrollment','Ansluter till servern och väntar på bekräftelse…',false);await waitForServer(target);
  feedback('enrollment','Servern har svarat. Kontrollerar anslutningskoden…',false);
  // The code POST is sent once only, even if the response is lost.
  if(state){state.ready=false;state.webTest=false;}codeSent=true;const result=await request('/api/enroll',{code});
  if(result.accepted!==true){const e=Error('Servern bekräftade inte anslutningskoden. Kontrollera servern före ett nytt försök.');e.status=502;throw e;}
  $('servercode').value='';feedback('enrollment',result.awaitingStation?'Koden godkänd. Administratören tilldelar nu stationen i TrainMeet Server.':'Koden godkänd. Boxens befintliga stationstilldelning är kvar.',false);
 }catch(e){if(codeSent&&!e.status)e.message+=' Kodförsöket kan ha behandlats. Kontrollera servern före ett nytt försök.';handleError(e,'enrollment');}finally{if(!$('message').classList.contains('error'))feedback('message','',false);finish();}}
$('enroll').onsubmit=e=>{e.preventDefault();return connectServer(false);};$('saveaddress').onclick=()=>connectServer(true);
// No overlapping polling or retries of commands. A timer only reads status.
async function poll(){if(!busy&&!document.hidden)await refresh();setTimeout(poll,1000);}poll();
setInterval(()=>{if(!busy&&online&&Date.now()-lastReply>3000)lost();},500);
document.addEventListener('visibilitychange',()=>{if(document.hidden)lost();});
</script></html>)TMBOX";
