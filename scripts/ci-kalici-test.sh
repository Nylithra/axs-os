#!/usr/bin/env bash
# Kalıcı depolama testi: ISO'yu aynı sanal diskle dört kez açar.
#   1) boş diski "kalici hazirla" ile biçimlendir
#   2) kalıcı kipte dosya yaz + paket kur
#   3) yeniden açınca dosya ve paket duruyor mu; "kalici sifirla"
#   4) fabrika ayarları: dosya ve paket gitmiş mi
# Çıktı: build/out/kalici-seri.log
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${AXSOS_BUILD:-build}/out"
[ -f "$OUT/axsos.iso" ] || { echo "ISO yok (önce ./build.sh)" >&2; exit 1; }
D="$OUT/kalici-test.img"
rm -f "$D" && qemu-img create -q -f raw "$D" 2G
LOG="$OUT/kalici-seri.log"; : > "$LOG"
if [ -w /dev/kvm ]; then BOOT=35; W=8; else BOOT=90; W=25; fi
fail() { printf '\033[1;31m[test] BAŞARISIZ:\033[0m %s\n' "$*" >&2; tail -40 "$LOG"; exit 1; }
ok()   { printf '\033[1;32m[test] tamam:\033[0m %s\n' "$*"; }
ac() { # ac <aşama> <komutlar...>
    local n=$1; shift
    echo "===== açılış $n" >> "$LOG"
    DISK="$D" RUN_MODE=iso BOOT_WAIT=$BOOT CMD_WAIT=$W scripts/qemu-test.sh "$@" 'sync; poweroff' >> "$LOG" 2>&1 || true
}
section() { awk -v n="===== açılış $1" '$0 == n {f = 1; next} /^===== açılış/ {f = 0} f' "$LOG"; }

ac 1 'kalici durum' 'kalici hazirla vda'
section 1 | grep -q "Hazır. Yeniden başlatınca" || fail "disk hazırlanamadı"
ok "boş disk kalıcı depolama olarak hazırlandı"

ac 2 'kalici durum | head -2' 'echo kalici-deneme-42 > /root/kalici.txt' 'axpkg install sayi-tahmin | tail -1'
section 2 | grep -q "Kalıcı depolama: ETKİN" || fail "ikinci açılışta kalıcı depolama etkin değil"
ok "ikinci açılışta kalıcı depolama etkin"

ac 3 'cat /root/kalici.txt' 'axpkg list | grep -c sayi-tahmin' 'kalici sifirla'
section 3 | grep -q "^kalici-deneme-42" || fail "dosya yeniden başlatınca kayboldu"
ok "dosya yeniden başlatınca korundu"
section 3 | grep -qx "1" || fail "kurulan paket yeniden başlatınca kayboldu"
ok "kurulan paket yeniden başlatınca korundu"

ac 4 'ls /root/kalici.txt 2>&1 | head -1' 'kalici durum | head -1'
section 4 | grep -q "No such file\|bulunamadı\|can't access" || fail "fabrika ayarlarına dönüş dosyayı silmedi"
section 4 | grep -q "Kalıcı depolama: ETKİN" || fail "sıfırlamadan sonra kalıcı depolama etkin değil"
ok "fabrika ayarlarına dönüş çalışıyor"
rm -f "$D"
echo "[test] kalıcı depolama testleri geçti"
