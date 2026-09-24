# AxsOS

Linux çekirdeği tabanlı, kendi minimal Linux dağıtımım. Sistemin ana dili, benim
yazdığım **Axs** programlama dilidir ([Nylithra/axs](https://github.com/Nylithra/axs)).
Kullanıcı alanı; kendi init'im (C), varsayılan kabuk GNU `bash` (kendi kabuğum `axsh` de var),
Axs yorumlayıcısı (`axs`), paket yöneticisi (`axpkg`, internetten paket indirir) ve kendi masaüstü
ortamım **AxsDE** üzerine kuruludur. Gerçek ağ kartı sürücüleriyle internete bağlanır; Firefox ve
Chrome kurulu gelmez, paket yöneticisinden indirilir.
Tek komutla derlenir; QEMU'da ve BIOS/UEFI ile açılan bir ISO olarak çalışır.

## Ortam

- GitHub Codespaces veya Google Cloud Shell (Debian/Ubuntu), **KVM yok** → QEMU TCG.
- Test: `qemu-system-x86_64 -nographic` (seri konsol `ttyS0`).
- Gerekli paketler: **`./build.sh deps`** eksikleri `apt-get` ile kurar.
  `./build.sh` her çalıştığında önce `scripts/deps.sh` ile denetler, eksik varsa
  derlemeye başlamadan durur. Liste `scripts/deps.sh` içinde.
- **Google Cloud Shell**: yalnızca `$HOME` (5 GB) kalıcıdır; apt ile kurulan paketler
  oturum yenilenince silinir → her yeni oturumda önce `./build.sh deps`.
  `build/` ~3 GB tutar; ev dizini dolarsa derleme klasörünü taşıyın:
  `export AXSOS_BUILD=/tmp/axsos-build` (geçicidir, oturumla silinir).

## Komutlar

| Komut | Açıklama |
|---|---|
| `./build.sh` | Her şeyi sırayla derler (indirme dahil). İlk derleme ~8 dk (4 çekirdek), sonrakiler artımlı (~25 sn). |
| `./build.sh <aşama>` | Tek aşama: `kernel busybox init axsh python axs shell axsde apps axpkg initramfs iso` |
| `sudo ./build.sh tarayici` | Uzak depo paketleri: `tarayici-ortami` (Ubuntu kütüphaneleri + Xvfb + xkopru), `firefox`, `google-chrome` → `build/uzak-depo/` (debootstrap + root ister; `./build.sh`'e dahil değil, CI yapar). |
| `./build.sh deps` | Eksik derleme paketlerini kurar (sudo apt-get). |
| `AXSOS_BUILD=/yol ./build.sh` | Derleme klasörünü değiştirir (`run.sh` de aynı değişkeni okur). |
| `./build.sh clean` | `build/` çıktılarını siler, `build/downloads/` korunur. |
| `./run.sh` | Metin kipi: kernel + initramfs, `-nographic`. Çıkış: `poweroff` ya da `Ctrl-a x`. |
| `./run.sh gui` | **Masaüstü (AxsDE).** Ekran varsa QEMU penceresi; yoksa tarayıcı: `http://localhost:6080` (noVNC). Seri konsol terminalde kalır. |
| `./run.sh iso` / `iso-gui` | ISO'yu GRUB ile açar (BIOS), metin / masaüstü. |
| `./run.sh iso-uefi` | ISO'yu GRUB ile açar (UEFI/OVMF). |
| `TIMEOUT=30 ./run.sh` | 30 sn sonra QEMU'yu kapatır. |
| `VERBOSE=1 ./run.sh` | Kernel mesajlarını göster (`quiet` kapalı). |
| `APPEND="..." ./run.sh` / `MEM=4G ./run.sh` | Ek kernel parametresi / bellek (varsayılan metin 512M, masaüstü 2G; tarayıcı için 3-4G). |
| `scripts/qemu-test.sh "cmd1" "cmd2"` | Otomatik test (metin): açar, komutları yazar, çıktıyı gösterir. `RUN_MODE=iso`, `BOOT_WAIT=20`, `CMD_WAIT=3` |
| `scripts/ci-test.sh` | ISO açılış testi: metin kipinde axs/python/axpkg/poweroff ve masaüstü ekran görüntüsü (CI da bunu çalıştırır). |
| `scripts/ci-tarayici-test.sh` | Tarayıcı testi: `build/uzak-depo`'yu sunar, ISO'da Firefox/Chrome'u resmi kaynaklardan kurup `https://example.com` ekran görüntüsü alır (internet gerekir; CI çalıştırır). |
| `axsde --bench` | Görüntüsüz performans ölçümü (sürükleme, yazma, boyutlandırma, tam çizim, başlatıcı; ms/kare). Host'ta: `AXSDE_FONTS=build/fonts`, `AXSDE_BENCH_SHOT=a.ppm`. |
| `scripts/qemu-gui.py wait:32 click:454,753 shot:/tmp/a.png` | Otomatik test (masaüstü): görüntüsüz QEMU + QMP; `click/dbl/drag/move/wheel/key/type/shot/wait`. `--iso` ile ISO'dan. Ekran 1280x800. |

Aşama bağımlılıkları: `busybox/init/axsh/axpkg/axsde/python` → kernel başlıkları (`kernel`);
`axs` → `python`; `shell` → `python` (statik OpenSSL/zlib); `apps` → `axsde` (stb + yazı tipleri);
`axpkg` → `apps` (depoya paketler); `tarayici` → `apps` (xkopru) + `axpkg`;
`initramfs` → hepsi; `iso` → `kernel` + `initramfs`.
Bir market uygulamasını değiştirince: `./build.sh apps && ./build.sh axpkg && ./build.sh initramfs`.
Kaynak değiştirince ilgili aşamayı, sonra `initramfs` (ve `iso`) aşamasını çalıştırın.

## Klasör yapısı

```
build.sh, run.sh          Tek giriş noktaları
config/versions.sh        Sabitlenmiş sürümler (kernel, busybox, zlib, openssl, sqlite, python, axs commit)
config/kernel.fragment    tinyconfig üzerine uygulanan kernel ayarları
config/busybox.fragment   BusyBox defconfig üzerine uygulanan ayarlar
scripts/common.sh         Ortak değişkenler/yardımcılar (ROOT, BUILD, OUT, log, die, axs_cc)
scripts/build-<aşama>.sh  Her aşamanın derleme betiği; build.sh bunları sırayla çağırır
scripts/qemu-test.sh      Otomatik QEMU testi
init/init.c               PID 1 (C)
axsh/axsh.c               Kabuk (C)
axpkg/axpkg.c             Paket yöneticisi (C)
axsde/                    Masaüstü ortamı (C): wm.c pencere yöneticisi/birleştirici, gfx.c çizim,
                          font.c metin, term.c terminal öykünücüsü, input.c klavye/fare,
                          fb.c DRM/fbdev, apps.c uygulama kaydı, ext.c dış uygulama sunucusu (ADP),
                          img.c PNG/JPG (stb_image), app_*.c yerleşik uygulamalar
axsde/adp.h               AxsDE uygulama protokolü (soket mesajları)
axsde/client/             libaxsapp: dış grafik uygulamaların kütüphanesi (axsapp.h)
apps/<ad>/                Market uygulamaları: main.c + app.conf (paket bilgisi)
apps/mkicon.c             Derlemede simge (128px PNG) ve örnek resim üretici (host'ta çalışır)
apps/xkopru/              X uygulamalarını (Firefox/Chrome) AxsDE penceresinde gösteren köprü
tarayici/                 Tarayıcı paketleri: tarayici-baslat (chroot + Xvfb + xkopru), firefox/, google-chrome/
scripts/repo-index.sh     Depo INDEX'i üretir (MANIFEST + sha256 + boyut + komutlar + simgeler)
scripts/ci-tarayici-test.sh, ci-web.py   Tarayıcı uçtan uca testi ve test HTTP sunucusu
rootfs/sbin/ag            Ağ yöneticisi (DHCP, durum, test, sabit adres)
scripts/novnc-server.py   Tarayıcı kipi: noVNC + WebSocket↔VNC köprüsü (tek port)
scripts/qemu-gui.py       Masaüstü test aracı (QMP ile tıklama/yazma/ekran görüntüsü)
pkgs/<ad>/                Terminal paketleri (MANIFEST + files/) → /var/lib/axpkg/repo/*.axp
rootfs/                   initramfs'e olduğu gibi kopyalanan dosyalar (/etc/rc, /etc/axshrc, ...)
iso/grub.cfg              ISO açılış menüsü
build/                    (git'e girmez)
  kernel-headers/         musl programları için kernel başlıkları
  tools/axs-cc            musl-gcc + kernel başlıkları sarmalayıcısı (statik derleme)
  deps/                   statik zlib, OpenSSL, SQLite (yalnızca derleme için)
  python-install/         statik CPython kurulumu
  stb/, fonts/            stb (truetype, image, image_write) ve yazı tipleri (Inter, JetBrains Mono)
  apps/, pkgs/, icons/    apps aşaması: libaxsapp nesneleri, uygulama paket dizinleri, simgeler
  novnc/                  noVNC istemcisi (yalnızca ./run.sh gui tarayıcı kipinde)
  shell/                  bash, ncurses, nano, curl kaynakları ve derlemeleri
  tarayici/rootfs/        tarayıcı çalışma ortamı (Ubuntu minbase + kütüphaneler; ~240 MB)
  uzak-depo/              uzak depo: INDEX, *.axp, icon-*.png (CI "paketler" sürümüne yükler)
  sysroot/                bizim programlarımız + python + axs (initramfs'e eklenir)
  rootfs/                 initramfs'in birleştirilmiş hali
  out/bzImage, out/initramfs.cpio.gz, out/axsos.iso   Çıktılar
```

## Mimari

- **Kernel**: Linux 6.12 LTS, `tinyconfig` + fragment (seri/VGA/EFI-fb konsol, initramfs,
  devtmpfs, ACPI poweroff, klavye, USB klavye/fare). ~2.9 MB. Ağ kartları: e1000 (QEMU,
  VirtualBox, VMware), e1000e/igb/igc (Intel), r8169/8139 (Realtek), pcnet32, vmxnet3, virtio-net;
  IPv4+IPv6. glibc programları için SysV IPC, POSIX mq, inotify, seccomp, ad alanları (tarayıcı
  korumalı alanları). Wi-Fi yok.
- **libc**: tüm kullanıcı alanı **musl** ile **statik** derlenir (`build/tools/axs-cc`).
  Paylaşımlı kütüphane yok; `/lib` yok.
- **initramfs**: kernel'in `gen_init_cpio` aracıyla üretilir (root yetkisi gerekmez).
  Birleştirme sırası: BusyBox → `build/sysroot` → `rootfs/` (sonraki öncekinin üzerine yazar).
  Kök dosya sistemi tmpfs'tir; init sınırı belleğin %90'ına çıkarır (kurulan paketler RAM'de).
- **init** (`/sbin/init`, `/init` → `sbin/init`): proc/sys/dev/devpts/dev/shm/tmp/run bağlar,
  hostname `axsos`, `/etc/rc` (ağ), her etkin konsolda (`/sys/class/tty/console/active`)
  logo + oturum kabuğu (`/bin/bash`, yoksa axsh, yoksa sh) başlatır ve yeniden açar.
- **Ağ** (`/sbin/ag`, `/etc/rc`, `/etc/udhcpc.script`): açılışta tüm gerçek kartlar açılır,
  `udhcpc` arka planda DHCP ile adres/rota/DNS alır ve kirayı yeniler; saat `ntpd` ile
  eşitlenir (HTTPS için). `ag durum|yenile|test|sabit <kart> <ip/önek> <geçit> [dns]`.
  Ayarlar > Ağ: kartlar (sürücü, bağlantı, MAC, IPv4, hız), ağ geçidi, DNS, yeniden bağlan.
- **Kabuk**: GNU bash 5.2 (statik, musl; readline, iş kontrolü `fg/bg/jobs`, tamamlama).
  `/etc/profile` + `/etc/bash.bashrc`: renkli istem (hata kodu), geçmiş, takma adlar
  (`ll`, `kur`, `kaldir`, `guncelle`), `axpkg` tamamlama, bilinmeyen komutta hangi paketin
  kurulacağını söyler (`axpkg provides`). Ek araçlar: `nano` (ncurses), `curl` (OpenSSL,
  `/etc/ssl/certs/ca-certificates.crt`), BusyBox. Terminal `TERM=xterm-256color`.
  `poweroff`/`reboot`/`halt` BusyBox sinyalleri (SIGUSR2/SIGTERM/SIGUSR1) ile çalışır.
- **axsh** (kendi kabuğum, `/bin/axsh`): pipe, `> >> < 2> 2>&1`, `; && || &`, tırnak/kaçış, `$AD ${AD} $? $$ ~`, glob,
  geçmiş (↑/↓, `history`, `!!`, `!n`, `~/.axsh_history`), UTF-8 satır düzenleme, Tab tamamlama.
  Genişletme her komut çalışmadan hemen önce yapılır. İş kontrolü (fg/bg) yok.
  Başlangıç dosyaları: `/etc/axshrc`, `~/.axshrc`.
- **Axs**: Axs Python ile yazılmış bir dil. Bu yüzden CPython 3.13, musl ile tamamen
  statik derlenir (`MODULE_BUILDTYPE=static`, C modülleri ikilinin içinde) ve zlib,
  OpenSSL 3.5 (Axs açılışta `ssl` yükler) ve SQLite (Axs `connect(x.db)`) statik olarak
  içine gömülür. ctypes/tkinter/curses/readline/bz2/lzma yok. Axs kaynağı
  `AXS_REF` commit'ine sabitlenir ve `/usr/lib/axs` altına kurulur; `/usr/bin/axs` bağlantısı
  vardır. Axs'in kendi test paketi (`tests/run_tests.py`) bu Python ile 66/66 geçer.
- **axpkg**: `.axp` = tar.gz (`MANIFEST`: name/version/description/depends, `files/`,
  isteğe bağlı `post-install`/`pre-remove`). Veritabanı `/var/lib/axpkg/db/<ad>/`,
  depo `/var/lib/axpkg/repo/`. Komutlar: `install [-f] <dosya|ad>`, `remove [-f]`, `list`,
  `available`, `info`, `files`, `owner`, `create <dizin>`. Bağımlılıkları depodan çözer,
  dosya çakışmasını ve ters bağımlılığı denetler. `AXPKG_ROOT`/`AXPKG_REPO` ile host'ta
  sahte kökte test edilebilir. **Uzak depo**: `/etc/axpkg/depolar` (varsayılan
  `REMOTE_REPO` = GitHub "paketler" sürümü); `axpkg update` INDEX + simgeleri
  `/var/lib/axpkg/remote/`'a indirir; yerelde olmayan paket curl ile indirilir, sha256
  doğrulanır, kurulunca silinir. `search [kelime]`, `upgrade`, `provides <komut>`, `names`.
  Kurulum betiği başarısız olursa kurulum geri alınır. Dosyalar hedefle aynı dosya sisteminde
  açılıp taşınır (büyük paketlerde RAM iki kat harcanmaz). `AXPKG_REMOTE="url ..."` ile test.
  Ek MANIFEST alanları (axpkg yok sayar, Market okur):
  `title`, `category` (Oyunlar/Araçlar/Grafik/Axs/Sistem), `about`, `app` (grafik uygulama
  kimliği), `run` (terminalde çalışacak komut), `open` (açılacak yol), `extra_size`
  (kurulum betiğinin ayrıca indirdiği bayt).
  Depoda `INDEX` (tüm MANIFEST'ler + `file`, `size`, `installed_size`; kayıtlar boş satırla
  ayrılır), `icons/<ad>.png` ve `PREINSTALL` (initramfs aşamasında `AXPKG_ROOT` ile hazır
  kurulanlar: hesap-makinesi, saat, resim-gorucu) bulunur.
- **AxsDE (masaüstü)**: `/usr/bin/axsde`, statik (~340 KB). Önce DRM/KMS (`/dev/dri/card0`,
  dumb buffer + DIRTYFB; QEMU'da bochs-drm), olmazsa `/dev/fb0` (VESA/EFI fb; `AXSDE_FBDEV=1`
  zorlar). `/dev/input/event*` okur (klavye, PS/2 fare, virtio tablet). Yazılımla çizim:
  kenar yumuşatmalı yuvarlak köşeler, gölgeler, buzlu cam (bulanıklık), stb_truetype ile metin.
  - Performans (KVM'siz TCG'de de akıcı): yalnızca değişen bölgeler çizilir (hasar listesi,
    yalnızca ucuzsa birleştirilir); pencere içerikleri ayrı yüzeylerde, başlık çubukları
    önbellekte; gölge 9 parçalı hazır tablodan; sürükleme/boyutlandırma sırasında içerik
    yeniden çizilmez; ağ durumu ioctl ile (süreç başlatmadan). Ölçüm: `axsde --bench`.
  - Kabuk: üst çubuk (başlatıcı, odaktaki uygulama, saat, ağ, TR/US, güç menüsü), dock,
    Spotlight benzeri başlatıcı (Super / Ctrl+Boşluk; `> komut` terminalde çalıştırır),
    sağ tık menüsü, bildirimler, 5 duvar kâğıdı, 6 vurgu rengi.
  - Pencereler: sürükle, sağ-alt köşeden boyutlandır, çift tıkla büyüt, küçült, Alt+Tab, Alt+F4.
  - Uygulamalar: Terminal (pty + axsh, xterm alt kümesi, 256/24 bit renk, geri kaydırma),
    Dosyalar, **Axs Stüdyo** (Axs sözdizimi renklendirme, otomatik girinti, F5 ile çalıştır,
    çıktı paneli), **Uygulama Marketi**, Sistem İzleyici, Ayarlar, Hoş Geldin.
  - **Uygulama Marketi** (`app_packages.c`): yerel + uzak depo `INDEX`'lerini okur (ağ varsa
    açılışta uzak dizini arka planda yeniler, ⟳ düğmesi); kategoriler (Keşfet, İnternet,
    Oyunlar, Araçlar, Grafik, Axs, Sistem, Kurulu), öne çıkan afiş, kart ızgarası ("İnternetten
    • ~boyut"), Türkçe duyarsız arama (ı/i, ş/s…), ayrıntı sayfası; Kur/Aç/Güncelle/Kaldır
    `axpkg` ile arka planda, indirme ilerlemesi kenar çubuğunda.
  - **Tarayıcılar** (Firefox, Google Chrome; uzak depo): glibc/GTK/X11 ister, AxsOS ise musl ve
    X'siz. `tarayici-ortami` paketi `/opt/tarayici`'ye küçük bir Ubuntu 24.04 kökü (Xvfb,
    matchbox, GTK, NSS, yazı tipleri; Mesa/LLVM çıkarılmış) + `xkopru` kurar. `firefox` /
    `google-chrome` paketleri kurulurken tarayıcıyı resmi kaynaktan indirir (mozilla.org Türkçe
    tar.xz / dl.google.com .deb). `tarayici-baslat`: chroot bağlamaları (proc, sys, dev, shm,
    tmp, ~/İndirilenler), Xvfb `:1`/`:2` (`-fbdir`), setxkbmap (TR/US), tarayıcı `axs` (uid
    1000) kullanıcısıyla (kendi korumalı alanıyla), `xkopru` pencere; pencere kapanınca tarayıcı
    kapanır. `xkopru` ekran belleğini (XWD) eşleyip yalnızca değişen satırları gönderir,
    klavye/fareyi ham X11 protokolüyle XTEST'e iletir (Xlib yok). `--basliksiz` ile ekransız.
  - **Market uygulamaları** (ayrı süreçler, `apps/`): Hesap Makinesi, Saat (kronometre,
    zamanlayıcı), Takvim (tatiller, notlar), Resim Görüntüleyici (+ örnek resimler), Çizim
    (PNG kaydı `~/Resimler`), Yılan, 2048, Mayın Tarlası. Terminal paketleri (`pkgs/`) de
    Market'te görünür, "Aç" onları terminalde çalıştırır.
  - **ADP** (AxsDE uygulama protokolü, `adp.h`): `/run/axsde.sock` (SOCK_SEQPACKET).
    Uygulama `HELLO` gönderir, sunucu pencereyi açıp memfd ortak tamponunu (SCM_RIGHTS)
    `CONFIGURE` ile verir; uygulama çizip `COMMIT(bölge)` yollar; sunucu klavye/fare/odak/
    boyut/kapat olaylarını iletir. Uygulama çökerse yalnızca penceresi kapanır.
  - Yeni dış uygulama: `apps/<ad>/main.c` (`axsapp_run(kimlik, başlık, g, y, &işlevler)`;
    AxsDE'nin `gfx/font/ui` işlevleri kullanılabilir, `axsapp_redraw_rect` ile kısmi çizim) +
    `apps/<ad>/app.conf` (name, title, version, category, description, about, isteğe bağlı
    `ext=png,jpg` dosya ilişkilendirme, `libs=img`) + `apps/mkicon.c`'ye simge.
    apps aşaması `/usr/bin/<ad>`, `/usr/share/axsde/apps/<ad>.app` ve simgeyi paketler.
  - Uygulama kaydı (`apps.c`): yerleşikler + `/usr/share/axsde/apps/*.app` (name, exec,
    icon, category, desc, ext). Başlatıcı ve dock buradan beslenir; Dosyalar'da çift tık
    `ext` eşleşen uygulamayı açar. Çalışan dış uygulamalar dock'ta ayraçtan sonra görünür.
  - Anında-mod arayüz (`ui.c`): uygulama `draw()` içinde düğmeleri çizer ve tıklamaları
    orada işler; fare hareketinde yalnızca üzerindeki öğe değişirse yeniden çizilir.
  - Yeni yerleşik uygulama: `axsde/app_x.c` içinde `const App APP_X`, `axsde.h`'de bildir,
    `apps.c`'deki `apps_scan()` içine `add_builtin` ekle (istersen `wm.c`'de `DOCK`'a da).
  - init, ekran konsolunda (tty1) framebuffer varsa shell yerine axsde başlatır;
    `axs.gui=0` çekirdek parametresi ya da güç menüsündeki "Metin konsoluna geç" ile kapatılır.
  - Ayarlar `/etc/axsde.conf` (RAM'de; yeniden başlatınca sıfırlanır). Varsayılan saat dilimi UTC+3.
- **ISO**: `grub-mkrescue` ile BIOS+UEFI hibrit. Menü: masaüstü (varsayılan),
  metin kipi, ayrıntılı, kurtarma (`rdinit=/bin/sh`). Konsol hem ekran (`tty0`) hem seri (`ttyS0`).

## GitHub Actions (`.github/workflows/build.yml`)

Her push'ta: `./build.sh deps` → `./build.sh` → `scripts/ci-test.sh` (ISO QEMU'da açılıp test
edilir; başarısızsa yayın yapılmaz) → `sudo ./build.sh tarayici` → `scripts/ci-tarayici-test.sh`
(ISO'da Firefox ve Chrome internetten kurulup sayfa açar) → artifact (ISO, SHA256SUMS,
ekran görüntüleri, seri günlükler) → **`paketler`** sürümüne uzak depo (INDEX, .axp, simgeler)
yüklenir → **Releases'te `son-surum`** ön sürümü güncellenir. `v*` etiketi gönderilince
(`git tag v0.2 && git push origin v0.2`) kalıcı sürüm oluşur. `build/downloads` ve
`build/deps` (statik zlib/OpenSSL/SQLite) `config/versions.sh` özetine göre önbelleklenir.

## İndirme kaynakları

Her bileşen önce resmi kaynaktan (kernel.org, busybox.net) denenir, erişilemezse GitHub
aynasından `git clone --depth 1` yapılır. bash, ncurses, nano (ftp.gnu.org) ve curl (curl.se)
erişilemezse Ubuntu arşivindeki aynı sürümün özgün (`orig`) arşivinden alınır (gregkh/linux, mirror/busybox, python/cpython,
openssl/openssl, madler/zlib, sqlite/sqlite, Nylithra/axs, nothings/stb, rsms/inter,
JetBrains/JetBrainsMono, novnc/noVNC). Yazı tipleri seyrek klonla tek dosya olarak alınır.

## Çalışma kuralları

- Aşamalar: 1) kernel 2) BusyBox + initramfs 3) C init 4) axsh 5) axs dili
  6) axpkg 7) bootable ISO 8) AxsDE masaüstü 9) Uygulama Marketi + uygulamalar
  10) bash + gerçek ağ + uzak depo + Firefox/Chrome. Hepsi tamamlandı.
- Masaüstü değişikliklerini `scripts/qemu-gui.py` ile ekran görüntüsü alarak doğrula.
- Her değişiklikten sonra QEMU'da test et (`scripts/qemu-test.sh`), git commit at.
- Her şey `./build.sh` ve `./run.sh` ile yeniden üretilebilir kalmalı; elle adım yok.
- C kodu `-Wall -Wextra -Werror` ile derlenir.
- Kullanıcı arayüzü mesajları Türkçe.
- Yeni paket eklemek için: `pkgs/<ad>/MANIFEST` + `pkgs/<ad>/files/...` oluştur,
  `./build.sh axpkg initramfs`.
