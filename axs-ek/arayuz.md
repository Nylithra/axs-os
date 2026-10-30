# Kolay Arayüz — Axs ile pencereli program

AxsOS masaüstünde (AxsDE) pencereli bir program yazmak için `use arayuz` yeter.
Pencereyi sistem çizer; sen yalnızca öğeleri sırayla eklersin, düğmeye basılınca ne
olacağını bir **iş** (fonksiyon) ile söylersin.

```axs
use arayuz

p = arayuz.pencere("Merhaba", 420, 300)    # başlık, genişlik, yükseklik
p.baslik("Merhaba!")
ad = p.giris("Adın")
sonuc = p.yazi("")

func selamla()
  sonuc.ayarla("Merhaba " + ad.deger() + "!")
end

p.dugme("Selamla", selamla, "birincil")
p.calistir()                                # pencere kapanana kadar bekler
```

Çalıştırmak için: Axs Stüdyo'da **F5**, ya da masaüstündeki Terminal'de `axs dosya.axs`.
Masaüstünde sağ tık → **Yeni pencereli program** hazır bir şablon açar.
Örnekler: `/usr/lib/axs/examples/arayuz/` (merhaba, sayac, yapilacaklar, ogeler).

## Öğeler

Öğeler eklendikleri sırayla **alt alta** dizilir. Yan yana koymak için `satir()`,
çerçeveli bir bölüm için `kutu("Başlık")` kullan; ikisinin de içine aynı işlerle öğe eklenir.

| İş | Ne yapar |
|---|---|
| `baslik(metin)` | büyük başlık |
| `yazi(metin)` | metin (uzunsa satır kaydırır, `\n` ile yeni satır) |
| `dugme(metin, iş, stil)` | düğme; stil: `"normal"`, `"birincil"`, `"tehlike"`, `"sade"` |
| `giris(ipucu, değer, iş)` | tek satırlık metin alanı; `iş` her değişimde çağrılır |
| `parola(ipucu)` | yazılanı `•` ile gizleyen giriş |
| `onay(metin, true/false, iş)` | onay kutusu |
| `secenek(["A", "B"], seçili, iş)` | seçenek düğmeleri (biri seçili) |
| `liste(öğeler, satır, iş, çift_tık_işi)` | seçilebilir liste |
| `kaydirici(en_az, en_çok, değer, iş)` | kaydırıcı |
| `ilerleme(0-100)` | ilerleme çubuğu |
| `resim(yol, yükseklik)` | PNG/JPG resim |
| `ayrac()` · `bosluk(piksel)` | çizgi · boşluk |
| `satir()` · `kutu(başlık)` | kaplar |

İşler isteğe bağlı değeri alabilir: `func degisti(yeni)` yazarsan yeni değer gelir,
`func degisti()` yazarsan gelmez.

## Bir öğeyle konuşmak

Her ekleme işi öğeyi döndürür; sonra onunla konuşursun:

```axs
etiket = p.yazi("0")
etiket.ayarla("42")         # metni ya da değeri değiştir
giris.deger()               # şu anki değer (metin, true/false, sayı, seçili öğe)
liste.secili()              # seçili sıra (yoksa -1)
liste.ogeler(["a", "b"])    # öğeleri değiştir · liste.ekle("c")
dugme.etkin(false)          # pasif yap
etiket.gizle() / etiket.goster() / etiket.sil()
giris.odak()                # imleci bu alana koy
dugme.tiklaninca(is)  ·  giris.degisince(is)  ·  giris.gonderilince(is)   # Enter
```

## Pencere

```axs
p.her(1, tik)            # her 1 saniyede bir tik() (0.1 de olur)
p.sonra(3, kapat_isi)    # 3 saniye sonra bir kez
p.baslik_yaz("Yeni başlık")
p.boyut(600, 400)
p.temizle()              # tüm öğeleri sil (sayfa değiştirmek için)
p.kapaninca(is)          # pencere kapanınca
p.kapat()
p.acik_mi()
```

## Hazır kutular

Pencere kurmadan tek satırda soru sormak için:

```axs
arayuz.mesaj("Kaydedildi!")                    # Tamam'a basılana kadar bekler
ad = arayuz.sor("Adın ne?", "varsayılan")      # yazılan metin ya da null
if arayuz.onayla("Silinsin mi?")               # true / false
  ...
end
```

## Komut çalıştırmak

```axs
arayuz.komut("wta notepad")                 # arka planda başlat (pencere donmaz)
cikti = arayuz.komut("uname -r", true)      # bitmesini bekle, çıktısını al
```

## Nasıl çalışır?

`use arayuz`, `/usr/bin/arayuz` programını başlatır ve ona satır satır komut yollar
(`ekle dugme d1 - Kaydet birincil`); pencerede bir şey olunca `tik d1` gibi olaylar geri
gelir. Bu yüzden aynı pencereler bash'ten ya da başka dillerden de kurulabilir
(komutların listesi: `/usr/bin/arayuz` kaynağının başında, `apps/arayuz/main.c`).
