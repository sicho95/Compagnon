// ─────────────────────────────────────────────────────────────────────────────
// tts.js — Text-to-Speech pour Nestor v2
//
// Chain TTS :
//   1. Mode silence → no-op total
//   2. gemini-2.5-pro-preview-tts   (qualité max, quota A)
//   3. gemini-2.5-flash-preview-tts (quota B, plus généreux)
//   4. Browser speechSynthesis      (offline, illimité)
//
// Voix Gemini configurables (multilingues — parlent français si le texte est FR) :
//   Masculines : Charon · Fenrir · Puck · Orus
//   Féminines  : Aoede · Kore · Schedar · Sulafat · Zephyr
// ─────────────────────────────────────────────────────────────────────────────

import { lsGet, lsSet } from '../storage/agents-db.js';

const LS_SILENT_MODE   = 'NESTOR_SILENT_MODE';
const LS_TTS_ENGINE    = 'NESTOR_TTS_ENGINE';   // 'browser' | 'gemini'
const LS_TTS_VOICE     = 'NESTOR_TTS_VOICE';    // Nom de voix browser
const LS_TTS_RATE      = 'NESTOR_TTS_RATE';     // Vitesse 0.5–2.0
const LS_GEMINI_VOICE  = 'NESTOR_GEMINI_VOICE'; // Voix Gemini (défaut: Charon)

export const GEMINI_VOICES = [
  { name: 'Charon',   gender: 'M', label: 'Charon (masculin)'   },
  { name: 'Fenrir',   gender: 'M', label: 'Fenrir (masculin)'   },
  { name: 'Puck',     gender: 'M', label: 'Puck (masculin)'     },
  { name: 'Orus',     gender: 'M', label: 'Orus (masculin)'     },
  { name: 'Aoede',    gender: 'F', label: 'Aoede (féminin)'     },
  { name: 'Kore',     gender: 'F', label: 'Kore (féminin)'      },
  { name: 'Schedar',  gender: 'F', label: 'Schedar (féminin)'   },
  { name: 'Sulafat',  gender: 'F', label: 'Sulafat (féminin)'   },
  { name: 'Zephyr',   gender: 'F', label: 'Zephyr (féminin)'    },
];

// ─── Mode silence ─────────────────────────────────────────────────────────────
export function isSilentMode() { return lsGet(LS_SILENT_MODE) === '1'; }
export function setSilentMode(enabled) { lsSet(LS_SILENT_MODE, enabled ? '1' : '0'); if (enabled) stopSpeech(); }
export function isSpeechEnabled() {
  if (isSilentMode()) return false;
  return typeof window !== 'undefined' &&
    (!!window.speechSynthesis || !!(lsGet('GEMINI_API_KEY') || '').trim());
}

// ─── Browser speechSynthesis ──────────────────────────────────────────────────
function getVoices() {
  return new Promise(resolve => {
    const v = window.speechSynthesis.getVoices();
    if (v.length > 0) { resolve(v); return; }
    window.speechSynthesis.addEventListener('voiceschanged', () => resolve(window.speechSynthesis.getVoices()), { once: true });
    setTimeout(() => resolve(window.speechSynthesis.getVoices()), 1000);
  });
}

function pickBrowserVoice(voices) {
  const saved = lsGet(LS_TTS_VOICE) || '';
  if (saved) { const f = voices.find(v => v.name === saved); if (f) return f; }
  return voices.find(v => v.lang?.startsWith('fr')) || voices[0] || null;
}

async function speakBrowser(text) {
  return new Promise((resolve, reject) => {
    if (!window.speechSynthesis) { reject(new Error('speechSynthesis absent')); return; }
    window.speechSynthesis.cancel();
    getVoices().then(voices => {
      const utter   = new SpeechSynthesisUtterance(text);
      utter.voice   = pickBrowserVoice(voices);
      utter.lang    = 'fr-FR';
      utter.rate    = parseFloat(lsGet(LS_TTS_RATE) || '1.0');
      utter.pitch   = 1.0;
      utter.onend   = () => resolve();
      utter.onerror = e => reject(new Error('speechSynthesis: ' + e.error));
      window.speechSynthesis.speak(utter);
    });
  });
}

// ─── Gemini TTS (un appel par modèle) ────────────────────────────────────────
let _geminiCtx = null;

async function speakGeminiModel(text, model) {
  const apiKey = (lsGet('GEMINI_API_KEY') || '').trim();
  if (!apiKey) throw new Error('GEMINI_API_KEY manquante');

  const voice   = lsGet(LS_GEMINI_VOICE) || 'Charon';
  const proxy   = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  const url     = `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${apiKey}`;
  const pUrl    = proxy + '?url=' + encodeURIComponent(url);

  const res = await fetch(pUrl, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      contents: [{ parts: [{ text }] }],
      generationConfig: {
        responseModalities: ['AUDIO'],
        speechConfig: { voiceConfig: { prebuiltVoiceConfig: { voiceName: voice } } },
      },
    }),
    signal: AbortSignal.timeout(15000),
  });

  if (!res.ok) {
    const errText = await res.text().catch(() => '');
    throw new Error(`${model} HTTP ${res.status}` + (errText ? ': ' + errText.slice(0, 80) : ''));
  }

  const data = await res.json();
  const b64  = data?.candidates?.[0]?.content?.parts?.[0]?.inlineData?.data;
  if (!b64) throw new Error(model + ': réponse audio vide');

  // PCM 16-bit 24kHz → AudioContext
  const bytes = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
  const pcm16 = new Int16Array(bytes.buffer);
  const f32   = Float32Array.from(pcm16, s => s / 32768);

  const ctx    = new (window.AudioContext || window.webkitAudioContext)();
  _geminiCtx   = ctx;
  const buf    = ctx.createBuffer(1, f32.length, 24000);
  buf.copyToChannel(f32, 0);
  const src    = ctx.createBufferSource();
  src.buffer   = buf;
  src.connect(ctx.destination);

  return new Promise((resolve, reject) => {
    src.onended = () => { ctx.close(); _geminiCtx = null; resolve(); };
    src.onerror = e  => { ctx.close(); _geminiCtx = null; reject(e); };
    src.start();
  });
}

// ─── Modèles Gemini TTS par ordre de priorité (quotas distincts) ──────────────
// Chaque modèle a son propre quota free-tier Gemini.
// Modifier ici si Gemini publie de nouveaux modèles.
const GEMINI_TTS_MODELS = [
  'gemini-3.1-flash-preview-tts',    // le plus récent, quota A (priorité max)
  'gemini-2.5-pro-preview-tts',      // quota B
  'gemini-2.5-flash-preview-tts',    // quota C (le plus généreux)
];

// ─── Point d'entrée principal ─────────────────────────────────────────────────
export async function speak(text) {
  if (!text?.trim() || isSilentMode()) return;

  const engine    = lsGet(LS_TTS_ENGINE) || 'browser';
  const hasGemini = !!(lsGet('GEMINI_API_KEY') || '').trim();

  if (engine === 'gemini' && hasGemini) {
    for (const model of GEMINI_TTS_MODELS) {
      try { await speakGeminiModel(text, model); return; }
      catch (e) { console.warn(`[TTS] ${model} échoué:`, e.message); }
    }
  }

  // Fallback : browser speechSynthesis (offline, illimité)
  if (window.speechSynthesis) {
    try { await speakBrowser(text); }
    catch (e) { console.warn('[TTS] Browser échoué:', e.message); }
  }
}

export function stopSpeech() {
  if (window.speechSynthesis) window.speechSynthesis.cancel();
  if (_geminiCtx) { _geminiCtx.close().catch(() => {}); _geminiCtx = null; }
}

export async function listBrowserVoices() {
  if (!window.speechSynthesis) return [];
  return (await getVoices()).map(v => ({ name: v.name, lang: v.lang }));
}

export function getTTSStatus() {
  if (isSilentMode()) return { engine: 'silent', reason: 'Mode silence activé' };
  const engine    = lsGet(LS_TTS_ENGINE) || 'browser';
  const hasGemini = !!(lsGet('GEMINI_API_KEY') || '').trim();
  const voice     = lsGet(LS_GEMINI_VOICE) || 'Charon';
  if (engine === 'gemini' && hasGemini)
    return { engine: 'gemini', reason: `Gemini TTS — voix ${voice} (pro → flash → browser)` };
  if (window.speechSynthesis)
    return { engine: 'browser', reason: 'Synthèse vocale navigateur (offline)' };
  return { engine: 'none', reason: 'Aucun TTS disponible' };
}
