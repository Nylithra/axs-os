# AxsOS - pinned upstream component versions
KERNEL_VERSION=6.12.111      # Linux LTS
BUSYBOX_VERSION=1.36.1       # statik, musl ile
ZLIB_VERSION=1.3.2
OPENSSL_VERSION=3.5.8        # Python ssl modülü için (Axs açılışta ssl yükler)
SQLITE_VERSION=3.53.4       # Axs connect(veri.db) için
PYTHON_VERSION=3.13.15       # Axs yorumlayıcısının çalışma zamanı
AXS_REPO=https://github.com/Nylithra/axs
AXS_REF=918fe0559287685fbfa2857df9fd2d2286ad8274  # Axs dili (commit veya dal)
# AxsDE masaüstü
STB_REF=2c980bb59875b0d32144a71867fbdebb2f77cd20   # nothings/stb (stb_truetype)
INTER_VERSION=v4.1           # rsms/inter (arayüz yazı tipi, OFL)
JBMONO_VERSION=v2.304        # JetBrains/JetBrainsMono (terminal/editör yazı tipi, OFL)
NOVNC_VERSION=v1.6.0          # tarayıcıdan masaüstü (./run.sh gui, ekran yoksa)
