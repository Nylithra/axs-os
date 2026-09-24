#!/usr/bin/env bash
# AxsOS - QEMU'da çalıştır (KVM yoksa TCG).
#   ./run.sh                 metin kipi: kernel + initramfs, -nographic, seri konsol
#   ./run.sh gui             masaüstü (AxsDE): ekran varsa QEMU penceresi, yoksa tarayıcı (noVNC)
#   ./run.sh iso             ISO'dan GRUB ile, metin kipi (BIOS)
#   ./run.sh iso-gui         ISO'dan GRUB ile, masaüstü
#   ./run.sh iso-uefi        ISO'dan GRUB ile, UEFI (OVMF), metin kipi
#   TIMEOUT=30 ./run.sh      30 sn sonra QEMU'yu kapat (otomatik test için)
#   VERBOSE=1 ./run.sh       kernel mesajlarını göster
#   APPEND="..." ./run.sh    kernel komut satırına ekle
#   MEM=4G ./run.sh          bellek miktarı (varsayılan 512M, masaüstünde 2G; tarayıcı için 3-4G)
#   PORT=6080 ./run.sh gui   tarayıcı kipinde HTTP portu
# QEMU'dan çıkış: Ctrl-a ardından x   (ya da sistemde: poweroff)
set -euo pipefail
cd "$(dirname "$0")"
. config/versions.sh

BUILD="${AXSOS_BUILD:-build}"
OUT="$BUILD/out"
MODE="${1:-kernel}"
GUI=0
case "$MODE" in gui|iso-gui) GUI=1 ;; esac

ARGS=(-m "${MEM:-$([ $GUI = 1 ] && echo 2G || echo 512M)}" -no-reboot -nic user,model=e1000)
if [ -e /dev/kvm ] && [ -w /dev/kvm ]; then
    ARGS+=(-enable-kvm -cpu host)
else
    ARGS+=(-accel tcg -smp 2)
fi

KCMD="panic=-1 ${APPEND:-}"
[ "${VERBOSE:-0}" = 1 ] || KCMD+=" quiet"

case "$MODE" in
    kernel|gui)
        [ -f "$OUT/bzImage" ] || { echo "Önce ./build.sh çalıştırın." >&2; exit 1; }
        if [ $GUI = 1 ]; then
            KCMD="console=tty0 console=ttyS0 video=1280x800 $KCMD"
        else
            KCMD="console=ttyS0 $KCMD"
        fi
        ARGS+=(-kernel "$OUT/bzImage" -append "$KCMD")
        [ -f "$OUT/initramfs.cpio.gz" ] && ARGS+=(-initrd "$OUT/initramfs.cpio.gz")
        ;;
    iso|iso-gui|iso-uefi)
        [ -f "$OUT/axsos.iso" ] || { echo "Önce ./build.sh çalıştırın (ISO yok)." >&2; exit 1; }
        ARGS+=(-cdrom "$OUT/axsos.iso" -boot d)
        if [ "$MODE" = iso-uefi ]; then
            FW=/usr/share/ovmf/OVMF.fd
            [ -f "$FW" ] || { echo "OVMF yok: sudo apt-get install ovmf" >&2; exit 1; }
            ARGS+=(-bios "$FW")
        fi
        ;;
    *) echo "Bilinmeyen kip: $MODE (kernel | gui | iso | iso-gui | iso-uefi)" >&2; exit 1 ;;
esac

if [ $GUI = 0 ]; then
    ARGS+=(-nographic)
else
    # Masaüstü: sanal tablet (fare imleci tam hizalı) + klavye; seri konsol terminalde kalır
    ARGS+=(-device virtio-tablet-pci -device virtio-keyboard-pci -serial mon:stdio -audio none)
    if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && [ "${NOVNC:-0}" != 1 ]; then
        ARGS+=(-display "gtk,zoom-to-fit=on" -name AxsOS)
    else
        # Ekran yok (Codespaces / Cloud Shell): tarayıcıda noVNC
        NOV="$BUILD/novnc"
        if [ ! -f "$NOV/vnc.html" ]; then
            echo "[axsos] noVNC indiriliyor ($NOVNC_VERSION)..."
            rm -rf "$NOV"
            git -c advice.detachedHead=false clone -q --depth 1 --branch "$NOVNC_VERSION" https://github.com/novnc/noVNC "$NOV"
        fi
        PORT="${PORT:-6080}"
        ARGS+=(-display none -vnc 127.0.0.1:1)
        python3 scripts/novnc-server.py "$NOV" "$PORT" 127.0.0.1:5901 &
        SRV=$!
        trap 'kill $SRV 2>/dev/null' EXIT
        echo
        echo "  ┌─────────────────────────────────────────────────────────────┐"
        echo "  │  AxsOS masaüstü tarayıcıda: http://localhost:$PORT             │"
        echo "  │  Codespaces: 'Ports' sekmesinde $PORT portunu açın            │"
        echo "  │  Cloud Shell: 'Web Önizleme' → 'Bağlantı noktasını değiştir' → $PORT │"
        echo "  │  Seri konsol bu terminalde. Çıkış: poweroff ya da Ctrl-a x   │"
        echo "  └─────────────────────────────────────────────────────────────┘"
        echo
    fi
fi

if [ -n "${TIMEOUT:-}" ]; then
    timeout --foreground "$TIMEOUT" qemu-system-x86_64 "${ARGS[@]}"
else
    qemu-system-x86_64 "${ARGS[@]}"
fi
