/*
 * libaxsapp - AxsDE için grafik uygulama kütüphanesi
 *
 * Uygulama, AxsDE ile aynı çizim/yazı/arayüz işlevlerini kullanır (axsde.h):
 * fill_rrect, draw_text, ui_button, draw_icon ... Pencere içeriği ortak bellektedir.
 *
 * En kolay kullanım:
 *
 *     static void draw(Surf *s, UiState *u) {
 *         fill_rect(s, (Rect){0, 0, s->w, s->h}, T.surface);
 *         if (ui_button(s, u, (Rect){20, 20, 120, 36}, "Merhaba", BTN_PRIMARY))
 *             axsapp_title("Tıklandı!");
 *     }
 *     int main(void) {
 *         AxsAppFuncs f = { .draw = draw };
 *         return axsapp_run("ornek", "Örnek", 400, 300, &f);
 *     }
 */
#ifndef AXSAPP_H
#define AXSAPP_H

#include "../axsde.h"

typedef enum { AE_NONE, AE_KEY, AE_MOUSE, AE_RESIZE, AE_FOCUS, AE_CLOSE } AxsEventType;

typedef struct {
    AxsEventType type;
    KeyEv key;
    MouseEv mouse;
    int focused;
} AxsEvent;

/* Alt düzey API */
int   axsapp_open(const char *id, const char *title, int w, int h); /* 0 = tamam */
Surf *axsapp_surf(void);
void  axsapp_commit(Rect r);              /* {0,0,0,0} = tamamı */
void  axsapp_title(const char *title);
void  axsapp_request_size(int w, int h);
int   axsapp_next(AxsEvent *ev, int timeout_ms); /* 1 olay, 0 zaman aşımı, -1 kapandı */
int   axsapp_focused(void);
int   axsapp_fd(void);
void  axsapp_close(void);

/* Kolay döngü */
typedef struct {
    void (*draw)(Surf *s, UiState *u);   /* her yeniden çizimde (tıklamalar burada işlenir) */
    void (*key)(KeyEv *e);               /* klavye (isteğe bağlı) */
    void (*mouse)(MouseEv *e);           /* ham fare olayı (isteğe bağlı) */
    void (*timer)(void);                 /* interval_ms'de bir (oyunlar için) */
    int interval_ms;
    void (*resized)(int w, int h);
} AxsAppFuncs;

int  axsapp_run(const char *id, const char *title, int w, int h, const AxsAppFuncs *f);
void axsapp_redraw(void);                /* bir sonraki turda yeniden çiz */
void axsapp_redraw_rect(Rect r);         /* yalnızca bu bölgeyi çiz (draw() kırpılır) */
void axsapp_set_interval(int ms);        /* zamanlayıcı aralığını değiştir (0 = kapalı) */
void axsapp_quit(void);

#endif
