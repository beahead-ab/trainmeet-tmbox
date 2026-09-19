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

async function editedAddressAndFreshAck() {
  let changed = false, statusReads = 0;
  const e = harness((call, env) => {
    if (call.url === '/api/server') {
      changed = true;
      env.serverStatus = snapshot({ configuredServer: '192.168.0.160', server: '192.168.0.160',
        httpPort: 8877, connected: false, enrollmentReady: false });
      return reply(env.serverStatus);
    }
    if (changed && call.url === '/api/status') {
      statusReads++;
      return reply({ ...env.serverStatus, connected: true, enrollmentReady: statusReads >= 3 });
    }
    if (call.url === '/api/enroll') {
      assert.equal(statusReads, 3, 'Do not submit code on MQTT connection alone');
      return reply({ accepted: true, awaitingStation: true });
    }
  });
  await e.flush();
  e.input('server', 'http://192.168.0.160:8877/');
  e.input('servercode', '123456');
  const started = e.requests.length;
  const task = e.submit();
  assert.equal(e.el('connect').disabled, true);
  assert.match(e.el('enrollment').textContent, /Sparar/);
  await e.run(task);
  assert.deepEqual(e.requests.slice(started).map(r => r.url),
    ['/api/server', '/api/status', '/api/status', '/api/status', '/api/enroll']);
  assert.deepEqual(e.posts('/api/server')[0].body,
    { host: 'http://192.168.0.160:8877/', port: 1883, httpPort: 8787 });
  assert.deepEqual(e.posts('/api/enroll')[0].body, { code: '123456' });
  assert.equal(e.el('server').value, '192.168.0.160');
  assert.equal(e.el('httpport').value, '8877');
  assert.equal(e.el('servercode').value, '');
  assert.match(e.el('enrollment').textContent, /Koden godkänd/);
  assert.equal(e.posts('/api/test').length, 0, 'Enrollment must not activate runtime input');
  assert.equal(e.now, 1000);
}

async function automaticDiscoveryKeepsEmptyHost() {
  const e = harness(call => call.url === '/api/enroll' ? reply({ accepted: true }) : undefined,
    snapshot({ configuredServer: '', server: 'discovered.local' }));
  await e.flush();
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.posts('/api/server').length, 0, 'Do not rewrite auto mode to the discovered host');
  assert.equal(e.el('server').value, '');
  assert.equal(e.posts('/api/enroll').length, 1);
  assert.equal(e.requests.filter(r => r.url === '/api/status').length, 2, 'Read fresh status before enrollment');
}

async function wrongCodeStaysVisibleWithoutFalseDisconnect() {
  const e = harness(call => call.url === '/api/enroll'
    ? reply({ error: 'Fel kod. Kontrollera den lokala koden.' }, 400) : undefined);
  await e.flush();
  e.input('servercode', '000000');
  await e.run(e.submit());
  assert.equal(e.el('servercode').value, '000000');
  assert.match(e.el('enrollment').textContent, /Fel kod/);
  assert(e.el('enrollment').classList.contains('error'));
  assert(!e.el('status').textContent.includes('bruten'));
  assert.equal(e.read('online'), true);
  assert.equal(e.el('connect').disabled, false);
  assert.equal(e.posts('/api/enroll').length, 1);
}

async function busyLocksEveryMutationAndKeepsCode() {
  const pending = deferred();
  const e = harness(call => call.url === '/api/enroll' ? pending.promise : undefined);
  await e.flush();
  e.input('servercode', '123456');
  e.input('pin', '654321');
  const first = e.submit();
  await e.flush();
  assert.equal(e.posts('/api/enroll').length, 1);
  for (const id of ['server', 'servercode', 'port', 'httpport', 'pin', 'pair-submit',
    'connect', 'saveaddress', 'start', 'stop', 'logout']) assert(e.el(id).disabled, id);
  for (const button of e.el('keys').children) assert(button.disabled);
  await e.submit();
  await e.el('saveaddress').onclick();
  await e.el('start').onclick();
  await e.el('stop').onclick();
  await e.el('logout').onclick();
  await e.el('pair').onsubmit({ preventDefault() {} });
  assert.equal(e.posts().length, 1);
  assert.equal(e.el('servercode').value, '123456');
  assert.equal(e.el('pin').value, '654321');
  assert.equal(e.el('controls').hidden, false);
  pending.resolve(reply({ accepted: true }));
  await e.run(first);
  assert.equal(e.el('servercode').value, '');
}

async function unreachableDeadlineResumesAndCanRetryExplicitly() {
  const e = harness(call => call.url === '/api/enroll' ? reply({ accepted: true }) : undefined,
    snapshot({ connected: false, enrollmentReady: false }));
  await e.flush();
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.now, 25000);
  assert.match(e.el('enrollment').textContent, /25 sekunder.*Ingen kod skickades/);
  assert.equal(e.el('servercode').value, '123456');
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.equal(e.read('busy'), false);
  assert.equal(e.el('connect').disabled, false);
  const reads = e.requests.length;
  e.serverStatus = snapshot();
  await e.advance(1000);
  assert(e.requests.length > reads, 'Global polling resumes after the bounded attempt');
  await e.run(e.submit());
  assert.equal(e.posts('/api/enroll').length, 1);
}

async function transportTimeoutIsNotARetriedPost() {
  const never = deferred();
  const e = harness(call => call.url === '/api/enroll' ? never.promise : undefined);
  await e.flush();
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.now, 5000);
  assert.equal(e.el('servercode').value, '123456');
  assert.match(e.el('enrollment').textContent, /svarade inte i tid.*Ingen automatisk omsändning/);
  assert.match(e.el('enrollment').textContent, /Kodförsöket kan ha behandlats/);
  assert.equal(e.posts('/api/enroll').length, 1);
  assert(e.posts('/api/enroll')[0].signal.aborted);
  assert.equal(e.el('connect').disabled, false);
  await e.advance(2000);
  assert.equal(e.posts('/api/enroll').length, 1);
}

async function initialPollCannotOverwriteCompletedAction() {
  for (const oldStatus of [200, 401]) {
    const old = deferred();
    let first = true;
    const e = harness((call, env) => {
      if (first && call.url === '/api/status') { first = false; return old.promise; }
      if (call.url === '/api/server') {
        env.serverStatus = snapshot({ configuredServer: 'new-server', server: 'new-server' });
        return reply(env.serverStatus);
      }
      if (call.url === '/api/enroll') return reply({ accepted: true });
    }, snapshot(), { ignoreAbort: true });
    e.input('server', 'new-server');
    e.input('servercode', '123456');
    await e.run(e.submit());
    assert(e.requests[0].signal.aborted, 'Abort old background polling at the mutation boundary');
    old.resolve(reply(oldStatus === 401 ? { error: 'expired old request' } : snapshot(), oldStatus));
    await e.flush();
    assert.equal(e.el('server').value, 'new-server');
    assert.equal(e.read('state.configuredServer'), 'new-server');
    assert.equal(e.el('controls').hidden, false);
    assert.equal(e.el('login').hidden, true);
  }
}

async function keysRetainSessionRevisionAllowlistAndStalePollSafety() {
  const old = deferred();
  let gets = 0;
  const e = harness((call, env) => {
    if (call.url === '/api/status' && ++gets === 2) return old.promise;
    if (call.url === '/api/key') {
      env.serverStatus = snapshot({ webTest: true, ready: false, waiting: true, revision: 8 });
      return reply(env.serverStatus);
    }
  }, snapshot({ webTest: true }), { ignoreAbort: true });
  await e.flush();
  const key = label => e.el('keys').children.find(b => b.textContent === label);
  assert.equal(key('1').disabled, false);
  assert.equal(key('9').disabled, true);
  await key('9').onclick();
  assert.equal(e.posts('/api/key').length, 0);
  await e.advance(1000); // An old, valid status is now in flight.
  await e.run(key('1').onclick());
  assert.deepEqual(e.posts('/api/key')[0].body, { key: '1', session: 'session-1', revision: 7 });
  assert(key('1').disabled);
  old.resolve(reply(snapshot({ webTest: true })));
  await e.flush();
  assert.equal(e.read('state.revision'), 8);
  assert(key('1').disabled, 'Old allowed-key snapshot must not re-enable a waiting key');
  await key('1').onclick();
  assert.equal(e.posts('/api/key').length, 1);
}

async function addressOnlyDoesNotClaimEnrollment() {
  const e = harness((call, env) => {
    if (call.url === '/api/server') {
      env.serverStatus = snapshot({ configuredServer: 'new-server', connected: false, enrollmentReady: false });
      return reply(env.serverStatus);
    }
  });
  await e.flush();
  e.input('server', 'new-server');
  e.input('servercode', '123456');
  await e.run(e.el('saveaddress').onclick());
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.equal(e.el('servercode').value, '123456');
  assert.match(e.el('enrollment').textContent, /Ingen anslutningskod har skickats eller godkänts/);
}

async function changedTargetFailsClosed() {
  let reads = 0;
  const e = harness(call => {
    if (call.url === '/api/status' && ++reads === 2)
      return reply(snapshot({ configuredServer: 'some-other-server' }));
  });
  await e.flush();
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.match(e.el('enrollment').textContent, /Serverinställningarna har ändrats/);
  assert.equal(e.el('servercode').value, '123456');
  assert.equal(e.read('online'), true);
}

async function authenticationAndValidationFailuresPreserveInput() {
  const e = harness(call => {
    if (call.url === '/api/login') return reply({ error: 'Fel webbtestkod.' }, 403);
    if (call.url === '/api/logout') return reply({ error: 'Försök igen.' }, 500);
  });
  await e.flush();
  e.input('pin', '111111');
  await e.run(e.el('pair').onsubmit({ preventDefault() {} }));
  assert.equal(e.el('pin').value, '111111');
  await e.run(e.el('logout').onclick());
  assert.equal(e.el('controls').hidden, false, 'Failed logout does not pretend to terminate the session');
  e.input('servercode', '123456');
  e.input('port', '0');
  await e.run(e.submit());
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.match(e.el('enrollment').textContent, /1 och 65535/);
}

async function expiredPairingExplainsFailureOutsideHiddenForm() {
  let reads = 0;
  const e = harness(call => {
    if (call.url === '/api/status' && ++reads === 2)
      return reply({ error: 'Webbtestsessionen har upphört.' }, 401);
  });
  await e.flush();
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.el('controls').hidden, true);
  assert.equal(e.el('login').hidden, false);
  assert.match(e.el('message').textContent, /parkopplas.*Webbtestsessionen har upphört/);
  assert(e.el('message').classList.contains('error'));
  assert.equal(e.el('servercode').value, '123456');
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.equal(e.el('pair-submit').disabled, false);
}

async function saveFailureDoesNotSendCodeOrDiscardDraft() {
  const e = harness(call => call.url === '/api/server'
    ? reply({ error: 'Inställningarna kunde inte sparas.' }, 500) : undefined);
  await e.flush();
  e.input('server', 'new-server');
  e.input('servercode', '123456');
  await e.run(e.submit());
  assert.equal(e.posts('/api/enroll').length, 0);
  assert.equal(e.el('servercode').value, '123456');
  assert.equal(e.el('server').value, 'new-server');
  assert.match(e.el('enrollment').textContent, /kunde inte sparas/);
  assert.equal(e.read('online'), true);
  await e.advance(1000);
  assert.equal(e.el('server').value, 'new-server', 'Polling must preserve the failed draft');
}

(async () => {
  assert.match(page, /<input id="server" maxlength="95"/);
  assert.match(page, /id="enrollment"[^>]*role="alert"[^>]*aria-live="polite"/);
  assert(!page.includes('id="settings"'), 'Server address and code share a single form');
  assert(!script.includes('AbortSignal.timeout'));
  assert(!script.includes('localStorage') && !script.includes('sessionStorage'));
  for (const test of [editedAddressAndFreshAck, automaticDiscoveryKeepsEmptyHost,
    wrongCodeStaysVisibleWithoutFalseDisconnect, busyLocksEveryMutationAndKeepsCode,
    unreachableDeadlineResumesAndCanRetryExplicitly, transportTimeoutIsNotARetriedPost,
    initialPollCannotOverwriteCompletedAction, keysRetainSessionRevisionAllowlistAndStalePollSafety,
    addressOnlyDoesNotClaimEnrollment, changedTargetFailsClosed,
    authenticationAndValidationFailuresPreserveInput, expiredPairingExplainsFailureOutsideHiddenForm,
    saveFailureDoesNotSendCodeOrDiscardDraft]) {
    await test();
    process.stdout.write(`PASS ${test.name}\n`);
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
