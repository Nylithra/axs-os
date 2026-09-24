#!/usr/bin/env bash
# Aşama 6: axpkg paket yöneticisini derle ve pkgs/ altındaki örnek paketleri
# /var/lib/axpkg/repo deposuna paketle.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
SYS="$BUILD/sysroot"
mkdir -p "$SYS/usr/bin" "$SYS/var/lib/axpkg/db" "$SYS/var/lib/axpkg/repo"
log "axpkg derleniyor..."
"$CC" -static -Os -Wall -Wextra -Werror -Wno-format-truncation -o "$SYS/usr/bin/axpkg" "$ROOT/axpkg/axpkg.c"
strip "$SYS/usr/bin/axpkg"

# Depo: pkgs/ (terminal paketleri) + build/pkgs/ (apps aşamasının grafik uygulamaları)
REPO="$SYS/var/lib/axpkg/repo"
rm -rf "$REPO" && mkdir -p "$REPO"
for d in "$ROOT"/pkgs/*/ "$BUILD"/pkgs/*/; do
    [ -f "$d/MANIFEST" ] || continue
    ( cd "$REPO" && "$SYS/usr/bin/axpkg" create "$d" >/dev/null ) \
        || die "paket oluşturulamadı: $d"
done
# Market bu dizini okur: MANIFEST alanları + dosya adı, boyutlar, sha256, komutlar; + simgeler
"$ROOT/scripts/repo-index.sh" "$REPO" "$BUILD/icons" >/dev/null
# Uzak depo (internetten indirilen paketler: tarayıcılar vb.)
mkdir -p "$SYS/etc/axpkg"
echo "$REMOTE_REPO" > "$SYS/etc/axpkg/depolar"
log "Hazır: /usr/bin/axpkg + $(ls "$REPO"/*.axp | wc -l) depo paketi (INDEX + simgeler); uzak depo: $REMOTE_REPO"

# Hazır kurulu gelenler (initramfs aşaması kurar)
echo "hesap-makinesi saat resim-gorucu" > "$REPO/PREINSTALL"
