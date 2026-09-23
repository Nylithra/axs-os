# 03 - İşler (fonksiyonlar)
func selamla(kisi, selam = "Merhaba")
  return selam + ", " + kisi + "!"
end

print(selamla("Ayşe"))
print(selamla("Mehmet", "Selam"))

func faktoriyel(n)
  if n <= 1
    return 1
  end
  return n * faktoriyel(n - 1)
end

print: 10! = (faktoriyel(10));
