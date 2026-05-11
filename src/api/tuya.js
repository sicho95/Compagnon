/**
 * tuya.js — Client Tuya IoT Cloud API
 * Endpoint EU : https://openapi.tuyaeu.com
 *
 * Config requise (à stocker dans src/storage ou config.js) :
 *   TUYA_CLIENT_ID     → iot.tuya.com → Cloud Project → Client ID
 *   TUYA_CLIENT_SECRET → iot.tuya.com → Cloud Project → Client Secret
 *
 * Utilisation :
 *   import TuyaAPI from './tuya.js';
 *   const tuya = new TuyaAPI(clientId, clientSecret);
 *   await tuya.init();
 *   const devices = await tuya.getDevices();
 *   await tuya.sendCommand(deviceId, [{ code: 'switch_1', value: true }]);
 */

const TUYA_BASE = 'https://openapi.tuyaeu.com';

export default class TuyaAPI {
  constructor(clientId, clientSecret) {
    this.clientId = clientId;
    this.clientSecret = clientSecret;
    this.accessToken = null;
    this.tokenExpiry = 0;
  }

  // ── Auth ───────────────────────────────────────────────────────────────

  async _sign(method, path, body = '') {
    const ts = Date.now().toString();
    const bodyHash = await this._sha256(body);
    const strToSign = [
      method.toUpperCase(),
      bodyHash,
      '',
      path
    ].join('\n');

    const tokenStr = this.accessToken
      ? this.clientId + this.accessToken + ts + strToSign
      : this.clientId + ts + strToSign;

    const sign = await this._hmacSha256(this.clientSecret, tokenStr);
    return { ts, sign: sign.toUpperCase() };
  }

  async _sha256(str) {
    const buf = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(str));
    return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2, '0')).join('');
  }

  async _hmacSha256(key, message) {
    const enc = new TextEncoder();
    const k = await crypto.subtle.importKey('raw', enc.encode(key), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
    const sig = await crypto.subtle.sign('HMAC', k, enc.encode(message));
    return Array.from(new Uint8Array(sig)).map(b => b.toString(16).padStart(2, '0')).join('');
  }

  async _getToken() {
    if (this.accessToken && Date.now() < this.tokenExpiry) return;
    const path = '/v1.0/token?grant_type=1';
    const { ts, sign } = await this._sign('GET', path);
    const res = await fetch(TUYA_BASE + path, {
      headers: {
        'client_id': this.clientId,
        'sign': sign,
        't': ts,
        'sign_method': 'HMAC-SHA256'
      }
    });
    const data = await res.json();
    if (!data.success) throw new Error('Tuya auth failed: ' + data.msg);
    this.accessToken = data.result.access_token;
    this.tokenExpiry = Date.now() + (data.result.expire_time - 60) * 1000;
  }

  async init() {
    await this._getToken();
  }

  // ── Request helper ─────────────────────────────────────────────────────

  async _request(method, path, body = null) {
    await this._getToken();
    const bodyStr = body ? JSON.stringify(body) : '';
    const { ts, sign } = await this._sign(method, path, bodyStr);
    const res = await fetch(TUYA_BASE + path, {
      method,
      headers: {
        'client_id': this.clientId,
        'access_token': this.accessToken,
        'sign': sign,
        't': ts,
        'sign_method': 'HMAC-SHA256',
        'Content-Type': 'application/json'
      },
      body: body ? bodyStr : undefined
    });
    const data = await res.json();
    if (!data.success) throw new Error(`Tuya API error [${path}]: ${data.msg}`);
    return data.result;
  }

  // ── Devices ────────────────────────────────────────────────────────────

  /** Retourne la liste de tous les appareils liés au compte SmartLife */
  async getDevices() {
    return this._request('GET', '/v1.3/iot-03/devices');
  }

  /** Statut complet d'un appareil (température, humidité, switch…) */
  async getDeviceStatus(deviceId) {
    return this._request('GET', `/v1.0/devices/${deviceId}/status`);
  }

  /**
   * Envoie une commande à un appareil
   * @param {string} deviceId
   * @param {Array<{code: string, value: any}>} commands
   * Exemples :
   *   [{ code: 'switch_1', value: true }]           // allumer
   *   [{ code: 'bright_value_v2', value: 500 }]     // luminosité (0-1000)
   *   [{ code: 'colour_data_v2', value: {h,s,v} }]  // couleur HSV
   */
  async sendCommand(deviceId, commands) {
    return this._request('POST', `/v1.0/devices/${deviceId}/commands`, { commands });
  }

  /** Raccourcis ────────────────────────────────────────────────────────── */

  async turnOn(deviceId)  { return this.sendCommand(deviceId, [{ code: 'switch_1', value: true  }]); }
  async turnOff(deviceId) { return this.sendCommand(deviceId, [{ code: 'switch_1', value: false }]); }
  async setBrightness(deviceId, pct) {
    const value = Math.round(Math.max(10, Math.min(1000, pct * 10)));
    return this.sendCommand(deviceId, [{ code: 'bright_value_v2', value }]);
  }
  async setColorTemp(deviceId, kelvin) {
    // Tuya : 0 = chaud, 1000 = froid
    const value = Math.round(((kelvin - 2700) / (6500 - 2700)) * 1000);
    return this.sendCommand(deviceId, [{ code: 'temp_value_v2', value: Math.max(0, Math.min(1000, value)) }]);
  }

  /** Retourne { temperature, humidity } pour un capteur Tuya */
  async getSensorData(deviceId) {
    const status = await this.getDeviceStatus(deviceId);
    const get = (code) => status.find(s => s.code === code)?.value ?? null;
    return {
      temperature: get('va_temperature') ?? get('temp_current'),
      humidity:    get('va_humidity')    ?? get('humidity_value')
    };
  }
}
