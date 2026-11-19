// Keeps the page itself on the phone so it opens instantly, and refreshes it
// in the background. The list always comes from the network: a stale list
// is worse than an honest "can't reach Needful".
"use strict";

const CACHE = "needful-shell-v1";
const SHELL = ["/", "/index.html", "/app.js", "/style.css", "/manifest.webmanifest",
               "/icon.svg", "/icon-192.png", "/icon-512.png"];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (event) => {
  event.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))))
    .then(() => self.clients.claim()));
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (event.request.method !== "GET" || url.origin !== self.location.origin || url.pathname.startsWith("/api/"))
    return; // Straight to the network.
  event.respondWith(caches.open(CACHE).then(async (cache) => {
    const cached = await cache.match(event.request);
    const fresh = fetch(event.request).then((response) => {
      if (response.ok) cache.put(event.request, response.clone());
      return response;
    });
    return cached || fresh;
  }));
});
