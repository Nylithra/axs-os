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
# GRUB arka planı (yüklenirken görünen açılış ekranı): logo + gradyan, PNG
LOGO="$BUILD/tools/acilis-logo"
[ -x "$LOGO" ] || gcc -O2 -I"$ROOT/axsde" -o "$LOGO" "$ROOT/tools/acilis-logo.c" "$ROOT/axsde/gfx.c" -lm
"$LOGO" --png "$BUILD/grub-arka.ppm" 1024 768 || die "GRUB arka planı üretilemedi"
python3 - "$BUILD/grub-arka.ppm" "$ISODIR/boot/grub/arka.png" <<'EOP'
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
hdr = d.split(b"\n", 3)
w, h = map(int, hdr[1].split())
px = hdr[3]
raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
def chunk(t, b):
    return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
open(sys.argv[2], "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                             + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
EOP

log "ISO oluşturuluyor (grub-mkrescue)..."
grub-mkrescue -o "$OUT/axsos.iso" "$ISODIR" -- -volid AXSOS >"$BUILD/iso.log" 2>&1 \
    || { cat "$BUILD/iso.log"; die "grub-mkrescue başarısız"; }
log "Hazır: build/out/axsos.iso ($(du -h "$OUT/axsos.iso" | cut -f1))"
