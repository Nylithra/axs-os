"""Axs kolay arayuz kutuphanesi:  use arayuz

AxsDE masaustunde pencereli program yazmanin en kolay yolu.

    use arayuz

    p = arayuz.pencere("Sayac", 360, 220)
    etiket = p.baslik("0")
    sayi = 0

    func arttir()
      sayi += 1
      etiket.ayarla(sayi)
    end

    p.dugme("Arttir", arttir, "birincil")
    p.calistir()

Pencereyi /usr/bin/arayuz cizer (C, AxsDE uygulama protokolu); bu kutuphane ona
satir satir komut yollar ve gelen olaylarda verilen isleri calistirir.
Belgeler: /usr/lib/axs/docs/arayuz.md
"""

import os
import select
import subprocess
import time

from axslang.errors import AxsRuntimeError, AxsTypeError
from axslang.lib import isim_alani, kutuphane
from axslang.values import Gomulu, Islev, cagrilabilir_mi, metin as _metin

ARAYUZ = os.environ.get("AXS_ARAYUZ", "/usr/bin/arayuz")
SOKET = os.environ.get("AXS_ARAYUZ_SOKET", "/run/axsde.sock")

STILLER = {"normal", "birincil", "tehlike", "sade", "primary", "danger", "ghost"}


def _kac(v):
    """Alan icindeki ozel karakterleri kacis dizisine cevirir."""
    if v is True:
        v = 1
    elif v is False or v is None:
        v = 0 if v is False else ""
    if isinstance(v, (list, tuple)):
        v = "|".join(_metin(x).replace("|", "/") for x in v)
    s = v if isinstance(v, str) else _metin(v)
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def _coz(s):
    cikti, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            i += 1
            c = {"n": "\n", "t": "\t"}.get(s[i], s[i])
        cikti.append(c)
        i += 1
    return "".join(cikti)


def _liste(v):
    if v is None:
        return []
    if isinstance(v, (list, tuple)):
        return [_metin(x) for x in v]
    if isinstance(v, dict):
        return [_metin(k) for k in v]
    return [x for x in _metin(v).split("|")]


class Pencere:
    def __init__(self, y, baslik, genislik, yukseklik):
        if not os.path.exists(ARAYUZ):
            raise AxsRuntimeError("arayuz: %s bulunamadi" % ARAYUZ)
        if not os.path.exists(SOKET):
            raise AxsRuntimeError("arayuz: masaustu (AxsDE) acik degil; pencereli programlar "
                                  "masaustundeki Terminal'den ya da Axs Studyo'dan calistirilir")
        self.y = y
        self.proc = subprocess.Popen([ARAYUZ], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     close_fds=True)
        self.oku_fd = self.proc.stdout.fileno()
        self.tampon = b""
        self.sayac = 0
        self.isler = {}          # kimlik -> {"tik": is, "degisti": is, "gonder": is}
        self.durum = {}          # kimlik -> {"tur", "deger", "ogeler"}
        self.zamanlayicilar = []  # [sonraki, aralik (0 = bir kez), is]
        self.acik = True
        self.kapanis_isi = None
        self.yolla("ac", baslik, int(genislik), int(yukseklik))

    # -- iletisim --------------------------------------------------
    def yolla(self, *alanlar):
        if not self.acik:
            return
        satir = "\t".join(_kac(a) for a in alanlar) + "\n"
        try:
            self.proc.stdin.write(satir.encode("utf-8"))
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError):
            self.acik = False

    def yeni_kimlik(self, tur):
        self.sayac += 1
        return "%s%d" % (tur, self.sayac)

    def cagir(self, is_, args):
        if is_ is None:
            return None
        if isinstance(is_, Islev):
            args = args[:len(is_.parametreler)]
        return self.y.cagir(is_, list(args))

    # -- olaylar ---------------------------------------------------
    def olay(self, satir):
        alanlar = [_coz(a) for a in satir.split("\t")]
        tur = alanlar[0]
        if tur == "kapandi":
            self.acik = False
            return
        if len(alanlar) < 2:
            return
        kimlik = alanlar[1]
        deger = alanlar[2] if len(alanlar) > 2 else None
        d = self.durum.get(kimlik)
        if d is not None and tur == "degisti":
            if d["tur"] == "onay":
                d["deger"] = deger == "1"
            elif d["tur"] == "kaydirici":
                d["deger"] = int(deger or 0)
            elif d["tur"] in ("secenek", "liste"):
                d["deger"] = deger
                if deger in d["ogeler"]:
                    d["secili"] = d["ogeler"].index(deger)
            else:
                d["deger"] = deger
        elif d is not None and tur == "gonder" and d["tur"] == "giris":
            d["deger"] = deger
        is_ = self.isler.get(kimlik, {}).get(tur)
        if is_ is not None:
            if d is not None and tur in ("degisti", "gonder"):
                self.cagir(is_, [d["deger"]])
            else:
                self.cagir(is_, [])

    def oku(self, sure):
        """sure saniye (None: sonsuz) olay bekler ve gelenleri isler."""
        r, _, _ = select.select([self.oku_fd], [], [], sure)
        if not r:
            return
        veri = os.read(self.oku_fd, 65536)
        if not veri:
            self.acik = False
            return
        self.tampon += veri
        while b"\n" in self.tampon and self.acik:
            satir, self.tampon = self.tampon.split(b"\n", 1)
            self.olay(satir.decode("utf-8", "replace"))

    def calistir(self):
        try:
            while self.acik:
                simdi = time.monotonic()
                sure = None
                if self.zamanlayicilar:
                    sure = max(0.0, min(z[0] for z in self.zamanlayicilar) - simdi)
                self.oku(sure)
                simdi = time.monotonic()
                for z in list(self.zamanlayicilar):
                    if not self.acik:
                        break
                    if z[0] <= simdi:
                        if z[1] > 0:
                            z[0] = simdi + z[1]
                        else:
                            self.zamanlayicilar.remove(z)
                        self.cagir(z[2], [])
        finally:
            self.kapat()
        if self.kapanis_isi is not None:
            self.cagir(self.kapanis_isi, [])
        return True

    def kapat(self):
        if self.proc.poll() is None:
            self.yolla("kapat")
            try:
                self.proc.stdin.close()
            except OSError:
                pass
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.acik = False
        return True


def _oge(p, kimlik, tur):
    """Bir ogeyi Axs'ten kullanilabilir hale getirir: etiket.ayarla("..."), giris.deger() ..."""
    d = p.durum[kimlik]

    def ayarla(deger):
        if tur in ("onay", "secenek", "liste", "kaydirici", "ilerleme"):
            if tur in ("secenek", "liste") and not isinstance(deger, (int, float)):
                deger = d["ogeler"].index(_metin(deger)) if _metin(deger) in d["ogeler"] else -1
            if tur in ("secenek", "liste"):
                d["secili"] = int(deger)
                d["deger"] = d["ogeler"][int(deger)] if 0 <= int(deger) < len(d["ogeler"]) else None
            elif tur == "onay":
                d["deger"] = bool(deger)
            else:
                d["deger"] = int(deger)
            p.yolla("ayarla", kimlik, "deger", d.get("secili", d["deger"]) if tur in ("secenek", "liste") else d["deger"])
        else:
            d["deger"] = _metin(deger)
            p.yolla("ayarla", kimlik, "metin", d["deger"])
        return True

    def deger():
        return d.get("deger")

    def secili():
        return d.get("secili", -1)

    def ogeler(liste=None):
        if liste is None:
            return list(d["ogeler"])
        d["ogeler"] = _liste(liste)
        if d.get("secili", -1) >= len(d["ogeler"]):
            d["secili"] = -1
            d["deger"] = None
        p.yolla("ayarla", kimlik, "ogeler", d["ogeler"])
        return True

    def ekle(oge):
        d["ogeler"].append(_metin(oge))
        p.yolla("ayarla", kimlik, "ogeler", d["ogeler"])
        return True

    def etkin(acik=True):
        p.yolla("ayarla", kimlik, "etkin", bool(acik))
        return True

    def gizle():
        p.yolla("ayarla", kimlik, "gorunur", 0)
        return True

    def goster():
        p.yolla("ayarla", kimlik, "gorunur", 1)
        return True

    def sil():
        p.yolla("sil", kimlik)
        p.durum.pop(kimlik, None)
        return True

    def odak():
        p.yolla("odak", kimlik)
        return True

    def stil(ad):
        p.yolla("ayarla", kimlik, "stil", _metin(ad))
        return True

    def baglayici(olay):
        def bagla(is_):
            if not cagrilabilir_mi(is_):
                raise AxsTypeError("bir is verilmeli")
            p.isler.setdefault(kimlik, {})[olay] = is_
            return True
        return bagla

    uyeler = {
        "kimlik": kimlik, "tur": tur,
        "ayarla": ayarla, "set": ayarla,
        "deger": deger, "value": deger, "metin": deger, "text": deger,
        "secili": secili, "ogeler": ogeler, "ekle": ekle,
        "etkin": etkin, "gizle": gizle, "goster": goster, "sil": sil, "odak": odak, "stil": stil,
        "tiklaninca": baglayici("tik"), "on_click": baglayici("tik"),
        "degisince": baglayici("degisti"), "on_change": baglayici("degisti"),
        "gonderilince": baglayici("gonder"), "on_submit": baglayici("gonder"),
    }
    if tur in ("satir", "kutu"):
        uyeler.update(_kap_uyeleri(p, kimlik))
    return isim_alani("oge", uyeler)


def _kap_uyeleri(p, ust):
    """Pencere, satir ve kutu icine oge ekleyen isler."""

    def yeni(tur, *degerler, deger=None, ogeler=None, isler=None):
        kimlik = p.yeni_kimlik(tur)
        p.durum[kimlik] = {"tur": tur, "deger": deger, "ogeler": ogeler or []}
        if isler:
            p.isler[kimlik] = {k: v for k, v in isler.items() if v is not None}
        p.yolla("ekle", tur, kimlik, ust, *degerler)
        return _oge(p, kimlik, tur)

    def is_mi(v):
        return v is not None and cagrilabilir_mi(v)

    def baslik(metin=""):
        return yeni("baslik", _metin(metin), deger=_metin(metin))

    def yazi(metin=""):
        return yeni("yazi", _metin(metin), deger=_metin(metin))

    def dugme(metin="Tamam", is_=None, stil="normal"):
        # dugme("Kaydet", "birincil", kaydet) sirasi da olur
        if isinstance(is_, str) and is_mi(stil):
            is_, stil = stil, is_
        if isinstance(is_, str) and is_ in STILLER:
            stil, is_ = is_, None
        return yeni("dugme", _metin(metin), _metin(stil), deger=_metin(metin), isler={"tik": is_})

    def giris(ipucu="", deger="", is_=None, gonder=None, gizli=False):
        if is_mi(deger) and is_ is None:
            is_, deger = deger, ""
        return yeni("giris", _metin(ipucu), _metin(deger), 1 if gizli else 0, deger=_metin(deger),
                    isler={"degisti": is_, "gonder": gonder})

    def parola(ipucu="Parola", is_=None, gonder=None):
        return giris(ipucu, "", is_, gonder, True)

    def onay(metin="", deger=False, is_=None):
        if is_mi(deger) and is_ is None:
            is_, deger = deger, False
        return yeni("onay", _metin(metin), 1 if deger else 0, deger=bool(deger), isler={"degisti": is_})

    def secenek(ogeler=None, secili=0, is_=None):
        if is_mi(secili) and is_ is None:
            is_, secili = secili, 0
        o = _liste(ogeler)
        secili = int(secili) if isinstance(secili, (int, float)) else (o.index(secili) if secili in o else 0)
        oge = yeni("secenek", o, secili, deger=o[secili] if 0 <= secili < len(o) else None, ogeler=o,
                   isler={"degisti": is_})
        p.durum[oge.uyeler["kimlik"]]["secili"] = secili
        return oge

    def liste(ogeler=None, satir=6, is_=None, cift=None):
        if is_mi(satir) and is_ is None:
            is_, satir = satir, 6
        o = _liste(ogeler)
        oge = yeni("liste", o, int(satir), deger=None, ogeler=o, isler={"degisti": is_, "gonder": cift})
        p.durum[oge.uyeler["kimlik"]]["secili"] = -1
        return oge

    def ilerleme(deger=0):
        return yeni("ilerleme", int(deger), deger=int(deger))

    def kaydirici(en_az=0, en_cok=100, deger=None, is_=None):
        if is_mi(deger) and is_ is None:
            is_, deger = deger, None
        deger = en_az if deger is None else deger
        return yeni("kaydirici", int(en_az), int(en_cok), int(deger), deger=int(deger), isler={"degisti": is_})

    def ayrac():
        return yeni("ayrac")

    def bosluk(piksel=10):
        return yeni("bosluk", int(piksel))

    def resim(yol, yukseklik=0):
        yol = _metin(yol)
        if not os.path.isabs(yol):
            yol = os.path.join(p.y.kok if getattr(p.y, "kok", None) else os.getcwd(), yol)
        return yeni("resim", yol, int(yukseklik), deger=yol)

    def satir_():
        return yeni("satir")

    def kutu(baslik_metni=""):
        return yeni("kutu", _metin(baslik_metni), deger=_metin(baslik_metni))

    return {
        "baslik": baslik, "title": baslik,
        "yazi": yazi, "label": yazi, "text": yazi,
        "dugme": dugme, "button": dugme,
        "giris": giris, "input": giris, "parola": parola, "password": parola,
        "onay": onay, "checkbox": onay,
        "secenek": secenek, "choice": secenek,
        "liste": liste, "list": liste,
        "ilerleme": ilerleme, "progress": ilerleme,
        "kaydirici": kaydirici, "slider": kaydirici,
        "ayrac": ayrac, "separator": ayrac,
        "bosluk": bosluk, "space": bosluk,
        "resim": resim, "image": resim,
        "satir": satir_, "row": satir_,
        "kutu": kutu, "box": kutu,
    }


def _pencere_alani(p):
    def calistir():
        return p.calistir()

    def kapat():
        p.acik = False
        return True

    def baslik_yaz(metin):
        p.yolla("baslikyaz", _metin(metin))
        return True

    def boyut(genislik, yukseklik):
        p.yolla("boyut", int(genislik), int(yukseklik))
        return True

    def temizle():
        p.yolla("temizle")
        p.durum.clear()
        p.isler.clear()
        return True

    def her(saniye, is_):
        if not cagrilabilir_mi(is_):
            raise AxsTypeError("her(saniye, is): ikinci deger bir is olmali")
        p.zamanlayicilar.append([time.monotonic() + float(saniye), max(0.02, float(saniye)), is_])
        return True

    def sonra(saniye, is_):
        if not cagrilabilir_mi(is_):
            raise AxsTypeError("sonra(saniye, is): ikinci deger bir is olmali")
        p.zamanlayicilar.append([time.monotonic() + float(saniye), 0, is_])
        return True

    def kapaninca(is_):
        p.kapanis_isi = is_
        return True

    def acik_mi():
        return p.acik

    uyeler = _kap_uyeleri(p, "-")
    uyeler.update({
        "calistir": calistir, "run": calistir, "goster": calistir,
        "kapat": kapat, "close": kapat,
        "baslik_yaz": baslik_yaz, "set_title": baslik_yaz,
        "boyut": boyut, "resize": boyut,
        "temizle": temizle, "clear": temizle,
        "her": her, "every": her, "sonra": sonra, "after": sonra,
        "kapaninca": kapaninca, "on_close": kapaninca,
        "acik_mi": acik_mi,
    })
    return isim_alani("pencere", uyeler)


@kutuphane("arayuz", "pencere", "gui")
def yukle(y):
    def pencere(baslik="Axs", genislik=480, yukseklik=360):
        return _pencere_alani(Pencere(y, _metin(baslik), genislik, yukseklik))

    def _yukseklik(metin, genislik):
        satir = sum(max(1, len(s) * 8 // max(1, genislik - 40) + 1) for s in _metin(metin).split("\n"))
        return satir * 20

    def _u(alan, ad):
        return alan.uyeler[ad].fn

    def mesaj(metin, baslik="Bilgi"):
        """Bilgi kutusu: Tamam'a basilana kadar bekler."""
        h = 116 + _yukseklik(metin, 400)
        p = Pencere(y, _metin(baslik), 400, min(h, 640))
        a = _pencere_alani(p)
        _u(a, "yazi")(metin)
        _u(a, "dugme")("Tamam", _kapatici(p), "birincil")
        p.calistir()
        return True

    def sor(soru, varsayilan="", baslik="Soru"):
        """Metin sorar; Tamam -> yazilan metin, Iptal/kapatma -> null."""
        sonuc = {"v": None}
        h = 170 + _yukseklik(soru, 420)
        p = Pencere(y, _metin(baslik), 420, min(h, 640))
        a = _pencere_alani(p)
        _u(a, "yazi")(soru)
        g = _u(a, "giris")("", _metin(varsayilan) if varsayilan is not None else "")

        def tamam(*_):
            sonuc["v"] = _u(g, "deger")()
            p.acik = False

        p.isler[g.uyeler["kimlik"]] = {"gonder": _py_is(tamam)}
        s = _u(a, "satir")()
        _u(s, "dugme")("İptal", _kapatici(p))
        _u(s, "dugme")("Tamam", _py_is(tamam), "birincil")
        p.calistir()
        return sonuc["v"]

    def onayla(soru, baslik="Onay", evet="Evet", hayir="Hayır"):
        """Evet/Hayir sorar: true ya da false."""
        sonuc = {"v": False}
        h = 116 + _yukseklik(soru, 400)
        p = Pencere(y, _metin(baslik), 400, min(h, 640))
        a = _pencere_alani(p)
        _u(a, "yazi")(soru)

        def e(*_):
            sonuc["v"] = True
            p.acik = False

        s = _u(a, "satir")()
        _u(s, "dugme")(_metin(hayir), _kapatici(p))
        _u(s, "dugme")(_metin(evet), _py_is(e), "birincil")
        p.calistir()
        return sonuc["v"]

    return isim_alani("arayuz", {
        "pencere": pencere, "window": pencere,
        "mesaj": mesaj, "message": mesaj,
        "sor": sor, "ask": sor,
        "onayla": onayla, "confirm": onayla,
    })


def _py_is(fn):
    """Python islevini Axs'in cagirabilecegi hazir ise cevirir."""
    return Gomulu("arayuz.is", lambda *a: fn(*a))


def _kapatici(p):
    def kapat(*_):
        p.acik = False
    return _py_is(kapat)
