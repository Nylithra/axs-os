/* AxsDE - Terminal uygulaması: pty içinde bash (yoksa axsh) */
#include "axsde.h"

#include <linux/input.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAD_X 10
#define PAD_Y 8

typedef struct {
    Term *t;
    int is_cmd;          /* tek komut çalıştırıyor (bitince tuşla kapanır) */
    int finished;
    int was_focused;
    int blink;
    int full;
    int selecting;       /* fareyle seçim sürüyor */
} TermSt;

enum { TA_COPY = 1, TA_PASTE, TA_ALL, TA_CLEAR };

static void grid_size(Win *w, int *cols, int *rows)
{
    int cw, ch;
    term_cell_size(&cw, &ch);
    *cols = maxi((w->content.w - 2 * PAD_X) / cw, 10);
    *rows = maxi((w->content.h - 2 * PAD_Y) / ch, 4);
}

static void t_init(Win *w, const char *arg)
{
    TermSt *st = calloc(1, sizeof *st);
    w->st = st;
    w->min_w = 360;
    w->min_h = 160;
    int cols, rows;
    grid_size(w, &cols, &rows);
    st->t = term_new(cols, rows);
    st->full = 1;
    const char *home = getenv("HOME");
    if (arg && *arg) {
        st->is_cmd = 1;
        char *argv[] = { "/bin/bash", "-c", (char *)arg, NULL };
        if (term_spawn(st->t, argv, home) < 0) {
            char *ax[] = { "/bin/axsh", "-c", (char *)arg, NULL };
            term_spawn(st->t, ax, home);
        }
    } else {
        /* oturum kabuğu: /etc/profile + /etc/bash.bashrc okunur */
        char *argv[] = { "/bin/bash", "-l", NULL };
        if (term_spawn(st->t, argv, home) < 0) {
            char *ax[] = { "/bin/axsh", NULL };
            if (term_spawn(st->t, ax, home) < 0) {
                char *sh[] = { "/bin/sh", NULL };
                term_spawn(st->t, sh, home);
            }
        }
    }
}

static void t_draw(Win *w, Surf *s)
{
    TermSt *st = w->st;
    int focused = wm_focused() == w;
    int full = st->full || focused != st->was_focused;
    st->was_focused = focused;
    st->full = 0;
    if (full)
        fill_rect(s, (Rect){ 0, 0, s->w, s->h }, HEX(0x13131D));
    int cw, ch;
    term_cell_size(&cw, &ch);
    int cols, rows;
    grid_size(w, &cols, &rows);
    Rect area = { PAD_X, PAD_Y, cols * cw, rows * ch };
    Rect d = term_draw(st->t, s, area, focused, full);
    w->dmg = full ? (Rect){ 0, 0, 0, 0 } : d;
    if (full)
        w->dmg = (Rect){ 0, 0, 0, 0 };
    else if (rect_empty(d))
        w->dmg = (Rect){ 0, 0, 1, 1 };
}

static void copy_sel(TermSt *st)
{
    char *s = term_sel_text(st->t);
    if (s) {
        clip_set(s);
        free(s);
    }
}

static void t_action(int a, int id)
{
    Win *w = wm_find_id(id);
    if (!w)
        return;
    TermSt *st = w->st;
    switch (a) {
    case TA_COPY: copy_sel(st); term_sel_clear(st->t); break;
    case TA_PASTE: term_paste(st->t, clip_get()); break;
    case TA_ALL: term_sel_start(st->t, 0, 0); term_sel_move(st->t, 9999, 9999); break;
    case TA_CLEAR: term_write(st->t, "\014", 1); break;   /* Ctrl+L */
    }
    w->dirty = 1;
}

static void t_key(Win *w, KeyEv *e)
{
    TermSt *st = w->st;
    if (!e->down)
        return;
    if (st->finished) {
        w->closing = 1;
        return;
    }
    int ctrl = e->mods & MOD_CTRL, shift = e->mods & MOD_SHIFT;
    /* Kopyala: Ctrl+Shift+C, ya da seçim varken Ctrl+C (seçim yoksa Ctrl+C programı durdurur) */
    if (ctrl && e->code == KEY_C && (shift || term_has_sel(st->t))) {
        copy_sel(st);
        term_sel_clear(st->t);
        w->dirty = 1;
        return;
    }
    /* Yapıştır: Ctrl+V, Ctrl+Shift+V, Shift+Insert */
    if ((ctrl && e->code == KEY_V) || (shift && e->code == KEY_INSERT)) {
        term_paste(st->t, clip_get());
        w->dirty = 1;
        return;
    }
    if (ctrl && shift && e->code == KEY_A) {
        t_action(TA_ALL, w->id);
        return;
    }
    if (key_is_modifier(e->code))
        return;
    term_sel_clear(st->t);
    term_key(st->t, e);
    w->dirty = 1;
}

static void cell_at(Win *w, int px, int py, int *x, int *y)
{
    int cw, ch;
    term_cell_size(&cw, &ch);
    *x = (px - PAD_X + cw / 2) / cw;
    *y = (py - PAD_Y) / ch;
    (void)w;
}

static void t_mouse(Win *w, MouseEv *e)
{
    TermSt *st = w->st;
    if (e->kind == M_WHEEL) {
        term_scroll(st->t, e->wheel * 3);
        w->dirty = 1;
        return;
    }
    int x, y;
    cell_at(w, e->x, e->y, &x, &y);
    if (e->kind == M_DOWN && e->button == 1) {
        if (e->clicks == 2) {
            int cw, ch;
            term_cell_size(&cw, &ch);
            term_sel_word(st->t, (e->x - PAD_X) / cw, y);
            st->selecting = 0;
        } else {
            term_sel_start(st->t, x, y);
            st->selecting = 1;
        }
        w->dirty = 1;
    } else if (e->kind == M_MOVE && st->selecting) {
        term_sel_move(st->t, x, y);
        w->dirty = 1;
    } else if (e->kind == M_UP && e->button == 1) {
        st->selecting = 0;
        if (!term_has_sel(st->t))
            term_sel_clear(st->t);
        w->dirty = 1;
    } else if (e->kind == M_DOWN && e->button == 2) {
        MenuItem m[4];
        int n = 0;
        if (term_has_sel(st->t))
            m[n++] = (MenuItem){ "Kopyala  (Ctrl+Shift+C)", TA_COPY, IC_FILE };
        if (clip_get()[0])
            m[n++] = (MenuItem){ "Yapıştır  (Ctrl+V)", TA_PASTE, IC_FILES };
        m[n++] = (MenuItem){ "Tümünü seç", TA_ALL, IC_NEW };
        m[n++] = (MenuItem){ "Ekranı temizle", TA_CLEAR, IC_REFRESH };
        Rect cr = wm_content_rect(w);
        wm_menu(cr.x + e->x, cr.y + e->y, m, n, t_action, w->id);
    }
}

static int t_fds(Win *w, int *fds, int max)
{
    TermSt *st = w->st;
    int fd = term_fd(st->t);
    if (fd < 0 || max < 1)
        return 0;
    fds[0] = fd;
    return 1;
}

static void t_io(Win *w, int fd)
{
    (void)fd;
    TermSt *st = w->st;
    if (!term_read(st->t)) {
        if (!st->is_cmd) {
            w->closing = 1;
            return;
        }
        char msg[160];
        int code = term_exit_code(st->t);
        snprintf(msg, sizeof msg, "\r\n\033[%sm[İşlem bitti%s%d — kapatmak için bir tuşa basın]\033[0m",
                 code ? "31" : "32", code ? ", hata kodu " : ", kod ", code);
        term_feed(st->t, msg, (int)strlen(msg));
        st->finished = 1;
    }
    w->dirty = 1;
}

static void t_tick(Win *w)
{
    TermSt *st = w->st;
    if (wm_focused() != w)
        return;
    if (++st->blink >= 2) {
        st->blink = 0;
        term_blink(st->t);
        w->dirty = 1;
    }
}

static void t_resize(Win *w)
{
    TermSt *st = w->st;
    int cols, rows;
    grid_size(w, &cols, &rows);
    term_resize(st->t, cols, rows);
    st->full = 1;
}

static void t_close(Win *w)
{
    TermSt *st = w->st;
    term_free(st->t);
    free(st);
}

const App APP_TERMINAL = {
    .id = "terminal", .name = "Terminal", .desc = "bash kabuğu",
    .icon = IC_TERMINAL, .w = 760, .h = 440,
    .init = t_init, .draw = t_draw, .key = t_key, .mouse = t_mouse,
    .fds = t_fds, .io = t_io, .tick = t_tick, .resize = t_resize, .close = t_close,
};
