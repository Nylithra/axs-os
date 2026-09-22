#!/usr/bin/env bash
# Aşama 4: AxsOS kabuğu axsh'ı statik derle.
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
mkdir -p "$BUILD/sysroot/bin"
log "axsh derleniyor..."
"$CC" -static -Os -Wall -Wextra -Werror -o "$BUILD/sysroot/bin/axsh" "$ROOT/axsh/axsh.c"
strip "$BUILD/sysroot/bin/axsh"
log "Hazır: /bin/axsh ($(du -h "$BUILD/sysroot/bin/axsh" | cut -f1))"
