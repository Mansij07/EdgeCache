#!/usr/bin/env bash
# Build + unit-test the C++ proxy inside WSL Ubuntu. Used for local validation.
set -u
echo "=== toolchain check ==="
have_gpp=$(command -v g++ || true)
have_cmake=$(command -v cmake || true)
echo "g++=$have_gpp cmake=$have_cmake"

if [ -z "$have_gpp" ] || [ -z "$have_cmake" ]; then
  echo "=== installing build-essential cmake (sudo) ==="
  if sudo -n true 2>/dev/null; then
    sudo apt-get update -y && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake
  else
    echo "NO_PASSWORDLESS_SUDO"
    exit 3
  fi
fi

echo "=== configure ==="
cd /mnt/d/EdgeCache/proxy || exit 1
rm -rf build-wsl
cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release -DEDGECACHE_KAFKA=OFF || exit 1

echo "=== build ==="
cmake --build build-wsl -j 4 || exit 1

echo "=== ctest ==="
cd build-wsl || exit 1
ctest --output-on-failure
echo "=== DONE rc=$? ==="
