#!/usr/bin/env bash
# Aşama 3: C ile yazılmış AxsOS init'ini statik derle.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
mkdir -p "$BUILD/sysroot/sbin"
log "init derleniyor..."
"$CC" -static -Os -Wall -Wextra -Werror -o "$BUILD/sysroot/sbin/init" "$ROOT/init/init.c"
strip "$BUILD/sysroot/sbin/init"
ln -sf sbin/init "$BUILD/sysroot/init"
log "Hazır: /sbin/init ($(du -h "$BUILD/sysroot/sbin/init" | cut -f1))"
