#!/usr/bin/env bash
# Aşama 2: BusyBox'ı statik derle (musl-gcc) ve build/busybox-install'a kur.
. "$(dirname "$0")/common.sh"

BSRC="$BUILD/busybox-$BUSYBOX_VERSION"
TARBALL="$DL/busybox-$BUSYBOX_VERSION.tar.bz2"
FRAG="$ROOT/config/busybox.fragment"

if [ ! -d "$BSRC" ]; then
    if [ ! -f "$TARBALL" ] && ! curl -fL --retry 3 -o "$TARBALL.part" \
            "https://busybox.net/downloads/busybox-$BUSYBOX_VERSION.tar.bz2"; then
        rm -f "$TARBALL.part"
        log "busybox.net erişilemedi, GitHub aynasından klonlanıyor..."
        git -c advice.detachedHead=false clone -q --depth 1 --branch "${BUSYBOX_VERSION//./_}" \
            https://github.com/mirror/busybox.git "$BSRC"
        rm -rf "$BSRC/.git"
    else
        [ -f "$TARBALL.part" ] && mv "$TARBALL.part" "$TARBALL"
        tar -C "$BUILD" -xf "$TARBALL"
    fi
fi

if [ ! -f "$BSRC/.config" ] || [ "$FRAG" -nt "$BSRC/.config" ]; then
    log "BusyBox yapılandırılıyor..."
    make -C "$BSRC" defconfig >/dev/null
    # Fragment'teki her seçeneği uygula (hem "=y" hem "is not set").
    while IFS= read -r line; do
        case "$line" in
            CONFIG_*=*) opt="${line%%=*}" ;;
            "# CONFIG_"*" is not set") opt="${line#\# }"; opt="${opt%% *}" ;;
            *) continue ;;
        esac
        sed -i "/^$opt=/d; /^# $opt is not set/d" "$BSRC/.config"
        echo "$line" >> "$BSRC/.config"
    done < "$FRAG"
    { yes "" || true; } | make -C "$BSRC" oldconfig >/dev/null
fi

CC="$(axs_cc)"
log "BusyBox derleniyor ($JOBS iş parçacığı)..."
make -C "$BSRC" CC="$CC" -j"$JOBS" busybox >/dev/null
file "$BSRC/busybox" | grep -q "statically linked" || die "busybox statik değil!"
rm -rf "$BUILD/busybox-install"
make -C "$BSRC" CC="$CC" CONFIG_PREFIX="$BUILD/busybox-install" install >/dev/null
log "Hazır: BusyBox $BUSYBOX_VERSION ($(du -h "$BSRC/busybox" | cut -f1), statik)"
