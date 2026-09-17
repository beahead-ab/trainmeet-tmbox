export const profiles = {
  'nodemcu-i2c': {
    title: 'NodeMCU · ESP8266', chip: 'ESP8266', screen: '16 × 2 tecken',
    board: 'NodeMCU 1.0 · ESP-12E / ESP-12F · 4 MB',
    keypad: '4 × 4 knappar via PCF8574 på I²C', code: 'TBX-XXXXXX',
    wiring: 'D2 / GPIO4 → SDA · D1 / GPIO5 → SCL',
    connections: [['LCD', '0x27 · via nivåomvandlare till 5 V'], ['Knappsats', 'PCF8574 0x20 · på 3,3 V-sidan'], ['Matris', 'Rader P0–P3 · kolumner P4–P7']],
    docs: 'firmware/esp8266/README.md',
    reset: 'Håll * i fem sekunder för att öppna installationsnätet igen. De sparade nätuppgifterna raderas inte av detta.',
    server: 'Lämna adressen tom bara om det finns en enda server på nätet. Annars anger du dess lokala IP-adress. MQTT-porten anges separat, normalt 1883.',
  },
  'esp32-s3': {
    title: 'TMBox · ESP32-S3', chip: 'ESP32-S3', screen: '20 × 4 tecken',
    board: 'ESP32-S3-DevKitC-1-N8R2',
    keypad: '4 × 4 knappar direkt på GPIO', code: 'TMBOX-XXXXXX',
    wiring: 'GPIO8 → SDA · GPIO9 → SCL',
    connections: [['LCD', '0x27 · via nivåomvandlare till 5 V'], ['Knappsatsens rader', 'GPIO4, 5, 6, 7'], ['Knappsatsens kolumner', 'GPIO15, 16, 17, 18']],
    docs: 'docs/TMBOX-V2-HARDWARE.md',
    reset: 'Håll * eller den separata provisioneringsknappen i fem sekunder för att radera Wi-Fi och serverval och börja om. Stationsdata i servern berörs inte.',
    server: 'Ange serverns lokala IP-adress om flera servrar finns på nätet. Med manuell adress används MQTT-port 1883. Vid automatisk upptäckt används den port servern annonserar.',
  },
};

export const steps = ['Välj box', 'Installera via USB', 'Anslut Wi-Fi', 'Välj station', 'Kontrollera'];

export function safeServerURL(raw) {
  const value = raw.trim();
  if (!value || /[\s\\]/.test(value)) throw new Error('Skriv serverns webbadress, till exempel http://192.168.2.160:8787.');
  const url = new URL(value.includes('://') ? value : `http://${value}`);
  if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || !url.hostname) {
    throw new Error('Använd en http- eller https-adress utan lösenord i adressen.');
  }
  return url.href;
}

export function availableRelease(catalog, id) {
  const profile = profiles[id];
  if (!profile || catalog?.schema !== 1 || !Array.isArray(catalog.profiles)) return null;
  const found = catalog.profiles.find(item => item.id === id);
  // Only our packaged, same-origin paths may become a flash target. In
  // particular, URL/query parameters never supply firmware or manifests.
  if (!found || found.chipFamily !== profile.chip || found.hardwareTested !== false
      || !/^[0-9a-f]{64}$/.test(found.sha256 ?? '')
      || !/^\d+\.\d+\.\d+$/.test(catalog.version ?? '')
      || found.manifest !== `firmware/${id}/manifest.json`
      || found.image !== `firmware/${id}/firmware.bin`
      || !Number.isInteger(found.bytes) || found.bytes < 1024) return null;
  return { ...found, version: catalog.version };
}

export function canInstall({ id, catalog, acknowledged, secure, serial, toolsReady }) {
  return Boolean(acknowledged && secure && serial && toolsReady && availableRelease(catalog, id));
}

export async function verifyImage(release, fetcher = fetch, cryptoAPI = crypto) {
  const response = await fetcher(release.image, { cache: 'no-store' });
  if (!response.ok) throw new Error('Firmwarefilen gick inte att hämta. Försök igen.');
  const bytes = await response.arrayBuffer();
  const digest = await cryptoAPI.subtle.digest('SHA-256', bytes);
  const hex = Array.from(new Uint8Array(digest), n => n.toString(16).padStart(2, '0')).join('');
  if (bytes.byteLength !== release.bytes || hex !== release.sha256) {
    throw new Error('Filen stämmer inte med versionens kontrollsumma. Installationen är spärrad.');
  }
  return bytes;
}
