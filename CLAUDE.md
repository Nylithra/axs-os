# AxsOS

Linux çekirdeği tabanlı, kendi minimal Linux dağıtımım. Sistemin ana dili, benim
yazdığım **Axs** programlama dilidir. Kullanıcı alanı; kendi init'im (C), kendi
shell'im (`axsh`), Axs yorumlayıcısı (`axs`) ve paket yöneticisi (`axpkg`) üzerine
kuruludur. Hedef: tek komutla derlenen, QEMU'da ve ISO olarak açılabilen bir sistem.

## Ortam

- GitHub Codespaces (Ubuntu), **KVM yok** → QEMU TCG (yazılım emülasyonu).
- Test: `qemu-system-x86_64 -nographic` (seri konsol `ttyS0`).
- Gerekli paketler:
  `sudo apt-get install -y build-essential flex bison bc libelf-dev libssl-dev qemu-system-x86 cpio xz-utils rsync musl-tools grub-pc-bin grub-common xorriso mtools`

## Komutlar

| Komut | Açıklama |
|---|---|
| `./build.sh` | Her şeyi sırayla derler (indirme dahil). |
| `./build.sh <aşama>` | Tek aşama: `kernel` (sonra: `busybox`, `init`, `axsh`, `axs`, `axpkg`, `iso`). |
| `./build.sh clean` | `build/` çıktılarını siler, `build/downloads/` korunur. |
| `./run.sh` | QEMU'da açar. Çıkış: `Ctrl-a` sonra `x`. |
| `TIMEOUT=30 ./run.sh` | 30 sn sonra QEMU'yu kapatır (otomatik test). |
| `APPEND="..." ./run.sh` | Kernel komut satırına ek parametre. |

## Klasör yapısı

```
build.sh, run.sh        Tek giriş noktaları
config/versions.sh      Sabitlenmiş upstream sürümleri (KERNEL_VERSION, ...)
config/kernel.fragment  tinyconfig üzerine uygulanan kernel ayarları
scripts/common.sh       Ortak değişkenler/yardımcılar (ROOT, BUILD, OUT, log, die)
scripts/build-<aşama>.sh  Her aşamanın derleme betiği; build.sh bunları çağırır
build/                  (git'e girmez) kaynaklar, indirmeler, çıktılar
build/out/bzImage       Derlenmiş kernel
build/out/initramfs.cpio.gz  (Aşama 2+) kök dosya sistemi
```

İleride eklenecek: `init/` (C init), `axsh/` (shell), `axpkg/` (paket yöneticisi),
`rootfs/` (initramfs'e kopyalanan sabit dosyalar), `iso/` (bootloader ayarları).

## Kernel

- Sürüm `config/versions.sh` içinde sabit (şu an 6.12 LTS).
- İndirme: önce `cdn.kernel.org` tarball, erişilemezse `github.com/gregkh/linux` shallow clone.
- Config: `make tinyconfig` + `config/kernel.fragment` + `olddefconfig`. Fragment
  değişince otomatik yeniden yapılandırılır; uygulanamayan seçenekler UYARI olarak
  basılır. Yeni özellik gerekiyorsa (ör. ağ, ext4) fragment'e ekleyin.
- 4 çekirdekte ~3 dk derlenir, bzImage ~1.4 MB.

## Çalışma kuralları

- Aşamalar: 1) kernel 2) BusyBox + initramfs 3) C init 4) axsh 5) axs dili
  6) axpkg 7) bootable ISO.
- Her aşama sonunda: QEMU'da test et, ne yapıldığını açıkla, git commit at,
  kullanıcı onayını bekle.
- Her şey `./build.sh` ve `./run.sh` ile yeniden üretilebilir kalmalı; elle adım yok.
- Kullanıcı arayüzü mesajları Türkçe.
