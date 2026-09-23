#!/usr/bin/env bash
# Derleme bağımlılıklarını denetler.
#   scripts/deps.sh           eksikleri listeler (eksik varsa 1 ile çıkar)
#   scripts/deps.sh install   eksikleri apt-get ile kurar (Debian/Ubuntu, Cloud Shell, Codespaces)
set -euo pipefail

# komut:paket  (paket adı Debian/Ubuntu için)
TOOLS=(
    gcc:build-essential make:build-essential strip:binutils perl:perl
    flex:flex bison:bison bc:bc rsync:rsync cpio:cpio xz:xz-utils gzip:gzip
    git:git curl:curl file:file python3:python3 musl-gcc:musl-tools
    qemu-system-x86_64:qemu-system-x86
    grub-mkrescue:grub-common xorriso:xorriso mformat:mtools
)
# başlık:paket
HEADERS=( gelf.h:libelf-dev openssl/ssl.h:libssl-dev )
# dosya:paket (ISO için GRUB modülleri, UEFI testi için OVMF)
FILES=(
    /usr/lib/grub/i386-pc/modinfo.sh:grub-pc-bin
    /usr/lib/grub/x86_64-efi/modinfo.sh:grub-efi-amd64-bin
    /usr/share/ovmf/OVMF.fd:ovmf
)

missing=()
for t in "${TOOLS[@]}"; do
    command -v "${t%%:*}" >/dev/null 2>&1 || missing+=("${t#*:}")
done
for h in "${HEADERS[@]}"; do
    echo "#include <${h%%:*}>" | gcc -E -x c - >/dev/null 2>&1 || missing+=("${h#*:}")
done
for f in "${FILES[@]}"; do
    [ -e "${f%%:*}" ] || missing+=("${f#*:}")
done
# tekrarları at
mapfile -t missing < <(printf '%s\n' "${missing[@]}" | awk 'NF && !seen[$0]++')

if [ "${#missing[@]}" -eq 0 ]; then
    [ "${1:-}" = install ] && echo "[axsos] Tüm bağımlılıklar kurulu."
    exit 0
fi

if [ "${1:-}" = install ]; then
    command -v apt-get >/dev/null || { echo "apt-get yok; elle kurun: ${missing[*]}" >&2; exit 1; }
    SUDO=""; [ "$(id -u)" -ne 0 ] && SUDO=sudo
    echo "[axsos] Kuruluyor: ${missing[*]}"
    $SUDO apt-get update -qq || true
    DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -qq "${missing[@]}"
    "$0" && echo "[axsos] Tüm bağımlılıklar kuruldu. Şimdi: ./build.sh"   # yeniden denetle
    exit
fi

printf '\033[1;31m[axsos] Eksik paketler:\033[0m %s\n' "${missing[*]}" >&2
printf 'Kurmak için:  \033[1m./build.sh deps\033[0m\n' >&2
printf '  (ya da: sudo apt-get install -y %s)\n' "${missing[*]}" >&2
exit 1
