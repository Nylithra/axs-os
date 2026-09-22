#!/usr/bin/env bash
# AxsOS - QEMU'da çalıştır (KVM yoksa TCG, -nographic, seri konsol).
#   ./run.sh                 kernel + initramfs ile doğrudan açılış
#   ./run.sh iso             ISO'dan GRUB ile açılış (BIOS)
#   ./run.sh iso-uefi        ISO'dan GRUB ile açılış (UEFI, OVMF gerekir)
#   TIMEOUT=30 ./run.sh      30 sn sonra QEMU'yu kapat (otomatik test için)
#   VERBOSE=1 ./run.sh       kernel mesajlarını göster
#   APPEND="..." ./run.sh    kernel komut satırına ekle
#   MEM=1G ./run.sh          bellek miktarı (varsayılan 512M)
# QEMU'dan çıkış: Ctrl-a ardından x   (ya da sistemde: poweroff)
set -euo pipefail
cd "$(dirname "$0")"

OUT=build/out
MODE="${1:-kernel}"
ARGS=(-m "${MEM:-512M}" -nographic -no-reboot -nic user,model=e1000)
[ -e /dev/kvm ] && [ -w /dev/kvm ] && ARGS+=(-enable-kvm -cpu host) || ARGS+=(-accel tcg)

case "$MODE" in
    kernel)
        [ -f "$OUT/bzImage" ] || { echo "Önce ./build.sh çalıştırın." >&2; exit 1; }
        CMDLINE="console=ttyS0 panic=-1 ${APPEND:-}"
        [ "${VERBOSE:-0}" = 1 ] || CMDLINE+=" quiet"
        ARGS+=(-kernel "$OUT/bzImage" -append "$CMDLINE")
        [ -f "$OUT/initramfs.cpio.gz" ] && ARGS+=(-initrd "$OUT/initramfs.cpio.gz")
        ;;
    iso|iso-uefi)
        [ -f "$OUT/axsos.iso" ] || { echo "Önce ./build.sh çalıştırın (ISO yok)." >&2; exit 1; }
        ARGS+=(-cdrom "$OUT/axsos.iso" -boot d)
        if [ "$MODE" = iso-uefi ]; then
            FW=/usr/share/ovmf/OVMF.fd
            [ -f "$FW" ] || { echo "OVMF yok: sudo apt-get install ovmf" >&2; exit 1; }
            ARGS+=(-bios "$FW")
        fi
        ;;
    *) echo "Bilinmeyen kip: $MODE (kernel | iso | iso-uefi)" >&2; exit 1 ;;
esac

if [ -n "${TIMEOUT:-}" ]; then
    exec timeout --foreground "$TIMEOUT" qemu-system-x86_64 "${ARGS[@]}"
fi
exec qemu-system-x86_64 "${ARGS[@]}"
