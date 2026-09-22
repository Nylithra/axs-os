#!/usr/bin/env bash
# AxsOS - QEMU'da çalıştır (KVM yok, -nographic, seri konsol).
#   ./run.sh                 normal açılış
#   TIMEOUT=30 ./run.sh      30 sn sonra QEMU'yu kapat (otomatik test için)
# QEMU'dan çıkış: Ctrl-a ardından x
set -euo pipefail
cd "$(dirname "$0")"

OUT=build/out
[ -f "$OUT/bzImage" ] || { echo "Önce ./build.sh çalıştırın." >&2; exit 1; }

ARGS=(-m "${MEM:-256M}" -nographic -no-reboot -kernel "$OUT/bzImage"
      -append "console=ttyS0 panic=-1 ${APPEND:-}")
[ -f "$OUT/initramfs.cpio.gz" ] && ARGS+=(-initrd "$OUT/initramfs.cpio.gz")
[ -e /dev/kvm ] && [ -w /dev/kvm ] && ARGS+=(-enable-kvm) || ARGS+=(-accel tcg)

if [ -n "${TIMEOUT:-}" ]; then
    exec timeout --foreground "$TIMEOUT" qemu-system-x86_64 "${ARGS[@]}"
fi
exec qemu-system-x86_64 "${ARGS[@]}"
