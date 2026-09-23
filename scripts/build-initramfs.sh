#!/usr/bin/env bash
# initramfs'i oluştur: BusyBox + AxsOS programları + rootfs/ şablonu
# -> build/out/initramfs.cpio.gz
. "$(dirname "$0")/common.sh"

KSRC="$BUILD/linux-$KERNEL_VERSION"
GEN="$KSRC/usr/gen_init_cpio"
STAGE="$BUILD/rootfs"
[ -x "$GEN" ] || die "gen_init_cpio yok, önce: ./build.sh kernel"
[ -d "$BUILD/busybox-install" ] || die "BusyBox yok, önce: ./build.sh busybox"

log "Kök dosya sistemi hazırlanıyor..."
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$BUILD/busybox-install/." "$STAGE/"
rm -f "$STAGE/linuxrc"
mkdir -p "$STAGE"/{dev,proc,sys,tmp,run,root,home,etc,var/log,usr/lib,mnt}
# Diğer aşamaların ürettiği dosyalar (init, axsh, axs, axpkg...)
[ -d "$BUILD/sysroot" ] && cp -a --remove-destination "$BUILD/sysroot/." "$STAGE/"
# Depodaki sabit dosyalar en son (üzerine yazar)
cp -a --remove-destination "$ROOT/rootfs/." "$STAGE/"
# Hazır kurulu market uygulamaları: axpkg sahte kökle (AXPKG_ROOT) kurar, veritabanı da güncellenir
PRE="$STAGE/var/lib/axpkg/repo/PREINSTALL"
if [ -f "$PRE" ]; then
    for p in $(cat "$PRE"); do
        AXPKG_ROOT="$STAGE" "$STAGE/usr/bin/axpkg" install "$p" >/dev/null || die "hazır kurulamadı: $p"
    done
    log "Hazır kurulu uygulamalar: $(cat "$PRE")"
fi

# gen_init_cpio listesi: sahiplik her zaman root, aygıt düğümleri root gerektirmez.
LIST="$BUILD/initramfs.list"
{
    echo "nod /dev/console 0600 0 0 c 5 1"
    echo "nod /dev/null 0666 0 0 c 1 3"
    cd "$STAGE"
    find . -mindepth 1 | LC_ALL=C sort | while IFS= read -r p; do
        path="${p#.}"
        mode=$(stat -c '%a' "$p")
        if [ -L "$p" ]; then
            echo "slink $path $(readlink "$p") $mode 0 0"
        elif [ -d "$p" ]; then
            echo "dir $path $mode 0 0"
        elif [ -f "$p" ]; then
            echo "file $path $STAGE$path $mode 0 0"
        fi
    done
} > "$LIST"
# /tmp herkes yazabilsin
sed -i 's#^dir /tmp .*#dir /tmp 1777 0 0#' "$LIST"

"$GEN" "$LIST" | gzip -9 > "$OUT/initramfs.cpio.gz"
log "Hazır: build/out/initramfs.cpio.gz ($(du -h "$OUT/initramfs.cpio.gz" | cut -f1))"
