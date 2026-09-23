/* AxsDE - tema, anında-mod arayüz öğeleri, yapılandırma ve yardımcılar */
#include "axsde.h"

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Tema                                                                */
/* ------------------------------------------------------------------ */

const uint32_t ACCENTS[N_ACCENTS][2] = {
    { HEX(0x8B5CF6), HEX(0xC084FC) }, /* Axs moru */
    { HEX(0x3B82F6), HEX(0x60A5FA) }, /* mavi */
    { HEX(0x10B981), HEX(0x34D399) }, /* zümrüt */
    { HEX(0xF97316), HEX(0xFB923C) }, /* turuncu */
    { HEX(0xEC4899), HEX(0xF472B6) }, /* pembe */
    { HEX(0x06B6D4), HEX(0x22D3EE) }, /* camgöbeği */
};
const char *ACCENT_NAMES[N_ACCENTS] = { "Axs Moru", "Gök Mavisi", "Zümrüt", "Gün Batımı", "Gül", "Buz" };

Theme T = {
    .base = HEX(0x181825),
    .surface = HEX(0x1E1E2E),
    .surface2 = HEX(0x28283A),
    .overlay = HEX(0x363650),
    .border = HEX(0x3A3A52),
    .text = HEX(0xECECF4),
    .subtext = HEX(0xB4B4C8),
    .muted = HEX(0x7C7C96),
    .accent = HEX(0x8B5CF6),
    .accent2 = HEX(0xC084FC),
    .red = HEX(0xF87171),
    .yellow = HEX(0xFBBF24),
    .green = HEX(0x34D399),
    .blue = HEX(0x60A5FA),
    .orange = HEX(0xFB923C),
    .title_focus = HEX(0x24243A),
    .title_blur = HEX(0x1E1E2E),
};

int accent_idx;

void theme_set_accent(int idx)
{
    accent_idx = clampi(idx, 0, N_ACCENTS - 1);
    T.accent = ACCENTS[accent_idx][0];
    T.accent2 = ACCENTS[accent_idx][1];
}

/* ------------------------------------------------------------------ */
/* Arayüz öğeleri                                                      */
/* ------------------------------------------------------------------ */

void ui_mouse(UiState *u, MouseEv *e)
{
    if (e->kind == M_MOVE) {
        ui_move(u, e);
        return;
    }
    u->mx = e->x;
    u->my = e->y;
    u->pressed = u->released = 0;
    u->wheel = 0;
    if (e->kind == M_DOWN && e->button == 1) {
        u->down = 1;
        u->pressed = 1;
        u->px = e->x;
        u->py = e->y;
        u->clicks = e->clicks;
    } else if (e->kind == M_UP && e->button == 1) {
        u->down = 0;
        u->released = 1;
    } else if (e->kind == M_WHEEL) {
        u->wheel = e->wheel;
    }
}

void ui_end(UiState *u)
{
    u->pressed = u->released = 0;
    u->wheel = 0;
}

void ui_begin(UiState *u) { u->nhot = 0; }

void ui_dispatch(Win *w, UiState *u, MouseEv *e)
{
    if (e->kind == M_MOVE) {
        if (ui_move(u, e))
            w->dirty = 1;
        return;
    }
    ui_mouse(u, e);
    surf_noclip(&w->content);
    w->app->draw(w, &w->content);
    ui_end(u);
    w->dirty = 1;
}

static void hot_add(UiState *u, Rect r)
{
    if (u->nhot < (int)(sizeof u->hot / sizeof u->hot[0]))
        u->hot[u->nhot++] = r;
}

static int hot_at(UiState *u, int x, int y)
{
    for (int i = u->nhot - 1; i >= 0; i--)
        if (rect_has(u->hot[i], x, y))
            return i;
    return -1;
}

int ui_move(UiState *u, MouseEv *e)
{
    u->mx = e->x;
    u->my = e->y;
    int h = hot_at(u, e->x, e->y);
    if (h != u->cur_hot || u->down) {
        u->cur_hot = h;
        return 1;
    }
    return 0;
}

int ui_hover(UiState *u, Rect r)
{
    hot_add(u, r);
    return rect_has(r, u->mx, u->my);
}

int ui_clicked(UiState *u, Rect r)
{
    hot_add(u, r);
    return u->released && rect_has(r, u->mx, u->my) && rect_has(r, u->px, u->py);
}

int ui_button(Surf *s, UiState *u, Rect r, const char *label, BtnStyle st)
{
    int hov = ui_hover(u, r);
    int act = hov && u->down && rect_has(r, u->px, u->py);
    uint32_t bg, fg = T.text;
    switch (st) {
    case BTN_PRIMARY: bg = T.accent; fg = HEX(0xFFFFFF); break;
    case BTN_DANGER:  bg = HEX(0xDC2626); fg = HEX(0xFFFFFF); break;
    case BTN_GHOST:   bg = hov ? T.overlay : ALPHA(T.overlay, 0); break;
    default:          bg = T.overlay; break;
    }
    if (st != BTN_GHOST) {
        if (act)
            bg = lighten(bg, -30);
        else if (hov)
            bg = lighten(bg, 25);
    }
    if (st == BTN_PRIMARY)
        fill_rrect_vgrad(s, r, 8, lighten(bg, 18), bg);
    else
        fill_rrect(s, r, 8, bg);
    if (st == BTN_NORMAL)
        stroke_rrect(s, r, 8, 1, ALPHA(HEX(0xFFFFFF), 18));
    draw_text_center(s, F_UI_BOLD, 13, r, fg, label);
    return ui_clicked(u, r);
}

int ui_icon_button(Surf *s, UiState *u, Rect r, IconId ic, const char *tip)
{
    int hov = ui_hover(u, r);
    if (hov)
        fill_rrect(s, r, 8, u->down ? T.border : T.overlay);
    int is = mini(r.w, r.h) - 12;
    draw_icon(s, ic, r.x + (r.w - is) / 2, r.y + (r.h - is) / 2, is);
    (void)tip;
    return ui_clicked(u, r);
}

void ui_panel(Surf *s, Rect r, int rad, uint32_t c)
{
    fill_rrect(s, r, rad, c);
    stroke_rrect(s, r, rad, 1, ALPHA(HEX(0xFFFFFF), 14));
}

void ui_progress(Surf *s, Rect r, float v, uint32_t c)
{
    v = v < 0 ? 0 : v > 1 ? 1 : v;
    fill_rrect(s, r, r.h / 2, T.overlay);
    int w = (int)(r.w * v);
    if (w >= r.h)
        fill_rrect_vgrad(s, (Rect){ r.x, r.y, w, r.h }, r.h / 2, lighten(c, 30), c);
    else if (w > 0)
        fill_rrect(s, (Rect){ r.x, r.y, r.h, r.h }, r.h / 2, ALPHA(c, 255 * w / r.h));
}

void ui_scrollbar(Surf *s, Rect r, int total, int visible, int offset)
{
    if (total <= visible || total <= 0)
        return;
    int th = maxi(r.h * visible / total, 24);
    int ty = r.y + (int)((long)(r.h - th) * offset / maxi(total - visible, 1));
    fill_rrect(s, (Rect){ r.x, ty, r.w, th }, r.w / 2, ALPHA(HEX(0xFFFFFF), 60));
}

void ui_badge(Surf *s, int x, int y, const char *text, uint32_t bg, uint32_t fg)
{
    int w = text_width(F_UI_BOLD, 11, text) + 14;
    fill_rrect(s, (Rect){ x, y, w, 20 }, 10, bg);
    draw_text(s, F_UI_BOLD, 11, x + 7, y + 3, fg, text);
}

/* ------------------------------------------------------------------ */
/* Metin alanı                                                         */
/* ------------------------------------------------------------------ */

void tf_set(TextField *t, const char *s)
{
    snprintf(t->buf, sizeof t->buf, "%s", s);
    t->len = (int)strlen(t->buf);
    t->cur = t->len;
}

int tf_key(TextField *t, KeyEv *e)
{
    if (!e->down)
        return 0;
    switch (e->code) {
    case KEY_ENTER:
    case KEY_KPENTER:
        return 2;
    case KEY_BACKSPACE:
        if (t->cur > 0) {
            int p = utf8_prev(t->buf, t->cur);
            memmove(t->buf + p, t->buf + t->cur, t->len - t->cur + 1);
            t->len -= t->cur - p;
            t->cur = p;
            return 1;
        }
        return 0;
    case KEY_DELETE:
        if (t->cur < t->len) {
            const char *q = t->buf + t->cur;
            utf8_next(&q);
            int n = (int)(q - (t->buf + t->cur));
            memmove(t->buf + t->cur, q, t->len - t->cur - n + 1);
            t->len -= n;
            return 1;
        }
        return 0;
    case KEY_LEFT:
        t->cur = utf8_prev(t->buf, t->cur);
        return 1;
    case KEY_RIGHT:
        if (t->cur < t->len) {
            const char *q = t->buf + t->cur;
            utf8_next(&q);
            t->cur = (int)(q - t->buf);
        }
        return 1;
    case KEY_HOME:
        t->cur = 0;
        return 1;
    case KEY_END:
        t->cur = t->len;
        return 1;
    }
    if ((e->mods & MOD_CTRL) && e->code == KEY_U) {
        tf_set(t, "");
        return 1;
    }
    if (e->ch >= 32 && e->ch != 127) {
        char tmp[4];
        int n = utf8_put(tmp, e->ch);
        if (t->len + n < (int)sizeof t->buf - 1) {
            memmove(t->buf + t->cur + n, t->buf + t->cur, t->len - t->cur + 1);
            memcpy(t->buf + t->cur, tmp, n);
            t->len += n;
            t->cur += n;
            return 1;
        }
    }
    return 0;
}

void tf_draw(Surf *s, TextField *t, Rect r, const char *placeholder)
{
    fill_rrect(s, r, 9, T.base);
    stroke_rrect(s, r, 9, t->focus ? 2 : 1, t->focus ? T.accent : T.border);
    int ty = r.y + (r.h - font_height(F_UI, 14)) / 2;
    Rect old = s->clip;
    surf_clip(s, rect_isect(old, rect_inset(r, 3)));
    if (!t->len && placeholder)
        draw_text(s, F_UI, 14, r.x + 12, ty, T.muted, placeholder);
    else
        draw_text(s, F_UI, 14, r.x + 12, ty, T.text, t->buf);
    if (t->focus) {
        int cx = r.x + 12 + text_width_n(F_UI, 14, t->buf, t->cur);
        fill_rect(s, (Rect){ cx, ty + 1, 2, font_height(F_UI, 14) - 2 }, T.accent);
    }
    s->clip = old;
}

/* ------------------------------------------------------------------ */
/* Yapılandırma: /etc/axsde.conf                                       */
/* ------------------------------------------------------------------ */

#define CONF "/etc/axsde.conf"

void config_load(void)
{
    theme_set_accent(0);
    FILE *f = fopen(CONF, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "accent=%d", &v) == 1)
            theme_set_accent(v);
        else if (sscanf(line, "wallpaper=%d", &v) == 1)
            wallpaper_idx = clampi(v, 0, N_WALLPAPERS - 1);
        else if (sscanf(line, "layout=%d", &v) == 1)
            kb_layout = clampi(v, 0, 1);
        else if (sscanf(line, "tz=%d", &v) == 1)
            tz_offset_min = clampi(v, -12 * 60, 14 * 60);
    }
    fclose(f);
}

void config_save(void)
{
    FILE *f = fopen(CONF, "w");
    if (!f)
        return;
    fprintf(f, "# AxsDE ayarları\naccent=%d\nwallpaper=%d\nlayout=%d\ntz=%d\n",
            accent_idx, wallpaper_idx, kb_layout, tz_offset_min);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Yardımcılar                                                         */
/* ------------------------------------------------------------------ */

long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void fmt_size(char *out, size_t n, long long b)
{
    if (b < 1024)
        snprintf(out, n, "%lld B", b);
    else if (b < 1024 * 1024)
        snprintf(out, n, "%.1f KB", b / 1024.0);
    else if (b < 1024LL * 1024 * 1024)
        snprintf(out, n, "%.1f MB", b / (1024.0 * 1024));
    else
        snprintf(out, n, "%.2f GB", b / (1024.0 * 1024 * 1024));
}

int read_file(const char *path, char *buf, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if (r < 0)
        return -1;
    buf[r] = 0;
    return (int)r;
}
