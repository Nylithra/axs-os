/* arayuz - AxsOS kolay arayüz sunucusu
 *
 * Herhangi bir program (Axs'in "use arayuz" kütüphanesi, bash betiği...) bu programı başlatıp
 * stdin'e satır satır komut yazarak pencere kurar; olaylar stdout'a satır satır gelir.
 * Alanlar TAB ile ayrılır; alan içinde \n, \t ve \\ kaçış dizisi olarak yazılır.
 *
 * Komutlar:
 *   ac <başlık> <genişlik> <yükseklik>        pencereyi aç (ilk komut)
 *   ekle <tür> <kimlik> <üst> [değerler...]     öğe ekle (üst: "-" ya da bir satır/kutu kimliği)
 *     baslik <metin>            büyük başlık
 *     yazi <metin>              metin (sözcük kaydırmalı, \n ile satır)
 *     dugme <metin> [stil]      düğme (stil: normal | birincil | tehlike | sade)
 *     giris <ipucu> <değer> [gizli]   tek satırlık metin alanı (gizli=1: parola)
 *     onay <metin> <0|1>        onay kutusu
 *     secenek <a|b|c> <seçili>  seçenek düğmeleri (biri seçili)
 *     liste <a|b|c> <satır>     seçilebilir liste (<satır> kadar yükseklik)
 *     ilerleme <0-100>          ilerleme çubuğu
 *     kaydirici <en az> <en çok> <değer>
 *     ayrac                     yatay çizgi
 *     bosluk <piksel>
 *     resim <yol> [yükseklik]   PNG/JPG
 *     satir                     yan yana öğeler için kap (üstü bu kimlik olanlar yan yana dizilir)
 *     kutu <başlık>             çerçeveli bölüm (içindekiler alt alta)
 *   ayarla <kimlik> <alan> <değer>     alan: metin | deger | ogeler | etkin | gorunur | stil
 *   sil <kimlik>  |  temizle  |  baslikyaz <başlık>  |  boyut <g> <y>  |  odak <kimlik>  |  kapat
 *
 * Olaylar:
 *   hazir                       pencere açıldı
 *   tik <kimlik>                düğmeye basıldı
 *   degisti <kimlik> <değer>    giriş/onay/seçenek/liste/kaydırıcı değişti
 *   gonder <kimlik> <değer>     girişte Enter'a basıldı
 *   kapandi                     pencere kapatıldı
 */
#define _GNU_SOURCE
#include "axsapp.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXW  400
#define PAD   20
#define GAP   12

typedef enum { W_BASLIK, W_YAZI, W_DUGME, W_GIRIS, W_ONAY, W_SECENEK, W_LISTE, W_ILERLEME,
               W_KAYDIRICI, W_AYRAC, W_BOSLUK, W_RESIM, W_SATIR, W_KUTU } WType;

static const char *TYPES[] = { "baslik", "yazi", "dugme", "giris", "onay", "secenek", "liste", "ilerleme",
                               "kaydirici", "ayrac", "bosluk", "resim", "satir", "kutu" };

typedef struct {
    int used;
    WType type;
    char id[48];
    int parent;              /* -1 kök */
    char *text;              /* metin / ipucu / başlık */
    char *opts[64];          /* seçenekler */
    int nopts;
    int value, vmin, vmax;
    int style, secret;
    int enabled, visible;
    int rows, scroll;
    TextField tf;
    Surf img;
    Rect r;                  /* son yerleşim (içerik koordinatı, kaydırma dahil) */
} Wg;

static Wg wg[MAXW];
static int nwg;
static int opened, focus_i = -1, scroll_y, content_h, drag_slider = -1;
static char title[128] = "Axs";
static int win_w = 480, win_h = 360;

/* ---------------------------------------------------------------- */
/* Çıktı                                                              */
/* ---------------------------------------------------------------- */

static void put_esc(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n') fputs("\\n", stdout);
        else if (*s == '\t') fputs("\\t", stdout);
        else if (*s == '\\') fputs("\\\\", stdout);
        else putchar(*s);
    }
}

static void emit(const char *ev, const Wg *w, const char *val)
{
    fputs(ev, stdout);
    if (w) {
        putchar('\t');
        put_esc(w->id);
    }
    if (val) {
        putchar('\t');
        put_esc(val);
    }
    putchar('\n');
    fflush(stdout);
}

static void emit_int(const char *ev, const Wg *w, int v)
{
    char b[24];
    snprintf(b, sizeof b, "%d", v);
    emit(ev, w, b);
}

/* ---------------------------------------------------------------- */
/* Öğeler                                                             */
/* ---------------------------------------------------------------- */

static int find(const char *id)
{
    for (int i = 0; i < nwg; i++)
        if (wg[i].used && !strcmp(wg[i].id, id))
            return i;
    return -1;
}

static void set_opts(Wg *w, const char *s)
{
    for (int i = 0; i < w->nopts; i++)
        free(w->opts[i]);
    w->nopts = 0;
    if (!s || !*s)
        return;
    const char *p = s;
    while (w->nopts < 64) {
        const char *q = strchr(p, '|');
        int n = q ? (int)(q - p) : (int)strlen(p);
        w->opts[w->nopts++] = strndup(p, n);
        if (!q)
            break;
        p = q + 1;
    }
}

static void set_text(Wg *w, const char *s)
{
    free(w->text);
    w->text = strdup(s ? s : "");
}

static void wg_free(Wg *w)
{
    free(w->text);
    set_opts(w, NULL);
    surf_free(&w->img);
    memset(w, 0, sizeof *w);
}

static void remove_tree(int i)
{
    for (int k = 0; k < nwg; k++)
        if (wg[k].used && wg[k].parent == i)
            remove_tree(k);
    if (focus_i == i)
        focus_i = -1;
    wg_free(&wg[i]);
}

static int focusable(Wg *w) { return w->used && w->visible && w->enabled && w->type == W_GIRIS; }

static void set_focus(int i)
{
    if (focus_i >= 0 && focus_i < nwg)
        wg[focus_i].tf.focus = 0;
    focus_i = i;
    if (i >= 0)
        wg[i].tf.focus = 1;
}

static int to_int(const char *s, int d) { return s && *s ? atoi(s) : d; }

static void apply_value(Wg *w, const char *v)
{
    switch (w->type) {
    case W_GIRIS: tf_set(&w->tf, v); break;
    case W_ONAY: w->value = to_int(v, 0) != 0; break;
    case W_SECENEK: case W_LISTE: w->value = clampi(to_int(v, 0), -1, maxi(0, w->nopts - 1)); break;
    case W_ILERLEME: w->value = clampi(to_int(v, 0), 0, 100); break;
    case W_KAYDIRICI: w->value = clampi(to_int(v, 0), w->vmin, w->vmax); break;
    case W_BOSLUK: w->value = to_int(v, 10); break;
    default: set_text(w, v); break;
    }
}

static int style_of(const char *s)
{
    if (!s) return BTN_NORMAL;
    if (!strcmp(s, "birincil") || !strcmp(s, "primary")) return BTN_PRIMARY;
    if (!strcmp(s, "tehlike") || !strcmp(s, "danger")) return BTN_DANGER;
    if (!strcmp(s, "sade") || !strcmp(s, "ghost")) return BTN_GHOST;
    return BTN_NORMAL;
}

static void add(char **f, int n)
{
    if (n < 3)
        return;
    int t = -1;
    for (int i = 0; i < (int)(sizeof TYPES / sizeof TYPES[0]); i++)
        if (!strcmp(f[0], TYPES[i]))
            t = i;
    if (t < 0) {
        fprintf(stderr, "arayuz: bilinmeyen öğe türü: %s\n", f[0]);
        return;
    }
    int old = find(f[1]);
    if (old >= 0)
        remove_tree(old);
    int i = -1;
    for (int k = 0; k < nwg; k++)
        if (!wg[k].used) { i = k; break; }
    if (i < 0) {
        if (nwg >= MAXW)
            return;
        i = nwg++;
    }
    Wg *w = &wg[i];
    memset(w, 0, sizeof *w);
    w->used = 1;
    w->type = t;
    snprintf(w->id, sizeof w->id, "%s", f[1]);
    w->parent = strcmp(f[2], "-") ? find(f[2]) : -1;
    w->enabled = w->visible = 1;
    const char *a = n > 3 ? f[3] : "", *b = n > 4 ? f[4] : "", *c = n > 5 ? f[5] : "";
    switch (w->type) {
    case W_BASLIK: case W_YAZI: case W_KUTU: set_text(w, a); break;
    case W_DUGME: set_text(w, a); w->style = style_of(b); break;
    case W_GIRIS: set_text(w, a); tf_set(&w->tf, b); w->secret = to_int(c, 0); break;
    case W_ONAY: set_text(w, a); w->value = to_int(b, 0) != 0; break;
    case W_SECENEK: set_opts(w, a); w->value = clampi(to_int(b, 0), 0, maxi(0, w->nopts - 1)); break;
    case W_LISTE: set_opts(w, a); w->rows = clampi(to_int(b, 6), 2, 30); w->value = -1; break;
    case W_ILERLEME: w->value = clampi(to_int(a, 0), 0, 100); break;
    case W_KAYDIRICI:
        w->vmin = to_int(a, 0);
        w->vmax = maxi(w->vmin + 1, to_int(b, 100));
        w->value = clampi(to_int(c, w->vmin), w->vmin, w->vmax);
        break;
    case W_BOSLUK: w->value = to_int(a, 10); break;
    case W_RESIM:
        set_text(w, a);
        w->rows = to_int(b, 0);
        if (img_load(a, &w->img) != 0)
            fprintf(stderr, "arayuz: resim açılamadı: %s\n", a);
        break;
    default: break;
    }
    if (w->type == W_GIRIS && focus_i < 0)
        set_focus(i);
}

static void set_field(char **f, int n)
{
    if (n < 3)
        return;
    int i = find(f[0]);
    if (i < 0)
        return;
    Wg *w = &wg[i];
    const char *k = f[1], *v = f[2];
    if (!strcmp(k, "metin")) {
        if (w->type == W_GIRIS) tf_set(&w->tf, v); else set_text(w, v);
    } else if (!strcmp(k, "deger")) apply_value(w, v);
    else if (!strcmp(k, "ogeler")) { set_opts(w, v); w->value = clampi(w->value, -1, w->nopts - 1); w->scroll = 0; }
    else if (!strcmp(k, "ipucu")) set_text(w, v);
    else if (!strcmp(k, "etkin")) w->enabled = to_int(v, 1) != 0;
    else if (!strcmp(k, "gorunur")) w->visible = to_int(v, 1) != 0;
    else if (!strcmp(k, "stil")) w->style = style_of(v);
    else if (!strcmp(k, "en_az")) w->vmin = to_int(v, 0);
    else if (!strcmp(k, "en_cok")) w->vmax = to_int(v, 100);
}

/* ---------------------------------------------------------------- */
/* Komutlar (stdin)                                                   */
/* ---------------------------------------------------------------- */

static void unesc(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) {
            s++;
            *o++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static void command(char *line)
{
    char *f[12];
    int n = 0;
    char *p = line;
    while (n < 12) {
        f[n++] = p;
        char *t = strchr(p, '\t');
        if (!t)
            break;
        *t = 0;
        p = t + 1;
    }
    for (int i = 0; i < n; i++)
        unesc(f[i]);
    const char *c = f[0];
    if (!strcmp(c, "ac")) {
        if (n > 1) snprintf(title, sizeof title, "%s", f[1]);
        if (n > 2) win_w = clampi(to_int(f[2], 480), 200, 1600);
        if (n > 3) win_h = clampi(to_int(f[3], 360), 120, 1000);
        opened = 1;
    } else if (!strcmp(c, "ekle")) {
        add(f + 1, n - 1);
    } else if (!strcmp(c, "ayarla")) {
        set_field(f + 1, n - 1);
    } else if (!strcmp(c, "sil") && n > 1) {
        int i = find(f[1]);
        if (i >= 0)
            remove_tree(i);
    } else if (!strcmp(c, "temizle")) {
        for (int i = 0; i < nwg; i++)
            if (wg[i].used)
                wg_free(&wg[i]);
        nwg = 0;
        focus_i = -1;
        scroll_y = 0;
    } else if (!strcmp(c, "baslikyaz") && n > 1) {
        snprintf(title, sizeof title, "%s", f[1]);
        axsapp_title(title);
    } else if (!strcmp(c, "boyut") && n > 2) {
        axsapp_request_size(clampi(to_int(f[1], 480), 200, 1600), clampi(to_int(f[2], 360), 120, 1000));
    } else if (!strcmp(c, "odak") && n > 1) {
        int i = find(f[1]);
        if (i >= 0 && focusable(&wg[i]))
            set_focus(i);
    } else if (!strcmp(c, "kapat")) {
        axsapp_quit();
    }
}

static char inbuf[1 << 16];
static size_t inlen;
static int in_eof;

/* stdin'den gelen tüm tam satırları işle; 0 = kapandı (EOF) */
static int read_input(void)
{
    for (;;) {
        if (inlen >= sizeof inbuf - 1)
            inlen = 0;   /* çok uzun satır: at */
        ssize_t r = read(0, inbuf + inlen, sizeof inbuf - 1 - inlen);
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            in_eof = 1;
            break;
        }
        inlen += r;
    }
    char *start = inbuf, *nl;
    while ((nl = memchr(start, '\n', inbuf + inlen - start))) {
        *nl = 0;
        if (nl > start && nl[-1] == '\r')
            nl[-1] = 0;
        command(start);
        start = nl + 1;
    }
    inlen = inbuf + inlen - start;
    memmove(inbuf, start, inlen);
    return !in_eof;
}

static void on_stdin(void)
{
    if (!read_input())
        axsapp_quit();   /* program bitti: pencereyi kapat */
    axsapp_redraw();
}

/* ---------------------------------------------------------------- */
/* Yerleşim ve çizim                                                  */
/* ---------------------------------------------------------------- */

/* sözcük kaydırma: satırları çizer (s NULL ise yalnızca sayar) */
static int wrap(Surf *s, FontId f, int size, int x, int y, int maxw, int lh, uint32_t col, const char *txt)
{
    int lines = 0;
    const char *p = txt;
    while (*p || lines == 0) {
        const char *q = p, *brk = NULL;
        while (*q && *q != '\n') {
            const char *nq = q;
            utf8_next(&nq);
            if (text_width_n(f, size, p, (int)(nq - p)) > maxw)
                break;
            if (*q == ' ')
                brk = q;
            q = nq;
        }
        if (*q && *q != '\n' && brk)
            q = brk;
        if (q == p && *q && *q != '\n') {   /* tek karakter bile sığmıyor */
            utf8_next(&q);
        }
        if (s)
            draw_text_n(s, f, size, x, y + lines * lh, col, p, (int)(q - p));
        lines++;
        p = q;
        if (*p == ' ' || *p == '\n')
            p++;
        if (!*p)
            break;
    }
    return lines;
}

static int child_count(int parent)
{
    int n = 0;
    for (int i = 0; i < nwg; i++)
        if (wg[i].used && wg[i].visible && wg[i].parent == parent)
            n++;
    return n;
}

static int layout(int parent, int x, int y, int w);

static int wg_height(Wg *g, int w)
{
    switch (g->type) {
    case W_BASLIK: return wrap(NULL, F_UI_BOLD, 22, 0, 0, w, 30, 0, g->text) * 30;
    case W_YAZI: return wrap(NULL, F_UI, 14, 0, 0, w, 20, 0, g->text) * 20;
    case W_DUGME: return 38;
    case W_GIRIS: return 40;
    case W_ONAY: return 26;
    case W_SECENEK: return 36;
    case W_LISTE: return g->rows * 30 + 4;
    case W_ILERLEME: return 12;
    case W_KAYDIRICI: return 28;
    case W_AYRAC: return 9;
    case W_BOSLUK: return maxi(0, g->value - GAP);
    case W_RESIM: {
        if (!g->img.px)
            return 24;
        int h = g->img.h * mini(w, g->img.w) / maxi(1, g->img.w);
        return g->rows > 0 ? mini(h, g->rows) : h;
    }
    default: return 0;
    }
}

/* öğeleri yerleştir; kullanılan yüksekliği döndürür */
static int layout(int parent, int x, int y, int w)
{
    int cy = y, first = 1;
    for (int i = 0; i < nwg; i++) {
        Wg *g = &wg[i];
        if (!g->used || !g->visible || g->parent != parent)
            continue;
        if (!first)
            cy += GAP;
        first = 0;
        if (g->type == W_SATIR) {
            int n = child_count(i);
            int cw = n ? (w - GAP * (n - 1)) / n : w, k = 0, h = 0;
            for (int j = 0; j < nwg; j++) {
                Wg *c = &wg[j];
                if (!c->used || !c->visible || c->parent != i)
                    continue;
                int ch;
                if (c->type == W_SATIR) {
                    ch = layout(j, x + k * (cw + GAP), cy, cw);   /* iç içe satır */
                    c->r = (Rect){ x + k * (cw + GAP), cy, cw, ch };
                } else if (c->type == W_KUTU) {
                    int th = c->text && c->text[0] ? 26 : 0;
                    ch = layout(j, x + k * (cw + GAP) + 16, cy + 14 + th, cw - 32) + 28 + th;
                    c->r = (Rect){ x + k * (cw + GAP), cy, cw, ch };
                } else {
                    ch = wg_height(c, cw);
                    c->r = (Rect){ x + k * (cw + GAP), cy, cw, ch };
                }
                h = maxi(h, ch);
                k++;
            }
            g->r = (Rect){ x, cy, w, h };
            cy += h;
        } else if (g->type == W_KUTU) {
            int th = g->text && g->text[0] ? 26 : 0;
            int h = layout(i, x + 16, cy + 14 + th, w - 32);
            g->r = (Rect){ x, cy, w, h + 28 + th };
            cy += g->r.h;
        } else {
            int h = wg_height(g, w);
            g->r = (Rect){ x, cy, w, h };
            cy += h;
        }
    }
    return cy - y;
}

static Rect shown(Rect r) { return (Rect){ r.x, r.y - scroll_y, r.w, r.h }; }

static void slider_set(Wg *g, int mx)
{
    Rect r = shown(g->r);
    int v = g->vmin + (int)((long)(mx - r.x - 8) * (g->vmax - g->vmin) / maxi(1, r.w - 16));
    v = clampi(v, g->vmin, g->vmax);
    if (v != g->value) {
        g->value = v;
        emit_int("degisti", g, v);
    }
}

static void draw_wg(Surf *s, UiState *u, int i)
{
    Wg *g = &wg[i];
    Rect r = shown(g->r);
    uint32_t tc = g->enabled ? T.text : T.muted;
    switch (g->type) {
    case W_BASLIK: wrap(s, F_UI_BOLD, 22, r.x, r.y, r.w, 30, tc, g->text); break;
    case W_YAZI: wrap(s, F_UI, 14, r.x, r.y, r.w, 20, g->enabled ? T.subtext : T.muted, g->text); break;
    case W_DUGME:
        if (!g->enabled) {
            fill_rrect(s, r, 10, ALPHA(T.overlay, 120));
            draw_text_center(s, F_UI, 14, r, T.muted, g->text);
        } else if (ui_button(s, u, r, g->text, g->style)) {
            emit("tik", g, NULL);
        }
        break;
    case W_GIRIS: {
        if (g->secret) {
            TextField t = g->tf;
            int n = 0;
            for (const char *p = g->tf.buf; *p; ) { utf8_next(&p); n++; }
            memset(t.buf, 0, sizeof t.buf);
            for (int k = 0; k < n && k < 200; k++) strcat(t.buf, "•");
            t.len = (int)strlen(t.buf);
            int cn = 0;
            for (const char *p = g->tf.buf; p < g->tf.buf + g->tf.cur; ) { utf8_next(&p); cn++; }
            t.cur = cn * (int)strlen("•");
            tf_draw(s, &t, r, g->text);
        } else {
            tf_draw(s, &g->tf, r, g->text);
        }
        if (g->enabled && ui_clicked(u, r) && focus_i != i)
            set_focus(i);
        break;
    }
    case W_ONAY: {
        Rect b = { r.x, r.y + 2, 22, 22 };
        int hov = ui_hover(u, r);
        fill_rrect(s, b, 6, g->value ? T.accent : (hov ? T.overlay : T.surface2));
        stroke_rrect(s, b, 6, 1, g->value ? T.accent : T.border);
        if (g->value) {
            draw_line(s, b.x + 5, b.y + 11, b.x + 9.5f, b.y + 15.5f, 2.4f, HEX(0xFFFFFF));
            draw_line(s, b.x + 9.5f, b.y + 15.5f, b.x + 17, b.y + 6.5f, 2.4f, HEX(0xFFFFFF));
        }
        draw_text_fit(s, F_UI, 14, r.x + 32, r.y + 4, r.w - 32, tc, g->text);
        if (g->enabled && ui_clicked(u, r)) {
            g->value = !g->value;
            emit_int("degisti", g, g->value);
        }
        break;
    }
    case W_SECENEK: {
        fill_rrect(s, r, 10, T.surface2);
        int n = maxi(1, g->nopts);
        for (int k = 0; k < g->nopts; k++) {
            Rect o = { r.x + 3 + k * (r.w - 6) / n, r.y + 3, (r.w - 6) / n, r.h - 6 };
            int on = k == g->value, hov = ui_hover(u, o);
            if (on)
                fill_rrect(s, o, 8, T.accent);
            else if (hov && g->enabled)
                fill_rrect(s, o, 8, T.overlay);
            draw_text_center(s, on ? F_UI_BOLD : F_UI, 13, o, on ? HEX(0xFFFFFF) : tc, g->opts[k]);
            if (g->enabled && ui_clicked(u, o) && !on) {
                g->value = k;
                emit("degisti", g, g->opts[k]);
            }
        }
        break;
    }
    case W_LISTE: {
        fill_rrect(s, r, 10, T.base);
        stroke_rrect(s, r, 10, 1, T.border);
        Rect oc = s->clip;
        surf_clip(s, rect_isect(oc, rect_inset(r, 2)));
        g->scroll = clampi(g->scroll, 0, maxi(0, g->nopts - g->rows));
        for (int k = 0; k < g->rows && k + g->scroll < g->nopts; k++) {
            int idx = k + g->scroll;
            Rect o = { r.x + 4, r.y + 2 + k * 30, r.w - 8, 30 };
            int on = idx == g->value, hov = ui_hover(u, o);
            if (on)
                fill_rrect(s, o, 7, ALPHA(T.accent, 150));
            else if (hov)
                fill_rrect(s, o, 7, T.surface2);
            draw_text_fit(s, F_UI, 14, o.x + 10, o.y + 6, o.w - 20, tc, g->opts[idx]);
            if (g->enabled && ui_clicked(u, o)) {
                g->value = idx;
                emit("degisti", g, g->opts[idx]);
                if (u->clicks == 2)
                    emit("gonder", g, g->opts[idx]);
            }
        }
        s->clip = oc;
        if (g->nopts > g->rows)
            ui_scrollbar(s, (Rect){ r.x + r.w - 8, r.y + 4, 4, r.h - 8 }, g->nopts, g->rows, g->scroll);
        break;
    }
    case W_ILERLEME: ui_progress(s, r, g->value / 100.0f, T.accent); break;
    case W_KAYDIRICI: {
        Rect tr = { r.x + 8, r.y + 12, r.w - 16, 5 };
        fill_rrect(s, tr, 2, T.overlay);
        float v = (float)(g->value - g->vmin) / maxi(1, g->vmax - g->vmin);
        fill_rrect(s, (Rect){ tr.x, tr.y, (int)(tr.w * v), tr.h }, 2, T.accent);
        float kx = tr.x + tr.w * v;
        fill_circle(s, kx, tr.y + 2.5f, 9, HEX(0xFFFFFF));
        stroke_circle(s, kx, tr.y + 2.5f, 9, 2, T.accent);
        ui_hover(u, r);
        if (g->enabled && u->pressed && rect_has(r, u->mx, u->my)) {
            drag_slider = i;
            slider_set(g, u->mx);
        }
        break;
    }
    case W_AYRAC: fill_rect(s, (Rect){ r.x, r.y + 4, r.w, 1 }, T.border); break;
    case W_RESIM:
        if (g->img.px) {
            int dw = mini(r.w, g->img.w), dh = g->img.h * dw / maxi(1, g->img.w);
            if (dh > r.h) { dh = r.h; dw = g->img.w * dh / maxi(1, g->img.h); }
            /* en yakın komşu ölçekleme */
            Rect k = rect_isect((Rect){ r.x, r.y, dw, dh }, s->clip);
            for (int yy = k.y; yy < k.y + k.h; yy++) {
                const uint32_t *sp = &g->img.px[(long)(yy - r.y) * g->img.h / maxi(1, dh) * g->img.stride];
                uint32_t *dp = &s->px[yy * s->stride];
                for (int xx = k.x; xx < k.x + k.w; xx++) {
                    uint32_t p = sp[(long)(xx - r.x) * g->img.w / maxi(1, dw)];
                    dp[xx] = (p >> 24) == 255 ? p : mix(dp[xx], p | 0xFF000000u, (int)(p >> 24));
                }
            }
        } else {
            draw_text(s, F_UI, 13, r.x, r.y + 4, T.muted, "(resim açılamadı)");
        }
        break;
    case W_KUTU:
        fill_rrect(s, r, 12, T.base);
        stroke_rrect(s, r, 12, 1, T.border);
        if (g->text && g->text[0])
            draw_text(s, F_UI_BOLD, 13, r.x + 16, r.y + 14, T.subtext, g->text);
        break;
    default: break;
    }
}

static void draw_tree(Surf *s, UiState *u, int parent)
{
    for (int i = 0; i < nwg; i++) {
        Wg *g = &wg[i];
        if (!g->used || !g->visible || g->parent != parent)
            continue;
        if (g->type != W_SATIR)
            draw_wg(s, u, i);
        if (g->type == W_SATIR || g->type == W_KUTU)
            draw_tree(s, u, i);
    }
}

static void draw(Surf *s, UiState *u)
{
    fill_rect(s, (Rect){ 0, 0, s->w, s->h }, T.surface);
    content_h = layout(-1, PAD, PAD, s->w - PAD * 2) + PAD * 2;
    scroll_y = clampi(scroll_y, 0, maxi(0, content_h - s->h));
    if (drag_slider >= 0 && drag_slider < nwg && wg[drag_slider].used) {
        if (u->down)
            slider_set(&wg[drag_slider], u->mx);
        else
            drag_slider = -1;
    }
    draw_tree(s, u, -1);
    if (content_h > s->h)
        ui_scrollbar(s, (Rect){ s->w - 8, 4, 4, s->h - 8 }, content_h, s->h, scroll_y);
}

static Wg *list_at(int x, int y)
{
    for (int i = 0; i < nwg; i++)
        if (wg[i].used && wg[i].visible && wg[i].type == W_LISTE && rect_has(shown(wg[i].r), x, y))
            return &wg[i];
    return NULL;
}

static void mouse(MouseEv *e)
{
    if (e->kind == M_WHEEL) {
        Wg *l = list_at(e->x, e->y);
        if (l && l->nopts > l->rows)
            l->scroll -= e->wheel * 2;
        else
            scroll_y -= e->wheel * 40;
        axsapp_redraw();
    } else if (e->kind == M_MOVE && drag_slider >= 0) {
        axsapp_redraw();
    }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    if (e->code == KEY_TAB) {   /* sonraki giriş alanı */
        int start = focus_i, n = nwg;
        for (int k = 1; k <= n; k++) {
            int i = ((start < 0 ? -1 : start) + ((e->mods & MOD_SHIFT) ? -k : k) + n * 2) % n;
            if (focusable(&wg[i])) {
                set_focus(i);
                break;
            }
        }
        return;
    }
    if (focus_i < 0 || !focusable(&wg[focus_i]))
        return;
    Wg *g = &wg[focus_i];
    int r = tf_key(&g->tf, e);
    if (r == 2)
        emit("gonder", g, g->tf.buf);
    else if (r == 1 && e->code != KEY_LEFT && e->code != KEY_RIGHT && e->code != KEY_HOME && e->code != KEY_END)
        emit("degisti", g, g->tf.buf);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* "ac" komutunu bekle (pencere boyutu ve başlık); en fazla 10 sn */
    if (argc > 1)
        snprintf(title, sizeof title, "%s", argv[1]);
    long t0 = now_ms();
    while (!opened && !in_eof && now_ms() - t0 < 10000) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(0, &fs);
        struct timeval tv = { 0, 100000 };
        if (select(1, &fs, NULL, NULL, &tv) > 0) {
            fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
            read_input();
        }
    }
    if (in_eof && !opened && !nwg)
        return 0;
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    const char *app = getenv("AXSDE_APP_ID");
    axsapp_watch_fd(0, on_stdin);
    emit("hazir", NULL, NULL);
    AxsAppFuncs f = { .draw = draw, .key = key, .mouse = mouse };
    int r = axsapp_run(app && *app ? app : "arayuz", title, win_w, win_h, &f);
    if (r)
        fprintf(stderr, "arayuz: AxsDE masaüstüne bağlanılamadı (masaüstü açık mı?)\n");
    emit("kapandi", NULL, NULL);
    return r;
}
