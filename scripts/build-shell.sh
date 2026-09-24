#!/usr/bin/env bash
# Kabuk ve temel araçlar: GNU bash (varsayılan kabuk), ncurses + nano, curl (OpenSSL ile HTTPS)
# ve CA sertifikaları. Hepsi musl ile statik. OpenSSL/zlib python aşamasında derlenir (build/deps).
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
SYS="$BUILD/sysroot"
DEPS="$BUILD/deps"
SH="$BUILD/shell"
[ -f "$DEPS/lib/libssl.a" ] || die "OpenSSL yok, önce: ./build.sh python"
mkdir -p "$SH" "$SYS/bin" "$SYS/usr/bin"

# fetch <hedef-dizin> <url>... : ilk erişilebilen adresten arşivi indirip aç
# (önce resmi kaynak, sonra Ubuntu arşivindeki aynı sürümün özgün "orig" arşivi)
fetch() {
    local dest="$1"; shift
    [ -d "$dest" ] && return
    local f="$DL/$(basename "$dest").tar"
    if [ ! -s "$f" ]; then
        local ok=0
        for u in "$@"; do
            log "İndiriliyor: $u"
            if curl -fsSL --retry 2 --max-time 300 -o "$f.part" "$u"; then ok=1; break; fi
        done
        [ "$ok" = 1 ] || die "indirilemedi: $(basename "$dest")"
        mv "$f.part" "$f"
    fi
    rm -rf "$dest.part" && mkdir -p "$dest.part"
    tar -C "$dest.part" --strip-components=1 -xf "$f"
    mv "$dest.part" "$dest"
}
UBU=http://archive.ubuntu.com/ubuntu/pool/main

# --- ncurses (statik, geniş karakter) ---
NC="$SH/ncurses-install"
if [ ! -f "$NC/lib/libncursesw.a" ]; then
    fetch "$SH/ncurses-$NCURSES_VER" "https://ftp.gnu.org/gnu/ncurses/ncurses-$NCURSES_VER.tar.gz" \
        "$UBU/n/ncurses/ncurses_$NCURSES_VER+20250216.orig.tar.gz"
    log "ncurses derleniyor..."
    ( cd "$SH/ncurses-$NCURSES_VER" && CC="$CC" CFLAGS="-O2" ./configure --prefix=/usr \
        --without-shared --without-debug --without-ada --without-cxx --without-cxx-binding \
        --without-manpages --without-progs --without-tests --enable-widec --disable-db-install \
        --with-terminfo-dirs=/usr/share/terminfo --with-default-terminfo-dir=/usr/share/terminfo >/dev/null \
      && make -j"$JOBS" libs >/dev/null && make DESTDIR="$SH/nc-destdir" install.libs install.includes >/dev/null ) \
        || die "ncurses derlenemedi"
    rm -rf "$NC" && mv "$SH/nc-destdir/usr" "$NC"
fi
# terminfo: yalnızca kullanılan terminaller (host'taki tic ile derlenir)
TI="$SYS/usr/share/terminfo"
if [ ! -f "$TI/x/xterm-256color" ]; then
    mkdir -p "$TI"
    tic -x -o "$TI" -e xterm-256color,xterm,xterm-color,linux,vt100,vt220,screen,screen-256color,tmux,tmux-256color,dumb \
        "$SH/ncurses-$NCURSES_VER/misc/terminfo.src" 2>/dev/null || die "terminfo derlenemedi"
fi

# --- nano ---
if [ ! -x "$SH/nano" ]; then
    fetch "$SH/nano-$NANO_VER" "https://ftp.gnu.org/gnu/nano/nano-$NANO_VER.tar.xz" \
        "$UBU/n/nano/nano_$NANO_VER.orig.tar.xz"
    log "nano derleniyor..."
    ( cd "$SH/nano-$NANO_VER" && CC="$CC" CFLAGS="-O2" CPPFLAGS="-I$NC/include -I$NC/include/ncursesw" \
        LDFLAGS="-static -L$NC/lib" NCURSESW_CFLAGS="-I$NC/include/ncursesw" NCURSESW_LIBS="-lncursesw" \
        ./configure --prefix=/usr --sysconfdir=/etc --disable-nls --disable-libmagic --enable-utf8 >/dev/null \
      && make -j"$JOBS" >/dev/null && cp src/nano "$SH/nano" ) || die "nano derlenemedi"
fi

# --- bash ---
if [ ! -x "$SH/bash" ]; then
    fetch "$SH/bash-$BASH_VER" "https://ftp.gnu.org/gnu/bash/bash-$BASH_VER.tar.gz" \
        "$UBU/b/bash/bash_$BASH_VER.orig.tar.xz"
    log "bash derleniyor..."
    ( cd "$SH/bash-$BASH_VER" && CC="$CC" CFLAGS="-O2" CPPFLAGS="-I$NC/include" LDFLAGS="-L$NC/lib" \
        ./configure --prefix=/usr --bindir=/bin --enable-static-link --without-bash-malloc --disable-nls \
        --with-curses --enable-readline --enable-history --enable-job-control >/dev/null \
      && make -j"$JOBS" TERMCAP_LIB=-lncursesw >/dev/null && cp bash "$SH/bash" ) || die "bash derlenemedi"
fi

# --- curl (OpenSSL + zlib, statik) ---
if [ ! -x "$SH/curl" ]; then
    fetch "$SH/curl-$CURL_VER" "https://curl.se/download/curl-$CURL_VER.tar.gz" \
        "$UBU/c/curl/curl_$CURL_VER.orig.tar.gz"
    log "curl derleniyor..."
    ( cd "$SH/curl-$CURL_VER" && CC="$CC" CFLAGS="-O2" CPPFLAGS="-I$DEPS/include" \
        LDFLAGS="-static -L$DEPS/lib" LIBS="-lssl -lcrypto -lz -lpthread" PKG_CONFIG=false \
        ./configure --prefix=/usr --disable-shared --enable-static --with-openssl="$DEPS" --with-zlib="$DEPS" \
        --without-libpsl --without-brotli --without-zstd --without-libidn2 --without-nghttp2 \
        --without-libssh2 --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet \
        --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
        --disable-manual --disable-docs --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
        --with-ca-path=/etc/ssl/certs >/dev/null \
      && make -j"$JOBS" LDFLAGS="-all-static -L$DEPS/lib" >/dev/null && cp src/curl "$SH/curl" ) \
        || die "curl derlenemedi"
fi

log "Kuruluyor..."
install -m755 "$SH/bash" "$SYS/bin/bash"
install -m755 "$SH/nano" "$SYS/usr/bin/nano"
install -m755 "$SH/curl" "$SYS/usr/bin/curl"
strip "$SYS/bin/bash" "$SYS/usr/bin/nano" "$SYS/usr/bin/curl"
# CA sertifikaları (HTTPS için; derleme makinesinin Mozilla kök sertifika listesi)
CA=/etc/ssl/certs/ca-certificates.crt
[ -f "$CA" ] || die "$CA yok (ca-certificates paketini kurun)"
mkdir -p "$SYS/etc/ssl/certs"
cp "$CA" "$SYS/etc/ssl/certs/ca-certificates.crt"
ln -sf certs/ca-certificates.crt "$SYS/etc/ssl/cert.pem"
log "Hazır: bash $BASH_VER, nano $NANO_VER, curl $CURL_VER (+ $(grep -c 'BEGIN CERT' "$CA") kök sertifika)"
