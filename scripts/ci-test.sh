#!/usr/bin/env bash
# ISO açılış testi (CI'da ve yerelde): ISO'yu QEMU'da gerçekten açar ve doğrular.
#   1) Metin: GRUB -> çekirdek -> init -> axsh; seri konsolda axs, axpkg ve python çalışıyor mu?
#   2) Masaüstü: AxsDE açılıyor mu? Ekran görüntüsü alır ve boş olmadığını denetler.
# Çıktılar: build/out/test-seri.log, build/out/ekran.png
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${AXSOS_BUILD:-build}/out"
ISO="$OUT/axsos.iso"
[ -f "$ISO" ] || { echo "ISO yok: $ISO (önce ./build.sh)" >&2; exit 1; }

# KVM varsa hızlı, yoksa yazılım emülasyonu: bekleme sürelerini ona göre ayarla
if [ -w /dev/kvm ]; then BOOT=35; GUIWAIT=45; else BOOT=90; GUIWAIT=120; fi

fail() { printf '\033[1;31m[test] BAŞARISIZ:\033[0m %s\n' "$*" >&2; exit 1; }
ok()   { printf '\033[1;32m[test] tamam:\033[0m %s\n' "$*"; }

echo "[test] 1/2 metin kipi açılışı (ISO, BIOS, GRUB)..."
RUN_MODE=iso BOOT_WAIT=$BOOT CMD_WAIT=10 scripts/qemu-test.sh \
    'uname -r' \
    'axs -e "print: AxsOS CI (6 * 7);"' \
    'axpkg available | grep -c axp; axpkg list' \
    'python3 -c "import ssl, sqlite3, zlib; print(\"python-ok\")"' \
    'poweroff' > "$OUT/test-seri.log" 2>&1 || true

grep -q "Linux version\|GNU GRUB\|AxsOS" "$OUT/test-seri.log" || fail "açılış çıktısı yok (bkz. $OUT/test-seri.log)"
grep -q "AxsOS CI 42" "$OUT/test-seri.log" || { tail -40 "$OUT/test-seri.log"; fail "axs çalışmadı"; }
ok "axs çalışıyor"
grep -q "python-ok" "$OUT/test-seri.log" || { tail -40 "$OUT/test-seri.log"; fail "python (ssl/sqlite3/zlib) çalışmadı"; }
ok "python + ssl + sqlite3 + zlib"
grep -q "hesap-makinesi" "$OUT/test-seri.log" || { tail -40 "$OUT/test-seri.log"; fail "hazır kurulu market uygulamaları yok"; }
ok "market deposu ve hazır kurulu uygulamalar"
grep -q "Power down\|Sistem kapatılıyor" "$OUT/test-seri.log" || fail "poweroff çalışmadı"
ok "poweroff"

echo "[test] 2/2 masaüstü açılışı (ISO, AxsDE)..."
rm -f "$OUT/ekran.png"
scripts/qemu-gui.py --iso "wait:$GUIWAIT" "shot:$OUT/ekran.png" >/dev/null
[ -f "$OUT/ekran.png" ] || fail "ekran görüntüsü alınamadı"
python3 - "$OUT/ekran.png" <<'EOF' || fail "masaüstü görünmüyor (ekran boş ya da metin konsolu)"
import struct, sys, zlib
d = open(sys.argv[1], "rb").read()
w, h = struct.unpack(">II", d[16:24])
pos, idat = 8, b""
while pos < len(d):
    ln, = struct.unpack(">I", d[pos:pos + 4])
    typ = d[pos + 4:pos + 8]
    if typ == b"IDAT":
        idat += d[pos + 8:pos + 8 + ln]
    pos += 12 + ln
raw = zlib.decompress(idat)
row = w * 3 + 1
def px(x, y):
    o = y * row + 1 + x * 3
    return raw[o:o + 3]
# ekranın genelinde renk çeşitliliği (metin konsolu ~2-3 renk olur)
colors = {px(x, y) for y in range(0, h, 7) for x in range(0, w, 7)}
# üst çubuk (y<34) ile dock bölgesi farklı olmalı; tamamen siyah olmamalı
dark = sum(1 for c in colors if max(c) < 12)
print("çözünürlük %dx%d, renk sayısı %d" % (w, h, len(colors)))
sys.exit(0 if len(colors) > 400 and dark < len(colors) * 0.5 else 1)
EOF
ok "AxsDE masaüstü açıldı ($OUT/ekran.png)"
echo "[test] tüm testler geçti"
