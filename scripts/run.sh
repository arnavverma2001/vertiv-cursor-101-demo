#!/usr/bin/env bash
# Build HelioSpan Monitor and serve it on http://127.0.0.1:8000
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${CXX:-}" ]] && ! printf 'int main(){return 0;}\n' | c++ -x c++ -std=c++17 -o /tmp/heliospan-cc-probe - >/dev/null 2>&1; then
  if command -v g++ >/dev/null 2>&1; then
    export CXX=g++
  fi
fi
rm -f /tmp/heliospan-cc-probe

jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"${jobs}"
exec ./build/heliospan
