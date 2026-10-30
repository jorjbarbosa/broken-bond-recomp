#!/usr/bin/env bash
# Applies patches/series to the sdk submodule, then builds the SDK with Clang and
# installs it in build/sdk-install. The Linux twin of build-sdk.ps1.
set -euo pipefail

configuration=Release
jobs=$(( $(nproc) < 12 ? $(nproc) : 12 ))
while [ $# -gt 0 ]; do
  case "$1" in
    --configuration) configuration="$2"; shift 2 ;;
    --jobs) jobs="$2"; shift 2 ;;
    *) echo "Usage: $0 [--configuration Debug|Release|RelWithDebInfo] [--jobs N]" >&2; exit 2 ;;
  esac
done
case "$configuration" in
  Debug|Release|RelWithDebInfo) ;;
  *) echo "Unknown configuration: $configuration" >&2; exit 2 ;;
esac

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_source="$project_root/sdk"
sdk_build="$project_root/build/sdk"
sdk_install="$project_root/build/sdk-install"

if [ ! -e "$sdk_source/.git" ]; then
  echo "SDK not found at $sdk_source. Run: git submodule update --init --recursive" >&2
  exit 1
fi

while IFS=$' \t\r' read -r patch_name _ || [ -n "$patch_name" ]; do
  if [ -z "$patch_name" ] || [ "${patch_name:0:1}" = "#" ]; then
    continue
  fi
  sdk_patch="$project_root/patches/$patch_name"
  if git -C "$sdk_source" apply --check "$sdk_patch" 2>/dev/null; then
    git -C "$sdk_source" apply "$sdk_patch"
  elif ! git -C "$sdk_source" apply --reverse --check "$sdk_patch" 2>/dev/null; then
    echo "SDK patch $patch_name cannot be applied or verified. Existing SDK changes were preserved." >&2
    exit 1
  fi
done < "$project_root/patches/series"

if [ "$(head -n 1 "$sdk_source/thirdparty/libmspack/cabextract/mspack/lzxd.c")" = '../../libmspack/mspack/lzxd.c' ]; then
  echo "The libmspack checkout contains symlink placeholders. Initialize the SDK submodules with Git core.symlinks=true before building." >&2
  exit 1
fi

# Line tables in librexruntime.so, for readable crash reports.
cmake --preset linux-amd64 -S "$sdk_source" -B "$sdk_build" \
  "-DCMAKE_C_FLAGS=-march=x86-64-v2 -gline-tables-only" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2 -gline-tables-only" \
  "-DCMAKE_INSTALL_PREFIX=$sdk_install" -DREXGLUE_BUILD_TESTS=OFF
cmake --build "$sdk_build" --config Release --target rexglue --parallel "$jobs"

configurations=(Release)
if [ "$configuration" != Release ]; then
  configurations+=("$configuration")
fi
for build_configuration in "${configurations[@]}"; do
  cmake --build "$sdk_build" --config "$build_configuration" --parallel "$jobs"
  cmake --install "$sdk_build" --config "$build_configuration"
done

echo "SDK installed to $sdk_install"
