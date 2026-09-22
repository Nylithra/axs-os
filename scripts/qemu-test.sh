#!/usr/bin/env bash
# Otomatik QEMU testi: sistemi açar, verilen komutları seri konsola yazar,
# çıktıyı gösterir ve QEMU'yu kapatır.
#   scripts/qemu-test.sh "uname -a" "ls /"
#   BOOT_WAIT=15 CMD_WAIT=2 scripts/qemu-test.sh ...
#   RUN_MODE=iso scripts/qemu-test.sh ...     (ISO ile)
cd "$(dirname "$0")/.."
BOOT_WAIT="${BOOT_WAIT:-12}"; CMD_WAIT="${CMD_WAIT:-1}"
TOTAL=$(( BOOT_WAIT + (${#@} + 3) * CMD_WAIT + 10 ))
{
    sleep "$BOOT_WAIT"
    for c in "$@"; do printf '%s\n' "$c"; sleep "$CMD_WAIT"; done
    sleep 3
} | TIMEOUT="$TOTAL" ./run.sh ${RUN_MODE:-} 2>&1 | sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g; s/\r*$//; s/.*\r//'
