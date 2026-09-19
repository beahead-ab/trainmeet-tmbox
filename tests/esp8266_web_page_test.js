'use strict';

// Run the exact shipped script. No browser, network, dependencies or real sleeps.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const page = fs.readFileSync(path.join(__dirname,
  '../firmware/esp8266/TrainMeetTambox8266/web_test_page.h'), 'utf8');
const script = page.split('<script>')[1].split('</script>')[0];
const reply = (body, status = 200) => ({ body, status });
const deferred = () => {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
};
const snapshot = extra => ({
  deviceCode: 'BOX123', firmware: 'test', ip: '192.168.0.50',
  server: 'old-server', configuredServer: 'old-server', configuredPort: 1883,
  httpPort: 8787, connected: true, enrollmentReady: true,
  panel: 'station', line1: 'READY', line2: '', lcd: true, keypad: true,
  ready: true, canStart: true, webTest: false, waiting: false, fresh: true,
  allowedKeys: '1A', session: 'session-1', revision: 7, ...extra,
});

function harness(handler, initial = snapshot(), { ignoreAbort = false } = {}) {
  const elements = new Map(), nodes = [], timers = new Map();
  let nextTimer = 1;
  const env = { now: 0, requests: [], serverStatus: initial };
  function element(tag, id, value = '') {
    let inputValue = String(value);
    const listeners = {}, classes = new Set();
    const el = { tag, id, textContent: '', disabled: false, hidden: false,
      get value() { return inputValue; }, set value(v) { inputValue = String(v); },
      children: [], append(child) { this.children.push(child); },
      setAttribute() {}, addEventListener(type, callback) { listeners[type] = callback; },
      dispatch(type) { if (listeners[type]) listeners[type](); },
      classList: {
        toggle(name, enabled) { if (enabled) classes.add(name); else classes.delete(name); },
        contains(name) { return classes.has(name); },
      },
    };
    nodes.push(el);
    if (id) elements.set(id, el);
    return el;
  }
  // Keep IDs and initial form defaults tied to the actual HTML as well.
  for (const match of page.matchAll(/<([a-z0-9]+)\b([^>]*\bid="([^"]+)"[^>]*)>/g)) {
    const el = element(match[1], match[3], (match[2].match(/\bvalue="([^"]*)"/) || [,''])[1]);
    el.hidden = /\bhidden\b/.test(match[2]);
  }
  // The pairing submit has no ID, but must also be disabled while busy.
  element('button', 'pair-submit');
  const document = {
    hidden: false,
    getElementById(id) { assert(elements.has(id), `Unknown DOM ID: ${id}`); return elements.get(id); },
    createElement: tag => element(tag), addEventListener() {},
    querySelectorAll(selector) {
      assert.equal(selector, 'input,button');
      return nodes.filter(el => el.tag === 'input' || el.tag === 'button');
    },
  };
  const schedule = (fn, delay, repeat) => {
    const id = nextTimer++;
    timers.set(id, { fn, at: env.now + delay, repeat });
    return id;
  };
  const context = vm.createContext({
    document, AbortController, // Intentionally no AbortSignal.timeout (older phones).
    Date: class extends Date { static now() { return env.now; } },
    setTimeout: (fn, delay) => schedule(fn, delay, 0), clearTimeout: id => timers.delete(id),
    setInterval: (fn, delay) => schedule(fn, delay, delay),
    fetch(url, options) {
      const call = { url, method: options.method,
        body: options.body === undefined ? undefined : JSON.parse(options.body), signal: options.signal };
      env.requests.push(call);
      let result;
      try { result = handler ? handler(call, env) : undefined; }
      catch (e) { return Promise.reject(e); }
      if (result === undefined) {
        assert.equal(call.method, 'GET', `Unexpected ${call.method} ${url}`);
        assert.equal(url, '/api/status');
        result = reply(env.serverStatus);
      }
      return new Promise((resolve, reject) => {
        if (!ignoreAbort) options.signal.addEventListener('abort', () => reject(Error('aborted')));
        Promise.resolve(result).then(r => resolve({
          ok: r.status >= 200 && r.status < 300, status: r.status,
          json: async () => structuredClone(r.body),
        }), reject);
      });
    },
  });
  env.el = id => document.getElementById(id);
  env.input = (id, value) => { env.el(id).value = value; env.el(id).dispatch('input'); };
  env.read = expression => vm.runInContext(expression, context);
  env.submit = () => env.el('enroll').onsubmit({ preventDefault() {} });
  env.posts = url => env.requests.filter(r => r.method === 'POST' && (!url || r.url === url));
  env.flush = async () => { for (let i = 0; i < 30; ++i) await Promise.resolve(); };
  env.advance = async milliseconds => {
    const until = env.now + milliseconds;
    await env.flush();
    for (let count = 0; count < 10000; ++count) {
      const item = [...timers].filter(([,t]) => t.at <= until).sort((a,b) => a[1].at - b[1].at)[0];
      if (!item) { env.now = until; await env.flush(); return; }
      const [id, t] = item;
      env.now = t.at;
      timers.delete(id);
      if (t.repeat) timers.set(id, { ...t, at: env.now + t.repeat });
      t.fn();
      await env.flush();
    }
    throw Error('Fake timer loop did not terminate');
  };
  env.run = async promise => {
    let done = false, failure;
    Promise.resolve(promise).then(() => { done = true; }, e => { done = true; failure = e; });
    const deadline = env.now + 35000;
    while (!done) {
      await env.flush();
      if (done) break;
      const next = Math.min(...[...timers.values()].map(t => t.at));
      assert(next <= deadline, 'Action did not finish within its deadline');
      await env.advance(Math.max(0, next - env.now));
    }
    if (failure) throw failure;
  };
  vm.runInContext(script, context);
  return env;
}


const editing = extra => snapshot({webTest:true,localEntry:true,entryContext:'entry-one',entryValue:'',allowedKeys:'0123456789#*',...extra});
async function digitsRemainInPhoneUntilConfirm() {
 const e=harness(call=>call.url==='/api/key'?reply(editing({ready:false,waiting:true})):undefined,editing());
 await e.flush();
 for(const key of '93') await e.read('press('+JSON.stringify(key)+')');
 assert.equal(e.posts().length,0);
 assert.equal(e.el('line2').textContent.trim(),'Tag: 93');
 await e.advance(2000);
 assert.equal(e.el('line2').textContent.trim(),'Tag: 93','poll must not overwrite the local draft');
 await e.run(e.read("press('#')"));
 assert.equal(e.posts().length,1);
 assert.deepEqual(e.posts()[0].body,{key:'#',session:'session-1',revision:7,train_number:'93',entryContext:'entry-one'});
 await e.read("press('#')");
 assert.equal(e.posts().length,1,'no duplicate command while awaiting server');
}
async function cancelSendsNoDigits() {
 const e=harness(call=>call.url==='/api/key'?reply(snapshot({webTest:true})):undefined,editing());
 await e.flush();await e.read("press('9')");await e.read("press('*')");
 assert.equal(e.posts().length,1);
 assert.equal(e.posts()[0].body.key,'*');
 assert.equal(e.posts()[0].body.train_number,undefined);
}
async function draftIsBoundedAndInvalidatedByContext() {
 const e=harness(null,editing());await e.flush();
 await e.read("press('#')");assert.equal(e.posts().length,0);
 for(const k of '1234567')await e.read('press('+JSON.stringify(k)+')');
 assert.equal(e.el('line2').textContent.trim(),'Tag: 12345');
 e.serverStatus=editing({entryContext:'entry-two',entryValue:'8'});
 await e.advance(1000);
 assert.equal(e.el('line2').textContent.trim(),'Tag: 8');
 e.read('lost()');assert.equal(e.read('entryValue'),'');
 await e.read("press('#')");assert.equal(e.posts().length,0);
}
async function automaticSessionNeedsNoCode() {
 let reads=0;
 const e=harness(call=>{
  if(call.url==='/api/status'&&!reads++)return reply({error:'session'},401);
  if(call.url==='/api/session')return reply(snapshot({panel:'',canStart:false,ready:false}));
 });
 await e.flush();assert.equal(e.posts('/api/session').length,1);
 assert.deepEqual(e.posts()[0].body,{});
 assert.equal(e.el('start').disabled,true);
 for(const id of ['pin','server','port','httpport','servercode'])assert(!page.includes('id="'+id+'"'));
}
async function explicitDisconnectDoesNotReclaimTheBox() {
 const e=harness(call=>call.url==='/api/logout'?reply({ok:true}):undefined,editing());
 await e.flush();
 await e.read('action("/api/logout",{})');
 const count=e.requests.length;
 await e.advance(3000);
 assert.equal(e.requests.length,count);
 assert.equal(e.read('closed'),true);
}
async function initialPollCannotOverwriteCompletedAction() {
 const pending=deferred();
 const e=harness(call=>call.url==='/api/status'?pending.promise:reply(editing()),snapshot(),{ignoreAbort:true});
 await e.read('action("/api/session",{})');
 pending.resolve(reply(snapshot({line1:'OLD'})));await e.flush();
 assert.equal(e.read('state.webTest'),true);
 assert.notEqual(e.el('line1').textContent,'OLD');
}
async function transportTimeoutIsNotARetriedPost() {
 const e=harness(call=>call.method==='POST'?new Promise(()=>{}):undefined,editing());
 await e.flush();await e.read("press('9')");
 await e.run(e.read("press('#')"));
 assert.equal(e.posts().length,1);
 assert.equal(e.read('entryValue'),'');
 assert.match(e.el('message').textContent,/Ingen automatisk omsändning/);
}
(async()=>{
 for(const fn of [digitsRemainInPhoneUntilConfirm,cancelSendsNoDigits,draftIsBoundedAndInvalidatedByContext,automaticSessionNeedsNoCode,explicitDisconnectDoesNotReclaimTheBox,initialPollCannotOverwriteCompletedAction,transportTimeoutIsNotARetriedPost]){
  await fn();console.log('PASS '+fn.name);
 }
})().catch(error=>{console.error(error);process.exitCode=1;});
