``` baslat.bat

@echo off
chcp 65001 >nul
title AxsOS - QEMU
cd /d "%~dp0"

set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"
if not exist "%QEMU%" (
  echo QEMU bulunamadi: %QEMU%
  pause
  exit /b 1
)

if not exist "AxsOS\axsos.iso" (
  if exist "axsos-iso.zip" (
    echo axsos-iso.zip aciliyor...
    powershell -NoProfile -Command "Expand-Archive -Force 'axsos-iso.zip' 'AxsOS'"
  )
)
if not exist "AxsOS\axsos.iso" (
  if exist "axsos.iso" (
    if not exist AxsOS mkdir AxsOS
    move /y "axsos.iso" "AxsOS\axsos.iso" >nul
  )
)
if not exist "AxsOS\axsos.iso" (
  echo AxsOS\axsos.iso bulunamadi. axsos-iso.zip dosyasini bu klasore indir.
  pause
  exit /b 1
)

rem Kalici depolama diski: kurulan uygulamalar, dosyalar ve ayarlar yeniden baslatinca korunur.
rem Ilk acilista AxsOS "Ayarlar > Depolama"dan diski hazirlamayi onerir (tek tik).
set "QIMG=C:\Program Files\qemu\qemu-img.exe"
if not exist "AxsOS\veri.qcow2" (
  echo Kalici depolama diski olusturuluyor: AxsOS\veri.qcow2 - 20 GB, doldukca buyur
  "%QIMG%" create -f qcow2 "AxsOS\veri.qcow2" 20G
)

rem Ortak pano: Windows'ta kopyala, AxsOS'ta yapistir (ve tersi). QEMU surumu destekliyorsa eklenir.
set "PANO="
"%QEMU%" -chardev help 2>nul | findstr /c:"qemu-vdagent" >nul && set "PANO=-device virtio-serial-pci -chardev qemu-vdagent,id=vda,clipboard=on -device virtserialport,chardev=vda,name=com.redhat.spice.0"

echo AxsOS baslatiliyor... kapatmak icin AxsOS icinde: poweroff
rem 4G bellek: Firefox/Chrome icin gerekli (tarayicisiz 2G yeter). whpx: Windows donanim hizlandirmasi
rem (Windows ozellikleri > "Windows Hypervisor Platform"); yoksa tcg (yazilim) kullanilir.
"%QEMU%" -m 4G -smp 2 -accel whpx -accel tcg -cdrom "AxsOS\axsos.iso" -drive file=AxsOS\veri.qcow2,if=virtio -boot d -name AxsOS -netdev user,id=n0 -device e1000,netdev=n0 -device virtio-tablet-pci -device virtio-keyboard-pci %PANO%

```
