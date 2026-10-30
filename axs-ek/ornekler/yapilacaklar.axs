# Yapılacaklar listesi: liste, giriş, dosyaya kaydetme
use arayuz

DOSYA = ortam("HOME", "/root") + "/yapilacaklar.txt"
isler = []
if dosya_var(DOSYA)
  for satir in oku(DOSYA).split("\n")
    if satir != ""
      isler.ekle(satir)
    end
  end
end

p = arayuz.pencere("Yapılacaklar", 440, 460)
p.baslik("Yapılacaklar")
durum = p.yazi("")
liste = p.liste(isler, 8)
yeni = p.giris("Yeni iş yaz, Enter'a bas")

func diske_yaz()
  kaydet(DOSYA, isler.join("\n"))
  durum.ayarla(metin(uzunluk(isler)) + " iş")
end

func ekle_is()
  if yeni.deger() != ""
    isler.ekle(yeni.deger())
    liste.ogeler(isler)
    yeni.ayarla("")
    diske_yaz()
  end
end

func bitir_is()
  i = liste.secili()
  if i >= 0
    isler.sil(isler[i])
    liste.ogeler(isler)
    diske_yaz()
  end
end

yeni.gonderilince(ekle_is)
s = p.satir()
s.dugme("Ekle", ekle_is, "birincil")
s.dugme("Seçileni tamamla", bitir_is)
durum.ayarla(metin(uzunluk(isler)) + " iş")
p.calistir()
