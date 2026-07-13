cmake_minimum_required(VERSION 3.16)
project(edgecache_proxy CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(Threads REQUIRED)

# Optional Kafka access-log producer. OFF by default so the default build and the
# unit tests have zero third-party dependencies. docker-compose builds it ON.
option(EDGECACHE_KAFKA "Build with the librdkafka access-log producer" OFF)

# Core library: everything except the entrypoint, so tests can link against it.
add_library(edgecache_core STATIC
  src/config/Config.cpp
  src/http/Http.cpp
  src/cache/CacheKey.cpp
  src/cache/LRUCache.cpp
  src/cache/ShardedCache.cpp
  src/policy/CachePolicy.cpp
  src/origin/HttpOriginClient.cpp
  src/origin/CircuitBreaker.cpp
  src/redis/RedisClient.cpp
  src/redis/RedisL2Cache.cpp
  src/redis/RuleStore.cpp
  src/redis/RedisCoordinator.cpp
  src/coalesce/InFlightRegistry.cpp
  src/metrics/MetricsRegistry.cpp
  src/metrics/MetricsServer.cpp
  src/net/Listener.cpp
  src/net/EventLoop.cpp
  src/analytics/KafkaAccessLogSink.cpp
  src/RequestHandler.cpp
  src/ProxyServer.cpp
)
target_include_directories(edgecache_core PUBLIC src)
target_link_libraries(edgecache_core PUBLIC Threads::Threads)

if(EDGECACHE_KAFKA)
  find_library(RDKAFKA_LIB rdkafka REQUIRED)
  target_compile_definitions(edgecache_core PUBLIC EDGECACHE_KAFKA)
  target_link_libraries(edgecache_core PUBLIC ${RDKAFKA_LIB})
  message(STATUS "EdgeCache: Kafka access-log producer ENABLED (${RDKAFKA_LIB})")
endif()

# The proxy binary. Statically link the C++/gcc runtimes so a slim runtime image
# (without libstdc++6 preinstalled) can run the binary.
add_executable(edgecache_proxy src/main.cpp)
target_link_libraries(edgecache_proxy PRIVATE edgecache_core)
target_link_options(edgecache_proxy PRIVATE -static-libgcc -static-libstdc++)

# -------- Tests --------
enable_testing()

add_executable(unit_tests
  test/test_main.cpp
  test/test_lru.cpp
  test/test_sharded_cache.cpp
  test/test_l2_serialize.cpp
  test/test_cache_key.cpp
  test/test_policy.cpp
  test/test_circuit_breaker.cpp
  test/test_coalescing.cpp
  test/test_rule_parse.cpp
  test/test_http_parse.cpp
)
target_link_libraries(unit_tests PRIVATE edgecache_core)
add_test(NAME unit_tests COMMAND unit_tests)
