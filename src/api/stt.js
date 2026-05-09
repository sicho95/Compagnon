// ─────────────────────────────────────────────────────────────────────────────
// stt.js — Speech-to-Text pour Nestor v2
//
// Capture micro → Groq Whisper whisper-large-v3 → texte transcrit
// Free tier Groq : 7 200 sec audio/jour (largement suffisant)
//
// Usage :
//   const { startRecording, stopRecording, isRecording } = await import('./stt.js');
//   await startRecording();
//   const text = await stopRecording();   // renvoie la transcription
// ─────────────────────────────────────────────────────────────────────────────

import { lsGet } from '../storage/agents-db.js';

let _mediaRecorder = null;
let _chunks        = [];
let _stream        = null;

export function isRecording() { return _mediaRecorder?.state === 'recording'; }

// ─── Choisit le codec le mieux supporté ───────────────────────────────────────
function bestMimeType() {
  const types = ['audio/webm;codecs=opus', 'audio/webm', 'audio/ogg;codecs=opus', 'audio/mp4'];
  return types.find(t => MediaRecorder.isTypeSupported(t)) || '';
}

// ─── Démarre la capture micro ─────────────────────────────────────────────────
export async function startRecording() {
  if (_mediaRecorder?.state === 'recording') return;

  _stream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  _chunks = [];

  const mimeType = bestMimeType();
  _mediaRecorder = new MediaRecorder(_stream, mimeType ? { mimeType } : {});
  _mediaRecorder.ondataavailable = e => { if (e.data?.size > 0) _chunks.push(e.data); };
  _mediaRecorder.start(250);  // chunk toutes les 250 ms
}

// ─── Arrête et transcrit via Groq Whisper ────────────────────────────────────
export async function stopRecording() {
  if (!_mediaRecorder || _mediaRecorder.state !== 'recording') return '';

  await new Promise(resolve => {
    _mediaRecorder.onstop = resolve;
    _mediaRecorder.stop();
  });

  _stream?.getTracks().forEach(t => t.stop());
  _stream = null;

  if (_chunks.length === 0) return '';

  const apiKey = (lsGet('GROQ_API_KEY') || '').trim();
  if (!apiKey) throw new Error('GROQ_API_KEY manquante pour Groq Whisper STT');

  const mimeType = _mediaRecorder.mimeType || 'audio/webm';
  const ext      = mimeType.includes('ogg') ? 'ogg' : mimeType.includes('mp4') ? 'mp4' : 'webm';
  const blob     = new Blob(_chunks, { type: mimeType });

  const form = new FormData();
  form.append('file',     blob, `audio.${ext}`);
  form.append('model',    'whisper-large-v3');
  form.append('language', 'fr');
  form.append('response_format', 'json');

  const res = await fetch('https://api.groq.com/openai/v1/audio/transcriptions', {
    method:  'POST',
    headers: { Authorization: 'Bearer ' + apiKey },
    body:    form,
    signal:  AbortSignal.timeout(20000),
  });

  if (!res.ok) {
    const err = await res.text().catch(() => '');
    throw new Error('Groq Whisper HTTP ' + res.status + (err ? ': ' + err.slice(0, 100) : ''));
  }

  const data = await res.json();
  return (data.text || '').trim();
}

// ─── Annule sans transcrire ───────────────────────────────────────────────────
export function cancelRecording() {
  if (_mediaRecorder?.state === 'recording') _mediaRecorder.stop();
  _stream?.getTracks().forEach(t => t.stop());
  _stream = null; _chunks = [];
}

// ─── Statut STT (pour UI Réglages) ───────────────────────────────────────────
export function getSTTStatus() {
  const hasGroq = !!(lsGet('GROQ_API_KEY') || '').trim();
  const hasMic  = !!(navigator.mediaDevices?.getUserMedia);
  if (!hasMic)   return { available: false, reason: 'Micro non accessible (HTTPS requis)' };
  if (!hasGroq)  return { available: false, reason: 'GROQ_API_KEY manquante' };
  return { available: true, reason: 'Groq Whisper v3 (7 200 s/j gratuit)' };
}
