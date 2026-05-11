/**
 * smarthome.js — Module UI Smarthome du Compagnon
 *
 * Gère l'interface domotique :
 *   - Lumières Tuya (on/off, luminosité, température couleur)
 *   - Prises connectées Alexa via SinricPro
 *   - Capteurs température/humidité Tuya
 *   - Aspirateur Ecovacs (stub — intégration future)
 *
 * Dépend de :
 *   import TuyaAPI  from '../api/tuya.js';
 *   import SinricAPI from '../api/sinric.js';
 *
 * Config (à placer dans src/storage/config.js ou passer au constructeur) :
 *   TUYA_CLIENT_ID, TUYA_CLIENT_SECRET
 *   SINRIC_APP_KEY, SINRIC_APP_SECRET
 */

import TuyaAPI   from '../api/tuya.js';
import SinricAPI from '../api/sinric.js';

// ── Config (à externaliser dans config.js / NVS / settings UI) ────────────
const CONFIG = {
  tuya:   { clientId: '',     clientSecret: ''   },
  sinric: { appKey:   '',     appSecret:    ''   },

  // Devices déclarés manuellement (obtenir les IDs via tuya.getDevices() / sinric.getDevices())
  devices: [
    // Exemples :
    // { id: 'bfXXXX', name: 'Salon - Lumière', type: 'tuya_light',   hasBrightness: true, hasTemp: true },
    // { id: 'bfYYYY', name: 'Salon - Prise',   type: 'tuya_switch'  },
    // { id: 'aaaaaa', name: 'Cuisine - Prise',  type: 'sinric_switch' },
    // { id: 'bbbbbb', name: 'Capteur Bureau',   type: 'tuya_sensor'  },
  ]
};

// ── SmarthomeModule ────────────────────────────────────────────────────────

export default class SmarthomeModule {
  constructor(config = CONFIG) {
    this.config = config;
    this.tuya   = new TuyaAPI(config.tuya.clientId, config.tuya.clientSecret);
    this.sinric = new SinricAPI(config.sinric.appKey, config.sinric.appSecret);
    this._state = {};  // cache état local { deviceId: { on, brightness, temp, ... } }
  }

  // ── Init ──────────────────────────────────────────────────────────────

  async init() {
    await Promise.allSettled([
      this.tuya.init(),
      // SinricPro s'authentifie à la première requête
    ]);
    await this.refreshAll();
  }

  // ── Refresh état de tous les devices ──────────────────────────────────

  async refreshAll() {
    const results = await Promise.allSettled(
      this.config.devices.map(d => this._refreshDevice(d))
    );
    results.forEach((r, i) => {
      if (r.status === 'rejected') {
        console.warn(`[Smarthome] Refresh failed for ${this.config.devices[i].name}:`, r.reason);
      }
    });
    return this._state;
  }

  async _refreshDevice(device) {
    if (device.type === 'tuya_sensor') {
      const data = await this.tuya.getSensorData(device.id);
      this._state[device.id] = { ...this._state[device.id], ...data, type: device.type, name: device.name };
    } else if (device.type.startsWith('tuya_')) {
      const status = await this.tuya.getDeviceStatus(device.id);
      const get = (code) => status.find(s => s.code === code)?.value ?? null;
      this._state[device.id] = {
        type: device.type,
        name: device.name,
        on:         get('switch_1') ?? get('switch_led') ?? false,
        brightness: get('bright_value_v2') ?? get('bright_value'),
        colorTemp:  get('temp_value_v2') ?? get('colour_temperature'),
      };
    }
    // SinricPro ne fournit pas d'état en REST polling → état local uniquement
  }

  // ── Getters état ──────────────────────────────────────────────────────

  getState(deviceId) {
    return this._state[deviceId] ?? null;
  }

  getAllStates() {
    return { ...this._state };
  }

  getSensors() {
    return this.config.devices
      .filter(d => d.type === 'tuya_sensor')
      .map(d => ({ ...d, ...this._state[d.id] }));
  }

  getLights() {
    return this.config.devices
      .filter(d => d.type === 'tuya_light' || d.type === 'sinric_light')
      .map(d => ({ ...d, ...this._state[d.id] }));
  }

  getSwitches() {
    return this.config.devices
      .filter(d => d.type === 'tuya_switch' || d.type === 'sinric_switch')
      .map(d => ({ ...d, ...this._state[d.id] }));
  }

  // ── Actions ───────────────────────────────────────────────────────────

  async toggle(deviceId) {
    const state = this._state[deviceId];
    const newOn = !(state?.on ?? false);
    await this._setOn(deviceId, newOn);
    return newOn;
  }

  async _setOn(deviceId, on) {
    const device = this.config.devices.find(d => d.id === deviceId);
    if (!device) throw new Error(`Device inconnu : ${deviceId}`);

    if (device.type.startsWith('tuya_')) {
      on ? await this.tuya.turnOn(deviceId) : await this.tuya.turnOff(deviceId);
    } else if (device.type.startsWith('sinric_')) {
      on ? await this.sinric.turnOn(deviceId) : await this.sinric.turnOff(deviceId);
    }
    if (!this._state[deviceId]) this._state[deviceId] = {};
    this._state[deviceId].on = on;
  }

  async setBrightness(deviceId, pct) {
    const device = this.config.devices.find(d => d.id === deviceId);
    if (!device) throw new Error(`Device inconnu : ${deviceId}`);

    if (device.type.startsWith('tuya_')) {
      await this.tuya.setBrightness(deviceId, pct);
    } else if (device.type.startsWith('sinric_')) {
      await this.sinric.setBrightness(deviceId, pct);
    }
    if (!this._state[deviceId]) this._state[deviceId] = {};
    this._state[deviceId].brightness = pct;
  }

  async setColorTemp(deviceId, kelvin) {
    const device = this.config.devices.find(d => d.id === deviceId);
    if (!device) throw new Error(`Device inconnu : ${deviceId}`);

    if (device.type.startsWith('tuya_')) {
      await this.tuya.setColorTemp(deviceId, kelvin);
    } else if (device.type.startsWith('sinric_')) {
      await this.sinric.setColorTemperature(deviceId, kelvin);
    }
    if (!this._state[deviceId]) this._state[deviceId] = {};
    this._state[deviceId].colorTemp = kelvin;
  }

  // ── Render UI (injection dans le DOM du Compagnon) ────────────────────

  /**
   * Génère le HTML de la carte smarthome à injecter dans l'app principale.
   * Appelle refreshAll() puis injecte dans le conteneur #smarthome-panel.
   */
  async renderPanel(containerId = 'smarthome-panel') {
    await this.refreshAll();
    const container = document.getElementById(containerId);
    if (!container) return;

    container.innerHTML = `
      <div class="smarthome-panel">
        <h2 class="smarthome-title">🏠 Maison</h2>

        ${this._renderSensorsSection()}
        ${this._renderLightsSection()}
        ${this._renderSwitchesSection()}

        <div class="smarthome-footer">
          <button class="smarthome-refresh-btn" id="smarthome-refresh">↻ Actualiser</button>
        </div>
      </div>
    `;

    document.getElementById('smarthome-refresh')?.addEventListener('click', () => this.renderPanel(containerId));
    this._attachEvents(containerId);
  }

  _renderSensorsSection() {
    const sensors = this.getSensors();
    if (!sensors.length) return '';
    return `
      <section class="smarthome-section">
        <h3>Capteurs</h3>
        <div class="smarthome-sensors">
          ${sensors.map(s => `
            <div class="sensor-card">
              <span class="sensor-name">${s.name}</span>
              <span class="sensor-value">
                ${s.temperature != null ? `🌡️ ${(s.temperature / 10).toFixed(1)} °C` : ''}
                ${s.humidity    != null ? `💧 ${(s.humidity    / 10).toFixed(0)} %`   : ''}
              </span>
            </div>
          `).join('')}
        </div>
      </section>`;
  }

  _renderLightsSection() {
    const lights = this.getLights();
    if (!lights.length) return '';
    return `
      <section class="smarthome-section">
        <h3>Lumières</h3>
        <div class="smarthome-lights">
          ${lights.map(l => `
            <div class="device-card ${l.on ? 'on' : 'off'}" data-id="${l.id}" data-type="light">
              <div class="device-row">
                <span class="device-name">${l.name}</span>
                <button class="toggle-btn" data-id="${l.id}">${l.on ? '💡' : '🌑'}</button>
              </div>
              ${l.brightness != null ? `
                <label>Luminosité
                  <input type="range" min="0" max="100" value="${Math.round(l.brightness / 10)}"
                    data-brightness="${l.id}">
                </label>` : ''}
              ${l.colorTemp != null ? `
                <label>Chaleur
                  <input type="range" min="2700" max="6500" step="100" value="${
                    Math.round(2700 + (l.colorTemp / 1000) * (6500 - 2700))
                  }" data-colortemp="${l.id}">
                </label>` : ''}
            </div>
          `).join('')}
        </div>
      </section>`;
  }

  _renderSwitchesSection() {
    const switches = this.getSwitches();
    if (!switches.length) return '';
    return `
      <section class="smarthome-section">
        <h3>Prises</h3>
        <div class="smarthome-switches">
          ${switches.map(s => `
            <div class="device-card ${s.on ? 'on' : 'off'}" data-id="${s.id}" data-type="switch">
              <span class="device-name">${s.name}</span>
              <button class="toggle-btn" data-id="${s.id}">${s.on ? '🟢' : '⚫'}</button>
            </div>
          `).join('')}
        </div>
      </section>`;
  }

  _attachEvents(containerId) {
    const container = document.getElementById(containerId);
    if (!container) return;

    // Toggle on/off
    container.querySelectorAll('.toggle-btn').forEach(btn => {
      btn.addEventListener('click', async () => {
        const id = btn.dataset.id;
        try {
          const newOn = await this.toggle(id);
          btn.textContent = newOn ? (btn.closest('[data-type="light"]') ? '💡' : '🟢')
                                  : (btn.closest('[data-type="light"]') ? '🌑' : '⚫');
          btn.closest('.device-card')?.classList.toggle('on',  newOn);
          btn.closest('.device-card')?.classList.toggle('off', !newOn);
        } catch(e) { console.error('[Smarthome] toggle error:', e); }
      });
    });

    // Brightness sliders
    container.querySelectorAll('[data-brightness]').forEach(slider => {
      slider.addEventListener('change', async () => {
        try { await this.setBrightness(slider.dataset.brightness, +slider.value); }
        catch(e) { console.error('[Smarthome] brightness error:', e); }
      });
    });

    // Color temp sliders
    container.querySelectorAll('[data-colortemp]').forEach(slider => {
      slider.addEventListener('change', async () => {
        try { await this.setColorTemp(slider.dataset.colortemp, +slider.value); }
        catch(e) { console.error('[Smarthome] colortemp error:', e); }
      });
    });
  }
}
