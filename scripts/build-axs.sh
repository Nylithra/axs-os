#!/usr/bin/env bash
# Aşama 5 (2/2): Axs dilini (Nylithra/axs) statik Python ile birlikte sisteme ekle.
#   /usr/bin/python3        statik CPython (musl)
#   /usr/lib/axs            Axs yorumlayıcısı (axslang), axsweb, kütüphaneler, örnekler
#   /usr/bin/axs            -> /usr/lib/axs/axs
. "$(dirname "$0")/common.sh"
SYS="$BUILD/sysroot"
SRC="$BUILD/axs-src"
PYI="$BUILD/python-install"
[ -x "$PYI/usr/bin/python3" ] || die "Python yok, önce: ./build.sh python"

# Axs kaynağını AXS_REF'e sabitle
if [ ! -d "$SRC/.git" ] || [ "$(git -C "$SRC" rev-parse HEAD 2>/dev/null)" != "$(git -C "$SRC" rev-parse "$AXS_REF^{commit}" 2>/dev/null)" ]; then
    log "Axs dili indiriliyor ($AXS_REPO @ ${AXS_REF:0:10})..."
    rm -rf "$SRC"
    git init -q "$SRC"
    git -C "$SRC" fetch -q --depth 1 "$AXS_REPO" "$AXS_REF"
    git -C "$SRC" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi

log "Python + Axs sisteme ekleniyor..."
mkdir -p "$SYS/usr"
cp -a --remove-destination "$PYI/usr/." "$SYS/usr/"

DEST="$SYS/usr/lib/axs"
rm -rf "$DEST"
mkdir -p "$DEST"
for f in axs axslang axsweb kutuphaneler examples docs README.md LICENSE; do
    [ -e "$SRC/$f" ] && cp -a "$SRC/$f" "$DEST/"
done
find "$DEST" -name __pycache__ -prune -exec rm -rf {} +
chmod +x "$DEST/axs"
# Bayt kodunu önceden derle (açılışta yazma gerekmesin)
"$PYI/usr/bin/python3" -m compileall -q -d /usr/lib/axs "$DEST/axslang" "$DEST/axsweb" >/dev/null
ln -sf ../lib/axs/axs "$SYS/usr/bin/axs"
ln -sf python3 "$SYS/usr/bin/python"
echo "$(git -C "$SRC" rev-parse --short HEAD)" > "$DEST/.surum"
log "Hazır: axs ($(git -C "$SRC" log -1 --format='%h %s' | cut -c1-60))"
