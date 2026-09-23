/*
 * ADP - AxsDE uygulama protokolü
 *
 * Dış grafik uygulamalar AxsDE'ye /run/axsde.sock (AF_UNIX, SOCK_SEQPACKET) ile
 * bağlanır. Pencere içeriği ortak bellektedir (memfd): uygulama çizer ve COMMIT
 * gönderir; AxsDE değişen bölgeyi kendi yüzeyine kopyalayıp ekrana getirir.
 *
 *   uygulama -> AxsDE                    AxsDE -> uygulama
 *   HELLO   a=gen b=yük  text=kimlik     CONFIGURE a=gen b=yük c=stride  (+ memfd)
 *   TITLE   text=başlık                  KEY       a=kod b=karakter c=mods d=basılı
 *   ICON    text=png yolu                MOUSE     a=tür b=x c=y d=tuş e=teker f=tık
 *   COMMIT  a,b,c,d = değişen bölge      FOCUS     a=1/0
 *   RESIZE  a=gen b=yük (istek)          CLOSE     (pencere kapatıldı: çık)
 *   QUIT                                 TICK      (her 250 ms, istenirse)
 *   WANT_TICK a=1/0
 */
#ifndef ADP_H
#define ADP_H

#include <stdint.h>

#define ADP_SOCKET "/run/axsde.sock"
#define ADP_TEXT 160

enum {
    ADP_HELLO = 1, ADP_TITLE, ADP_ICON, ADP_COMMIT, ADP_RESIZE, ADP_QUIT, ADP_WANT_TICK,
    ADP_CONFIGURE = 100, ADP_KEY, ADP_MOUSE, ADP_FOCUS, ADP_CLOSE, ADP_TICK,
};

typedef struct {
    uint32_t type;
    int32_t a, b, c, d, e, f;
    char text[ADP_TEXT];
} AdpMsg;

#endif
