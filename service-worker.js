const CACHE = 'nestor-v4';
const PRECACHE = [
  './',
  './index.html',
  './css/style.css',
  './manifest.json',
  './src/app.js',
  './src/api/backends.js',
  './src/api/backends.json',
  './src/api/search.js',
  './src/api/tts.js',
  './src/storage/agents-db.js',
  './src/core/default-agents.js',
  './src/core/gardener.js',
  './src/core/orchestrator-engine.js',
  './src/ui/dashboard.js',
  './src/ui/radar-view.js',
  './src/ui/bourse-view.js',
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE).then(cache => cache.addAll(PRECACHE))
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then(keys =>
      Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', (event) => {
  const url = new URL(event.request.url);
  // Laisser passer tous les appels API externes sans cache
  if (url.hostname !== location.hostname) {
    return;
  }
  event.respondWith(
    caches.match(event.request).then(resp => resp || fetch(event.request))
  );
});
