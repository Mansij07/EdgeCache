# Kubernetes Deployment

## Layout

```
k8s/
  base/                 # the EdgeCache app (proxy, control-plane, analytics) + HPA/Ingress/NetworkPolicy
  overlays/local/       # base + dev infra (redis/postgres/kafka/dummy-origin) for kind/minikube
```

Stateful infra is **not** hand-rolled in `base/` — in production install it via
Helm charts and point the app config/secrets at those services.

## Local demo (kind)

```bash
# 1. Build images and load them into kind
docker build -t edgecache-proxy:local            ./proxy
docker build -t edgecache-control-plane:local    ./control-plane
docker build -t edgecache-analytics-consumer:local ./analytics-consumer
docker build -t edgecache-dummy-origin:local     ./dummy-origin

kind create cluster --name edgecache
kind load docker-image edgecache-proxy:local edgecache-control-plane:local \
  edgecache-analytics-consumer:local edgecache-dummy-origin:local --name edgecache

# 2. Deploy app + dev infra
kubectl apply -k deploy/k8s/overlays/local

# 3. Watch it come up
kubectl -n edgecache get pods -w
```

Port-forward to try it:

```bash
kubectl -n edgecache port-forward svc/edgecache-proxy 8080:80 &
kubectl -n edgecache port-forward svc/control-plane   9000:9000 &
curl -i http://localhost:8080/products/1
```

## HPA / autoscaling demo

The base ships an HPA scaling the proxy on **CPU** (simple, dependency-free):

```bash
kubectl -n edgecache get hpa edgecache-proxy -w
# generate load (from another shell):
kubectl -n edgecache run loadgen --image=williamyeh/wrk --restart=Never -- \
  -t4 -c200 -d120s http://edgecache-proxy.edgecache.svc.cluster.local/products/1
# watch replicas climb, then fall after load stops
kubectl -n edgecache get pods -w
```

### Scaling on request RATE (custom metric)

For the cleaner "scale on live request rate" story, install the Prometheus
Adapter and replace the HPA's Resource metric with a Pods metric on
`rate(edgecache_requests_total[1m])`. The proxy already exports that metric and
the pods carry `prometheus.io/scrape` annotations.

## Production infra via Helm (sketch)

```bash
helm repo add bitnami https://charts.bitnami.com/bitnami
helm install redis    bitnami/redis    -n edgecache --set auth.enabled=true
helm install postgres bitnami/postgresql -n edgecache
# Kafka via Strimzi operator or bitnami/kafka, with PersistentVolumeClaims.
```

Then update `edgecache-secrets` (`DATABASE_URL`, `REDIS_URL`) and the proxy
ConfigMap (`EDGECACHE_REDIS_HOST`, `EDGECACHE_KAFKA_BROKERS`) to point at them,
and drop `overlays/local/infra.yaml`.

## Probes recap

- **liveness** = `/healthz` (process only) — a Redis blip won't get a serving pod killed.
- **readiness** = `/readyz` (Redis reachable) — gates traffic on purge-freshness.
