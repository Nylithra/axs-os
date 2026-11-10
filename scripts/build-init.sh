#!/usr/bin/env bash
# Aşama 3: C ile yazılmış AxsOS init'ini statik derle.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
mkdir -p "$BUILD/sysroot/sbin"
log "init derleniyor..."
"$CC" -static -Os -Wall -Wextra -Werror -o "$BUILD/sysroot/sbin/init" "$ROOT/init/init.c"
strip "$BUILD/sysroot/sbin/init"
ln -sf sbin/init "$BUILD/sysroot/init"
# açılış ekranı logosu (init, masaüstü açılana kadar çerçeve tamponuna çizer)
mkdir -p "$BUILD/tools" "$BUILD/sysroot/usr/share/axsos"
gcc -O2 -I"$ROOT/axsde" -o "$BUILD/tools/acilis-logo" "$ROOT/tools/acilis-logo.c" "$ROOT/axsde/gfx.c" -lm \
    || die "açılış logosu aracı derlenemedi"
"$BUILD/tools/acilis-logo" --ham "$BUILD/sysroot/usr/share/axsos/acilis.ppm" 200
log "Hazır: /sbin/init ($(du -h "$BUILD/sysroot/sbin/init" | cut -f1))"
