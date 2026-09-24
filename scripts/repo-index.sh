#!/usr/bin/env bash
# Depo dizini üret: scripts/repo-index.sh <depo-dizini> [simge-dizini]
# Dizindeki her .axp için MANIFEST alanlarını + file, size, sha256, installed_size ve
# provides (paketin /usr/bin, /bin, /sbin altındaki komutları) INDEX dosyasına yazar.
# Paketin içinde /usr/share/axsde/icons/<ad>.png varsa ya da simge dizininde <ad>.png varsa
# depo/icons/<ad>.png olarak kopyalanır (Market bunları gösterir). FLAT=1 ise (GitHub sürüm
# dosyaları gibi alt dizin olmayan depolar) simgeler depo/icon-<ad>.png olur.
set -euo pipefail
REPO="$1"
ICONS="${2:-}"
cd "$REPO"
[ "${FLAT:-0}" = 1 ] || mkdir -p icons
: > INDEX.new
for f in $(ls *.axp 2>/dev/null | LC_ALL=C sort); do
    man="$(tar -xzOf "$f" MANIFEST 2>/dev/null || tar -xzOf "$f" ./MANIFEST)"
    name="$(printf '%s\n' "$man" | sed -n 's/^name=//p' | head -1)"
    list="$(tar -tzvf "$f")"
    provides="$(printf '%s\n' "$list" | awk '{print $NF}' | sed -n 's#^\(\./\)\{0,1\}files/\(usr/\)\{0,1\}s\{0,1\}bin/\([^/][^/]*\)$#\3#p' | sort -u | tr '\n' ' ' | sed 's/ $//')"
    isize="$(printf '%s\n' "$list" | awk '{s += $3} END {print s + 0}')"
    {
        printf '%s\n' "$man" | grep -v '^$'
        echo "file=$f"
        echo "size=$(stat -c %s "$f")"
        echo "sha256=$(sha256sum "$f" | cut -d' ' -f1)"
        echo "installed_size=$isize"
        [ -n "$provides" ] && echo "provides=$provides"
        echo
    } >> INDEX.new
    ic="icons/$name.png"
    [ "${FLAT:-0}" = 1 ] && ic="icon-$name.png"
    if [ -n "$ICONS" ] && [ -f "$ICONS/$name.png" ]; then
        cp "$ICONS/$name.png" "$ic"
    elif tar -tzf "$f" | grep -qx "\(\./\)\{0,1\}files/usr/share/axsde/icons/$name.png"; then
        tar -xzOf "$f" "files/usr/share/axsde/icons/$name.png" > "$ic" 2>/dev/null || rm -f "$ic"
    fi
    if [ -f "$ic" ]; then sed -i '$d' INDEX.new; printf 'icon=%s\n\n' "$ic" >> INDEX.new; fi
done
mv INDEX.new INDEX
echo "[repo-index] $(grep -c '^name=' INDEX) paket: $REPO/INDEX"
