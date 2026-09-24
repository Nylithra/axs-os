#!/usr/bin/env bash
# Tarayıcı uçtan uca testi (CI): ISO'yu QEMU'da açar; AxsOS, build/uzak-depo'daki paketleri
# (ağ kartı -> QEMU NAT -> 10.0.2.2) indirir, Firefox'u mozilla.org'dan ve Chrome'u
# google.com'dan gerçekten kurar ve ikisiyle de https://example.com ekran görüntüsü alır.
# Çıktılar: build/out/tarayici-seri.log, build/out/firefox.png, build/out/chrome.png
set -euo pipefail
cd "$(dirname "$0")/.."
B="${AXSOS_BUILD:-build}"
OUT="$B/out"
[ -f "$B/uzak-depo/INDEX" ] || { echo "önce: sudo ./build.sh tarayici" >&2; exit 1; }
PORT="${PORT:-8765}"
WEB="$B/ci-web"
rm -rf "$WEB" && mkdir -p "$WEB"
cp -l "$B"/uzak-depo/* "$WEB/" 2>/dev/null || cp "$B"/uzak-depo/* "$WEB/"

cat > "$WEB/test.sh" <<'EOT'
#!/bin/sh
# AxsOS içinde çalışan test betiği
H=http://10.0.2.2:PORT
echo "$H" > /etc/axpkg/depolar
ag durum
ag test
axpkg update || { echo TARAYICI-TEST-HATA update; exit 1; }
free -m
yukle() { [ -s "$1" ] && curl -s -T "$1" "$H/$(basename "$1")"; }
axpkg install firefox && echo FIREFOX-KURULDU
/usr/lib/tarayici/tarayici-baslat --basliksiz firefox --headless --window-size 1280,800 \
    --screenshot /home/axs/İndirilenler/firefox.png https://example.com
[ -s /root/İndirilenler/firefox.png ] && echo "FIREFOX-EKRAN-TAMAM" && yukle /root/İndirilenler/firefox.png
axpkg install google-chrome && echo CHROME-KURULDU
/usr/lib/tarayici/tarayici-baslat --basliksiz chrome --headless=new --window-size=1280,800 \
    --screenshot=/home/axs/İndirilenler/chrome.png https://example.com
[ -s /root/İndirilenler/chrome.png ] && echo "CHROME-EKRAN-TAMAM" && yukle /root/İndirilenler/chrome.png
free -m
echo TARAYICI-TEST-BITTI
EOT
sed -i "s/PORT/$PORT/" "$WEB/test.sh"

python3 scripts/ci-web.py "$WEB" "$PORT" &
WEBPID=$!
LOG="$OUT/tarayici-seri.log"
FIFO="$(mktemp -u)"; mkfifo "$FIFO"
trap 'kill $WEBPID 2>/dev/null; rm -f "$FIFO"; pkill -f "^qemu-system-x86_64.*axsos.iso" 2>/dev/null || true' EXIT
if [ -w /dev/kvm ]; then BOOT=40; LIMIT=1200; else BOOT=100; LIMIT=3600; fi
MEM="${MEM:-4G}" TIMEOUT=$((LIMIT + BOOT + 60)) ./run.sh iso < "$FIFO" > "$LOG" 2>&1 &
exec 3>"$FIFO"
sleep "$BOOT"
printf 'curl -s http://10.0.2.2:%s/test.sh | sh 2>&1\n' "$PORT" >&3
t=0
until grep -q "TARAYICI-TEST-BITTI\|TARAYICI-TEST-HATA" "$LOG" || [ $t -ge $LIMIT ]; do sleep 5; t=$((t + 5)); done
printf 'poweroff\n' >&3
sleep 5
exec 3>&-
cp "$WEB"/yukari/*.png "$OUT/" 2>/dev/null || true

# Masaüstünde Firefox (AxsDE penceresinde) ekran görüntüsü — bilgi amaçlı, testi düşürmez
if grep -q "FIREFOX-EKRAN-TAMAM" "$LOG"; then
    echo "[test] masaüstünde Firefox ekran görüntüsü alınıyor..."
    GW=$([ -w /dev/kvm ] && echo 45 || echo 120)
    MEM="${MEM:-4G}" timeout 900 scripts/qemu-gui.py --iso "wait:$GW" click:1210,17 key:ctrl-alt-t wait:3 \
        "type:echo http://10.0.2.2:$PORT > /etc/axpkg/depolar; axpkg update; axpkg install firefox && firefox https://example.com &" \
        key:ret wait:240 "shot:$OUT/firefox-masaustu.png" >/dev/null 2>&1 || echo "[test] (masaüstü görüntüsü alınamadı)"
fi

sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$LOG" | grep -E "FIREFOX|CHROME|TARAYICI|HATA|kuruldu|sha256|tamam|BAŞARISIZ" || true
ok=1
grep -q "FIREFOX-EKRAN-TAMAM" "$LOG" && echo "[test] tamam: Firefox kuruldu ve sayfa açtı" || { echo "[test] BAŞARISIZ: Firefox"; ok=0; }
grep -q "CHROME-EKRAN-TAMAM" "$LOG" && echo "[test] tamam: Chrome kuruldu ve sayfa açtı" || { echo "[test] BAŞARISIZ: Chrome"; ok=0; }
[ $ok = 1 ] || { tail -60 "$LOG"; exit 1; }
