/* AxsDE - Dosyalar: dosya yöneticisi */
#include "axsde.h"

#include <dirent.h>
#include <errno.h>
#include <linux/input.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAXE 1024
#define ROW_H 34
#define SIDE_W 190

typedef struct {
    char name[256];
    int dir, exec;
    long long size;
    time_t mtime;
} Ent;

typedef struct {
    UiState ui;
    char cwd[PATH_MAX];
    Ent *ents;
    int n, sel, scroll;
    char status[128];
} FilesSt;

static const struct { const char *name; const char *path; IconId ic; } PLACES[] = {
    { "Ev", "/root", IC_HOME },
    { "Axs örnekleri", "/usr/lib/axs/examples", IC_STUDIO },
    { "Axs belgeleri", "/usr/lib/axs/docs", IC_FILE },
    { "Paket deposu", "/var/lib/axpkg/repo", IC_PACKAGE },
    { "Geçici", "/tmp", IC_FOLDER },
    { "Kök dizin", "/", IC_FOLDER },
};
#define N_PLACES ((int)(sizeof PLACES / sizeof PLACES[0]))

static int ent_cmp(const void *a, const void *b)
{
    const Ent *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    return strcasecmp(x->name, y->name);
}

static void load(Win *w, const char *path)
{
    FilesSt *st = w->st;
    char real[PATH_MAX];
    if (!realpath(path, real))
        snprintf(real, sizeof real, "%s", path);
    DIR *d = opendir(real);
    if (!d) {
        snprintf(st->status, sizeof st->status, "Açılamadı: %s", strerror(errno));
        return;
    }
    snprintf(st->cwd, sizeof st->cwd, "%s", real);
    st->n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && st->n < MAXE) {
        if (e->d_name[0] == '.')
            continue;
        Ent *x = &st->ents[st->n];
        snprintf(x->name, sizeof x->name, "%s", e->d_name);
        char full[PATH_MAX + 256];
        snprintf(full, sizeof full, "%s/%s", st->cwd, e->d_name);
        struct stat sb;
        if (stat(full, &sb) < 0)
            continue;
        x->dir = S_ISDIR(sb.st_mode);
        x->exec = !x->dir && (sb.st_mode & 0111);
        x->size = sb.st_size;
        x->mtime = sb.st_mtime;
        st->n++;
    }
    closedir(d);
    qsort(st->ents, st->n, sizeof(Ent), ent_cmp);
    st->sel = st->n ? 0 : -1;
    st->scroll = 0;
    snprintf(st->status, sizeof st->status, "%d öğe", st->n);
    const char *base = strrchr(st->cwd, '/');
    char title[128];
    snprintf(title, sizeof title, "Dosyalar — %s", (base && base[1]) ? base + 1 : "/");
    wm_set_title(w, title);
    w->dirty = 1;
}

static void f_init(Win *w, const char *arg)
{
    FilesSt *st = calloc(1, sizeof *st);
    st->ents = calloc(MAXE, sizeof(Ent));
    st->ui.cur_hot = -1;
    w->st = st;
    load(w, arg && *arg ? arg : (getenv("HOME") ? getenv("HOME") : "/"));
}

static void full_path(FilesSt *st, int i, char *out, size_t n)
{
    snprintf(out, n, "%s%s%s", st->cwd, strcmp(st->cwd, "/") ? "/" : "", st->ents[i].name);
}

static IconId ent_icon(Ent *e)
{
    if (e->dir)
        return IC_FOLDER;
    size_t l = strlen(e->name);
    if ((l > 4 && !strcmp(e->name + l - 4, ".axs")) || (l > 4 && !strcmp(e->name + l - 4, ".nyl")))
        return IC_AXSFILE;
    if (l > 4 && !strcmp(e->name + l - 4, ".axp"))
        return IC_PACKAGE;
    if (e->exec)
        return IC_EXEC;
    return IC_FILE;
}

static void open_ent(Win *w, int i)
{
    FilesSt *st = w->st;
    if (i < 0 || i >= st->n)
        return;
    char p[PATH_MAX + 256];
    full_path(st, i, p, sizeof p);
    if (st->ents[i].dir)
        load(w, p);
    else
        wm_open_file(p);
}

static void go_up(Win *w)
{
    FilesSt *st = w->st;
    if (!strcmp(st->cwd, "/"))
        return;
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s", st->cwd);
    char *s = strrchr(p, '/');
    if (s == p)
        s[1] = 0;
    else if (s)
        *s = 0;
    load(w, p);
}

static void new_axs_file(Win *w)
{
    FilesSt *st = w->st;
    char p[PATH_MAX + 64];
    for (int i = 1; i < 100; i++) {
        snprintf(p, sizeof p, "%s/yeni%d.axs", st->cwd, i);
        if (access(p, F_OK) != 0)
            break;
    }
    FILE *f = fopen(p, "w");
    if (!f) {
        wm_notify("Dosya oluşturulamadı", strerror(errno), IC_FILES);
        return;
    }
    fputs("# Yeni Axs programı\n\nad = \"dünya\"\nprint: Merhaba ad;!\n", f);
    fclose(f);
    load(w, st->cwd);
    wm_open(&APP_STUDIO, p);
}

static void f_draw(Win *w, Surf *s)
{
    FilesSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.surface);

    /* kenar çubuğu */
    fill_rect(s, (Rect){ 0, 0, SIDE_W, H }, T.base);
    fill_rect(s, (Rect){ SIDE_W - 1, 0, 1, H }, T.border);
    draw_text(s, F_UI_BOLD, 11, 18, 16, T.muted, "YERLER");
    for (int i = 0; i < N_PLACES; i++) {
        Rect r = { 8, 38 + i * 36, SIDE_W - 16, 32 };
        int cur = !strcmp(st->cwd, PLACES[i].path);
        int hov = ui_hover(u, r);
        if (cur)
            fill_rrect(s, r, 8, ALPHA(T.accent, 90));
        else if (hov)
            fill_rrect(s, r, 8, T.surface2);
        draw_icon(s, PLACES[i].ic, r.x + 8, r.y + 7, 18);
        draw_text(s, cur ? F_UI_BOLD : F_UI, 13, r.x + 34, r.y + 8, cur ? T.text : T.subtext, PLACES[i].name);
        if (ui_clicked(u, r))
            load(w, PLACES[i].path);
    }

    /* araç çubuğu */
    int x0 = SIDE_W + 12;
    if (ui_icon_button(s, u, (Rect){ x0, 10, 34, 34 }, IC_UP, "Yukarı"))
        go_up(w);
    if (ui_icon_button(s, u, (Rect){ x0 + 38, 10, 34, 34 }, IC_HOME, "Ev"))
        load(w, getenv("HOME") ? getenv("HOME") : "/");
    if (ui_icon_button(s, u, (Rect){ x0 + 76, 10, 34, 34 }, IC_REFRESH, "Yenile"))
        load(w, st->cwd);
    Rect pathr = { x0 + 118, 10, W - x0 - 118 - 150, 34 };
    fill_rrect(s, pathr, 9, T.base);
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, rect_inset(pathr, 2)));
    draw_text_fit(s, F_UI, 13, pathr.x + 12, pathr.y + 9, pathr.w - 20, T.text, st->cwd);
    s->clip = oc;
    if (ui_button(s, u, (Rect){ W - 140, 10, 128, 34 }, "+ Axs dosyası", BTN_PRIMARY))
        new_axs_file(w);

    /* başlık satırı */
    int ly = 56;
    Rect lr = { SIDE_W + 8, ly, W - SIDE_W - 16, H - ly - 30 };
    draw_text(s, F_UI_BOLD, 11, lr.x + 44, ly + 4, T.muted, "AD");
    draw_text(s, F_UI_BOLD, 11, lr.x + lr.w - 230, ly + 4, T.muted, "BOYUT");
    draw_text(s, F_UI_BOLD, 11, lr.x + lr.w - 130, ly + 4, T.muted, "DEĞİŞTİRİLME");
    ly += 24;
    int vis = (H - ly - 30) / ROW_H;
    st->scroll = clampi(st->scroll, 0, maxi(0, st->n - vis));
    if (!st->n)
        draw_text_center(s, F_UI, 14, (Rect){ lr.x, ly + 40, lr.w, 30 }, T.muted, "Bu klasör boş");
    surf_clip(s, rect_isect(oc, (Rect){ lr.x, ly, lr.w, vis * ROW_H }));
    for (int i = 0; i < vis && i + st->scroll < st->n; i++) {
        int idx = i + st->scroll;
        Ent *e = &st->ents[idx];
        Rect r = { lr.x, ly + i * ROW_H, lr.w - 10, ROW_H - 2 };
        int sel = idx == st->sel;
        int hov = ui_hover(u, r);
        if (sel)
            fill_rrect(s, r, 8, ALPHA(T.accent, 100));
        else if (hov)
            fill_rrect(s, r, 8, T.surface2);
        draw_icon(s, ent_icon(e), r.x + 10, r.y + 5, 22);
        draw_text_fit(s, e->dir ? F_UI_BOLD : F_UI, 13, r.x + 44, r.y + 8, r.w - 44 - 240, T.text, e->name);
        char sz[32] = "—";
        if (!e->dir)
            fmt_size(sz, sizeof sz, e->size);
        draw_text(s, F_UI, 12, r.x + r.w - 220, r.y + 9, T.subtext, sz);
        struct tm tm;
        time_t mt = e->mtime + tz_offset_min * 60;
        gmtime_r(&mt, &tm);
        char dt[32];
        snprintf(dt, sizeof dt, "%02d.%02d.%04d %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
        draw_text(s, F_UI, 12, r.x + r.w - 120, r.y + 9, T.subtext, dt);
        if (u->pressed && rect_has(r, u->mx, u->my)) {
            st->sel = idx;
            if (u->clicks == 2)
                open_ent(w, idx);
        }
    }
    s->clip = oc;
    ui_scrollbar(s, (Rect){ W - 10, ly, 5, vis * ROW_H }, st->n, vis, st->scroll);

    /* durum çubuğu */
    fill_rect(s, (Rect){ SIDE_W, H - 28, W - SIDE_W, 28 }, T.base);
    char stt[256];
    if (st->sel >= 0 && st->sel < st->n)
        snprintf(stt, sizeof stt, "%s   •   Seçili: %s   •   Açmak için çift tıkla", st->status, st->ents[st->sel].name);
    else
        snprintf(stt, sizeof stt, "%s", st->status);
    draw_text_fit(s, F_UI, 12, SIDE_W + 14, H - 22, W - SIDE_W - 28, T.muted, stt);
}

static void f_mouse(Win *w, MouseEv *e)
{
    FilesSt *st = w->st;
    if (e->kind == M_WHEEL) {
        st->scroll -= e->wheel * 3;
        w->dirty = 1;
        return;
    }
    ui_dispatch(w, &st->ui, e);
}

static void f_key(Win *w, KeyEv *e)
{
    FilesSt *st = w->st;
    if (!e->down)
        return;
    int vis = (w->content.h - 110) / ROW_H;
    switch (e->code) {
    case KEY_DOWN: st->sel = mini(st->sel + 1, st->n - 1); break;
    case KEY_UP: st->sel = maxi(st->sel - 1, 0); break;
    case KEY_PAGEDOWN: st->sel = mini(st->sel + vis, st->n - 1); break;
    case KEY_PAGEUP: st->sel = maxi(st->sel - vis, 0); break;
    case KEY_HOME: st->sel = 0; break;
    case KEY_END: st->sel = st->n - 1; break;
    case KEY_ENTER: case KEY_KPENTER: open_ent(w, st->sel); return;
    case KEY_BACKSPACE: go_up(w); return;
    case KEY_F5: load(w, st->cwd); return;
    default: return;
    }
    if (st->sel < st->scroll)
        st->scroll = st->sel;
    if (st->sel >= st->scroll + vis)
        st->scroll = st->sel - vis + 1;
    w->dirty = 1;
}

static void f_close(Win *w)
{
    FilesSt *st = w->st;
    free(st->ents);
    free(st);
}

const App APP_FILES = {
    .id = "dosyalar", .name = "Dosyalar", .desc = "Dosya yöneticisi",
    .icon = IC_FILES, .w = 820, .h = 500,
    .init = f_init, .draw = f_draw, .mouse = f_mouse, .key = f_key, .close = f_close,
};
