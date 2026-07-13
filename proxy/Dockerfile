# ---- Build stage ----
FROM ubuntu:22.04 AS build
ARG ENABLE_KAFKA=ON
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ca-certificates librdkafka-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY test ./test

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DEDGECACHE_KAFKA=${ENABLE_KAFKA} \
    && cmake --build build -j "$(nproc)"

# Run the unit tests as part of the image build so a broken build fails CI.
RUN cd build && ctest --output-on-failure

# ---- Runtime stage ----
FROM debian:bookworm-slim AS runtime
# librdkafka1 provides the runtime .so for the (optionally compiled-in) Kafka
# access-log producer. curl is used by the container HEALTHCHECK.
RUN apt-get update && apt-get install -y --no-install-recommends \
      curl ca-certificates librdkafka1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -r -u 10001 edgecache

COPY --from=build /src/build/edgecache_proxy /usr/local/bin/edgecache_proxy

USER edgecache
EXPOSE 8080 9100

# Liveness/readiness are served on the metrics port.
HEALTHCHECK --interval=10s --timeout=2s --retries=3 \
  CMD curl -fsS http://localhost:9100/healthz || exit 1

ENTRYPOINT ["/usr/local/bin/edgecache_proxy"]
