#!/usr/bin/env python3
"""Ubuntu paket bağımlılık çözücü (build-araclar.sh için).

    deb-coz.py <Packages.gz dizini> <atlanacaklar> paket1 paket2 ...

Packages.gz dosyalarını (ör. noble-main.gz, noble-updates-main.gz) okur, en yeni sürümü
seçer, Depends + Pre-Depends kapanışını çıkarır (alternatiflerden ilk bulunanı, sanal
paketler Provides ile). Çıktı: her satırda "ad sürüm Filename SHA256 Installed-Size".
"""
import gzip
import os
import re
import sys


def vkey(v):
    """Kaba Debian sürüm karşılaştırması: epoch, sonra sayı/metin parçaları."""
    ep, _, rest = v.partition(":") if ":" in v else ("0", "", v)
    parts = re.findall(r"\d+|[A-Za-z]+|~|[^\dA-Za-z~]+", rest)
    key = [int(ep)]
    for p in parts:
        if p == "~":
            key.append((-1, 0, ""))
        elif p.isdigit():
            key.append((1, int(p), ""))
        else:
            key.append((0, 0, p))
    return key


def oku(dizin):
    paketler, saglayan = {}, {}
    for ad in sorted(os.listdir(dizin)):
        if not ad.endswith(".gz"):
            continue
        with gzip.open(os.path.join(dizin, ad), "rt", encoding="utf-8", errors="replace") as f:
            kayit = {}
            for satir in f.read().split("\n\n"):
                kayit = {}
                son = None
                for l in satir.split("\n"):
                    if l.startswith(" ") and son:
                        kayit[son] += "\n" + l
                    elif ":" in l:
                        k, _, v = l.partition(":")
                        kayit[k] = v.strip()
                        son = k
                if "Package" not in kayit:
                    continue
                p = kayit["Package"]
                eski = paketler.get(p)
                if eski is None or vkey(kayit["Version"]) > vkey(eski["Version"]):
                    paketler[p] = kayit
    for p, k in paketler.items():
        for s in k.get("Provides", "").split(","):
            s = s.strip().split(" ")[0]
            if s:
                saglayan.setdefault(s, []).append(p)
    return paketler, saglayan


def main():
    dizin, atla = sys.argv[1], set(sys.argv[2].split())
    paketler, saglayan = oku(dizin)
    istenen = sys.argv[3:]
    secilen, sira = set(), list(istenen)
    while sira:
        p = sira.pop(0)
        p = p.split(":")[0]
        if p in secilen or p in atla:
            continue
        if p not in paketler:
            if p in saglayan:
                p = saglayan[p][0]
                if p in secilen or p in atla:
                    continue
            else:
                print("bulunamadı: " + p, file=sys.stderr)
                sys.exit(1)
        secilen.add(p)
        k = paketler[p]
        for alan in ("Pre-Depends", "Depends"):
            for grup in k.get(alan, "").split(","):
                grup = grup.strip()
                if not grup:
                    continue
                adaylar = [a.strip().split(" ")[0].split(":")[0] for a in grup.split("|")]
                if any(a in secilen for a in adaylar):
                    continue
                if any(a in atla for a in adaylar):
                    continue
                for a in adaylar:
                    if a in paketler or a in saglayan:
                        sira.append(a)
                        break
    for p in sorted(secilen):
        k = paketler[p]
        print(p, k["Version"], k["Filename"], k.get("SHA256", "-"), k.get("Installed-Size", "0"))


if __name__ == "__main__":
    main()
