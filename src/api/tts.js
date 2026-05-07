// tts.js — Moteur Text-to-Speech Nestor, cascade multi-provider
//
// Cascade dans l'ordre :
//   1. Gemini 2.0 Flash TTS (gemini-2.0-flash-preview-tts) — quota A
//   2. Gemini 2.5 Flash TTS (gemini-2.5-flash-preview-tts) — quota B
//   3. Groq PlayAI TTS      (playai-tts, voix masculine FR) — quota C
//   4. Web Speech API       (speechSynthesis, offline)       — gratuit
//
// Chaque provider est tenté dans l'ordre ; toute erreur (dont 429/503)
// déclenche silencieusement le suivant.

import { lsGet, lsSet } from '../storage/agents-db.js';

// ─── Clés localStorage ────────────────────────────────────────────────────────
const LS_SILENT     = 'NESTOR_SILENT_MODE';
const LS_VOICE_BR   = 'NESTOR_TTS_VOICE';         // voix browser sélectionnée
const LS_VOICE_GEM  = 'NESTOR_TTS_VOICE_GEMINI';  // voix Gemini TTS
const LS_VOICE_GROQ = 'NESTOR_TTS_VOICE_GROQ';    // voix Groq PlayAI
const LS_RATE       = 'NESTOR_TTS_RATE';           // vitesse lecture 0.5–2.0

const DEF_GEMINI_VOICE = 'Charon';        // grave masculin, bon rendu FR
const DEF_GROQ_VOICE   = 'Fritz-PlayAI'; // masculin PlayAI multilingue

// ─── Cascade TTS (ordre priorité) ─────────────────────────────────────────────
export const TTS_PROVIDERS = [
  {
    id: 'gemini-2.0', label: 'Gemini 2.0 Flash TTS',
    model: 'gemini-2.0-flash-preview-tts', quota: 'quota A', keyName: 'GEMINI_API_KEY',
  },
  {
    id: 'gemini-2.5', label: 'Gemini 2.5 Flash TTS',
    model: 'gemini-2.5-flash-preview-tts', quota: 'quota B', keyName: 'GEMINI_API_KEY',
  },
  {
    id: 'groq-playai', label: 'Groq PlayAI TTS',
    model: 'playai-tts', quota: 'quota C', keyName: 'GROQ_API_KEY',
  },
  {
    id: 'browser', label: 'Web Speech API',
    model: null, quota: 'gratuit', keyName: null,
  },
];

// Voix Gemini TTS disponibles (voix préfabriquées)
export const GEMINI_VOICES = [
  { name: 'Charon',  hint: 'Masculin grave (défaut FR)' },
  { name: 'Fenrir',  hint: 'Masculin expressif' },
  { name: 'Puck',    hint: 'Masculin léger' },
  { name: 'Orus',    hint: 'Masculin profond' },
  { name: 'Aoede',   hint: 'Féminin clair' },
  { name: 'Kore',    hint: 'Féminin doux' },
  { name: 'Zephyr',  hint: 'Féminin lumineux' },
  { name: 'Leda',    hint: 'Féminin naturel' },
];

// Voix Groq PlayAI disponibles
export const GROQ_VOICES = [
  { name: 'Fritz-PlayAI',    hint: 'Masculin (défaut FR)' },
  { name: 'Atlas-PlayAI',    hint: 'Masculin profond' },
  { name: 'Angelo-PlayAI',   hint: 'Masculin chaleureux' },
  { name: 'Orion-PlayAI',    hint: 'Masculin clair' },
  { name: 'Celeste-PlayAI',  hint: 'Féminin lumineux' },
  { name: 'Adelaide-PlayAI', hint: 'Féminin naturel' },
];

// ─── Mode silence ─────────────────────────────────────────────────────────────
export function isSilentMode() { return lsGet(LS_SILENT) === '1'; }

export function setSilentMode(enabled) {
  lsSet(LS_SILENT, enabled ? '1' : '0');
  if (enabled) stopSpeech();
}

export function isSpeechEnabled() {
  if (isSilentMode()) return false;
  return typeof window !== 'undefined' && !!window.speechSynthesis;
}

// ─── AudioContext partagé ─────────────────────────────────────────────────────
let _activeCtx = null;

// Convertir et lire un buffer PCM 16 bits signé 24 kHz (format Gemini TTS)
async function playPcm16(b64) {
  const bytes  = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
  const pcm16  = new Int16Array(bytes.buffer);
  const f32    = Float32Array.from(pcm16, s => s / 32768);
  const ctx    = new (window.AudioContext || window.webkitAudioContext)();
  _activeCtx   = ctx;
  const buffer = ctx.createBuffer(1, f32.length, 24000);
  buffer.copyToChannel(f32, 0);
  const src = ctx.createBufferSource();
  src.buffer = buffer;
  src.connect(ctx.destination);
  return new Promise((resolve, reject) => {
    src.onended = () => { ctx.close(); _activeCtx = null; resolve(); };
    src.onerror = e => { ctx.close(); _activeCtx = null; reject(e); };
    src.start();
  });
}

// Décoder et lire un ArrayBuffer audio quelconque (WAV, MP3 — format Groq TTS)
async function playArrayBuffer(arrayBuffer) {
  const ctx  = new (window.AudioContext || window.webkitAudioContext)();
  _activeCtx = ctx;
  try {
    const decoded = await ctx.decodeAudioData(arrayBuffer);
    const src = ctx.createBufferSource();
    src.buffer = decoded;
    src.connect(ctx.destination);
    return new Promise((resolve, reject) => {
      src.onended = () => { ctx.close(); _activeCtx = null; resolve(); };
      src.onerror = e => { ctx.close(); _activeCtx = null; reject(e); };
      src.start();
    });
  } catch (e) {
    ctx.close(); _activeCtx = null;
    throw e;
  }
}

// ─── Provider 1 & 2 : Gemini TTS ─────────────────────────────────────────────
async function speakGeminiModel(text, model, voiceName) {
  const apiKey = (lsGet('GEMINI_API_KEY') || '').trim();
  if (!apiKey) throw err('GEMINI_API_KEY manquante', 0);

  const proxy    = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  const endpoint = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${apiKey}`;
  const url      = proxy + '?url=' + encodeURIComponent(endpoint);

  const res = await fetch(url, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      contents: [{ parts: [{ text }] }],
      generationConfig: {
        responseModalities: ['AUDIO'],
        speechConfig: { voiceConfig: { prebuiltVoiceConfig: { voiceName } } },
      },
    }),
    signal: AbortSignal.timeout(15000),
  });

  if (!res.ok) {
    const body = await res.text().catch(() => '');
    throw err(`Gemini [${model}] HTTP ${res.status}` + (body ? ': ' + body.slice(0, 80) : ''), res.status);
  }

  const data = await res.json();
  const b64  = data?.candidates?.[0]?.content?.parts?.[0]?.inlineData?.data;
  if (!b64) throw err(`Gemini [${model}] : réponse audio vide`, 0);

  await playPcm16(b64);
}

// ─── Provider 3 : Groq PlayAI TTS ────────────────────────────────────────────
async function speakGroqPlayAI(text, voiceName) {
  const apiKey = (lsGet('GROQ_API_KEY') || '').trim();
  if (!apiKey) throw err('GROQ_API_KEY manquante', 0);

  const res = await fetch('https://api.groq.com/openai/v1/audio/speech', {
    method:  'POST',
    headers: {
      'Content-Type':  'application/json',
      'Authorization': 'Bearer ' + apiKey,
    },
    body: JSON.stringify({
      model:           'playai-tts',
      input:           text,
      voice:           voiceName,
      response_format: 'wav',
    }),
    signal: AbortSignal.timeout(20000),
  });

  if (!res.ok) {
    const body = await res.text().catch(() => '');
    throw err(`Groq PlayAI HTTP ${res.status}` + (body ? ': ' + body.slice(0, 80) : ''), res.status);
  }

  await playArrayBuffer(await res.arrayBuffer());
}

// ─── Provider 4 : Web Speech API ─────────────────────────────────────────────
function getVoices() {
  return new Promise(resolve => {
    const voices = window.speechSynthesis.getVoices();
    if (voices.length > 0) { resolve(voices); return; }
    window.speechSynthesis.addEventListener('voiceschanged',
      () => resolve(window.speechSynthesis.getVoices()), { once: true });
    setTimeout(() => resolve(window.speechSynthesis.getVoices()), 1000);
  });
}

async function speakBrowser(text, voiceName, lang) {
  if (!window.speechSynthesis) throw err('speechSynthesis non disponible', 0);
  window.speechSynthesis.cancel();
  const voices = await getVoices();
  const saved  = voiceName || lsGet(LS_VOICE_BR) || '';
  const voice  = (saved && voices.find(v => v.name === saved))
    || voices.find(v => v.lang?.startsWith('fr'))
    || voices[0]
    || null;

  return new Promise((resolve, reject) => {
    const utter   = new SpeechSynthesisUtterance(text);
    utter.voice   = voice;
    utter.lang    = lang || 'fr-FR';
    utter.rate    = parseFloat(lsGet(LS_RATE) || '1.0');
    utter.pitch   = 1.0;
    utter.onend   = () => resolve();
    utter.onerror = e => reject(err('speechSynthesis : ' + e.error, 0));
    window.speechSynthesis.speak(utter);
  });
}

// ─── Point d'entrée principal ─────────────────────────────────────────────────
export async function speak(text, options = {}) {
  if (!text?.trim()) return;
  if (isSilentMode()) return;

  const voiceOverride = options.voice || null;
  const lang          = options.lang  || 'fr-FR';
  const gemKey        = !!(lsGet('GEMINI_API_KEY') || '').trim();
  const groqKey       = !!(lsGet('GROQ_API_KEY')   || '').trim();

  const cascade = [
    gemKey  && { id: 'gemini-2.0',  fn: () => speakGeminiModel(text, 'gemini-2.0-flash-preview-tts', voiceOverride || lsGet(LS_VOICE_GEM) || DEF_GEMINI_VOICE) },
    gemKey  && { id: 'gemini-2.5',  fn: () => speakGeminiModel(text, 'gemini-2.5-flash-preview-tts', voiceOverride || lsGet(LS_VOICE_GEM) || DEF_GEMINI_VOICE) },
    groqKey && { id: 'groq-playai', fn: () => speakGroqPlayAI(text, voiceOverride || lsGet(LS_VOICE_GROQ) || DEF_GROQ_VOICE) },
               { id: 'browser',    fn: () => speakBrowser(text, voiceOverride, lang) },
  ].filter(Boolean);

  for (const { id, fn } of cascade) {
    console.info(`[Nestor/TTS] Tentative : ${id}`);
    try {
      await fn();
      return;
    } catch (e) {
      console.warn(`[Nestor/TTS] ${id} échoué (${e.status ?? 'err'}) : ${e.message}`);
    }
  }

  console.error('[Nestor/TTS] Tous les providers ont échoué.');
}

// ─── Arrêter la lecture en cours ──────────────────────────────────────────────
export function stopSpeech() {
  if (window.speechSynthesis) window.speechSynthesis.cancel();
  if (_activeCtx) { _activeCtx.close().catch(() => {}); _activeCtx = null; }
}

// ─── Listing des voix browser (pour UI Réglages) ──────────────────────────────
export async function listBrowserVoices() {
  if (!window.speechSynthesis) return [];
  return (await getVoices()).map(v => ({ name: v.name, lang: v.lang }));
}

// ─── Statut TTS (pour UI Réglages) ───────────────────────────────────────────
export function getTTSStatus() {
  if (isSilentMode()) return { engine: 'silent', reason: 'Mode silence activé — aucun son' };
  const gemKey  = !!(lsGet('GEMINI_API_KEY') || '').trim();
  const groqKey = !!(lsGet('GROQ_API_KEY')   || '').trim();
  const hasBr   = typeof window !== 'undefined' && !!window.speechSynthesis;
  const active  = [...(gemKey ? ['Gemini'] : []), ...(groqKey ? ['Groq'] : []), ...(hasBr ? ['Browser'] : [])];
  if (!active.length) return { engine: 'none', reason: 'Aucun TTS disponible' };
  const engine  = gemKey ? 'gemini' : groqKey ? 'groq' : 'browser';
  return { engine, reason: 'Cascade : ' + active.join(' → ') };
}

// ─── Utilitaire interne ───────────────────────────────────────────────────────
function err(message, status) {
  return Object.assign(new Error(message), { status });
}
