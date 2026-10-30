# Tüm öğeler: onay, seçenek, kaydırıcı, ilerleme, kutu, mesaj/sor/onayla
use arayuz

p = arayuz.pencere("Kolay Arayüz Öğeleri", 520, 600)
p.baslik("Öğeler")

k = p.kutu("Ayarlar")
koyu = k.onay("Koyu tema", true)
boyut = k.secenek(["Küçük", "Orta", "Büyük"], 1)
ses = k.kaydirici(0, 100, 40)
bilgi = k.yazi("")

func goster()
  t = "Tema: "
  if koyu.deger()
    t = t + "koyu"
  else
    t = t + "açık"
  end
  bilgi.ayarla(t + " • Boyut: " + boyut.deger() + " • Ses: " + metin(ses.deger()))
end

koyu.degisince(goster)
boyut.degisince(goster)
ses.degisince(goster)
goster()

p.ayrac()
cubuk = p.ilerleme(0)
yuzde = 0
func ilerlet()
  yuzde += 5
  if yuzde > 100
    yuzde = 0
  end
  cubuk.ayarla(yuzde)
end
p.her(0.2, ilerlet)

func sor_ad()
  ad = arayuz.sor("Adın ne?", "", "Tanışalım")
  if ad != null
    arayuz.mesaj("Merhaba " + ad + "!", "Selam")
  end
end

func cik()
  if arayuz.onayla("Programdan çıkılsın mı?")
    p.kapat()
  end
end

s = p.satir()
s.dugme("Adımı sor", sor_ad)
s.dugme("Çık", cik, "tehlike")
p.calistir()
