import { profiles, steps, availableRelease, safeServerURL, canInstall, verifyImage } from './model.js';

const repo = 'https://github.com/beahead-ab/trainmeet-tmbox/blob/main/';
const state = { step: 0, id: '', acknowledged: false, installed: false, server: '', checks: new Set(), finished: false };
let catalog = null;
let generation = 0;
let blobURLs = [];
const content = document.querySelector('#step-content');
const escape = value => String(value).replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
const boxPicture = tall => `<div class="box-picture" aria-hidden="true"><div class="lcd ${tall ? 'tall' : ''}">TRAINMEET<br>TMBOX${tall ? '<br>DIN STATION' : ''}</div><div class="keys">${'<span></span>'.repeat(16)}</div></div>`;
const heading = (title, lead) => `<h2 id="step-title" tabindex="-1">${title}</h2><p class="lede">${lead}</p>`;

function hardware() {
  const selected = profiles[state.id];
  return heading('Vilken box har du?', 'Läs märkningen på kretskortet och kontrollera hur knappsatsen är kopplad. Chipet ensamt räcker inte för att välja rätt program.') +
    `<div class="profile-grid" role="radiogroup" aria-label="Boxmodell">${Object.entries(profiles).map(([id, profile]) => `
      <label class="profile"><input type="radio" name="profile" value="${id}" ${state.id === id ? 'checked' : ''}>
        ${boxPicture(id === 'esp32-s3')}
        <span class="profile-copy"><strong>${profile.title}</strong><span class="description">${profile.screen}<br>${profile.keypad}</span></span>
      </label>`).join('')}</div>
    ${selected ? `<section class="card"><div class="profile-heading"><h3>Kontrollera din koppling</h3><span class="tag">${selected.chip}</span></div>
      <p class="status">${selected.board}</p><p><code>${selected.wiring}</code></p>
      <dl class="inline-list">${selected.connections.map(([name, value]) => `<dt>${name}</dt><dd>${value}</dd>`).join('')}</dl>
      <a href="${repo + selected.docs}" target="_blank" rel="noopener noreferrer">Se hela kopplingsanvisningen ↗</a></section>` : ''}
    <div class="notice warning"><strong>5 V får aldrig nå kortets GPIO.</strong> Stäng av strömmen när du kopplar. En 5 V LCD behöver dubbelriktad I²C-nivåomvandling och gemensam jord.</div>
    <details><summary>Annat kort eller annan I²C-modul?</summary><p>Den här guiden erbjuder bara dessa två exakta profiler. En vanlig ESP32 är inte en ESP32-S3. MCP23017 eller en annan aktiv knappmodul är inte PCF8574. Välj inte en nästan likadan profil; kontrollera hårdvaran först.</p><p>NodeMCU-knappsatsens ordning och I²C-adresser måste stämma med anvisningen. De kan inte identifieras automatiskt av USB-installationen.</p></details>`;
}

function usb() {
  const profile = profiles[state.id];
  const release = availableRelease(catalog, state.id);
  const secure = window.isSecureContext;
  const serial = 'serial' in navigator;
  return heading('Installera med USB', 'Du behöver inte skriva kod eller installera Arduino IDE. Använd en dator med Chrome eller Edge och en USB-kabel som klarar data.') +
    `<div class="card"><div class="profile-heading"><h3>${profile.title}</h3><span class="pill warning">${release ? `Firmware ${release.version}` : 'Guide · firmware saknas'}</span></div>
      <p class="status">${profile.board} · ${profile.screen}</p>
      <ol class="tasks"><li>Anslut boxen med USB.<p>${state.id === 'esp32-s3' ? 'Använd kortets USB–UART-uttag vid första installationen.' : 'Anslut kabeln direkt till NodeMCU-kortet.'} Stäng Arduino IDE:s seriella monitor om den är öppen.</p></li>
      <li>Kontrollera filen nedan och välj sedan USB-port.<p>Webbläsaren ber om din tillåtelse. Välj boxens port, inte en annan ansluten enhet.</p></li>
      <li>Välj <strong>Install</strong> och bekräfta i USB-dialogen.<p>För en ny box: välj <strong>Erase device</strong> när frågan visas. Dra inte ur USB-kabeln. Vänta på att dialogen bekräftar att installationen lyckades.</p></li></ol>
      <div class="notice warning"><strong>Det gamla programmet ersätts.</strong> Det här är en nyinstallation, inte en garanterat databevarande uppdatering. Wi-Fi och andra sparade inställningar kan försvinna även utan full radering. En box i pågående trafik ska inte installeras om.</div>
      ${!secure ? '<p class="error">USB-installation kräver HTTPS eller localhost. Guiden kan fortfarande läsas här.</p>' : ''}
      ${!serial ? '<p class="error">Den här webbläsaren saknar Web Serial. Öppna guiden i Chrome eller Edge på en dator. På iPhone kan du läsa guiden men inte installera via USB.</p>' : ''}
      ${!release ? '<div class="notice">Det här guidepaketet innehåller inga färdiga firmwarefiler. Installationsknappen är därför spärrad. Du kan läsa alla steg ändå. Använd det kompletta installationspaketet från ett godkänt bygge.</div>' : `<p class="download">Byggd från <code>${escape(catalog.sourceCommit?.slice(0,12) || 'okänd revision')}</code> · ${(release.bytes / 1024).toFixed(0)} kB<br><a href="${release.image}" download>Firmwarefil</a> · <a href="SHA256SUMS">Kontrollsummor</a></p>`}
      <label class="check"><input id="acknowledge" type="checkbox" ${state.acknowledged ? 'checked' : ''}><span>Jag har kontrollerat kort och koppling. Boxen är ny eller ur trafik, och jag accepterar att det gamla programmet och sparade inställningar kan ersättas. Jag vet att fysisk provkörning återstår.</span></label>
      <div id="flash-area"><button id="prepare" class="button primary" type="button" ${release && secure && serial && state.acknowledged ? '' : 'disabled'}>Kontrollera installationsfilen</button></div>
      <p id="flash-status" class="status" role="status" aria-live="polite"></p>
    </div>
    <label class="check"><input id="installed" type="checkbox" ${state.installed ? 'checked' : ''}><span>USB-dialogen visade att installationen lyckades och boxen har startat om.</span></label>
    <p class="fine-print">Markera bara om du faktiskt installerat. Du kan också välja ”Läs nästa steg” utan att ha en box ansluten.</p>
    <details><summary>Ingen port eller anslutningen misslyckas?</summary><ol class="tasks"><li>Byt till en USB-datakabel och prova en annan USB-port. En lampa på kortet visar bara att det får ström.</li><li>Stäng program som använder seriell port. Saknas USB-drivrutin, följ korttillverkarens anvisning för just dess USB-krets.</li><li>ESP32-S3: håll BOOT, tryck och släpp RESET och släpp sedan BOOT. Försök ansluta igen. NodeMCU brukar starta uppladdningsläget automatiskt.</li><li>Om fel chip identifieras: avbryt. Välj aldrig en annan modell för att kringgå felet.</li></ol></details>`;
}

function wifi() {
  const profile = profiles[state.id];
  return heading('Ge boxen ett nätverk', 'Boxen öppnar ett eget tillfälligt installationsnät när inget sparat Wi-Fi fungerar. En telefon är praktisk för detta steg.') +
    `<div class="card"><ol class="tasks"><li>Behåll USB-strömmen och vänta på uppstart.<p>Anteckna boxens kod: <code>${profile.code}</code>. Koden kommer från hårdvaran och används senare i servern.</p></li>
      <li>Anslut telefonen till <code>TrainMeet-XXXXXX</code>.<p>Välj att stanna ansluten även om telefonen varnar för ”inget internet”. Gör detta på en betrodd plats; installationsnätet är tillfälligt och öppet.</p></li>
      <li>Öppna installationsportalen.<p>Om den inte kommer upp automatiskt, skriv <code>http://192.168.4.1</code> i telefonens webbläsare.</p></li>
      <li>Välj träffens <strong>2,4 GHz-Wi-Fi</strong> och ange dess lösenord.<p>Servern och boxen ska finnas på samma lokala nät. Ett isolerat gästnät fungerar normalt inte.</p></li>
      <li>Kontrollera serveradressen och spara.<p>${profile.server}</p><p>Skriv bara IP eller värdnamn, utan <code>http://</code>, sökväg eller webbport. Adressen kan till exempel vara <code>192.168.2.160</code> — använd din egen servers adress.</p></li>
      <li>Anslut telefonen till det vanliga Wi-Fi-nätet igen.<p>Boxen försöker nu kontakta servern. Installationsguiden behöver inte hållas öppen.</p></li></ol></div>
    <div class="notice"><strong>Inte Cloud-adressen.</strong> Boxen pratar MQTT direkt med TrainMeet Server, oavsett om servern är en Raspberry Pi, Mac, PC eller Linux-dator. MQTT-porten är normalt <strong>1883</strong>; webbadmin använder normalt <strong>8787</strong>.</div>
    <details><summary>Byta nätverk eller börja om?</summary><p>${profile.reset}</p></details>
    <p class="fine-print">Wi-Fi anges i boxens portal, inte i denna webbsida. USB-konfiguration via Improv Serial är ännu inte implementerad. Guiden kan därför inte automatiskt bekräfta anslutningen.</p>`;
}

function station() {
  const profile = profiles[state.id];
  return heading('Koppla boxen till en station', 'Nu tar TrainMeet Server över. Det är servern som har träffen, tidtabellen och alla trafikregler — boxen är dess klient.') +
    `<div class="card"><h3>Öppna din server</h3><p class="status">Serverns webbadress, inte Cloud och inte boxens installationsportal. Exemplet nedan är ingen förvald server.</p>
      <form id="server-form"><label class="field">TrainMeet Servers webbadress<input id="server-url" type="text" inputmode="url" autocomplete="off" placeholder="http://192.168.2.160:8787" value="${escape(state.server)}"></label>
        <button class="button secondary" type="submit">Visa serverlänk</button><p id="url-error" class="error" role="alert"></p><div id="server-link"></div></form></div>
    <ol class="tasks"><li>Logga in som administratör på servern.<p>Om servern är ny: skapa ditt eget adminkonto, hämta eller importera en träff och aktivera dess config först.</p></li>
      <li>Öppna administrationen för boxar/enheter.<p>Hitta boxen med samma kod som på displayen: <code>${profile.code}</code>. Menynamnet kan skilja mellan serverversioner.</p></li>
      <li>Tilldela rätt station och spara.<p>Stationen måste finnas i den aktiva träffen. På boxen ska rätt stationsnamn och aktuell information visas.</p></li>
      ${state.id === 'nodemcu-i2c' ? '<li>Kontrollera den logiska A–D-panelen.<p>NodeMCU behöver en entydig v1-panel för stationen. Visas <code>V1-PANEL SAKNAS</code>: kontrollera panelen och serverversionen. Om stationen har flera paneler krävs uttrycklig tilldelning; boxen väljer inte åt dig.</p></li>' : ''}</ol>
    <div class="notice warning"><strong>Gemensam trafiklogik måste finnas i servern.</strong> Server 1.4.1 har NodeMCU-tilldelningsrättningen men inte hela samkörningen mellan 8266, ESP32 och TKL. Den finns ännu bara i en utvecklingsversion. Använd en kontrollerad testserver tills den är släppt och provkörd. <a href="https://github.com/beahead-ab/trainmeet-server/blob/ca77f74889060f4905ca90802c8509d4f47e619a/docs/shared-traffic.md" target="_blank" rel="noopener noreferrer">Läs kompatibilitetsnoteringen ↗</a></div>
    <details><summary>Servern syns i webbläsaren men boxen hittar den inte?</summary><p>Att HTTPS fungerar betyder inte att MQTT fungerar. Kontrollera lokalt nät, brandvägg, MQTT-broker och att servern lyssnar på LAN. Ange lokal IP om mDNS inte passerar nätverket. Öppna inte en lösenordslös MQTT-port mot internet.</p></details>`;
}

const checklist = [
  ['display', 'Rätt stationsnamn visas och hela displayen går att läsa.'],
  ['keys', 'Alla knappar är kontrollerade på testbänk och ger rätt tecken, utan dubbeltryck.'],
  ['reconnect', 'Efter omstart återkommer samma boxkod och rätt station. Vid bortkopplad server spärras trafikåtgärder.'],
  ['traffic', 'Ett helt trafikärende är provkört mellan stationer på en testträff, inklusive ESP8266, ESP32 eller TKL som ska användas tillsammans.'],
];
function check() {
  return heading('Kontrollera innan trafik', 'Installationen är inte samma sak som ett godkänt funktionstest. Du kan avsluta guiden nu och göra kontrollerna när hårdvaran finns på plats.') +
    `<div class="card"><h3>${profiles[state.id].title}</h3><p class="status">${availableRelease(catalog, state.id) ? `Firmware ${catalog.version}` : 'Guide utan firmwarefiler'} · Ingen hårdvara har verifierats automatiskt.</p>
      <label class="check"><input id="installed" type="checkbox" ${state.installed ? 'checked' : ''}><span>Jag har sett att USB-installationen lyckades och boxen startade om.</span></label>
      ${checklist.map(([id, text]) => `<label class="check"><input type="checkbox" data-check="${id}" ${state.checks.has(id) ? 'checked' : ''}><span>${text}</span></label>`).join('')}
      <p class="fine-print">Testa knappsatsen på en separat testträff eller med hårdvarutestet, inte genom att trycka alla knappar under pågående trafik. ESP32:s D-tangent har ännu ingen åtgärd i den vanliga navigeringen.</p>
      <a href="${repo + (state.id === 'nodemcu-i2c' ? 'firmware/esp8266/README.md#3-testa-först-display-och-knappsats' : 'docs/BANKTEST.md')}" target="_blank" rel="noopener noreferrer">Fullständig bänktestlista ↗</a>
    </div><div id="summary" aria-live="polite"></div>
    <p class="status">Efter installationen kan datorn kopplas bort. Boxen behöver fortsatt stabil USB-ström, Wi-Fi och kontakt med TrainMeet Server. Servern fattar trafikbesluten för båda boxmodellerna.</p>`;
}

function render(focus = true) {
  generation++;
  blobURLs.forEach(url => URL.revokeObjectURL(url));
  blobURLs = [];
  document.querySelector('#step-count').textContent = `Steg ${state.step + 1} av ${steps.length}`;
  document.querySelector('#progress-fill').style.width = `${(state.step + 1) * 20}%`;
  document.querySelector('#step-list').innerHTML = steps.map((label, index) => `<li><button class="step-link" type="button" data-step="${index}" ${index === state.step ? 'aria-current="step"' : ''} ${index > 0 && !state.id ? 'disabled' : ''} aria-label="Steg ${index + 1}: ${label}"><span class="number">${index + 1}</span><span class="label">${label}</span></button></li>`).join('');
  content.innerHTML = [hardware, usb, wifi, station, check][state.step]();
  const back = document.querySelector('#back');
  back.disabled = state.step === 0;
  const next = document.querySelector('#next');
  next.disabled = !state.id;
  updateNext();
  if (state.step === 4) updateSummary();
  if (focus) document.querySelector('#step-title').focus();
}

function updateNext() {
  document.querySelector('#next').textContent = state.step === 4 ? 'Avsluta guiden' : state.step === 1 && !state.installed ? 'Läs nästa steg →' : 'Fortsätt →';
}

function updateSummary() {
  const complete = state.installed && state.checks.size === checklist.length;
  document.querySelector('#summary').innerHTML = `<div class="result"><h3>${state.finished ? 'Guiden är genomgången' : 'Din kontrollista'}</h3><p>${complete ? 'Du har markerat alla kontroller som utförda. Det är din egen bekräftelse, inte ett automatiskt godkännande av hårdvaran.' : `${state.checks.size} av ${checklist.length} funktionskontroller markerade. ${state.installed ? 'USB-installationen är markerad som utförd.' : 'Ingen genomförd USB-installation är markerad.'} Provkörning återstår innan trafikdrift.`}</p><p class="fine-print">Markeringarna gäller bara denna öppna guide och sparas inte i servern.</p>${state.finished ? '<button class="button secondary" id="restart" type="button">Installera en annan box</button>' : ''}</div>`;
}

async function prepare() {
  const viewGeneration = generation;
  const release = availableRelease(catalog, state.id);
  const area = document.querySelector('#flash-area');
  const status = document.querySelector('#flash-status');
  const parameters = { id: state.id, catalog, acknowledged: state.acknowledged, secure: window.isSecureContext, serial: 'serial' in navigator, toolsReady: true };
  if (!canInstall(parameters)) return;
  document.querySelector('#prepare').disabled = true;
  status.textContent = 'Hämtar firmware och kontrollerar SHA-256 … Ingen USB-anslutning öppnas ännu.';
  try {
    const bytes = await verifyImage(release);
    await import('./web-tools.js');
    if (viewGeneration !== generation || !state.acknowledged) return;
    const imageURL = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
    // Freeze the verified bytes into this installation. The flasher never
    // re-fetches an unchecked binary after the checksum check.
    const manifest = { name: `TrainMeet TMBox ${state.id}`, version: release.version,
      new_install_prompt_erase: true, new_install_improv_wait_time: 0,
      builds: [{ chipFamily: release.chipFamily, improv: false, parts: [{ path: imageURL, offset: 0 }] }] };
    const manifestURL = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
    blobURLs.push(imageURL, manifestURL);
    const install = document.createElement('esp-web-install-button');
    install.setAttribute('manifest', manifestURL);
    install.innerHTML = '<button slot="activate" class="button primary" type="button">Anslut boxen och installera</button><span slot="unsupported">Använd Chrome eller Edge på en dator.</span><span slot="not-allowed">Öppna guiden över HTTPS eller localhost.</span>';
    area.replaceChildren(install);
    status.textContent = 'Filen är kontrollerad. Nästa knapp öppnar datorns USB-portväljare. Följ sedan Install i USB-dialogen (på engelska).';
  } catch (error) {
    if (viewGeneration !== generation) return;
    status.className = 'error';
    status.textContent = `Installationen kunde inte förberedas. ${error.message}`;
    const retry = document.querySelector('#prepare');
    if (retry) retry.disabled = false;
  }
}

document.addEventListener('click', event => {
  // The flasher owns the device until its modal has closed. Do not replace
  // verified Blob URLs or change hardware profile while it is using them.
  if (document.querySelector('ewt-install-dialog')) return;
  const step = event.target.closest('[data-step]');
  if (step && !step.disabled) { state.step = Number(step.dataset.step); render(); }
  if (event.target.closest('#back') && state.step > 0) { state.step--; render(); }
  if (event.target.closest('#next') && state.id) {
    if (state.step < 4) { state.step++; render(); }
    else { state.finished = true; updateSummary(); document.querySelector('#summary').scrollIntoView({ block: 'nearest' }); }
  }
  if (event.target.closest('#prepare')) prepare();
  if (event.target.closest('#restart')) {
    Object.assign(state, { step: 0, id: '', acknowledged: false, installed: false, checks: new Set(), finished: false });
    render();
  }
});

document.addEventListener('change', event => {
  const target = event.target;
  if (target.name === 'profile') {
    state.id = target.value;
    state.acknowledged = state.installed = state.finished = false;
    state.checks.clear();
    render(false);
    document.querySelector(`input[value="${state.id}"]`).focus({ preventScroll: true });
  }
  if (target.id === 'acknowledge') { state.acknowledged = target.checked; render(false); document.querySelector('#acknowledge').focus({ preventScroll: true }); }
  if (target.id === 'installed') { state.installed = target.checked; updateNext(); if (state.step === 4) updateSummary(); }
  if (target.dataset.check) { target.checked ? state.checks.add(target.dataset.check) : state.checks.delete(target.dataset.check); updateSummary(); }
});

document.addEventListener('input', event => {
  if (event.target.id === 'server-url') { state.server = event.target.value; document.querySelector('#server-link').replaceChildren(); }
});
document.addEventListener('submit', event => {
  if (event.target.id !== 'server-form') return;
  event.preventDefault();
  const error = document.querySelector('#url-error');
  error.textContent = '';
  document.querySelector('#server-link').replaceChildren();
  try {
    const url = safeServerURL(state.server);
    const link = document.createElement('a');
    link.href = url; link.target = '_blank'; link.rel = 'noopener noreferrer';
    link.className = 'button primary'; link.textContent = `Öppna ${new URL(url).host} ↗`;
    document.querySelector('#server-link').append(link);
  } catch (exception) { error.textContent = exception.message; }
});
window.addEventListener('beforeunload', event => {
  if (document.querySelector('ewt-install-dialog')) { event.preventDefault(); event.returnValue = ''; }
});

render(false);
try {
  const response = await fetch('./catalog.json', { cache: 'no-store' });
  if (response.ok) catalog = await response.json();
} catch { /* Static reading remains available when firmware catalog is absent. */ }
if (state.step === 1) render(false);
