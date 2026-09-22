#!/usr/bin/env bash
# Aşama 5 (1/2): Axs'in çalışma zamanı olan CPython'u musl ile tamamen statik derle.
# zlib + OpenSSL de statik derlenip içine gömülür. Çıktı: build/python-install
. "$(dirname "$0")/common.sh"
CC="$(axs_cc)"
DEPS="$BUILD/deps"          # statik zlib/openssl (yalnızca derleme için)
PYI="$BUILD/python-install" # /usr altına kurulacak python
PYSRC="$BUILD/cpython-$PYTHON_VERSION"
mkdir -p "$DEPS"

# git_src <dizin> <repo> <etiket>
git_src() {
    [ -d "$1" ] && return
    log "$(basename "$1") indiriliyor..."
    rm -rf "$1.part"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$3" "$2" "$1.part"
    mv "$1.part" "$1"
}

# --- zlib ---
if [ ! -f "$DEPS/lib/libz.a" ]; then
    git_src "$BUILD/zlib-$ZLIB_VERSION" https://github.com/madler/zlib "v$ZLIB_VERSION"
    log "zlib derleniyor..."
    ( cd "$BUILD/zlib-$ZLIB_VERSION" && CC="$CC" CFLAGS="-O2 -fPIC" ./configure --static --prefix="$DEPS" >/dev/null \
      && make -j"$JOBS" >/dev/null && make install >/dev/null )
fi

# --- OpenSSL ---
if [ ! -f "$DEPS/lib/libssl.a" ]; then
    git_src "$BUILD/openssl-$OPENSSL_VERSION" https://github.com/openssl/openssl "openssl-$OPENSSL_VERSION"
    log "OpenSSL derleniyor (birkaç dakika)..."
    ( cd "$BUILD/openssl-$OPENSSL_VERSION" \
      && CC="$CC" ./Configure linux-x86_64 no-shared no-module no-tests no-docs no-apps \
            no-async threads --prefix="$DEPS" --libdir=lib --openssldir=/etc/ssl >/dev/null \
      && make -j"$JOBS" build_sw >/dev/null && make install_sw >/dev/null 2>&1 )
fi

# --- SQLite ---
if [ ! -f "$DEPS/lib/libsqlite3.a" ]; then
    git_src "$BUILD/sqlite-$SQLITE_VERSION" https://github.com/sqlite/sqlite "version-$SQLITE_VERSION"
    log "SQLite derleniyor..."
    ( cd "$BUILD/sqlite-$SQLITE_VERSION" && CC="$CC" CFLAGS="-O2 -fPIC" ./configure --disable-shared \
            --disable-tcl --disable-readline --prefix="$DEPS" >/dev/null \
      && make -j"$JOBS" libsqlite3.a sqlite3.h >/dev/null \
      && install -D -m644 libsqlite3.a "$DEPS/lib/libsqlite3.a" \
      && install -D -m644 sqlite3.h "$DEPS/include/sqlite3.h" ) || die "SQLite derlenemedi"
fi

# --- CPython ---
git_src "$PYSRC" https://github.com/python/cpython "v$PYTHON_VERSION"
if [ ! -f "$PYSRC/python" ]; then
    log "CPython $PYTHON_VERSION yapılandırılıyor..."
    cd "$PYSRC"
    # Tüm C modüllerini python ikilisinin içine göm (statik ikili .so yükleyemez).
    MODULE_BUILDTYPE=static CC="$CC" \
    CPPFLAGS="-I$DEPS/include" LDFLAGS="-static -L$DEPS/lib" \
    ZLIB_CFLAGS="-I$DEPS/include" ZLIB_LIBS="-L$DEPS/lib -lz" \
    LIBSQLITE3_CFLAGS="-I$DEPS/include" LIBSQLITE3_LIBS="-L$DEPS/lib -lsqlite3 -lm" \
    ./configure --prefix=/usr --disable-shared --disable-test-modules \
        --without-ensurepip --without-system-libmpdec --without-system-expat \
        --with-openssl="$DEPS" --with-openssl-rpath=no \
        py_cv_module__ctypes=n/a py_cv_module__tkinter=n/a \
        py_cv_module__curses=n/a py_cv_module__curses_panel=n/a py_cv_module_readline=n/a \
        py_cv_module__dbm=n/a py_cv_module__gdbm=n/a py_cv_module__bz2=n/a py_cv_module__lzma=n/a \
        py_cv_module__uuid=n/a py_cv_module_nis=n/a \
        > "$BUILD/python-configure.log" 2>&1 || die "configure başarısız, bkz. build/python-configure.log"
    log "CPython derleniyor (birkaç dakika)..."
    make -j"$JOBS" > "$BUILD/python-make.log" 2>&1 || die "make başarısız, bkz. build/python-make.log"
    cd "$ROOT"
fi
file "$PYSRC/python" | grep -q "statically linked" || die "python statik değil!"

log "CPython kuruluyor..."
rm -rf "$PYI"
make -C "$PYSRC" DESTDIR="$PYI" install >/dev/null 2>&1 || die "make install başarısız"
LIB="$PYI/usr/lib/python${PYTHON_VERSION%.*}"
# Gömülü sistem için gereksiz parçaları at
rm -rf "$LIB"/{test,idlelib,tkinter,turtledemo,ensurepip,lib2to3,pydoc_data,venv} \
       "$LIB"/config-* "$PYI/usr/include" "$PYI/usr/share" "$PYI/usr/lib/pkgconfig" \
       "$PYI/usr/bin/idle"* "$PYI/usr/lib/libpython"*.a "$PYI/usr/bin/pydoc"* "$PYI/usr/bin/"*-config
find "$LIB" -name __pycache__ -prune -exec rm -rf {} +
find "$LIB" -name 'tests' -type d -prune -exec rm -rf {} +
strip "$PYI/usr/bin/python${PYTHON_VERSION%.*}"
# Önceden derlenmiş bayt kodu (açılışı hızlandırır)
"$PYSRC/python" -m compileall -q -j"$JOBS" -d "/usr/lib/python${PYTHON_VERSION%.*}" "$LIB" >/dev/null || true
log "Hazır: python ($(du -sh "$PYI" | cut -f1))"
