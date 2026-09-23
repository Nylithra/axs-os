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
rm -rf "$REPO" && mkdir -p "$REPO/icons"
INDEX="$REPO/INDEX"
: > "$INDEX"
for d in "$ROOT"/pkgs/*/ "$BUILD"/pkgs/*/; do
    [ -f "$d/MANIFEST" ] || continue
    ( cd "$REPO" && "$SYS/usr/bin/axpkg" create "$d" >/dev/null ) \
        || die "paket oluşturulamadı: $d"
    name="$(sed -n 's/^name=//p' "$d/MANIFEST")"
    ver="$(sed -n 's/^version=//p' "$d/MANIFEST")"
    file="$name-$ver.axp"
    # Market bu dizini okur: MANIFEST alanları + dosya adı, boyutlar; kayıtlar boş satırla ayrılır
    {
        grep -v '^$' "$d/MANIFEST"
        echo "file=$file"
        echo "size=$(stat -c %s "$REPO/$file")"
        echo "installed_size=$(du -sb "$d/files" | cut -f1)"
        echo
    } >> "$INDEX"
    [ -f "$BUILD/icons/$name.png" ] && cp "$BUILD/icons/$name.png" "$REPO/icons/"
done
log "Hazır: /usr/bin/axpkg + $(ls "$REPO"/*.axp | wc -l) depo paketi (INDEX + simgeler)"

# Hazır kurulu gelenler (initramfs aşaması kurar)
echo "hesap-makinesi saat resim-gorucu" > "$REPO/PREINSTALL"
