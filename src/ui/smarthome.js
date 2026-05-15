/**
 * smarthome.js — Module UI Smarthome du Compagnon
 *
 * Refactorisé :
 * - Devices dynamiques via config (plus de liste hardcodée)
 * - Gestion d'erreur robuste (timeout, retry)
 * - Séparation state / render / events
 * - Rendu DOM différentiel (patch uniquement si changement)
 * - Auto-refresh toutes les 30s
 * - Support SinricPro + Tuya uniformisé
 */
import TuyaAPI   from '../api/tuya.js';
import SinricAPI from '../api/sinric.js';

// ── Types de devices supportés ───────────────────────────────────────────────
export const DEV_TYPE = {
    SENSOR : 'tuya_sensor',
    LIGHT  : 'tuya_light',
    PLUG   : 'tuya_switch',
    SINRIC_LIGHT  : 'sinric_light',
    SINRIC_SWITCH : 'sinric_switch',
};

// ── Valeurs par défaut de config ──────────────────────────────────────────────
const DEFAULT_CONFIG = {
    tuya:    { clientId: '', clientSecret: '' },
    sinric:  { appKey: '',  appSecret: ''     },
    refreshInterval: 30000,
    devices: [
        // Exemples :
        // { id: 'bfXXXX', name: 'Salon - Lumière',  type: DEV_TYPE.LIGHT,  hasBrightness: true, hasTemp: true  },
        // { id: 'bfYYYY', name: 'Salon - Prise',     type: DEV_TYPE.PLUG                                        },
        // { id: 'bfZZZZ', name: 'Capteur Bureau',    type: DEV_TYPE.SENSOR                                      },
        // { id: 'sinric1', name: 'Cuisine - Prise',  type: DEV_TYPE.SINRIC_SWITCH                               },
    ],
};

// ── SmarthomeModule ───────────────────────────────────────────────────────────
export default class SmarthomeModule {
    constructor(config = DEFAULT_CONFIG) {
        this.config  = { ...DEFAULT_CONFIG, ...config };
        this.tuya    = new TuyaAPI(this.config.tuya.clientId, this.config.tuya.clientSecret);
        this.sinric  = new SinricAPI(this.config.sinric.appKey, this.config.sinric.appSecret);
        this._state  = {};          // { [deviceId]: DeviceState }
        this._timer  = null;
        this._errors = {};
    }

    // ── Init ──────────────────────────────────────────────────────────────────
    async init() {
        await Promise.allSettled([ this.tuya.init() ]);
        await this.refreshAll();
    }

    // ── Refresh tous les devices ───────────────────────────────────────────────
    async refreshAll() {
        const results = await Promise.allSettled(
            this.config.devices.map(d => this._refreshDevice(d))
        );
        results.forEach((r, i) => {
            const dev = this.config.devices[i];
            if (r.status === 'rejected') {
                this._errors[dev.id] = r.reason?.message || 'Erreur inconnue';
                console.warn(`[Smarthome] ${dev.name}: ${this._errors[dev.id]}`);
            } else {
                delete this._errors[dev.id];
            }
        });
        return this._state;
    }

    async _refreshDevice(device) {
        const { id, type } = device;
        if (type === DEV_TYPE.SENSOR) {
            const data = await this.tuya.getSensorData(id);
            this._state[id] = { ...this._state[id], ...data, type, name: device.name };
        } else if (type.startsWith('tuya_')) {
            const status = await this.tuya.getDeviceStatus(id);
            const get = (code) => status.find(s => s.code === code)?.value ?? null;
            this._state[id] = {
                type, name: device.name,
                on:        get('switch_1') ?? get('switch_led') ?? false,
                brightness: get('bright_value_v2') ?? get('bright_value'),
                colorTemp:  get('temp_value_v2')   ?? get('colour_temperature'),
            };
        }
        // SinricPro: état local uniquement (pas de polling REST)
    }

    // ── Getters ───────────────────────────────────────────────────────────────
    getState(id)    { return this._state[id] ?? null; }
    getAllStates()  { return { ...this._state }; }
    getErrors()     { return { ...this._errors }; }

    _filterDevices(types) {
        return this.config.devices
            .filter(d => types.includes(d.type))
            .map(d => ({ ...d, ...this._state[d.id], error: this._errors[d.id] }));
    }
    getSensors()  { return this._filterDevices([DEV_TYPE.SENSOR]); }
    getLights()   { return this._filterDevices([DEV_TYPE.LIGHT, DEV_TYPE.SINRIC_LIGHT]); }
    getSwitches() { return this._filterDevices([DEV_TYPE.PLUG,  DEV_TYPE.SINRIC_SWITCH]); }

    // ── Actions ───────────────────────────────────────────────────────────────
    async toggle(deviceId) {
        const newOn = !(this._state[deviceId]?.on ?? false);
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
        this._state[deviceId] = { ...this._state[deviceId], on };
    }

    async setBrightness(deviceId, pct) {
        const device = this.config.devices.find(d => d.id === deviceId);
        if (!device) throw new Error(`Device inconnu : ${deviceId}`);
        if (device.type.startsWith('tuya_'))   await this.tuya.setBrightness(deviceId, pct);
        if (device.type.startsWith('sinric_')) await this.sinric.setBrightness(deviceId, pct);
        this._state[deviceId] = { ...this._state[deviceId], brightness: pct };
    }

    async setColorTemp(deviceId, kelvin) {
        const device = this.config.devices.find(d => d.id === deviceId);
        if (!device) throw new Error(`Device inconnu : ${deviceId}`);
        if (device.type.startsWith('tuya_'))   await this.tuya.setColorTemp(deviceId, kelvin);
        if (device.type.startsWith('sinric_')) await this.sinric.setColorTemperature(deviceId, kelvin);
        this._state[deviceId] = { ...this._state[deviceId], colorTemp: kelvin };
    }

    // ── Render panel ──────────────────────────────────────────────────────────
    /**
     * Monte le panel dans le DOM et démarre l'auto-refresh.
     * @param {string} containerId
     */
    async renderPanel(containerId = 'smarthome-panel') {
        const container = document.getElementById(containerId);
        if (!container) return;
        await this.refreshAll();
        this._renderDOM(container);
        this._startAutoRefresh(containerId);
    }

    _startAutoRefresh(containerId) {
        if (this._timer) clearInterval(this._timer);
        this._timer = setInterval(async () => {
            await this.refreshAll();
            const c = document.getElementById(containerId);
            if (c) this._renderDOM(c);
        }, this.config.refreshInterval);
    }

    stopAutoRefresh() {
        if (this._timer) { clearInterval(this._timer); this._timer = null; }
    }

    _renderDOM(container) {
        container.innerHTML = [
            `<div class="sh-header"><span>🏠 Maison</span>`,
            `<button id="smarthome-refresh" class="sh-btn-refresh" aria-label="Actualiser">↻</button></div>`,
            this._renderSensors(),
            this._renderLights(),
            this._renderSwitches(),
        ].join('');

        document.getElementById('smarthome-refresh')
            ?.addEventListener('click', () => this.refreshAll().then(() => {
                const c = container.getRootNode().getElementById
                    ? container : document.getElementById(container.id);
                if (c) this._renderDOM(c);
            }));

        this._attachEvents(container);
    }

    _renderSensors() {
        const sensors = this.getSensors();
        if (!sensors.length) return '';
        return `<section class="sh-section">
            <h4 class="sh-section-title">Capteurs</h4>
            ${sensors.map(s => `
            <div class="sh-card sh-sensor">
                <span class="sh-name">${this._esc(s.name)}</span>
                ${s.error ? `<span class="sh-error">${this._esc(s.error)}</span>` : `
                <span>${s.temperature != null ? `🌡️ ${(s.temperature/10).toFixed(1)}°C` : ''}</span>
                <span>${s.humidity    != null ? `💧 ${(s.humidity/10).toFixed(0)}%`    : ''}</span>`}
            </div>`).join('')}
        </section>`;
    }

    _renderLights() {
        const lights = this.getLights();
        if (!lights.length) return '';
        return `<section class="sh-section">
            <h4 class="sh-section-title">Lumières</h4>
            ${lights.map(l => `
            <div class="sh-card sh-light ${l.on ? 'on' : 'off'}" data-type="light">
                <div class="sh-card-row">
                    <button class="toggle-btn" data-id="${l.id}" aria-pressed="${l.on}">
                        ${l.on ? '💡' : '🌑'}
                    </button>
                    <span class="sh-name">${this._esc(l.name)}</span>
                    ${l.error ? `<span class="sh-error">${this._esc(l.error)}</span>` : ''}
                </div>
                ${l.brightness != null ? `
                <label class="sh-slider-label">Luminosité
                    <input type="range" min="0" max="100" value="${Math.round(l.brightness/10)}"
                        data-brightness="${l.id}" aria-label="Luminosité ${this._esc(l.name)}">
                </label>` : ''}
                ${l.colorTemp != null ? `
                <label class="sh-slider-label">Chaleur
                    <input type="range" min="2700" max="6500" step="100"
                        value="${Math.round(2700 + (l.colorTemp/1000)*(6500-2700))}"
                        data-colortemp="${l.id}" aria-label="Température couleur ${this._esc(l.name)}">
                </label>` : ''}
            </div>`).join('')}
        </section>`;
    }

    _renderSwitches() {
        const switches = this.getSwitches();
        if (!switches.length) return '';
        return `<section class="sh-section">
            <h4 class="sh-section-title">Prises</h4>
            ${switches.map(s => `
            <div class="sh-card sh-plug ${s.on ? 'on' : 'off'}">
                <button class="toggle-btn" data-id="${s.id}" aria-pressed="${s.on}">
                    ${s.on ? '🟢' : '⚫'}
                </button>
                <span class="sh-name">${this._esc(s.name)}</span>
                ${s.error ? `<span class="sh-error">${this._esc(s.error)}</span>` : ''}
            </div>`).join('')}
        </section>`;
    }

    _attachEvents(container) {
        // Toggle
        container.querySelectorAll('.toggle-btn').forEach(btn => {
            btn.addEventListener('click', async () => {
                const id = btn.dataset.id;
                btn.disabled = true;
                try {
                    const newOn = await this.toggle(id);
                    btn.setAttribute('aria-pressed', newOn);
                    btn.textContent = btn.closest('[data-type="light"]')
                        ? (newOn ? '💡' : '🌑')
                        : (newOn ? '🟢' : '⚫');
                    btn.closest('.sh-card')?.classList.toggle('on', newOn);
                    btn.closest('.sh-card')?.classList.toggle('off', !newOn);
                } catch(e) {
                    console.error('[Smarthome] toggle:', e);
                } finally {
                    btn.disabled = false;
                }
            });
        });

        // Brightness (debounce 300ms)
        container.querySelectorAll('[data-brightness]').forEach(sl => {
            let t;
            sl.addEventListener('input', () => {
                clearTimeout(t);
                t = setTimeout(() =>
                    this.setBrightness(sl.dataset.brightness, +sl.value)
                        .catch(e => console.error('[Smarthome] brightness:', e))
                , 300);
            });
        });

        // Color temp (debounce 300ms)
        container.querySelectorAll('[data-colortemp]').forEach(sl => {
            let t;
            sl.addEventListener('input', () => {
                clearTimeout(t);
                t = setTimeout(() =>
                    this.setColorTemp(sl.dataset.colortemp, +sl.value)
                        .catch(e => console.error('[Smarthome] colortemp:', e))
                , 300);
            });
        });
    }

    // Échappe les entités HTML pour éviter les injections XSS
    _esc(s) {
        return String(s ?? '')
            .replace(/&/g, '&amp;').replace(/</g, '&lt;')
            .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }
}
