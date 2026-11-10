#!/usr/bin/env bash
# Hazır kurulu geliştirme ve sistem araçları: gcc, make, binutils (as, ld, ar...), vim, htop,
# iproute2 (ip), ping (iputils), GNU tar, gzip, bzip2.
#
# AxsOS'un kendi programları musl ile statik derlenir; bu araçlar ise Ubuntu'nun derlenmiş
# paketlerinden (glibc) alınır: glibc çalışma zamanı (/lib64/ld-linux-x86-64.so.2 +
# /usr/lib/x86_64-linux-gnu) sisteme eklenir, böylece gcc'nin derlediği programlar da AxsOS'ta
# doğrudan çalışır. Paketler archive.ubuntu.com'dan indirilir, sha256 doğrulanır.
. "$(dirname "$0")/common.sh"
SYS="$BUILD/sysroot"
W="$BUILD/araclar"
DL="$BUILD/downloads/deb"
IDX="$W/index-$ARACLAR_UBUNTU"
R="$W/root"
MIRROR="${UBUNTU_MIRROR:-http://archive.ubuntu.com/ubuntu}"
# Ana ayna bağlantıyı koparır ya da takılırsa sırayla diğer aynalar denenir.
AYNALAR="$MIRROR http://azure.archive.ubuntu.com/ubuntu http://us.archive.ubuntu.com/ubuntu http://de.archive.ubuntu.com/ubuntu"
# indir <hedef> <aynaya göre yol>
indir() {
    local a
    for a in $AYNALAR; do
        curl -fsSL --connect-timeout 20 --max-time 600 --speed-limit 1024 --speed-time 60 \
            --retry 2 --retry-all-errors --retry-delay 3 -o "$1" "$a/$2" && return 0
        log "ayna yanıt vermedi, sıradaki deneniyor: $a"
    done
    return 1
}
command -v dpkg-deb >/dev/null || die "dpkg-deb yok: ./build.sh deps"
mkdir -p "$DL" "$IDX"

# Paket dizinleri (günde bir tazelenir)
for s in "$ARACLAR_UBUNTU" "$ARACLAR_UBUNTU-updates" "$ARACLAR_UBUNTU-security"; do
    for c in main universe; do
        f="$IDX/$s-$c.gz"
        if [ ! -s "$f" ] || [ -n "$(find "$f" -mtime +1 2>/dev/null)" ]; then
            log "Paket dizini: $s/$c"
            indir "$f.yeni" "dists/$s/$c/binary-amd64/Packages.gz" && mv "$f.yeni" "$f" \
                || { [ -s "$f" ] || die "Ubuntu paket dizini indirilemedi: $s/$c"; }
        fi
    done
done

# Bağımlılık kapanışı. Atlananlar: kurulum betiklerine gerekenler (dpkg, debconf, perl),
# AxsOS'ta zaten olanlar (bash, coreutils yerine BusyBox, ncurses terminfo) ve python (vim'in
# python desteği yalnızca :python için, kitaplığı yeter).
ATLA="debconf perl-base perl dpkg base-files tzdata sensible-utils init-system-helpers usrmerge
libcap2-bin python3 python3.12 python3-minimal python3.12-minimal libpython3.12-stdlib
libpython3.12-minimal media-types netbase lsb-release adduser passwd login readline-common mawk
ncurses-base ncurses-bin coreutils debianutils dash bash xxd"
LISTE="$W/paketler.txt"
python3 "$ROOT/scripts/deb-coz.py" "$IDX" "$(echo $ATLA)" $ARACLAR_PAKETLER > "$LISTE.yeni" \
    || die "bağımlılıklar çözülemedi"
mv "$LISTE.yeni" "$LISTE"

ozet="$(sha256sum "$LISTE" | cut -c1-16)"
if [ -f "$R/.tamam-$ozet" ]; then
    log "Araçlar güncel ($(wc -l < "$LISTE") paket)"
else
    log "Araçlar indiriliyor ($(wc -l < "$LISTE") Ubuntu paketi)..."
    rm -rf "$R" && mkdir -p "$R"
    while read -r ad surum yol sha boyut; do
        d="$DL/$(basename "$yol")"
        if [ ! -s "$d" ] || [ "$(sha256sum "$d" | cut -d' ' -f1)" != "$sha" ]; then
            indir "$d.part" "$yol" || die "indirilemedi: $ad"
            [ "$(sha256sum "$d.part" | cut -d' ' -f1)" = "$sha" ] || die "sha256 tutmuyor: $ad"
            mv "$d.part" "$d"
        fi
        dpkg-deb -x "$d" "$R" || die "açılamadı: $ad"
    done < "$LISTE"

    log "Gereksiz dosyalar temizleniyor..."
    rm -rf "$R"/usr/share/{doc,man,info,locale,lintian,bug,bash-completion,zsh,gdb,gcc,menu,pixmaps,applications} \
        "$R"/etc/{cron.daily,logrotate.d,init.d,apparmor.d} "$R"/usr/lib/systemd "$R"/lib/systemd
    G="$R/usr/libexec/gcc/x86_64-linux-gnu"
    # LTO (-flto) derleyicisi ve eklenti başlıkları (~70 MB): gündelik C derlemesi için gerekmez
    rm -f "$G"/*/lto1 "$G"/*/lto-dump "$R"/usr/bin/*lto-dump*
    rm -rf "$R"/usr/lib/gcc/x86_64-linux-gnu/*/plugin
    # -fsanitize kitaplıkları (~30 MB)
    rm -f "$R"/usr/lib/x86_64-linux-gnu/lib{asan,tsan,hwasan,lsan,ubsan}.so* \
        "$R"/usr/lib/gcc/x86_64-linux-gnu/*/lib{asan,tsan,hwasan,lsan,ubsan}*
    # gold bağlayıcısı ve gprofng (ld.bfd yeter)
    rm -f "$R"/usr/bin/*gold* "$R"/usr/bin/*gprofng* "$R"/usr/lib/x86_64-linux-gnu/libgprofng*
    # vim: belgeler, dersler, çeviriler, yazım sözlükleri
    for v in "$R"/usr/share/vim/vim*; do
        [ -d "$v" ] && rm -rf "$v"/{doc,tutor,lang,spell,print,keymap}
    done

    # update-alternatives yerine bağlantılar (kurulum betikleri çalıştırılmaz)
    ln -sf vim.basic "$R/usr/bin/vim"
    ln -sf vim.basic "$R/usr/bin/vi"
    ln -sf vim.basic "$R/usr/bin/view"
    ln -sf gcc "$R/usr/bin/cc"
    # vim ayarı: renkli sözdizimi, fare, Türkçe karakterler
    mkdir -p "$R/etc/vim"
    cat > "$R/etc/vim/vimrc.local" <<'EOV'
syntax on
set encoding=utf-8 mouse=a number ruler showcmd incsearch hlsearch background=dark
filetype plugin indent on
EOV
    # glibc'nin kitaplık önbelleği
    mkdir -p "$R/etc/ld.so.conf.d"
    printf 'include /etc/ld.so.conf.d/*.conf\n' > "$R/etc/ld.so.conf"
    printf '/lib/x86_64-linux-gnu\n/usr/lib/x86_64-linux-gnu\n/usr/local/lib\n' > "$R/etc/ld.so.conf.d/x86_64-linux-gnu.conf"
    ldconfig -r "$R" 2>/dev/null || die "ldconfig başarısız"
    touch "$R/.tamam-$ozet"
fi

# sysroot'a ekle: AxsOS'un kendi dosyalarının üzerine yazılmaz (bash, curl, nano, python...)
log "Sisteme ekleniyor..."
( cd "$R" && find . -mindepth 1 ! -name '.tamam-*' \( -type f -o -type l \) -print0 ) |
while IFS= read -r -d '' f; do
    hedef="$SYS/${f#./}"
    # usrmerge: /bin, /sbin, /lib Ubuntu'da /usr altına bağlantıdır
    [ -e "$hedef" ] || [ -L "$hedef" ] && continue
    mkdir -p "$(dirname "$hedef")"
    cp -a "$R/${f#./}" "$hedef"
done
# yürütülebilir glibc programlarının yükleyicisi: /lib64/ld-linux-x86-64.so.2
mkdir -p "$SYS/usr/lib64"
[ -e "$SYS/lib64" ] || ln -s usr/lib64 "$SYS/lib64"
[ -e "$SYS/lib" ] || ln -s usr/lib "$SYS/lib"
for t in gcc make ld as ar vim htop ip ping tar gzip bzip2; do
    [ -x "$SYS/usr/bin/$t" ] || [ -x "$SYS/bin/$t" ] || [ -x "$SYS/usr/sbin/$t" ] || die "eksik: $t"
done
log "Hazır: $(ls "$R"/usr/bin/gcc-[0-9]* | head -1 | sed 's#.*/##'), make, binutils, vim, htop, iproute2, ping, tar, gzip, bzip2 ($(du -sh "$R" | cut -f1))"
