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

# Örnek paketleri oluştur (host'ta da derlenmiş statik axpkg çalışır)
rm -f "$SYS/var/lib/axpkg/repo/"*.axp
for d in "$ROOT"/pkgs/*/; do
    [ -f "$d/MANIFEST" ] || continue
    ( cd "$SYS/var/lib/axpkg/repo" && "$SYS/usr/bin/axpkg" create "$d" >/dev/null ) \
        || die "paket oluşturulamadı: $d"
done
log "Hazır: /usr/bin/axpkg + $(ls "$SYS/var/lib/axpkg/repo" | wc -l) depo paketi"
