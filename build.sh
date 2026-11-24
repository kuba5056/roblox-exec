#!/usr/bin/env bash
# rbx-exec one-shot build. uses the nix flake when available, falls
# back to a direct mingw + cargo path for non-nix dev shells.
set -euo pipefail

cd "$(dirname "$0")"

if command -v nix >/dev/null 2>&1; then
  echo ">> nix build"
  nix build .#default -o result
  echo ">> installed under ./result/bin/rbx-exec"
  exit 0
fi

echo ">> fallback build (no nix)"
need() { command -v "$1" >/dev/null || { echo "missing: $1"; exit 2; }; }
need x86_64-w64-mingw32-gcc
need x86_64-w64-mingw32-g++
need cargo
need cmake

mkdir -p dist

echo ">> shim64.dll"
pushd shim >/dev/null
x86_64-w64-mingw32-gcc \
  -shared -O2 -static-libgcc -nostdlib \
  -Wl,--entry=_DllMainCRTStartup \
  -o ../dist/shim64.dll \
  shim.c pe.c ldr_unlink.c hooks.c luau_bridge.c pipe.c scan.c \
  -lkernel32 -lntdll -luser32 -lwininet
popd >/dev/null

echo ">> rbx-loader.exe"
pushd loader >/dev/null
x86_64-w64-mingw32-g++ \
  -std=c++20 -O2 -static -static-libgcc -static-libstdc++ \
  -o ../dist/rbx-loader.exe \
  loader.cpp manual_map.cpp apc_inject.cpp \
  -lntdll -lkernel32
popd >/dev/null

echo ">> luau-compile"
if [[ ! -d luau/src ]]; then
  git clone --depth 1 --branch 0.650 https://github.com/luau-lang/luau.git luau/src
fi
cmake -S luau/src -B luau/build -DLUAU_BUILD_CLI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build luau/build -j --target luau-compile
cp luau/build/luau-compile dist/

echo ">> rbx-exec-ui"
pushd ui >/dev/null
cargo build --release
cp target/release/rbx-exec-ui ../dist/
popd >/dev/null

install -m 0755 scripts/rbx-exec dist/rbx-exec-launcher
echo ">> dist/ ready"
