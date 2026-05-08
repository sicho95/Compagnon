// musique-view.js — Contrôle musique Nestor via BLE
// Protocole BLE : service 4FAFC201-..., char MUSIC_CMD (write), char MUSIC_STAT (notify)
// Commandes ESP32 : music:scan | music:connect:{ip}:{port} | music:list_files
//                   music:play:{path} | music:pause | music:stop | music:next
//                   music:prev | music:vol:{0-100}

import { lsGet, lsSet } from '../storage/agents-db.js';

const BLE_SERVICE  = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';
const BLE_MUSIC_CMD  = 'a0b1c2d3-0001-2222-3333-aabb12345678';
const BLE_MUSIC_STAT = 'a0b1c2d3-0002-2222-3333-aabb12345678';

const LS_SPOTIFY_ID     = 'SPOTIFY_CLIENT_ID';
const LS_SPOTIFY_SECRET = 'SPOTIFY_CLIENT_SECRET';
const LS_SPOTIFY_TOKEN  = 'SPOTIFY_ACCESS_TOKEN';

// ─── État interne ─────────────────────────────────────────────────────────
let _device    = null;
let _server    = null;
let _cmdChar   = null;
let _statChar  = null;
let _connected = false;

let _speakers  = [];  // [{name, ip, port}]
let _files     = [];  // [{name, path}]
let _status    = { playing: false, track: '', artist: '', vol: 70, airplay: '', ok: false };

let _onUpdate  = null;  // callback de re-rendu

// ─── BLE helpers ──────────────────────────────────────────────────────────
const enc = s => new TextEncoder().encode(s);
const dec = v => new TextDecoder().decode(v instanceof DataView ? v.buffer : v);

export async function musicBleConnect() {
    if (!navigator.bluetooth) throw new Error('Web Bluetooth non disponible (Chrome requis)');
    _device = await navigator.bluetooth.requestDevice({
        filters: [{ name: 'Compagnon-Nestor' }],
        optionalServices: [BLE_SERVICE],
    });
    _device.addEventListener('gattserverdisconnected', () => {
        _connected = false; _cmdChar = null; _statChar = null;
        if (_onUpdate) _onUpdate();
    });
    _server = await _device.gatt.connect();
    const svc = await _server.getPrimaryService(BLE_SERVICE);
    _cmdChar  = await svc.getCharacteristic(BLE_MUSIC_CMD);
    _statChar = await svc.getCharacteristic(BLE_MUSIC_STAT);
    await _statChar.startNotifications();
    _statChar.addEventListener('characteristicvaluechanged', e => {
        _handle_notification(dec(e.target.value));
    });
    _connected = true;
    return _device.name;
}

export function musicBleDisconnect() {
    if (_device?.gatt?.connected) _device.gatt.disconnect();
    _connected = false; _cmdChar = null; _statChar = null; _device = null;
}

async function _send(cmd) {
    if (!_cmdChar) throw new Error('BLE non connecté');
    await _cmdChar.writeValueWithResponse(enc(cmd));
}

function _handle_notification(json) {
    try {
        const d = JSON.parse(json);
        if (d.speakers) { _speakers = d.speakers; if (_onUpdate) _onUpdate(); return; }
        if (d.files)    { _files    = d.files;    if (_onUpdate) _onUpdate(); return; }
        // Notification de statut
        if ('playing' in d) { Object.assign(_status, d); if (_onUpdate) _onUpdate(); }
    } catch {}
}

// ─── Commandes musique ────────────────────────────────────────────────────
export const musicScanSpeakers = () => _send('music:scan');
export const musicConnect      = (ip, port) => _send(`music:connect:${ip}:${port}`);
export const musicListFiles    = () => _send('music:list_files');
export const musicPlay         = (path)  => _send(`music:play:${path}`);
export const musicPause        = () => _send('music:pause');
export const musicStop         = () => _send('music:stop');
export const musicNext         = () => _send('music:next');
export const musicPrev         = () => _send('music:prev');
export const musicSetVol       = (v)     => _send(`music:vol:${Math.round(v)}`);

// ─── Spotify PKCE OAuth ───────────────────────────────────────────────────
function _base64url(buf) {
    return btoa(String.fromCharCode(...new Uint8Array(buf)))
        .replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '');
}

async function _pkce_challenge(verifier) {
    const enc = new TextEncoder().encode(verifier);
    const hash = await crypto.subtle.digest('SHA-256', enc);
    return _base64url(hash);
}

function _random_str(len = 64) {
    const arr = new Uint8Array(len);
    crypto.getRandomValues(arr);
    return _base64url(arr).slice(0, len);
}

export async function spotifyStartAuth() {
    const clientId = (lsGet(LS_SPOTIFY_ID) || '').trim();
    if (!clientId) throw new Error('SPOTIFY_CLIENT_ID manquant dans les réglages');
    const verifier = _random_str(64);
    const challenge = await _pkce_challenge(verifier);
    lsSet('SPOTIFY_PKCE_VERIFIER', verifier);
    const redirect = window.location.href.split('?')[0];
    const params = new URLSearchParams({
        response_type: 'code',
        client_id:     clientId,
        scope:         'streaming user-read-playback-state user-modify-playback-state',
        redirect_uri:  redirect,
        code_challenge_method: 'S256',
        code_challenge: challenge,
        state: 'nestor_spotify',
    });
    window.location.href = 'https://accounts.spotify.com/authorize?' + params.toString();
}

export async function spotifyHandleCallback() {
    const url = new URL(window.location.href);
    const code  = url.searchParams.get('code');
    const state = url.searchParams.get('state');
    if (!code || state !== 'nestor_spotify') return false;

    const clientId  = (lsGet(LS_SPOTIFY_ID) || '').trim();
    const verifier  = lsGet('SPOTIFY_PKCE_VERIFIER') || '';
    const redirect  = window.location.href.split('?')[0];

    const res = await fetch('https://accounts.spotify.com/api/token', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
            grant_type:    'authorization_code',
            code,
            redirect_uri:  redirect,
            client_id:     clientId,
            code_verifier: verifier,
        }),
    });
    if (!res.ok) throw new Error('Token Spotify échoué : ' + res.status);
    const data = await res.json();
    lsSet(LS_SPOTIFY_TOKEN, JSON.stringify({
        access:   data.access_token,
        refresh:  data.refresh_token,
        expires:  Date.now() + data.expires_in * 1000,
    }));
    // Nettoyer l'URL
    window.history.replaceState({}, '', redirect);
    return true;
}

function _spotify_token() {
    try { return JSON.parse(lsGet(LS_SPOTIFY_TOKEN) || 'null'); } catch { return null; }
}

export function spotifyIsLinked() {
    const t = _spotify_token();
    return !!(t && t.access && t.expires > Date.now());
}

// ─── DOM helpers ──────────────────────────────────────────────────────────
function el(tag, styles) {
    const e = document.createElement(tag);
    if (styles) Object.assign(e.style, styles);
    return e;
}

function btn(text, variant, onClick) {
    const b = document.createElement('button');
    b.textContent = text;
    const isPrimary = variant === 'primary';
    const isPurple  = variant === 'purple';
    Object.assign(b.style, {
        padding:      isPrimary ? '7px 14px' : '6px 12px',
        background:   isPrimary ? '#1a3a1a' : isPurple ? '#1b0030' : '#222',
        color:        isPrimary ? '#7ef' : isPurple ? '#CC99FF' : '#ccc',
        border:       '1px solid ' + (isPrimary ? '#2a6a3a' : isPurple ? '#9933FF55' : '#333'),
        borderRadius: '8px', fontSize: '13px', cursor: 'pointer', whiteSpace: 'nowrap',
    });
    b.onclick = onClick;
    return b;
}

function labelEl(text) {
    const l = el('div', { fontSize: '12px', color: '#888', marginBottom: '2px', marginTop: '6px' });
    l.textContent = text;
    return l;
}

function showToast(msg, isErr) {
    let t = document.getElementById('nestor-toast');
    if (!t) {
        t = document.createElement('div');
        t.id = 'nestor-toast';
        Object.assign(t.style, {
            position: 'fixed', bottom: '24px', left: '50%', transform: 'translateX(-50%)',
            padding: '10px 20px', borderRadius: '20px', fontSize: '13px',
            maxWidth: '80vw', textAlign: 'center', zIndex: '9999',
            transition: 'opacity 0.3s', pointerEvents: 'none',
        });
        document.body.appendChild(t);
    }
    t.textContent = msg;
    t.style.background = isErr ? '#4a1a1a' : '#1a0030';
    t.style.color       = isErr ? '#f88'   : '#CC99FF';
    t.style.border      = '1px solid ' + (isErr ? '#6a2a2a' : '#9933FF44');
    t.style.opacity = '1';
    clearTimeout(t._timer);
    t._timer = setTimeout(() => { t.style.opacity = '0'; }, 3000);
}

// ─── Render principal ─────────────────────────────────────────────────────
export function renderMusiqueView(container, state, rerender) {
    container.innerHTML = '';
    _onUpdate = rerender;

    // Vérifier si on revient d'un callback Spotify
    spotifyHandleCallback().catch(() => {});

    const wrap = el('div', { display: 'flex', flexDirection: 'column', gap: '12px' });

    // ── Bloc BLE ────────────────────────────────────────────────────────────
    const bleCard = el('div', {
        background: '#0d0020', border: '1px solid #9933FF33',
        borderRadius: '12px', padding: '12px',
    });
    const bleRow = el('div', { display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap' });

    const bleDot = el('div', { width: '10px', height: '10px', borderRadius: '50%', flexShrink: '0',
        background: _connected ? '#9933FF' : '#444' });
    const bleLabel = el('div', { fontSize: '13px', color: _connected ? '#CC99FF' : '#666', flex: '1' });
    bleLabel.textContent = _connected ? '● Connecté — ' + (_device?.name || '') : '○ BLE non connecté';

    const btnBle = btn(_connected ? 'Déconnecter' : '🔗 Connecter BLE', _connected ? '' : 'purple', async () => {
        if (_connected) {
            musicBleDisconnect(); rerender();
        } else {
            try {
                const name = await musicBleConnect();
                showToast('BLE connecté : ' + name);
                rerender();
            } catch (e) { showToast('Connexion BLE échouée : ' + e.message, true); }
        }
    });

    bleRow.append(bleDot, bleLabel, btnBle);
    bleCard.appendChild(bleRow);
    wrap.appendChild(bleCard);

    // ── Bloc enceintes AirPlay ───────────────────────────────────────────────
    const apCard = el('div', {
        background: '#0d0020', border: '1px solid #9933FF33',
        borderRadius: '12px', padding: '12px',
    });
    const apTitle = el('div', { fontWeight: '600', fontSize: '13px', color: '#CC99FF', marginBottom: '8px' });
    apTitle.textContent = '📡 Enceintes AirPlay';
    apCard.appendChild(apTitle);

    const apRow = el('div', { display: 'flex', gap: '8px', flexWrap: 'wrap', alignItems: 'center' });

    // Sélecteur enceintes
    const apSelect = document.createElement('select');
    Object.assign(apSelect.style, {
        flex: '1', minWidth: '140px', background: '#111', color: '#CC99FF',
        border: '1px solid #9933FF44', borderRadius: '8px', padding: '7px', fontSize: '13px',
    });
    const fillSelect = () => {
        apSelect.innerHTML = '';
        if (_speakers.length === 0) {
            const opt = document.createElement('option');
            opt.textContent = 'Aucune enceinte';
            apSelect.appendChild(opt);
        } else {
            _speakers.forEach((s, i) => {
                const opt = document.createElement('option');
                opt.value = i;
                opt.textContent = s.name + ' (' + s.ip + ')';
                apSelect.appendChild(opt);
            });
        }
    };
    fillSelect();

    const btnScan = btn('🔍 Chercher', 'purple', async () => {
        if (!_connected) { showToast('Connecter le BLE d\'abord', true); return; }
        btnScan.textContent = '⏳'; btnScan.disabled = true;
        try { await musicScanSpeakers(); }
        catch (e) { showToast('Scan échoué : ' + e.message, true); }
        btnScan.textContent = '🔍 Chercher'; btnScan.disabled = false;
    });

    const btnConn = btn('Connecter', 'purple', async () => {
        if (!_connected) { showToast('Connecter le BLE d\'abord', true); return; }
        const idx = parseInt(apSelect.value);
        if (isNaN(idx) || !_speakers[idx]) { showToast('Sélectionner une enceinte', true); return; }
        btnConn.textContent = '⏳'; btnConn.disabled = true;
        try {
            await musicConnect(_speakers[idx].ip, _speakers[idx].port);
            showToast('Connexion AirPlay envoyée : ' + _speakers[idx].name);
        } catch (e) { showToast('Erreur : ' + e.message, true); }
        btnConn.textContent = 'Connecter'; btnConn.disabled = false;
    });

    apRow.append(apSelect, btnScan, btnConn);
    apCard.appendChild(apRow);

    // Statut AirPlay
    if (_status.airplay) {
        const apStat = el('div', { fontSize: '12px', marginTop: '6px',
            color: _status.ok ? '#9933FF' : '#664488' });
        apStat.textContent = _status.ok
            ? '✓ Connecté — ' + _status.airplay
            : '✗ Non connecté — ' + (_status.airplay || '');
        apCard.appendChild(apStat);
    }
    wrap.appendChild(apCard);

    // ── Lecteur central ───────────────────────────────────────────────────────
    const playerCard = el('div', {
        background: '#0d0020', border: '1px solid #9933FF44',
        borderRadius: '14px', padding: '16px', textAlign: 'center',
    });

    // Icône / pochette
    const noteIcon = el('div', { fontSize: '48px', marginBottom: '8px', userSelect: 'none' });
    noteIcon.textContent = '♪';

    // Titre + artiste
    const trackEl = el('div', {
        fontSize: '15px', fontWeight: '600', color: '#CC99FF',
        marginBottom: '2px', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap',
    });
    trackEl.textContent = _status.track || '— Aucun morceau —';

    const artistEl = el('div', { fontSize: '12px', color: '#664488', marginBottom: '12px' });
    artistEl.textContent = _status.artist || '';

    // Boutons de contrôle
    const ctrlRow = el('div', { display: 'flex', justifyContent: 'center', gap: '12px', marginBottom: '12px' });

    const mkCtrl = (icon, action) => {
        const b = el('button', {
            fontSize: '22px', background: '#1b0030', color: '#CC99FF',
            border: '1px solid #9933FF44', borderRadius: '50%',
            width: '48px', height: '48px', cursor: 'pointer', display: 'flex',
            alignItems: 'center', justifyContent: 'center',
        });
        b.textContent = icon;
        b.onclick = async () => {
            if (!_connected) { showToast('BLE non connecté', true); return; }
            try { await action(); } catch (e) { showToast(e.message, true); }
        };
        return b;
    };

    ctrlRow.append(
        mkCtrl('⏮', musicPrev),
        mkCtrl(_status.playing ? '⏸' : '▶', musicPause),
        mkCtrl('⏭', musicNext),
    );

    // Volume slider
    const volRow = el('div', { display: 'flex', alignItems: 'center', gap: '8px' });
    const volIcon = el('div', { fontSize: '14px', color: '#664488' });
    volIcon.textContent = '🔊';
    const volSlider = document.createElement('input');
    volSlider.type = 'range'; volSlider.min = 0; volSlider.max = 100;
    volSlider.value = _status.vol;
    Object.assign(volSlider.style, { flex: '1', accentColor: '#9933FF' });
    const volVal = el('div', { fontSize: '12px', color: '#664488', minWidth: '28px', textAlign: 'right' });
    volVal.textContent = _status.vol + '%';
    volSlider.oninput = () => { volVal.textContent = volSlider.value + '%'; };
    volSlider.onchange = async () => {
        if (!_connected) return;
        try { await musicSetVol(parseInt(volSlider.value)); }
        catch (e) { showToast(e.message, true); }
    };
    volRow.append(volIcon, volSlider, volVal);

    playerCard.append(noteIcon, trackEl, artistEl, ctrlRow, volRow);
    wrap.appendChild(playerCard);

    // ── SD card browser ───────────────────────────────────────────────────────
    const sdCard = el('div', {
        background: '#0a000d', border: '1px solid #9933FF22',
        borderRadius: '12px', padding: '12px',
    });
    const sdHeader = el('div', { display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '8px' });
    const sdTitle = el('div', { fontWeight: '600', fontSize: '13px', color: '#CC99FF', flex: '1' });
    sdTitle.textContent = '💾 SD card';
    const btnListFiles = btn('📂 Lister', 'purple', async () => {
        if (!_connected) { showToast('BLE non connecté', true); return; }
        btnListFiles.textContent = '⏳'; btnListFiles.disabled = true;
        try { await musicListFiles(); }
        catch (e) { showToast(e.message, true); }
        btnListFiles.textContent = '📂 Lister'; btnListFiles.disabled = false;
    });
    sdHeader.append(sdTitle, btnListFiles);
    sdCard.appendChild(sdHeader);

    // Liste des fichiers
    const fileList = el('div', {
        maxHeight: '160px', overflowY: 'auto', display: 'flex',
        flexDirection: 'column', gap: '4px',
    });
    if (_files.length === 0) {
        const hint = el('div', { fontSize: '12px', color: '#444', padding: '6px 0' });
        hint.textContent = 'Appuyer sur « Lister » pour récupérer les fichiers SD';
        fileList.appendChild(hint);
    } else {
        _files.forEach(f => {
            const row = el('div', {
                display: 'flex', alignItems: 'center', gap: '8px',
                padding: '5px 8px', borderRadius: '8px', cursor: 'pointer',
                background: _status.track === f.name ? '#1b0030' : 'transparent',
            });
            row.onmouseenter = () => { row.style.background = '#1b0030'; };
            row.onmouseleave = () => { row.style.background = _status.track === f.name ? '#1b0030' : 'transparent'; };
            const ico = el('span', { fontSize: '14px' }); ico.textContent = '♪';
            const name = el('div', { flex: '1', fontSize: '12px', color: '#CC99FF',
                overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' });
            name.textContent = f.name;
            const btnPlay = btn('▶', 'purple', async () => {
                if (!_connected) { showToast('BLE non connecté', true); return; }
                try { await musicPlay(f.path); showToast('Lecture : ' + f.name); }
                catch (e) { showToast(e.message, true); }
            });
            Object.assign(btnPlay.style, { padding: '3px 8px', fontSize: '12px' });
            row.append(ico, name, btnPlay);
            fileList.appendChild(row);
        });
    }
    sdCard.appendChild(fileList);
    wrap.appendChild(sdCard);

    // ── Spotify ────────────────────────────────────────────────────────────────
    const spCard = el('div', {
        background: '#001a0a', border: '1px solid #1DB95444',
        borderRadius: '12px', padding: '12px',
    });
    const spTitle = el('div', { fontWeight: '600', fontSize: '13px', color: '#1DB954', marginBottom: '8px' });
    spTitle.textContent = '🎵 Spotify Connect';
    spCard.appendChild(spTitle);

    const clientId = (lsGet(LS_SPOTIFY_ID) || '').trim();
    const hasKeys  = !!clientId;
    const isLinked = spotifyIsLinked();

    if (!hasKeys) {
        const noKeys = el('div', { fontSize: '12px', color: '#888', lineHeight: '1.5' });
        noKeys.innerHTML = 'Renseigner <b>SPOTIFY_CLIENT_ID</b> dans les réglages<br>'
            + 'et créer une app sur <a href="https://developer.spotify.com" target="_blank" '
            + 'style="color:#1DB954">developer.spotify.com</a>';
        spCard.appendChild(noKeys);
    } else if (!isLinked) {
        const authBtn = btn('🔗 Lier le compte Spotify', '', async () => {
            try { await spotifyStartAuth(); }
            catch (e) { showToast(e.message, true); }
        });
        authBtn.style.background = '#1a3020';
        authBtn.style.color = '#1DB954';
        authBtn.style.borderColor = '#1DB95444';
        authBtn.style.width = '100%';
        authBtn.style.marginTop = '4px';
        spCard.appendChild(authBtn);
    } else {
        const linked = el('div', { fontSize: '12px', color: '#1DB954', marginBottom: '6px' });
        linked.textContent = '✓ Compte Spotify lié (OAuth PKCE)';
        spCard.appendChild(linked);
        const hint = el('div', { fontSize: '11px', color: '#555', lineHeight: '1.4' });
        hint.textContent = 'Contrôle de lecture via Spotify Connect — à implémenter avec SpotifyArduino côté ESP32.';
        spCard.appendChild(hint);
        const unlinkBtn = btn('Délier', '', () => {
            lsSet(LS_SPOTIFY_TOKEN, ''); rerender(); showToast('Compte Spotify délié.');
        });
        unlinkBtn.style.fontSize = '11px'; unlinkBtn.style.marginTop = '6px';
        spCard.appendChild(unlinkBtn);
    }
    wrap.appendChild(spCard);

    // ── Amazon Music (désactivé) ──────────────────────────────────────────────
    const amCard = el('div', {
        background: '#111', border: '1px solid #333',
        borderRadius: '12px', padding: '12px', opacity: '0.5',
    });
    const amRow = el('div', { display: 'flex', alignItems: 'center', gap: '10px' });
    const amIcon = el('div', { fontSize: '20px' }); amIcon.textContent = '📦';
    const amText = el('div', {});
    const amTitle = el('div', { fontWeight: '600', fontSize: '13px', color: '#666' });
    amTitle.textContent = 'Amazon Music';
    const amDesc = el('div', { fontSize: '11px', color: '#555' });
    amDesc.textContent = 'Non disponible (DRM propriétaire)';
    amText.append(amTitle, amDesc);
    amRow.append(amIcon, amText);
    amCard.appendChild(amRow);
    wrap.appendChild(amCard);

    container.appendChild(wrap);
}

export function cleanupMusiqueView() {
    _onUpdate = null;
}
