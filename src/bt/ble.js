/**
 * ble.js — Couche Web Bluetooth Nestor
 * UUID Service : 12345678-1234-5678-1234-56789abcdef0
 */
export const NESTOR_SERVICE = '12345678-1234-5678-1234-56789abcdef0';
export const CHAR = {
  WIFI_SCAN:      '12345678-0001-5678-1234-56789abcdef0',
  WIFI_PROVISION: '12345678-0002-5678-1234-56789abcdef0',
  AGENT_SYNC:     '12345678-0003-5678-1234-56789abcdef0',
  TEXT_INPUT:     '12345678-0004-5678-1234-56789abcdef0',
  LLM_RELAY:      '12345678-0005-5678-1234-56789abcdef0',
  DEVICE_STATUS:  '12345678-0006-5678-1234-56789abcdef0',
  GPS:            '6e400003-b5a3-f393-e0a9-e50e24dcca9e',
};

let _device = null, _server = null, _chars = {};
let _onDisconnect = null, _onStatusChange = null;
let _gpsWatchId = null;

// Encoder en bytes — Uint8Array transmis tel quel, string/objet sérialisé en UTF-8
const enc = v => v instanceof Uint8Array
  ? v
  : new TextEncoder().encode(typeof v === 'string' ? v : JSON.stringify(v));
const dec = v => new TextDecoder().decode(v instanceof DataView ? v.buffer : v);

export const bleAvailable   = () => !!(navigator.bluetooth);
export const bleConnected   = () => !!(_device?.gatt?.connected);
export const bleDeviceName  = () => _device?.name || null;
export const onDisconnect   = fn => { _onDisconnect  = fn; };
export const onStatusChange = fn => { _onStatusChange = fn; };

export async function bleConnect() {
  if (!bleAvailable()) throw new Error('Web Bluetooth non disponible — utilise Chrome sur Android');
  _device = await navigator.bluetooth.requestDevice({
    filters: [{ name: 'Nestor' }],
    optionalServices: [NESTOR_SERVICE],
  });
  _device.addEventListener('gattserverdisconnected', () => {
    stopGpsWatch();
    _chars = {}; _server = null;
    if (_onDisconnect) _onDisconnect();
  });
  _server = await _device.gatt.connect();
  const svc = await _server.getPrimaryService(NESTOR_SERVICE);
  for (const [key, uuid] of Object.entries(CHAR)) {
    try { _chars[key] = await svc.getCharacteristic(uuid); }
    catch { console.warn('[BLE] char manquante:', key); }
  }
  if (_chars.DEVICE_STATUS) {
    await _chars.DEVICE_STATUS.startNotifications();
    _chars.DEVICE_STATUS.addEventListener('characteristicvaluechanged', e => {
      try { if (_onStatusChange) _onStatusChange(JSON.parse(dec(e.target.value))); } catch {}
    });
  }
  // Démarrer la surveillance GPS dès que la connexion BLE est établie
  startGpsWatch();
  return _device.name;
}

export async function bleDisconnect() {
  stopGpsWatch();
  if (_device?.gatt?.connected) _device.gatt.disconnect();
  _chars = {}; _server = null; _device = null;
}

export async function bleWrite(charKey, value) {
  const char = _chars[charKey];
  if (!char) throw new Error(`[BLE] char ${charKey} indisponible`);
  const data = enc(value);
  const CHUNK = 512;
  if (data.length <= CHUNK) {
    await char.writeValueWithResponse(data);
  } else {
    const total = Math.ceil(data.length / CHUNK);
    for (let i = 0; i < total; i++) {
      const chunk = data.slice(i * CHUNK, (i + 1) * CHUNK);
      await char.writeValueWithResponse(chunk);
      await new Promise(r => setTimeout(r, 20));
    }
  }
}

export async function bleRead(charKey) {
  const char = _chars[charKey];
  if (!char) throw new Error(`[BLE] char ${charKey} indisponible`);
  return dec(await char.readValue());
}

export async function bleSubscribe(charKey, callback) {
  const char = _chars[charKey];
  if (!char) throw new Error(`[BLE] char ${charKey} indisponible`);
  await char.startNotifications();
  char.addEventListener('characteristicvaluechanged', e => {
    try { callback(dec(e.target.value)); } catch {}
  });
}

// Surveiller la géolocalisation et envoyer lat/lon via BLE toutes les ≤1 s
// Encodage : 8 octets float32 little-endian (lat 0-3, lon 4-7)
export function startGpsWatch() {
  if (_gpsWatchId !== null || !('geolocation' in navigator) || !bleConnected()) return;
  _gpsWatchId = navigator.geolocation.watchPosition(
    pos => {
      if (!_chars.GPS) return;
      const buf  = new ArrayBuffer(8);
      const view = new DataView(buf);
      view.setFloat32(0, pos.coords.latitude,  true);
      view.setFloat32(4, pos.coords.longitude, true);
      bleWrite('GPS', new Uint8Array(buf)).catch(e => console.warn('[GPS]', e.message));
    },
    err => console.warn('[GPS] watchPosition:', err.message),
    { maximumAge: 0, timeout: 1000, enableHighAccuracy: true }
  );
}

// Arrêter la surveillance GPS et libérer la ressource
export function stopGpsWatch() {
  if (_gpsWatchId !== null) {
    navigator.geolocation.clearWatch(_gpsWatchId);
    _gpsWatchId = null;
  }
}
