# WTA merkezi: kurulu Windows programları, kurulum dosyaları ve araçlar
# (Axs + kolay arayüz ile yazıldı: /usr/lib/axs/docs/arayuz.md)
use arayuz

EV = ortam("HOME", "/root")

func kurulu_programlar()
  sonuc = []
  for satir in arayuz.komut("wta liste --ham", true).split("\n")
    parca = satir.split("\t")
    if uzunluk(parca) == 2
      sonuc.ekle(parca)
    end
  end
  return sonuc
end

func kurulum_dosyalari()
  sonuc = []
  for klasor in [EV + "/İndirilenler", EV + "/Masaüstü"]
    for ad in dosyalar(klasor)
      k = ad.lower()
      if k.ends(".exe") or k.ends(".msi") or k.ends(".bat")
        sonuc.ekle(klasor + "/" + ad)
      end
    end
  end
  return sonuc
end

p = arayuz.pencere("Windows Programları (WTA)", 560, 640)
p.baslik("Windows Programları")
surum = arayuz.komut("wta surum 2>/dev/null", true)
p.yazi("WTA (Windows To AxsOS) • " + surum + "\nBir .exe ya da .msi dosyasına çift tıklaman yeter, program Windows masaüstünde açılır.")

programlar = []
k1 = p.kutu("Kurulu programlar")
plist = k1.liste([], 5)
bosmesaj = k1.yazi("")

func program_ac()
  i = plist.secili()
  if i >= 0
    arayuz.komut("/var/lib/wta/kisayollar/" + programlar[i][0])
  end
end

func yenile()
  programlar = kurulu_programlar()
  adlar = []
  for pr in programlar
    adlar.ekle(pr[1])
  end
  plist.ogeler(adlar)
  if uzunluk(adlar) == 0
    bosmesaj.ayarla("Henüz kurulu program yok. Aşağıdan bir kurulum dosyası çalıştır.")
  else
    bosmesaj.ayarla("")
  end
  kurulumlar = kurulum_dosyalari()
  klist.ogeler(map(kurulumlar, func(y) -> y.split("/")[-1]))
end

plist.gonderilince(program_ac)
s1 = k1.satir()
s1.dugme("Aç", program_ac, "birincil")
s1.dugme("Yenile", yenile)

kurulumlar = []
k2 = p.kutu("İndirilenler ve Masaüstündeki kurulum dosyaları")
klist = k2.liste([], 3)

func kur()
  i = klist.secili()
  if i >= 0
    arayuz.komut("wta '" + kurulumlar[i] + "'")
  else
    arayuz.mesaj("Önce listeden bir dosya seç. .exe/.msi dosyalarını İndirilenler klasörüne indir (Firefox/Chrome ile) ya da masaüstüne kopyala.", "Kurulum")
  end
end

klist.gonderilince(kur)
k2.dugme("Seçileni çalıştır / kur", kur, "birincil")

k3 = p.kutu("Araçlar")
r1 = k3.satir()
r1.dugme("C: sürücüsü (gezgin)", func() -> arayuz.komut("wta gezgin"))
r1.dugme("Program kaldır", func() -> arayuz.komut("wta kaldir"))
r2 = k3.satir()
r2.dugme("Wine ayarları", func() -> arayuz.komut("wta ayarlar"))
r2.dugme(".NET / HTML bileşenleri", func() -> arayuz.komut("wta bilesenler"))
r3 = k3.satir()
r3.dugme("Not Defteri", func() -> arayuz.komut("wta notepad"))
r3.dugme("Hepsini kapat", func() -> arayuz.komut("wta kapat"), "tehlike")

yenile()
p.her(10, yenile)
p.calistir()
