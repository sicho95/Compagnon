// ─────────────────────────────────────────────────────────────────────────────
// tts.js — Text-to-Speech pour Nestor
//
// Stratégie :
//   1. Vérification mode silencieux (variable globale ou localStorage)
//   2. Gemini TTS (API cloud, voix naturelle) si clé GEMINI_API_KEY configurée
//   3. Web Speech API (speechSynthesis) du navigateur/téléphone — gratuit,
//      natif, fonctionne hors-ligne, utilise les voix du système.
//
// OpenAI TTS : NON intégré (payant, pas de free tier stable).
//
// Usage :
//   import { speak, stopSpeech, isSpeechEnabled } from './tts.js';
//   await speak('Bonjour, je suis Nestor.');
//   stopSpeech(); // Stoppe la lecture en cours
// ─────────────────────────────────────────────────────────────────────────────

import { lsGet } from '../storage/agents-db.js';

const LS_SILENT_MODE = 'NESTOR_SILENT_MODE';
const LS_TTS_ENGINE  = 'NESTOR_TTS_ENGINE';  // 'browser' | 'gemini'
const LS_TTS_VOICE   = 'NESTOR_TTS_VOICE';   // Nom de voix browser sélectionnée
const LS_TTS_RATE    = 'NESTOR_TTS_RATE';    // Vitesse 0.5–2.0

// ─── Silencieux ───────────────────────────────────────────────────────────────

export function isSilentMode() {
  return lsGet(LS_SILENT_MODE) === '1';
}

export function setSilentMode(val) {
  import('../storage/agents-db.js').then(({ lsSet }) => lsSet(LS_SILENT_MODE, val ? '1' : '0'));
}

// Retourne true si le TTS est activé (pas silencieux ET API dispo)
export function isSpeechEnabled() {
  if (isSilentMode()) return false;
  return !!window.speechSynthesis || !!(lsGet('GEMINI_API_KEY') || '').trim();
}

// ─── Browser Speech Synthesis ─────────────────────────────────────────────────

function getVoices() {
  return new Promise((resolve) => {
    const voices = window.speechSynthesis.getVoices();
    if (voices.length > 0) { resolve(voices); return; }
    // Chrome/Firefox chargent les voix en async
    window.speechSynthesis.addEventListener('voiceschanged', () => {
      resolve(window.speechSynthesis.getVoices());
    }, { once: true });
    setTimeout(() => resolve(window.speechSynthesis.getVoices()), 1000);
  });
}

function pickVoice(voices) {
  const savedVoiceName = lsGet(LS_TTS_VOICE) || '';
  if (savedVoiceName) {
    const found = voices.find(v => v.name === savedVoiceName);
    if (found) return found;
  }
  // Préférence : voix française
  return voices.find(v => v.lang && v.lang.startsWith('fr')) || voices[0] || null;
}

async function speakBrowser(text) {
  return new Promise((resolve, reject) => {
    if (!window.speechSynthesis) { reject(new Error('speechSynthesis non supporté')); return; }

    window.speechSynthesis.cancel(); // Stoppe toute lecture précédente

    getVoices().then((voices) => {
      const utter = new SpeechSynthesisUtterance(text);
      utter.voice = pickVoice(voices);
      utter.lang  = 'fr-FR';
      utter.rate  = parseFloat(lsGet(LS_TTS_RATE) || '1.0');
      utter.pitch = 1.0;
      utter.onend   = () => resolve();
      utter.onerror = (e) => reject(new Error('speechSynthesis erreur : ' + e.error));
      window.speechSynthesis.speak(utter);
    });
  });
}

// ─── Gemini TTS ───────────────────────────────────────────────────────────────
// Modèle : gemini-2.5-flash-preview-tts (gratuit dans le free tier Gemini)
// Retourne de l'audio PCM 24kHz → on joue via AudioContext

async function speakGemini(text) {
  const apiKey = (lsGet('GEMINI_API_KEY') || '').trim();
  if (!apiKey) throw new Error('GEMINI_API_KEY manquante');

  const proxy    = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  const endpoint = `https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-tts:generateContent?key=${apiKey}`;
  const proxyUrl = proxy + '?url=' + encodeURIComponent(endpoint);

  const res = await fetch(proxyUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      contents: [{ parts: [{ text }] }],
      generationConfig: {
        responseModalities: ['AUDIO'],
        speechConfig: {
          voiceConfig: { prebuiltVoiceConfig: { voiceName: 'Aoede' } },
        },
      },
    }),
  });

  if (!res.ok) {
    const err = await res.text().catch(() => '');
    throw new Error('Gemini TTS erreur ' + res.status + (err ? ' : ' + err.slice(0, 100) : ''));
  }

  const data = await res.json();
  const b64  = data?.candidates?.[0]?.content?.parts?.[0]?.inlineData?.data;
  if (!b64) throw new Error('Gemini TTS : réponse audio vide');

  // Décoder base64 → ArrayBuffer → PCM 16-bit → lecture AudioContext
  const binary    = atob(b64);
  const bytes     = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);

  const ctx       = new (window.AudioContext || window.webkitAudioContext)();
  const sampleRate = 24000;
  const pcm16     = new Int16Array(bytes.buffer);
  const float32   = new Float32Array(pcm16.length);
  for (let i = 0; i < pcm16.length; i++) float32[i] = pcm16[i] / 32768;

  const buffer    = ctx.createBuffer(1, float32.length, sampleRate);
  buffer.copyToChannel(float32, 0);
  const src       = ctx.createBufferSource();
  src.buffer      = buffer;
  src.connect(ctx.destination);

  return new Promise((resolve, reject) => {
    src.onended = () => { ctx.close(); resolve(); };
    src.onerror = (e) => { ctx.close(); reject(e); };
    src.start();
  });
}

// ─── Point d'entrée principal ─────────────────────────────────────────────────

let _currentCtx = null;

export async function speak(text) {
  if (!text || !text.trim()) return;
  if (isSilentMode()) return;

  const engine = lsGet(LS_TTS_ENGINE) || 'browser';
  const hasGemini = !!(lsGet('GEMINI_API_KEY') || '').trim();

  if (engine === 'gemini' && hasGemini) {
    try {
      await speakGemini(text);
      return;
    } catch (e) {
      console.warn('[Nestor/TTS] Gemini TTS échoué, bascule sur browser :', e.message);
    }
  }

  // Browser speech synthesis (défaut)
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
  if (_currentCtx) { _currentCtx.close().catch(() => {}); _currentCtx = null; }
}

// ─── Listing des voix browser (pour UI Réglages) ──────────────────────────────

export async function listBrowserVoices() {
  if (!window.speechSynthesis) return [];
  const voices = await getVoices();
  return voices.map(v => ({ name: v.name, lang: v.lang }));
}
