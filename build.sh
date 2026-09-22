#!/usr/bin/env bash
# AxsOS - tek komutla derleme.
#   ./build.sh            her şeyi derle
#   ./build.sh kernel     yalnızca kernel
#   ./build.sh deps       eksik derleme paketlerini kur (apt-get)
#   ./build.sh clean      build/ çıktılarını sil (indirmeler korunur)
# Derleme klasörü: AXSOS_BUILD=/baska/yer ./build.sh  (varsayılan: ./build, ~3 GB)
set -euo pipefail
cd "$(dirname "$0")"

STAGES=(kernel busybox init axsh python axs axpkg initramfs iso)

run_stage() { bash "scripts/build-$1.sh"; }

case "${1:-all}" in
    deps)  exec scripts/deps.sh install ;;
    clean) ;;
    *)     scripts/deps.sh ;;   # eksik araç varsa derlemeden önce dur
esac

BUILD="${AXSOS_BUILD:-build}"

case "${1:-all}" in
    all)   for s in "${STAGES[@]}"; do run_stage "$s"; done ;;
    clean) find "$BUILD" -mindepth 1 -maxdepth 1 ! -name downloads -exec rm -rf {} + 2>/dev/null || true ;;
    *)     [ -f "scripts/build-$1.sh" ] || { echo "Bilinmeyen aşama: $1" >&2; exit 1; }
           run_stage "$1" ;;
esac
