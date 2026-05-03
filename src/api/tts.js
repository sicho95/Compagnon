// ─────────────────────────────────────────────────────────────────────────────
// tts.js — Text-to-Speech pour Nestor
//
// Stratégie :
//   1. Mode silence : si activé, speak() est un no-op total
//   2. Web Speech API (browser) — gratuit, natif, offline, voix système
//      → Moteur par défaut, fonctionne toujours sans clé
//   3. Gemini TTS (optionnel) — voix naturelle si GEMINI_API_KEY configurée
//      → Modèle : gemini-2.5-flash-preview-tts (free tier Gemini)
//      → Retourne PCM 16-bit 24kHz → AudioContext
//      → Bascule silencieuse sur browser si Gemini échoue
//
// Usage :
//   import { speak, stopSpeech, isSpeechEnabled, setSilentMode } from './tts.js';
//   await speak('Bonjour, je suis Nestor.');
//   setSilentMode(true);  // Coupe tout le son
//   stopSpeech();         // Stoppe la lecture en cours
// ─────────────────────────────────────────────────────────────────────────────

import { lsGet, lsSet } from '../storage/agents-db.js';

const LS_SILENT_MODE = 'NESTOR_SILENT_MODE';
const LS_TTS_ENGINE  = 'NESTOR_TTS_ENGINE';  // 'browser' | 'gemini'
const LS_TTS_VOICE   = 'NESTOR_TTS_VOICE';   // Nom de voix browser
const LS_TTS_RATE    = 'NESTOR_TTS_RATE';    // Vitesse 0.5–2.0

// ─── Mode silence ─────────────────────────────────────────────────────────────

export function isSilentMode() {
  return lsGet(LS_SILENT_MODE) === '1';
}

export function setSilentMode(enabled) {
  lsSet(LS_SILENT_MODE, enabled ? '1' : '0');
  if (enabled) stopSpeech();
}

/** true si le TTS peut produire du son (pas silencieux + au moins une API disponible) */
export function isSpeechEnabled() {
  if (isSilentMode()) return false;
  return typeof window !== 'undefined' &&
    (!!window.speechSynthesis || !!(lsGet('GEMINI_API_KEY') || '').trim());
}

// ─── Browser Speech Synthesis ─────────────────────────────────────────────────

function getVoices() {
  return new Promise((resolve) => {
    const voices = window.speechSynthesis.getVoices();
    if (voices.length > 0) { resolve(voices); return; }
    window.speechSynthesis.addEventListener('voiceschanged', () => {
      resolve(window.speechSynthesis.getVoices());
    }, { once: true });
    setTimeout(() => resolve(window.speechSynthesis.getVoices()), 1000);
  });
}

function pickVoice(voices) {
  const saved = lsGet(LS_TTS_VOICE) || '';
  if (saved) {
    const found = voices.find(v => v.name === saved);
    if (found) return found;
  }
  // Préférence voix française
  return voices.find(v => v.lang?.startsWith('fr')) || voices[0] || null;
}

async function speakBrowser(text) {
  return new Promise((resolve, reject) => {
    if (!window.speechSynthesis) { reject(new Error('speechSynthesis non supporté')); return; }
    window.speechSynthesis.cancel();
    getVoices().then((voices) => {
      const utter  = new SpeechSynthesisUtterance(text);
      utter.voice  = pickVoice(voices);
      utter.lang   = 'fr-FR';
      utter.rate   = parseFloat(lsGet(LS_TTS_RATE) || '1.0');
      utter.pitch  = 1.0;
      utter.onend  = () => resolve();
      utter.onerror = (e) => reject(new Error('speechSynthesis erreur : ' + e.error));
      window.speechSynthesis.speak(utter);
    });
  });
}

// ─── Gemini TTS (optionnel) ───────────────────────────────────────────────────
// Nécessite GEMINI_API_KEY dans Réglages.
// Bascule silencieuse sur browser si absent ou en erreur.

let _geminiCtx = null;

async function speakGemini(text) {
  const apiKey = (lsGet('GEMINI_API_KEY') || '').trim();
  if (!apiKey) throw new Error('GEMINI_API_KEY manquante');

  const proxy    = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  const endpoint = `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-tts:generateContent?key=${apiKey}`;
  const proxyUrl = proxy + '?url=' + encodeURIComponent(endpoint);

  const res = await fetch(proxyUrl, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify({
      contents: [{ parts: [{ text }] }],
      generationConfig: {
        responseModalities: ['AUDIO'],
        speechConfig: {
          voiceConfig: { prebuiltVoiceConfig: { voiceName: 'Aoede' } },
        },
      },
    }),
    signal: AbortSignal.timeout(15000),
  });

  if (!res.ok) {
    const err = await res.text().catch(() => '');
    throw new Error('Gemini TTS erreur ' + res.status + (err ? ' : ' + err.slice(0, 100) : ''));
  }

  const data = await res.json();
  const b64  = data?.candidates?.[0]?.content?.parts?.[0]?.inlineData?.data;
  if (!b64) throw new Error('Gemini TTS : réponse audio vide');

  // PCM 16-bit 24kHz → AudioContext
  const binary  = atob(b64);
  const bytes   = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);

  const ctx       = new (window.AudioContext || window.webkitAudioContext)();
  _geminiCtx      = ctx;
  const pcm16     = new Int16Array(bytes.buffer);
  const float32   = new Float32Array(pcm16.length);
  for (let i = 0; i < pcm16.length; i++) float32[i] = pcm16[i] / 32768;

  const buffer = ctx.createBuffer(1, float32.length, 24000);
  buffer.copyToChannel(float32, 0);
  const src = ctx.createBufferSource();
  src.buffer = buffer;
  src.connect(ctx.destination);

  return new Promise((resolve, reject) => {
    src.onended = () => { ctx.close(); _geminiCtx = null; resolve(); };
    src.onerror = (e) => { ctx.close(); _geminiCtx = null; reject(e); };
    src.start();
  });
}

// ─── Point d'entrée principal ─────────────────────────────────────────────────

export async function speak(text) {
  if (!text?.trim()) return;
  if (isSilentMode()) return; // Mode silence : no-op total

  const engine    = lsGet(LS_TTS_ENGINE) || 'browser';
  const hasGemini = !!(lsGet('GEMINI_API_KEY') || '').trim();

  // Gemini optionnel (uniquement si engine='gemini' ET clé présente)
  if (engine === 'gemini' && hasGemini) {
    try {
      await speakGemini(text);
      return;
    } catch (e) {
      console.warn('[Nestor/TTS] Gemini TTS échoué, bascule browser :', e.message);
    }
  }

  // Browser speech synthesis (défaut + fallback)
  if (window.speechSynthesis) {
    try {
      await speakBrowser(text);
    } catch (e) {
      console.warn('[Nestor/TTS] Browser speech échoué :', e.message);
    }
  }
}

export function stopSpeech() {
  if (window.speechSynthesis) window.speechSynthesis.cancel();
  if (_geminiCtx) { _geminiCtx.close().catch(() => {}); _geminiCtx = null; }
}

// ─── Listing des voix browser (pour UI Réglages) ──────────────────────────────

export async function listBrowserVoices() {
  if (!window.speechSynthesis) return [];
  const voices = await getVoices();
  return voices.map(v => ({ name: v.name, lang: v.lang }));
}

// ─── Statut TTS (pour UI Réglages) ───────────────────────────────────────────

export function getTTSStatus() {
  if (isSilentMode()) return { engine: 'silent', reason: 'Mode silence activé — aucun son' };
  const engine    = lsGet(LS_TTS_ENGINE) || 'browser';
  const hasGemini = !!(lsGet('GEMINI_API_KEY') || '').trim();
  if (engine === 'gemini' && hasGemini) return { engine: 'gemini',  reason: 'Gemini TTS actif (voix naturelle)' };
  if (window.speechSynthesis)           return { engine: 'browser', reason: 'Synthèse vocale navigateur (offline)' };
  return                                       { engine: 'none',    reason: 'Aucun TTS disponible' };
}
