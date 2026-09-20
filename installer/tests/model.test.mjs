import { test } from 'node:test';
import assert from 'node:assert/strict';
import { webcrypto } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { profiles, steps, safeServerURL, availableRelease, canInstall, verifyImage } from '../model.js';

const catalog = { schema: 1, version: '0.3.2', profiles: [{
  id: 'nodemcu-i2c', chipFamily: 'ESP8266', hardwareTested: false,
  manifest: 'firmware/nodemcu-i2c/manifest.json', image: 'firmware/nodemcu-i2c/firmware.bin',
  sha256: 'a'.repeat(64), bytes: 1024,
}] };
const options = { catalog, id: 'nodemcu-i2c', acknowledged: true, secure: true, serial: true, toolsReady: true };

test('only the two explicit wiring profiles are offered', () => {
  assert.deepEqual(Object.keys(profiles), ['nodemcu-i2c', 'esp32-s3']);
  assert.match(profiles['nodemcu-i2c'].keypad, /PCF8574/);
  assert.match(profiles['esp32-s3'].keypad, /GPIO/);
});
test('installer separates device setup from local administrator assignment', async () => {
  assert.equal(steps[2], 'Wi-Fi och server');
  assert.equal(steps[3], 'Invänta station');
  const ui = await readFile(new URL('../app.js', import.meta.url), 'utf8');
  assert.match(ui, /Administratören tilldelar stationen/);
  assert.match(ui, /För träffens lokala administratör/);
  assert.match(ui, /Kopplingen lagras i den lokala servern/);
  assert.doesNotMatch(ui, /<(?:input|select)[^>]*(?:id|name)=["']station/);
});
test('source preview and missing images cannot enable USB install', () => {
  assert.equal(availableRelease(null, 'nodemcu-i2c'), null);
  assert.equal(availableRelease({ schema: 1, profiles: [] }, 'esp32-s3'), null);
  assert.equal(canInstall({ ...options, catalog: null }), false);
});
test('all installation gates are required', () => {
  assert.equal(canInstall(options), true);
  for (const gate of ['acknowledged', 'secure', 'serial', 'toolsReady']) {
    assert.equal(canInstall({ ...options, [gate]: false }), false, gate);
  }
});
test('a chip mismatch or unknown board is never accepted', () => {
  assert.equal(availableRelease(catalog, 'esp32-s3'), null);
  assert.equal(availableRelease(catalog, 'esp32-benny'), null);
  const bad = structuredClone(catalog);
  bad.profiles[0].chipFamily = 'ESP32';
  assert.equal(availableRelease(bad, 'nodemcu-i2c'), null);
});
test('remote or traversal manifest/image paths are refused', () => {
  for (const field of ['manifest', 'image']) {
    for (const value of ['https://example.com/evil.bin', '../other.bin', '//evil.test/x']) {
      const bad = structuredClone(catalog); bad.profiles[0][field] = value;
      assert.equal(availableRelease(bad, 'nodemcu-i2c'), null);
    }
  }
});
test('catalog requires checksum, length and honest hardware status', () => {
  for (const [field, value] of [['sha256', 'no'], ['bytes', 0], ['hardwareTested', true]]) {
    const bad = structuredClone(catalog); bad.profiles[0][field] = value;
    assert.equal(availableRelease(bad, 'nodemcu-i2c'), null);
  }
});
test('server web address accepts LAN and HTTPS without issuing a request', () => {
  assert.equal(safeServerURL(' 192.168.2.160:8787 '), 'http://192.168.2.160:8787/');
  assert.equal(safeServerURL('https://server.trainmeet.app'), 'https://server.trainmeet.app/');
  assert.equal(safeServerURL('http://[::1]:8787/'), 'http://[::1]:8787/');
});
test('unsafe server links are rejected', () => {
  for (const value of ['', 'javascript:alert(1)', 'file:///etc/passwd', 'ftp://server',
    'http://admin:secret@server', 'http://foo bar', 'https://server\\@evil']) {
    assert.throws(() => safeServerURL(value), undefined, value);
  }
});
test('integrity check returns the exact verified bytes; corruption fails closed', async () => {
  const bytes = new Uint8Array(1024).fill(7);
  const hash = Buffer.from(await webcrypto.subtle.digest('SHA-256', bytes)).toString('hex');
  const release = { image: 'firmware/nodemcu-i2c/firmware.bin', bytes: 1024, sha256: hash };
  const get = async () => new Response(bytes);
  assert.deepEqual(new Uint8Array(await verifyImage(release, get, webcrypto)), bytes);
  await assert.rejects(() => verifyImage({ ...release, sha256: '0'.repeat(64) }, get, webcrypto), /kontrollsumma/);
  await assert.rejects(() => verifyImage({ ...release, bytes: 1 }, get, webcrypto), /kontrollsumma/);
  await assert.rejects(() => verifyImage(release, async () => new Response('', { status: 404 }), webcrypto), /hämta/);
});
