/* AxsDE - Terminal uygulaması: pty içinde axsh */
#include "axsde.h"

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
} TermSt;

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
        char *argv[] = { "/bin/axsh", "-c", (char *)arg, NULL };
        term_spawn(st->t, argv, home);
    } else {
        char *argv[] = { "-axsh", NULL };
        /* argv[0] "-" ile başlarsa oturum kabuğu; execvp yol için gerçek adı ister */
        char *real[] = { "/bin/axsh", NULL };
        (void)argv;
        if (term_spawn(st->t, real, home) < 0) {
            char *sh[] = { "/bin/sh", NULL };
            term_spawn(st->t, sh, home);
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

static void t_key(Win *w, KeyEv *e)
{
    TermSt *st = w->st;
    if (!e->down)
        return;
    if (st->finished) {
        w->closing = 1;
        return;
    }
    term_key(st->t, e);
    w->dirty = 1;
}

static void t_mouse(Win *w, MouseEv *e)
{
    TermSt *st = w->st;
    if (e->kind == M_WHEEL) {
        term_scroll(st->t, e->wheel * 3);
        w->dirty = 1;
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
    .id = "terminal", .name = "Terminal", .desc = "axsh kabuğu",
    .icon = IC_TERMINAL, .w = 760, .h = 440,
    .init = t_init, .draw = t_draw, .key = t_key, .mouse = t_mouse,
    .fds = t_fds, .io = t_io, .tick = t_tick, .resize = t_resize, .close = t_close,
};
