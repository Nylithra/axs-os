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

echo AxsOS baslatiliyor... (kapatmak icin AxsOS icinde: poweroff)
rem 4G bellek: Firefox/Chrome icin gerekli (tarayicisiz 2G yeter). whpx: Windows donanim hizlandirmasi
rem (Windows ozellikleri > "Windows Hypervisor Platform"); yoksa tcg (yazilim) kullanilir.
"%QEMU%" -m 4G -smp 2 -accel whpx -accel tcg -cdrom "AxsOS\axsos.iso" -boot d -name AxsOS -netdev user,id=n0 -device e1000,netdev=n0 -device virtio-tablet-pci -device virtio-keyboard-pci

```
