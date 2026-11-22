// Keeps a copy of the page itself on the phone, for when the desktop can't
// be reached. The page comes from the network whenever it can, so a new
// version shows on the next open rather than the one after. The list always
// comes from the network: a stale list is worse than an honest "can't reach
// Needful".
"use strict";

const CACHE = "needful-shell-v2";
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
    try {
      const response = await fetch(event.request);
      if (response.ok) cache.put(event.request, response.clone());
      return response;
    } catch (error) {
      const cached = await cache.match(event.request);
      if (cached) return cached;
      throw error;
    }
  }));
});
