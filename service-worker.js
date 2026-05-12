const CACHE = 'nestor-v7';

const PRECACHE = [
  './',
  './index.html',
  './manifest.json',
  './css/style.css',
  './src/app.js',
  './src/core/settings-store.js',
  './src/core/default-agents.js',
  './src/core/gardener.js',
  './src/core/orchestrator-engine.js',
  './src/api/backends.js',
  './src/api/alexa.js',
  './src/api/search.js',
  './src/api/stt.js',
  './src/api/tts.js',
  './src/bt/ble.js',
  './src/bt/ble_protocol.js',
  './src/bt/ble_status.js',
  './src/device/device_settings.js',
  './src/device/provisioning.js',
  './src/input/bt_keyboard.js',
  './src/storage/agents-db.js',
  './src/sync/agents_sync.js',
  './src/sync/key-sync.js',
  './src/system/wake_lock.js',
  './src/ui/bourse-view.js',
  './src/ui/companion.js',
  './src/ui/dashboard.js',
  './src/ui/meteo-view.js',
  './src/ui/musique-view.js',
  './src/ui/radar-view.js',
];

// Précache tolérant : un fichier manquant ne plante plus l'install
async function safePrecache(cache, urls) {
  const results = await Promise.allSettled(
    urls.map(url =>
      cache.add(url).catch(err => {
        console.warn('[SW] Précache ignoré (404 ?) :', url, err.message);
      })
    )
  );
  const failed = results.filter(r => r.status === 'rejected');
  if (failed.length) console.warn('[SW] Échecs précache :', failed);
}

self.addEventListener('install', e => {
  e.waitUntil(
    caches.open(CACHE)
      .then(c => safePrecache(c, PRECACHE))
      .then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  const url = new URL(e.request.url);
  // Stratégie network-first pour les API externes
  if (url.hostname !== self.location.hostname) {
    e.respondWith(
      fetch(e.request).catch(() => caches.match(e.request))
    );
    return;
  }
  // Cache-first pour les assets locaux
  e.respondWith(
    caches.match(e.request).then(cached => {
      if (cached) return cached;
      return fetch(e.request).then(resp => {
        if (resp && resp.status === 200) {
          const clone = resp.clone();
          caches.open(CACHE).then(c => c.put(e.request, clone));
        }
        return resp;
      });
    })
  );
});
