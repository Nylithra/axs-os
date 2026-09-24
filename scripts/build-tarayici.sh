#!/usr/bin/env bash
# Tarayıcı paketleri (uzak depo için): tarayici-ortami, firefox, google-chrome.
#
# Firefox ve Chrome glibc + GTK + X11 ister; AxsOS ise musl ile statik ve X'siz. Bu yüzden
# tarayıcılar /opt/tarayici altındaki küçük bir Ubuntu kök dosya sisteminde (chroot) çalışır:
#   - Xvfb (bellekteki X sunucusu) ve matchbox (tek pencere, tam ekran) chroot içinde,
#   - xkopru (AxsDE uygulaması) Xvfb'nin ekran belleğini bir AxsDE penceresinde gösterir,
#     klavye/fareyi XTEST ile X'e iletir.
# firefox / google-chrome paketleri tarayıcının kendisini kurulum sırasında resmi
# kaynaklardan indirir (Mozilla / Google); depoda yalnızca kurulum betikleri bulunur.
#
# Çıktı: build/uzak-depo/*.axp + INDEX + icon-*.png  (CI "paketler" sürümüne yükler)
# debootstrap ve root yetkisi gerekir (CI'da sudo ile çalışır).
. "$(dirname "$0")/common.sh"
[ "$(id -u)" = 0 ] || die "root gerekli: sudo ./build.sh tarayici"
command -v debootstrap >/dev/null || die "debootstrap yok: sudo apt-get install debootstrap"
T="$BUILD/tarayici"
R="$T/rootfs"
OUTR="$BUILD/uzak-depo"
MIRROR="${UBUNTU_MIRROR:-http://archive.ubuntu.com/ubuntu}"
mkdir -p "$T" "$OUTR"

PKGS=(
    # X sunucusu, pencere yöneticisi, klavye düzeni
    xvfb matchbox-window-manager x11-xkb-utils xkb-data xauth
    # yazı tipleri
    fontconfig fonts-dejavu-core fonts-liberation
    # Firefox ve Chrome'un ortak kütüphaneleri
    libgtk-3-0t64 libdbus-glib-1-2 libasound2t64 libx11-xcb1 libxtst6 libpci3 libnss3 libnspr4
    libgbm1 libatk-bridge2.0-0t64 libcups2t64 libxkbcommon0 libxcomposite1 libxdamage1
    libxrandr2 libxss1 libvulkan1 libdrm2 libu2f-udev libcurl4t64 libpango-1.0-0 libcairo2
    xdg-utils ca-certificates
    # kurulum sırasında tarayıcı arşivlerini açmak için
    xz-utils bzip2
)

if [ ! -f "$R/.tamam-$TARAYICI_UBUNTU-$TARAYICI_ORTAM_VER" ]; then
    # yarım kalmış chroot'taki bağlamaları çöz
    for m in proc sys dev/pts dev; do umount -l "$R/$m" 2>/dev/null || true; done
    rm -rf "$R"
    log "Ubuntu $TARAYICI_UBUNTU temel sistemi kuruluyor (debootstrap, birkaç dakika)..."
    debootstrap --variant=minbase --components=main,universe "$TARAYICI_UBUNTU" "$R" "$MIRROR" \
        > "$T/debootstrap.log" 2>&1 || { tail -20 "$T/debootstrap.log"; die "debootstrap başarısız"; }
    cat > "$R/etc/apt/sources.list" <<EOS
deb $MIRROR $TARAYICI_UBUNTU main universe
deb $MIRROR $TARAYICI_UBUNTU-updates main universe
deb $MIRROR $TARAYICI_UBUNTU-security main universe
EOS
    mount -t proc proc "$R/proc"
    mount --bind /dev "$R/dev"
    trap 'umount -l "$R/proc" "$R/dev" 2>/dev/null || true' EXIT
    log "Tarayıcı kütüphaneleri kuruluyor (${#PKGS[@]} paket + bağımlılıklar)..."
    chroot "$R" /usr/bin/env DEBIAN_FRONTEND=noninteractive LC_ALL=C sh -c \
        "apt-get update -q && apt-get install -y -q --no-install-recommends ${PKGS[*]}" \
        > "$T/apt.log" 2>&1 || { tail -30 "$T/apt.log"; die "apt-get başarısız"; }
    # tarayıcıları root olmayan kullanıcıyla çalıştır (Chrome/Firefox korumalı alanı için)
    chroot "$R" useradd -m -u 1000 -s /bin/bash axs >/dev/null 2>&1 || true
    chroot "$R" fc-cache -f >/dev/null 2>&1 || true
    chroot "$R" sh -c 'dbus-uuidgen > /etc/machine-id 2>/dev/null || cat /proc/sys/kernel/random/uuid | tr -d - > /etc/machine-id'
    umount -l "$R/proc" "$R/dev"
    trap - EXIT
    log "Gereksiz dosyalar temizleniyor..."
    rm -rf "$R"/usr/share/{doc,man,info,lintian,linda,bug,groff} "$R"/var/cache/apt/* \
        "$R"/var/lib/apt/lists/* "$R"/var/log/* "$R"/usr/share/locale/*
    find "$R/usr/share/i18n" -mindepth 1 -maxdepth 1 ! -name charmaps -exec rm -rf {} + 2>/dev/null || true
    # Mesa'nın yazılım OpenGL/Vulkan sürücüleri (LLVM ~180 MB): tarayıcılar yazılımla çizer
    # (Firefox WebRender yazılım kipi, Chrome SwiftShader); Xvfb GLX'siz çalışır.
    rm -rf "$R"/usr/lib/x86_64-linux-gnu/libLLVM* "$R"/usr/lib/x86_64-linux-gnu/libgallium* \
        "$R"/usr/lib/x86_64-linux-gnu/dri "$R"/usr/lib/x86_64-linux-gnu/libvulkan_* \
        "$R"/usr/share/vulkan/icd.d
    # Ubuntu'ya özgü simge temaları, udev donanım veritabanı, systemd hizmetleri (chroot'ta çalışmaz)
    rm -rf "$R"/usr/share/icons/{Humanity,Humanity-Dark,ubuntu-mono-dark,ubuntu-mono-light,LoginIcons} \
        "$R"/usr/lib/udev/hwdb.bin "$R"/etc/udev/hwdb.bin "$R"/usr/lib/systemd \
        "$R"/usr/share/perl* "$R"/usr/share/zoneinfo/right "$R"/usr/share/zoneinfo/posix
    rm -f "$R/etc/resolv.conf" && touch "$R/etc/resolv.conf"
    mkdir -p "$R/opt" "$R/home/axs/İndirilenler"
    touch "$R/.tamam-$TARAYICI_UBUNTU-$TARAYICI_ORTAM_VER"
fi
log "Kök dosya sistemi: $(du -sh "$R" | cut -f1)"

# --- paket dizinleri ---
[ -x "$BUILD/apps/xkopru" ] || die "xkopru yok, önce: ./build.sh apps"
P="$T/paketler"
rm -rf "$P" && mkdir -p "$P"

# tarayici-ortami
O="$P/tarayici-ortami"
mkdir -p "$O/files/opt" "$O/files/usr/bin" "$O/files/usr/lib/tarayici"
cp -a "$R" "$O/files/opt/tarayici"
rm -f "$O/files/opt/tarayici/.tamam-"*
install -m755 "$BUILD/apps/xkopru" "$O/files/usr/bin/xkopru"
install -m755 "$ROOT/tarayici/tarayici-baslat" "$O/files/usr/lib/tarayici/tarayici-baslat"
cat > "$O/MANIFEST" <<EOM
name=tarayici-ortami
title=Tarayıcı Çalışma Ortamı
version=$TARAYICI_ORTAM_VER
category=Sistem
description=Firefox ve Chrome için kütüphaneler ve X sunucusu (Ubuntu $TARAYICI_UBUNTU tabanlı)
about=Firefox ve Google Chrome, glibc/GTK/X11 kütüphanelerine ihtiyaç duyar. Bu paket, /opt/tarayici altında bu kütüphaneleri içeren küçük bir Ubuntu $TARAYICI_UBUNTU kök dosya sistemi, bellekte çalışan bir X sunucusu (Xvfb) ve tarayıcı penceresini AxsDE masaüstünde gösteren xkopru köprüsünü kurar. Tarayıcı paketleri bunu kendiliğinden kurar.
EOM
cat > "$O/pre-remove" <<'EOM'
#!/bin/sh
# chroot bağlamalarını çöz (tarayıcı çalışıyorsa kapat)
/usr/lib/tarayici/tarayici-baslat --kapat-hepsi 2>/dev/null
exit 0
EOM

# firefox / google-chrome kurulum paketleri
mkdir -p "$P/firefox" "$P/google-chrome"
cp -a "$ROOT/tarayici/firefox/." "$P/firefox/"
cp -a "$ROOT/tarayici/google-chrome/." "$P/google-chrome/"
for n in firefox google-chrome; do
    install -D -m644 "$BUILD/icons/$n.png" "$P/$n/files/usr/share/axsde/icons/$n.png"
done

log "Paketler oluşturuluyor (sıkıştırma birkaç dakika sürebilir)..."
rm -f "$OUTR"/*.axp "$OUTR"/INDEX "$OUTR"/icon-*.png
AXPKG="$BUILD/sysroot/usr/bin/axpkg"
[ -x "$AXPKG" ] || die "axpkg yok, önce: ./build.sh axpkg"
for d in "$P"/*/; do
    ( cd "$OUTR" && "$AXPKG" create "$d" >/dev/null ) || die "paket oluşturulamadı: $d"
done
FLAT=1 "$ROOT/scripts/repo-index.sh" "$OUTR" "$BUILD/icons" >/dev/null
log "Hazır: build/uzak-depo ($(ls "$OUTR" | wc -l) dosya, $(du -sh "$OUTR" | cut -f1))"
ls -la "$OUTR"
