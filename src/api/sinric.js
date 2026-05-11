/**
 * sinric.js — Client SinricPro (bridge ESP32/Web → Alexa)
 * Utilise l'API REST SinricPro pour contrôler les devices Alexa depuis le web.
 *
 * Config requise :
 *   SINRIC_APP_KEY    → sinric.pro → Credentials → App Key
 *   SINRIC_APP_SECRET → sinric.pro → Credentials → App Secret
 *
 * Utilisation :
 *   import SinricAPI from './sinric.js';
 *   const sinric = new SinricAPI(appKey, appSecret);
 *   await sinric.turnOn(deviceId);
 *   await sinric.setVolume(deviceId, 50);
 */

const SINRIC_BASE = 'https://api.sinric.pro/api/v1';

export default class SinricAPI {
  constructor(appKey, appSecret) {
    this.appKey = appKey;
    this.appSecret = appSecret;
    this._token = null;
    this._tokenExpiry = 0;
  }

  // ── Auth JWT ───────────────────────────────────────────────────────────

  async _getToken() {
    if (this._token && Date.now() < this._tokenExpiry) return this._token;
    const res = await fetch(`${SINRIC_BASE}/auth`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ appKey: this.appKey, appSecret: this.appSecret })
    });
    const data = await res.json();
    if (!data.success) throw new Error('SinricPro auth failed: ' + (data.message || JSON.stringify(data)));
    this._token = data.accessToken;
    // Token valide 24h par défaut
    this._tokenExpiry = Date.now() + 23 * 3600 * 1000;
    return this._token;
  }

  async _request(method, path, body = null) {
    const token = await this._getToken();
    const res = await fetch(SINRIC_BASE + path, {
      method,
      headers: {
        'Authorization': `Bearer ${token}`,
        'Content-Type': 'application/json'
      },
      body: body ? JSON.stringify(body) : undefined
    });
    const data = await res.json();
    if (!data.success) throw new Error(`SinricPro error [${path}]: ${data.message}`);
    return data;
  }

  // ── Devices ────────────────────────────────────────────────────────────

  /** Liste tous les appareils enregistrés dans SinricPro */
  async getDevices() {
    return this._request('GET', '/devices');
  }

  // ── Actions génériques ─────────────────────────────────────────────────

  async _action(deviceId, action, value = {}) {
    return this._request('PUT', `/devices/${deviceId}/${action}`, value);
  }

  // ── Raccourcis communs ─────────────────────────────────────────────────

  /** Allume un device (prise, lumière…) */
  async turnOn(deviceId)  { return this._action(deviceId, 'action', { action: 'setPowerState', value: { state: 'On'  } }); }
  /** Éteint un device */
  async turnOff(deviceId) { return this._action(deviceId, 'action', { action: 'setPowerState', value: { state: 'Off' } }); }

  /** Luminosité (0-100) */
  async setBrightness(deviceId, pct) {
    return this._action(deviceId, 'action', { action: 'setBrightness', value: { brightness: Math.round(pct) } });
  }

  /** Volume (0-100) — pour Echo/TV */
  async setVolume(deviceId, vol) {
    return this._action(deviceId, 'action', { action: 'setVolume', value: { volume: Math.round(vol) } });
  }

  /** Couleur RGB */
  async setColor(deviceId, r, g, b) {
    return this._action(deviceId, 'action', { action: 'setColor', value: { color: { r, g, b } } });
  }

  /** Température de couleur (Kelvin) */
  async setColorTemperature(deviceId, kelvin) {
    return this._action(deviceId, 'action', { action: 'setColorTemperature', value: { colorTemperature: kelvin } });
  }
}
