#!/usr/bin/env bash
# Aşama 7: GRUB ile açılabilir AxsOS ISO'su (BIOS + UEFI hibrit).
. "$(dirname "$0")/common.sh"
[ -f "$OUT/bzImage" ] && [ -f "$OUT/initramfs.cpio.gz" ] || die "Önce kernel ve initramfs derleyin"
command -v grub-mkrescue >/dev/null || die "grub-mkrescue yok: sudo apt-get install grub-pc-bin grub-efi-amd64-bin grub-common xorriso mtools"

ISODIR="$BUILD/iso"
rm -rf "$ISODIR"
mkdir -p "$ISODIR/boot/grub"
cp "$OUT/bzImage" "$OUT/initramfs.cpio.gz" "$ISODIR/boot/"
cp "$ROOT/iso/grub.cfg" "$ISODIR/boot/grub/grub.cfg"

log "ISO oluşturuluyor (grub-mkrescue)..."
grub-mkrescue -o "$OUT/axsos.iso" "$ISODIR" -- -volid AXSOS >"$BUILD/iso.log" 2>&1 \
    || { cat "$BUILD/iso.log"; die "grub-mkrescue başarısız"; }
log "Hazır: build/out/axsos.iso ($(du -h "$OUT/axsos.iso" | cut -f1))"
