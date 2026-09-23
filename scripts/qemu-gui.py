#!/usr/bin/env python3
"""AxsOS masaüstü test aracı: QEMU'yu görüntüsüz açar, QMP ile fare/klavye
olayı gönderir ve ekran görüntüsü (PNG) alır.

Kullanım:
    scripts/qemu-gui.py [--iso] komut1 komut2 ...

Komutlar:
    wait:SN                 bekle (saniye, ondalık olabilir)
    shot:DOSYA.png          ekran görüntüsü al
    move:X,Y                fareyi taşı (1280x800 ekran koordinatı)
    click:X,Y[,sag]         tıkla
    dbl:X,Y                 çift tıkla
    drag:X1,Y1,X2,Y2        sürükle
    key:ctrl-alt-t          tuş birleşimi gönder (QEMU qcode adları)
    type:metin              metni yaz (ASCII; boşluk için _ yerine gerçek boşluk)
    wheel:X,Y,+/-N          fare tekerleği
Örnek:
    scripts/qemu-gui.py wait:40 shot:/tmp/masaustu.png key:ctrl-alt-t wait:3 type:ls wait:1 key:ret shot:/tmp/t.png
"""
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.environ.get("AXSOS_BUILD", os.path.join(ROOT, "build"))
OUT = os.path.join(BUILD, "out")
W, H = 1280, 800


class QMP:
    def __init__(self, path):
        for _ in range(100):
            try:
                self.s = socket.socket(socket.AF_UNIX)
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile("rw")
        json.loads(self.f.readline())
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                return r

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})

    def events(self, evs):
        return self.cmd("input-send-event", events=evs)


def abs_move(q, x, y):
    q.events([
        {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (W - 1))}},
        {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (H - 1))}},
    ])


def button(q, down, btn="left"):
    q.events([{"type": "btn", "data": {"down": down, "button": btn}}])


def png_from_ppm(ppm, png):
    with open(ppm, "rb") as f:
        data = f.read()
    # P6\n<w> <h>\n255\n
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    raw = b"".join(b"\x00" + pix[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(png, "wb") as f:
        f.write(out)


# ABD düzeni fiziksel tuşlar (testlerde masaüstünün klavye düzeni US seçilmeli)
KEYMAP = {" ": "spc", "\n": "ret", ".": "dot", ",": "comma", "-": "minus", "/": "slash",
          "=": "equal", ";": "semicolon", "'": "apostrophe", "[": "bracket_left", "]": "bracket_right",
          "\\": "backslash", "`": "grave_accent",
          '"': "shift-apostrophe", ":": "shift-semicolon", "!": "shift-1", "@": "shift-2", "#": "shift-3",
          "$": "shift-4", "%": "shift-5", "^": "shift-6", "&": "shift-7", "*": "shift-8", "(": "shift-9",
          ")": "shift-0", "_": "shift-minus", "+": "shift-equal", "<": "shift-comma", ">": "shift-dot",
          "?": "shift-slash", "|": "shift-backslash", "~": "shift-grave_accent", "{": "shift-bracket_left",
          "}": "shift-bracket_right"}


def type_text(q, text):
    for ch in text:
        if ch.isupper():
            q.hmp("sendkey shift-" + ch.lower())
        elif ch in KEYMAP:
            q.hmp("sendkey " + KEYMAP[ch])
        else:
            q.hmp("sendkey " + ch)
        time.sleep(0.05)


def main():
    args = sys.argv[1:]
    iso = False
    if args and args[0] == "--iso":
        iso = True
        args = args[1:]
    tmp = tempfile.mkdtemp(prefix="axsgui")
    sock = os.path.join(tmp, "qmp.sock")
    qemu = ["qemu-system-x86_64", "-m", os.environ.get("MEM", "768M"), "-display", "none",
            "-qmp", "unix:%s,server,nowait" % sock, "-serial", "file:%s/serial.log" % tmp,
            "-nic", "user,model=e1000", "-device", "virtio-tablet-pci", "-device", "virtio-keyboard-pci",
            "-no-reboot"]
    if os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.W_OK):
        qemu += ["-enable-kvm", "-cpu", "host"]
    else:
        qemu += ["-accel", "tcg", "-smp", "2"]
    if iso:
        qemu += ["-cdrom", os.path.join(OUT, "axsos.iso"), "-boot", "d"]
    else:
        qemu += ["-kernel", os.path.join(OUT, "bzImage"), "-initrd", os.path.join(OUT, "initramfs.cpio.gz"),
                 "-append", "console=tty0 console=ttyS0 video=1280x800 quiet"]
    p = subprocess.Popen(qemu)
    q = QMP(sock)
    t0 = time.time()
    try:
        for a in args:
            k, _, v = a.partition(":")
            if k == "wait":
                time.sleep(float(v))
            elif k == "shot":
                ppm = os.path.join(tmp, "s.ppm")
                q.hmp("screendump " + ppm)
                time.sleep(0.5)
                png_from_ppm(ppm, v)
                print("[%.0fs] ekran görüntüsü: %s" % (time.time() - t0, v))
            elif k == "move":
                x, y = map(int, v.split(","))
                abs_move(q, x, y)
            elif k in ("click", "dbl"):
                parts = v.split(",")
                x, y = int(parts[0]), int(parts[1])
                btn = "right" if len(parts) > 2 and parts[2] == "sag" else "left"
                abs_move(q, x, y)
                time.sleep(0.15)
                for _ in range(2 if k == "dbl" else 1):
                    button(q, True, btn)
                    time.sleep(0.08)
                    button(q, False, btn)
                    time.sleep(0.12)
            elif k == "drag":
                x1, y1, x2, y2 = map(int, v.split(","))
                abs_move(q, x1, y1)
                time.sleep(0.2)
                button(q, True)
                for i in range(1, 11):
                    abs_move(q, x1 + (x2 - x1) * i // 10, y1 + (y2 - y1) * i // 10)
                    time.sleep(0.08)
                button(q, False)
            elif k == "wheel":
                x, y, n = v.split(",")
                abs_move(q, int(x), int(y))
                n = int(n)
                for _ in range(abs(n)):
                    q.events([{"type": "btn", "data": {"down": True, "button": "wheel-up" if n > 0 else "wheel-down"}}])
                    q.events([{"type": "btn", "data": {"down": False, "button": "wheel-up" if n > 0 else "wheel-down"}}])
                    time.sleep(0.05)
            elif k == "key":
                q.hmp("sendkey " + v)
            elif k == "type":
                type_text(q, v)
            else:
                print("bilinmeyen komut:", a)
    finally:
        try:
            q.cmd("quit")
        except Exception:
            pass
        p.wait(timeout=10)
        log = os.path.join(tmp, "serial.log")
        if os.environ.get("SHOW_SERIAL") and os.path.exists(log):
            print(open(log, errors="replace").read()[-3000:])


if __name__ == "__main__":
    main()
