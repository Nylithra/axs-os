#!/usr/bin/env bash
# Stage 1: download and build the Linux kernel (x86_64, minimal config).
. "$(dirname "$0")/common.sh"

KSRC="$BUILD/linux-$KERNEL_VERSION"
KMAJOR="${KERNEL_VERSION%%.*}"
TARBALL="$DL/linux-$KERNEL_VERSION.tar.xz"

fetch_kernel() {
    [ -d "$KSRC" ] && return
    if [ ! -f "$TARBALL" ]; then
        log "Kernel $KERNEL_VERSION indiriliyor (kernel.org)..."
        if ! curl -fL --retry 3 -o "$TARBALL.part" \
            "https://cdn.kernel.org/pub/linux/kernel/v$KMAJOR.x/linux-$KERNEL_VERSION.tar.xz"; then
            rm -f "$TARBALL.part"
            log "kernel.org erişilemedi, GitHub aynasından (gregkh/linux) klonlanıyor..."
            rm -rf "$KSRC.part"
            git -c advice.detachedHead=false clone -q --depth 1 --branch "v$KERNEL_VERSION" \
                https://github.com/gregkh/linux.git "$KSRC.part"
            rm -rf "$KSRC.part/.git"
            mv "$KSRC.part" "$KSRC"
            return
        fi
        mv "$TARBALL.part" "$TARBALL"
    fi
    log "Arşiv açılıyor..."
    # Yarıda kalan açma işlemi bozuk kaynak ağacı bırakmasın
    rm -rf "$KSRC.part" && mkdir -p "$KSRC.part"
    tar -C "$KSRC.part" --strip-components=1 -xf "$TARBALL"
    mv "$KSRC.part" "$KSRC"
}

configure_kernel() {
    local frag="$ROOT/config/kernel.fragment"
    # Yalnızca fragment değiştiyse ya da önceki yapılandırma yarıda kaldıysa yeniden yapılandır.
    local stamp="$KSRC/.axsos-configured"
    if [ -f "$stamp" ] && [ "$stamp" -nt "$frag" ]; then
        return
    fi
    rm -f "$stamp"
    log "Kernel yapılandırılıyor (tinyconfig + config/kernel.fragment)..."
    make -C "$KSRC" ARCH=x86_64 tinyconfig >/dev/null
    "$KSRC/scripts/kconfig/merge_config.sh" -m -O "$KSRC" "$KSRC/.config" "$frag" >/dev/null
    make -C "$KSRC" ARCH=x86_64 olddefconfig >/dev/null

    # Verify every requested option survived dependency resolution.
    local missing=0
    while IFS= read -r line; do
        case "$line" in CONFIG_*=*) ;; *) continue ;; esac
        grep -qxF "$line" "$KSRC/.config" || { log "UYARI: uygulanamadı: $line"; missing=1; }
    done < "$frag"
    [ "$missing" = 0 ] || log "Bazı seçenekler uygulanamadı (yukarıya bakın)."
    touch "$stamp"
}

fetch_kernel
configure_kernel
log "Kernel derleniyor ($JOBS iş parçacığı)..."
make -C "$KSRC" ARCH=x86_64 -j"$JOBS" bzImage
cp "$KSRC/arch/x86/boot/bzImage" "$OUT/bzImage"

# Kullanıcı alanı programları (musl) için kernel başlıkları
if [ ! -f "$KHDR/.done-$KERNEL_VERSION" ]; then
    log "Kernel başlıkları kuruluyor..."
    rm -rf "$KHDR"
    make -C "$KSRC" ARCH=x86_64 INSTALL_HDR_PATH="$KHDR" headers_install >/dev/null
    touch "$KHDR/.done-$KERNEL_VERSION"
fi
log "Hazır: build/out/bzImage ($(du -h "$OUT/bzImage" | cut -f1))"
