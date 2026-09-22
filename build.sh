#!/usr/bin/env bash
# AxsOS - tek komutla derleme.
#   ./build.sh            her şeyi derle
#   ./build.sh kernel     yalnızca kernel
#   ./build.sh clean      build/ çıktılarını sil (indirmeler korunur)
set -euo pipefail
cd "$(dirname "$0")"

STAGES=(kernel busybox init axsh python axs axpkg initramfs)

run_stage() { bash "scripts/build-$1.sh"; }

case "${1:-all}" in
    all)   for s in "${STAGES[@]}"; do run_stage "$s"; done ;;
    clean) find build -mindepth 1 -maxdepth 1 ! -name downloads -exec rm -rf {} + 2>/dev/null || true ;;
    *)     [ -f "scripts/build-$1.sh" ] || { echo "Bilinmeyen aşama: $1" >&2; exit 1; }
           run_stage "$1" ;;
esac
