/**
 * device_settings.js — Config ESP32 depuis le téléphone
 * Inclut la gestion PMIC AXP2101 + batterie AT103030 (1000mAh, 3.7V)
 */
import { bleWrite, bleRead } from '../bt/ble.js';
import { lsGet, lsSet } from '../storage/agents-db.js';

const LS_CFG = 'nestor_device_config';

// ─── AT103030 : LiPo 1000mAh 3.7V — courbe SoC calibrée ───────────────────
export const BATTERY_PROFILE = {
  model:          'AT103030',
  capacityMah:    1000,
  chemistry:      'LiPo',
  voltageMin:     3.0,   // V — seuil coupure critique
  voltageLow:     3.3,   // V — seuil alarme basse
  voltageNominal: 3.7,   // V — tension nominale
  voltageFull:    4.2,   // V — charge complète
  socCurve: [
    { v: 4.20, pct: 100 },
    { v: 4.10, pct:  92 },
    { v: 4.00, pct:  82 },
    { v: 3.90, pct:  70 },
    { v: 3.80, pct:  55 },
    { v: 3.70, pct:  40 },
    { v: 3.60, pct:  26 },
    { v: 3.50, pct:  14 },
    { v: 3.40, pct:   6 },
    { v: 3.30, pct:   2 },
    { v: 3.00, pct:   0 },
  ],
};

export function voltageToSoc(voltage) {
  const c = BATTERY_PROFILE.socCurve;
  if (voltage >= c[0].v) return 100;
  if (voltage <= c[c.length - 1].v) return 0;
  for (let i = 0; i < c.length - 1; i++) {
    if (voltage <= c[i].v && voltage >= c[i + 1].v) {
      const ratio = (voltage - c[i + 1].v) / (c[i].v - c[i + 1].v);
      return Math.round(c[i + 1].pct + ratio * (c[i].pct - c[i + 1].pct));
    }
  }
  return 0;
}

// ─── AXP2101 — registres I2C addr 0x34 ────────────────────────────────────
export const AXP2101_REGS = {
  STATUS:             0x00,
  PMU_CFG:            0x10,
  CHARGE_CFG:         0x14,
  CHARGE_CTRL:        0x15,
  TERM_CFG:           0x16,
  VBAT_ADC_H:         0x34,
  VBAT_ADC_L:         0x35,
  VBUS_ADC_H:         0x36,
  IBUS_ADC_H:         0x38,
  IRQ_EN:             0x40,
  IRQ_STATUS:         0x48,
  TS_CFG:             0x50,
  GAUGE_CFG:          0xA0,
};

export const AXP2101_CHARGE_CONFIG = {
  chargeVoltageMv:       4200,
  chargeCurrentMa:        500,
  terminationCurrentMa:    50,
  lowBattThresholdMv:    3300,
  uvloThresholdMv:       3000,
  gaugeEnabled:          true,
  irqMask: {
    chargeComplete: true,
    batteryLow:     true,
    batteryInsert:  true,
    batteryRemove:  true,
    vbusInsert:     true,
    vbusRemove:     true,
  },
};

const DEFAULT = {
  llm: {
    provider: 'github_models',
    model:    'gpt-4o-mini',
    apiKey:   '',
    proxyUrl: 'https://proxy.sicho95.workers.dev',
  },
  display: { brightness: 80, sleepAfterMs: 30000, theme: 'dark' },
  system:  { name: 'Nestor', language: 'fr', ntpServer: 'pool.ntp.org', timezone: 'Europe/Paris' },
  ble:     { relayLlmOnNoWifi: true, autoSyncAgents: true },
  battery: {
    model:                'AT103030',
    capacityMah:          1000,
    alertLowMv:           3300,
    alertCriticalMv:      3000,
    chargeVoltageMv:      4200,
    chargeCurrentMa:       500,
    terminationCurrentMa:   50,
    gaugeEnabled:          true,
    displayMode:          'percent',  // 'percent' | 'voltage'
  },
};

function deepMerge(base, over) {
  const r = { ...base };
  for (const k of Object.keys(over))
    r[k] = (over[k] && typeof over[k] === 'object' && !Array.isArray(over[k]))
      ? deepMerge(base[k] || {}, over[k]) : over[k];
  return r;
}

export function getDeviceConfig() {
  try { return deepMerge(DEFAULT, JSON.parse(lsGet(LS_CFG)) || {}); }
  catch { return { ...DEFAULT }; }
}
export function saveDeviceConfig(cfg) { lsSet(LS_CFG, JSON.stringify(cfg)); }

export async function pushDeviceConfig(cfg) {
  saveDeviceConfig(cfg);
  await bleWrite('AGENT_SYNC', { cmd: 'config', config: cfg });
}

export async function pullDeviceConfig() {
  await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_config' }));
  const raw = await bleRead('AGENT_SYNC');
  const parsed = JSON.parse(raw);
  if (parsed.config) { const m = deepMerge(DEFAULT, parsed.config); saveDeviceConfig(m); return m; }
  return getDeviceConfig();
}

export async function pushBatteryConfig(batteryCfg) {
  await bleWrite('AGENT_SYNC', JSON.stringify({
    cmd:     'pmic_config',
    battery: batteryCfg,
    axp:     AXP2101_CHARGE_CONFIG,
  }));
}

export async function pullBatteryStatus() {
  await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'battery_status' }));
  const raw = await bleRead('AGENT_SYNC');
  const data = JSON.parse(raw);
  return {
    voltageMv: data.voltage_mv ?? 0,
    soc:       data.soc        ?? voltageToSoc((data.voltage_mv ?? 0) / 1000),
    charging:  data.charging   ?? false,
    sourceMv:  data.source_mv  ?? 0,
  };
}
