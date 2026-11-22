// Keeps a copy of the page itself on the phone, so it opens even when the
// desktop can't be reached. The page comes from that copy, so it stays the
// version you have until you choose to update: the server writes a
// fingerprint of the page's files into VERSION, any change to them makes
// this worker new, and a new worker waits until the page asks it to take
// over. The list always comes from the network: a stale list is worse than
// an honest "can't reach Needful".
"use strict";

const VERSION = "{{version}}";
const CACHE = "needful-shell-" + VERSION;
const SHELL = ["/", "/index.html", "/app.js", "/style.css", "/manifest.webmanifest",
               "/icon.svg", "/icon-192.png", "/icon-512.png"];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) =>
    cache.addAll(SHELL.map((path) => new Request(path, { cache: "reload" })))));
});

self.addEventListener("message", (event) => {
  if (event.data === "update") self.skipWaiting();
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
    const cached = await cache.match(event.request, { ignoreSearch: true });
    if (cached) return cached;
    const response = await fetch(event.request);
    if (response.ok) cache.put(event.request, response.clone());
    return response;
  }));
});
