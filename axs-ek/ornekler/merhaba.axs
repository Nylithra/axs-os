# Kolay arayüz: ilk pencereli program
use arayuz

p = arayuz.pencere("Merhaba", 420, 300)
p.baslik("Merhaba!")
p.yazi("Adını yaz ve düğmeye bas (ya da Enter):")
ad = p.giris("Adın")
sonuc = p.yazi("")

func selamla()
  if ad.deger() == ""
    sonuc.ayarla("Önce adını yaz :)")
  else
    sonuc.ayarla("Merhaba " + ad.deger() + ", AxsOS'a hoş geldin!")
  end
end

ad.gonderilince(selamla)
p.dugme("Selamla", selamla, "birincil")
p.calistir()
