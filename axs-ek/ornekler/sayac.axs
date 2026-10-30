# Sayaç: düğmeler, yan yana satır, zamanlayıcı
use arayuz

p = arayuz.pencere("Sayaç", 380, 300)
saat = p.yazi("")
gosterge = p.baslik("0")
sayi = 0

func guncelle()
  gosterge.ayarla(sayi)
end

func arttir()
  sayi += 1
  guncelle()
end

func azalt()
  sayi -= 1
  guncelle()
end

func sifirla()
  sayi = 0
  guncelle()
end

s = p.satir()
s.dugme("−", azalt)
s.dugme("+", arttir, "birincil")
p.dugme("Sıfırla", sifirla, "sade")

acik_kalma = 0
func tik()
  saat.ayarla("Pencere " + metin(acik_kalma) + " saniyedir açık")
  acik_kalma += 1
end

tik()
p.her(1, tik)
p.calistir()
print: Son değer: sayi;
