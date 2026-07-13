#include "RequestHandler.h"

#include <cctype>
#include <chrono>
#include <thread>

#include "cache/CacheKey.h"

namespace edgecache {

namespace {
void decorate(HttpResponse& resp, const std::string& xcache, const Config& cfg,
              const std::string& policyReason = "") {
    resp.headers["X-Cache"] = xcache;
    resp.headers["X-EdgeCache-Replica"] = cfg.replicaId;
    if (!policyReason.empty()) resp.headers["X-Cache-Policy"] = policyReason;
}

CacheEntry entryFrom(const HttpResponse& resp, const CacheDecision& d) {
    CacheEntry e;
    e.status = resp.status;
    e.reason = resp.reason;
    e.headers = resp.headers;
    e.body = resp.body;
    e.storedAt = Clock::now();
    e.ttlSeconds = d.ttlSeconds;
    e.staleWhileRevalidateSeconds = d.staleWhileRevalidateSeconds;
    e.etag = resp.header("ETag");
    return e;
}
}  // namespace

OriginTarget RequestHandler::targetFor(const std::string& /*path*/) const {
    // MVP: single default origin. Multi-origin routing (rule.originId ->
    // registered origin host) is a documented extension point; the rule's TTL/SWR
    // still apply via the policy regardless of which origin serves it.
    OriginTarget t;
    t.host = cfg_.defaultOriginHost;
    t.port = cfg_.defaultOriginPort;
    t.connectTimeoutMs = cfg_.originConnectTimeoutMs;
    t.readTimeoutMs = cfg_.originReadTimeoutMs;
    return t;
}

OriginResult RequestHandler::fetchGuarded(const HttpRequest& req, const OriginTarget& target,
                                          bool& circuitRejected) {
    circuitRejected = false;
    CircuitBreaker& cb = breakers_.forOrigin(target.host);
    if (!cb.allowRequest()) {
        circuitRejected = true;
        metrics_.recordCircuitReject();
        OriginResult r;
        r.ok = false;
        r.error = "circuit open";
        return r;
    }

    auto start = std::chrono::steady_clock::now();
    OriginResult r = origin_.fetch(req, target);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    metrics_.observeOriginLatency(secs);

    if (r.ok) {
        cb.recordSuccess();
    } else {
        cb.recordFailure();
        metrics_.recordOriginError();
    }
    return r;
}

void RequestHandler::maybeStore(const HttpRequest& req, const HttpResponse& resp,
                                ShardedCache& cache) {
    if (!req.isCacheableMethod()) return;
    CacheDecision d = policy_.decide(req.path, resp);
    if (!d.cacheable) return;
    CacheKey key = CacheKey::fromRequest(req, targetFor(req.path).host);
    cache.put(key.value, entryFrom(resp, d));
}

void RequestHandler::storeTiers(const HttpRequest& req, const HttpResponse& resp,
                                ShardedCache& cache) {
    if (!req.isCacheableMethod()) return;
    CacheDecision d = policy_.decide(req.path, resp);
    if (!d.cacheable) return;
    CacheKey key = CacheKey::fromRequest(req, targetFor(req.path).host);
    CacheEntry entry = entryFrom(resp, d);
    cache.put(key.value, entry);                          // L1
    if (l2_) l2_->put(key.value, entry, d.ttlSeconds);    // L2 write-through
}

void RequestHandler::triggerRevalidation(const HttpRequest& req, const std::string& key,
                                         ShardedCache& cache) {
    // Only one background refresh per key at a time (reuse the coalescing map).
    auto acq = inflight_.acquire(key);
    if (!acq.leader) return;  // a refresh (or a live fetch) is already underway

    // Capture by value; all injected deps outlive the process, so referencing
    // them from a detached thread is safe.
    std::thread([this, req, key, &cache]() {
        OriginTarget target = targetFor(req.path);
        bool rejected = false;
        OriginResult r = fetchGuarded(req, target, rejected);
        HttpResponse resp;
        bool ok = false;
        if (r.ok) {
            resp = r.response;
            storeTiers(req, resp, cache);  // refresh both L1 and L2
            ok = true;
        }
        inflight_.publish(key, resp, ok);
    }).detach();
}

HttpResponse RequestHandler::handle(const HttpRequest& req, ShardedCache& cache) {
    auto start = std::chrono::steady_clock::now();
    HttpResponse resp = process(req, cache);
    if (accessLog_) {
        double latencyMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        AccessLogEvent ev;
        ev.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        ev.replicaId = cfg_.replicaId;
        ev.path = req.path;
        ev.cacheKey = CacheKey::fromRequest(req, targetFor(req.path).host).value;
        ev.result = resp.header("X-Cache", "unknown");
        // Normalize to lowercase result tokens for the analytics schema.
        for (auto& c : ev.result) c = static_cast<char>(::tolower(c));
        ev.status = resp.status;
        ev.latencyMs = latencyMs;
        ev.bytesServed = resp.body.size();
        accessLog_->log(ev);  // non-blocking, may drop
    }
    return resp;
}

HttpResponse RequestHandler::process(const HttpRequest& req, ShardedCache& cache) {
    OriginTarget target = targetFor(req.path);

    // Non-cacheable methods are proxied straight through (still circuit-guarded).
    if (!req.isCacheableMethod()) {
        metrics_.recordMiss();
        bool rejected = false;
        OriginResult r = fetchGuarded(req, target, rejected);
        HttpResponse resp;
        if (r.ok) {
            resp = r.response;
            decorate(resp, "PASS", cfg_);
        } else {
            resp = HttpResponse::simple(rejected ? 503 : 502,
                                        rejected ? "Service Unavailable" : "Bad Gateway",
                                        rejected ? "origin circuit open\n" : "origin error\n");
            decorate(resp, "PASS", cfg_);
        }
        metrics_.recordBytesServed(resp.body.size());
        return resp;
    }

    CacheKey key = CacheKey::fromRequest(req, target.host);
    auto now = Clock::now();

    // --- Cache lookup ---
    auto cached = cache.get(key.value);
    if (cached) {
        if (cached->isFresh(now)) {
            metrics_.recordHit();
            HttpResponse resp = cached->toResponse();
            auto age = std::chrono::duration_cast<std::chrono::seconds>(now - cached->storedAt)
                           .count();
            resp.headers["Age"] = std::to_string(age);
            decorate(resp, "HIT", cfg_);
            metrics_.recordBytesServed(resp.body.size());
            return resp;
        }
        if (cached->isServeableStale(now)) {
            // Stale-while-revalidate: serve the stale copy now, refresh in the
            // background so exactly one fetch refreshes it (no stampede).
            metrics_.recordHit();
            metrics_.recordStaleServed();
            HttpResponse resp = cached->toResponse();
            decorate(resp, "STALE", cfg_);
            triggerRevalidation(req, key.value, cache);
            metrics_.recordBytesServed(resp.body.size());
            return resp;
        }
        // Fully expired — fall through to a miss.
    }

    // --- Miss path with request coalescing ---
    metrics_.recordMiss();
    auto acq = inflight_.acquire(key.value);

    if (acq.leader) {
        // Consult the shared L2 tier before paying for an origin round-trip: a
        // key another replica already fetched is served here without hitting
        // origin, and promoted into this replica's L1 for subsequent hits.
        if (l2_) {
            if (auto l2entry = l2_->get(key.value)) {
                metrics_.recordL2Hit();
                cache.put(key.value, *l2entry);  // promote L2 -> L1
                HttpResponse resp = l2entry->toResponse();
                resp.headers["Age"] = "0";
                decorate(resp, "HIT", cfg_, "l2");
                resp.headers["X-Cache-Tier"] = "L2";
                inflight_.publish(key.value, resp, true);
                metrics_.recordBytesServed(resp.body.size());
                return resp;
            }
            metrics_.recordL2Miss();
        }

        bool rejected = false;
        OriginResult r = fetchGuarded(req, target, rejected);
        HttpResponse resp;
        bool success = false;
        if (r.ok) {
            resp = r.response;
            storeTiers(req, resp, cache);  // populate both L1 and L2
            decorate(resp, "MISS", cfg_, policy_.decide(req.path, resp).reason);
            success = true;
        } else {
            // If we have a stale copy, prefer serving it over a hard error.
            if (cached && cached->isServeableStale(now)) {
                resp = cached->toResponse();
                decorate(resp, "STALE", cfg_, "origin-unavailable");
                metrics_.recordStaleServed();
            } else {
                resp = HttpResponse::simple(rejected ? 503 : 502,
                                            rejected ? "Service Unavailable" : "Bad Gateway",
                                            rejected ? "origin circuit open\n" : "origin error\n");
                decorate(resp, "MISS", cfg_);
            }
        }
        inflight_.publish(key.value, resp, success);
        metrics_.recordBytesServed(resp.body.size());
        return resp;
    }

    // Waiter: block on the leader's fetch instead of hitting origin ourselves.
    acq.slot->waitReady();
    HttpResponse resp;
    {
        std::lock_guard<std::mutex> lk(acq.slot->m);
        resp = acq.slot->response;
    }
    if (acq.slot->success) {
        // Store the shared result so subsequent requests for this key hit cache.
        maybeStore(req, resp, cache);
    }
    decorate(resp, "MISS", cfg_, "coalesced");
    metrics_.recordBytesServed(resp.body.size());
    return resp;
}

}  // namespace edgecache
