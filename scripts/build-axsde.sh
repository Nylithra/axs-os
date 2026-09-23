#!/usr/bin/env bash
# AxsDE masaüstü ortamı: stb_truetype + yazı tipleri + statik derleme.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
SYS="$BUILD/sysroot"
STB="$BUILD/stb"
FONTS="$BUILD/fonts"

# stb_truetype (tek başlık dosyası), sabit commit
if [ ! -f "$STB/.ref-$STB_REF" ]; then
    log "stb_truetype indiriliyor..."
    rm -rf "$STB" && git init -q "$STB"
    git -C "$STB" fetch -q --depth 1 https://github.com/nothings/stb "$STB_REF"
    git -C "$STB" -c advice.detachedHead=false checkout -q FETCH_HEAD
    touch "$STB/.ref-$STB_REF"
fi

# sparse_get <depo> <etiket> <dosya> <hedef>: depodan yalnızca tek dosyayı çek
sparse_get() {
    local tmp="$BUILD/fontsrc-$(basename "$1")-$2"
    if [ ! -f "$tmp/$3" ]; then
        rm -rf "$tmp"
        git -c advice.detachedHead=false clone -q --depth 1 --filter=blob:none --sparse --branch "$2" "$1" "$tmp"
        git -C "$tmp" sparse-checkout set --no-cone "/$3"
    fi
    cp "$tmp/$3" "$4"
}
mkdir -p "$FONTS"
if [ ! -f "$FONTS/.done-$INTER_VERSION-$JBMONO_VERSION" ]; then
    log "Yazı tipleri indiriliyor (Inter $INTER_VERSION, JetBrains Mono $JBMONO_VERSION)..."
    sparse_get https://github.com/rsms/inter "$INTER_VERSION" docs/font-files/InterVariable.ttf "$FONTS/Inter.ttf"
    sparse_get https://github.com/JetBrains/JetBrainsMono "$JBMONO_VERSION" fonts/ttf/JetBrainsMono-Regular.ttf "$FONTS/JetBrainsMono-Regular.ttf"
    sparse_get https://github.com/JetBrains/JetBrainsMono "$JBMONO_VERSION" fonts/ttf/JetBrainsMono-Bold.ttf "$FONTS/JetBrainsMono-Bold.ttf"
    touch "$FONTS/.done-$INTER_VERSION-$JBMONO_VERSION"
fi

log "AxsDE derleniyor..."
mkdir -p "$SYS/usr/bin" "$SYS/usr/share/fonts/axsde"
"$CC" -static -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation \
    -isystem "$STB" -o "$SYS/usr/bin/axsde" "$ROOT"/axsde/*.c -lm
strip "$SYS/usr/bin/axsde"
cp "$FONTS"/*.ttf "$SYS/usr/share/fonts/axsde/"
log "Hazır: /usr/bin/axsde ($(du -h "$SYS/usr/bin/axsde" | cut -f1)) + yazı tipleri ($(du -sh "$FONTS" | cut -f1))"
