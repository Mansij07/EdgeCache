'use strict';
// A trivial origin used to exercise EdgeCache. It returns responses with
// tweakable Cache-Control headers and counts how many times it was actually
// hit, so you can prove the cache is absorbing load (origin hit count stays flat
// while client request count climbs).
const express = require('express');

const app = express();
const PORT = parseInt(process.env.PORT || '8081', 10);

let originHits = 0;
const perPathHits = {};

app.use((req, _res, next) => {
  originHits += 1;
  perPathHits[req.path] = (perPathHits[req.path] || 0) + 1;
  next();
});

// Operational endpoints (not counted toward "content" hits conceptually, but
// simplest to leave them; they're rarely called on the hot path).
app.get('/health', (_req, res) => res.status(200).send('ok'));

// How many times the origin was reached — the number that should stay LOW when
// caching works.
app.get('/__origin_stats', (_req, res) => {
  res.json({ originHits, perPathHits });
});

// A cacheable product resource. maxage is tweakable via ?maxage=N.
app.get('/products/:id', (req, res) => {
  const maxAge = parseInt(req.query.maxage || '60', 10);
  const swr = parseInt(req.query.swr || '0', 10);
  let cc = `public, max-age=${maxAge}`;
  if (swr > 0) cc += `, stale-while-revalidate=${swr}`;
  res.set('Cache-Control', cc);
  res.json({
    id: req.params.id,
    name: `Product ${req.params.id}`,
    // Timestamp + nonce let you SEE a HIT reuse the same body vs. a MISS producing a new one.
    generatedAt: new Date().toISOString(),
    nonce: Math.random().toString(36).slice(2),
    servedByOriginHitNumber: originHits,
  });
});

// Explicitly uncacheable variants.
app.get('/private/:id', (req, res) => {
  res.set('Cache-Control', 'private');
  res.json({ id: req.params.id, secret: true, generatedAt: new Date().toISOString() });
});

app.get('/nostore/:id', (req, res) => {
  res.set('Cache-Control', 'no-store');
  res.json({ id: req.params.id, generatedAt: new Date().toISOString() });
});

// A deliberately slow endpoint to exercise origin timeouts and SWR.
app.get('/slow/:id', (req, res) => {
  const delayMs = parseInt(req.query.delay || '3000', 10);
  setTimeout(() => {
    res.set('Cache-Control', 'public, max-age=30');
    res.json({ id: req.params.id, delayedMs: delayMs, generatedAt: new Date().toISOString() });
  }, delayMs);
});

// Default: small cacheable page.
app.get('/', (_req, res) => {
  res.set('Cache-Control', 'public, max-age=30');
  res.send('EdgeCache dummy origin\n');
});

app.listen(PORT, () => {
  console.log(`[dummy-origin] listening on :${PORT}`);
});
