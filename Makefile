# EdgeCache convenience targets. On Windows without `make`, run the underlying
# commands directly (see each target).
.PHONY: help build up down logs seed smoke test test-proxy test-cp \
        load-hot load-zipf k8s-local k8s-down clean

help:
	@echo "EdgeCache targets:"
	@echo "  make build        - build all docker images"
	@echo "  make up            - docker compose up (detached)"
	@echo "  make down          - docker compose down"
	@echo "  make seed          - register origin + rules in the control plane"
	@echo "  make smoke         - end-to-end purge-propagation check"
	@echo "  make test          - run all test suites"
	@echo "  make test-proxy    - build the C++ proxy and run its unit tests (in Docker)"
	@echo "  make load-hot      - wrk hot-key load test"
	@echo "  make load-zipf     - k6 Zipfian load test"
	@echo "  make k8s-local     - deploy to a local kind/minikube cluster"

build:
	cd deploy && docker compose build

up:
	cd deploy && docker compose up -d --build

down:
	cd deploy && docker compose down

logs:
	cd deploy && docker compose logs -f

seed:
	bash scripts/seed.sh

smoke:
	bash test/integration/purge_propagation.sh

# Build the C++ core + run unit tests inside a throwaway Docker build stage
# (works even without a local POSIX toolchain).
test-proxy:
	docker build --target build -t edgecache-proxy-test ./proxy

test-cp:
	cd control-plane && npm install && npm run typecheck && npm test

test: test-proxy test-cp
	@echo "All test suites executed."

load-hot:
	wrk -t4 -c100 -d30s -s test/load/hot-key.lua http://localhost:8080

load-zipf:
	k6 run -e BASE=http://localhost:8080 -e KEYSPACE=1000 test/load/zipfian.js

k8s-local:
	kubectl apply -k deploy/k8s/overlays/local

k8s-down:
	kubectl delete -k deploy/k8s/overlays/local || true

clean:
	rm -rf proxy/build control-plane/dist control-plane/node_modules \
	       analytics-consumer/dist analytics-consumer/node_modules \
	       dummy-origin/node_modules
