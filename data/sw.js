// MotionOS Service Worker — cache-first for offline UI shell
// Bump CACHE_VERSION after every `pio run -t uploadfs` to bust stale cache
const CACHE_VERSION = 'v1';
const CACHE = `motionos-${CACHE_VERSION}`;
const ASSETS = ['/', '/index.html', '/login.html', '/manifest.json', '/icon-192.png', '/icon-512.png'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(ASSETS)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys().then(ks => Promise.all(ks.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  // Never cache API, WS, or auth endpoints
  const url = new URL(e.request.url);
  if (url.pathname.startsWith('/api') || url.pathname === '/login' || url.pathname === '/logout')
    return;

  e.respondWith(
    caches.match(e.request).then(cached => {
      if (cached) return cached;
      return fetch(e.request).then(res => {
        if (res.ok && (url.pathname.endsWith('.html') || url.pathname.endsWith('.js') ||
            url.pathname.endsWith('.json') || url.pathname.endsWith('.png'))) {
          const clone = res.clone();
          caches.open(CACHE).then(c => c.put(e.request, clone));
        }
        return res;
      }).catch(() => caches.match('/'));
    })
  );
});