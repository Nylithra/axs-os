#!/usr/bin/env bash
# Market uygulamaları: apps/<ad>/ altındaki grafik programları libaxsapp ile statik derle,
# simgelerini üret ve her birini axpkg paket dizinine (build/pkgs/<ad>) hazırla.
# Paketlerin .axp'ye dönüştürülmesi ve depo dizini (INDEX) axpkg aşamasında yapılır.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
STB="$BUILD/stb"
FONTS="$BUILD/fonts"
[ -f "$STB/stb_image.h" ] && [ -f "$FONTS/Inter.ttf" ] || die "stb/yazı tipleri yok, önce: ./build.sh axsde"
W="$BUILD/apps"
PK="$BUILD/pkgs"
ICONS="$BUILD/icons"
mkdir -p "$W/obj" "$ICONS"
rm -rf "$PK" && mkdir -p "$PK"

CFLAGS=(-O2 -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -I"$ROOT/axsde" -I"$ROOT/axsde/client" -isystem "$STB")

# 1) simge üreticisi (host'ta çalışır)
log "Simge üreticisi derleniyor (host)..."
gcc "${CFLAGS[@]}" -o "$W/mkicon" "$ROOT/apps/mkicon.c" \
    "$ROOT"/axsde/{gfx,font,icons,ui}.c -lm
export AXSDE_FONTS="$FONTS"

# 2) libaxsapp nesneleri (musl, statik)
log "libaxsapp derleniyor..."
LIBOBJ=()
for src in client/axsapp gfx font icons ui img; do
    o="$W/obj/$(basename "$src").o"
    "$CC" "${CFLAGS[@]}" -c -o "$o" "$ROOT/axsde/$src.c"
    LIBOBJ+=("$o")
done
IMG_OBJ="$W/obj/img.o"

# conf_get <dosya> <anahtar>
conf_get() { sed -n "s/^$2=//p" "$1" | head -1; }

# 3) uygulamalar
n=0
for d in "$ROOT"/apps/*/; do
    conf="$d/app.conf"
    [ -f "$conf" ] || continue
    id="$(conf_get "$conf" name)"
    title="$(conf_get "$conf" title)"
    libs="$(conf_get "$conf" libs)"
    objs=("${LIBOBJ[@]}")
    [ "$libs" = img ] || objs=("${objs[@]/$IMG_OBJ}")
    P="$PK/$id"
    mkdir -p "$P/files/usr/bin" "$P/files/usr/share/axsde/apps" "$P/files/usr/share/axsde/icons"
    "$CC" -static "${CFLAGS[@]}" -o "$P/files/usr/bin/$id" "$d/main.c" ${objs[@]} -lm \
        || die "derlenemedi: $id"
    strip "$P/files/usr/bin/$id"
    "$W/mkicon" icon "$id" "$ICONS/$id.png" 128 || die "simge üretilemedi: $id"
    cp "$ICONS/$id.png" "$P/files/usr/share/axsde/icons/$id.png"
    # MANIFEST: app.conf'taki paket alanları (libs derlemeye özeldir)
    grep -v '^libs=' "$conf" > "$P/MANIFEST"
    echo "app=$id" >> "$P/MANIFEST"
    {
        echo "name=$title"
        echo "exec=/usr/bin/$id"
        echo "icon=/usr/share/axsde/icons/$id.png"
        echo "category=$(conf_get "$conf" category)"
        echo "desc=$(conf_get "$conf" description)"
        ext="$(conf_get "$conf" ext)"
        [ -n "$ext" ] && echo "ext=$ext"
    } > "$P/files/usr/share/axsde/apps/$id.app"
    n=$((n + 1))
done

# xkopru: X uygulamalarını AxsDE penceresinde gösteren köprü (tarayici-ortami paketine girer)
"$CC" -static "${CFLAGS[@]}" -o "$W/xkopru" "$ROOT/apps/xkopru/main.c" ${LIBOBJ[@]/$IMG_OBJ} -lm \
    || die "derlenemedi: xkopru"
strip "$W/xkopru"

# Resim Görüntüleyici örnek resimlerle gelir
if [ -d "$PK/resim-gorucu" ]; then
    S="$PK/resim-gorucu/files/usr/share/axsde/ornek-resimler"
    mkdir -p "$S"
    "$W/mkicon" samples "$S" || die "örnek resimler üretilemedi"
fi

# 4) pkgs/ altındaki (terminal) paketlerin simgeleri
for d in "$ROOT"/pkgs/*/ "$ROOT"/tarayici/*/ tarayici-ortami; do
    name="$(basename "$d")"
    "$W/mkicon" icon "$name" "$ICONS/$name.png" 128 2>/dev/null || true
done
log "Hazır: $n uygulama paketi (build/pkgs), $(ls "$ICONS" | wc -l) simge"
