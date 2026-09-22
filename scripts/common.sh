# Shared helpers for AxsOS build scripts. Source, don't execute.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# AXSOS_BUILD ile derleme klasörü değiştirilebilir (ör. disk kotası dar ortamlarda)
BUILD="$(mkdir -p "${AXSOS_BUILD:-$ROOT/build}" && cd "${AXSOS_BUILD:-$ROOT/build}" && pwd)"
DL="$BUILD/downloads"
OUT="$BUILD/out"
JOBS="${JOBS:-$(nproc)}"

. "$ROOT/config/versions.sh"

mkdir -p "$BUILD" "$DL" "$OUT"

log() { printf '\033[1;36m[axsos]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[axsos] HATA:\033[0m %s\n' "$*" >&2; exit 1; }

# musl + kernel başlıklarıyla statik derleyici sarmalayıcısı.
# Kullanım: CC="$(axs_cc)" (build-kernel.sh başlıkları kurar).
KHDR="$BUILD/kernel-headers"
TOOLS="$BUILD/tools"
axs_cc() {
    [ -d "$KHDR/include/linux" ] || die "Kernel başlıkları yok, önce: ./build.sh kernel"
    mkdir -p "$TOOLS"
    cat > "$TOOLS/axs-cc" <<EOC
#!/bin/sh
# glibc gcc'nin "x86_64-linux-gnu" multiarch cevabı musl hedefiyle çelişir (CPython configure)
[ "\$1" = "--print-multiarch" ] && exit 0
exec musl-gcc -isystem "$KHDR/include" "\$@"
EOC
    chmod +x "$TOOLS/axs-cc"
    echo "$TOOLS/axs-cc"
}
