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
    { "Masaüstü", "/root/Masaüstü", IC_HOME },
    { "İndirilenler", "/root/İndirilenler", IC_FOLDER },
    { "Resimler", "/root/Resimler", IC_FOLDER },
    { "Kalıcı disk", "/mnt/kalici", IC_SAVE },
    { "Axs örnekleri", "/usr/lib/axs/examples", IC_STUDIO },
    { "Axs belgeleri", "/usr/lib/axs/docs", IC_FILE },
    { "Paket deposu", "/var/lib/axpkg/repo", IC_PACKAGE },
    { "Geçici", "/tmp", IC_FOLDER },
    { "Kök dizin", "/", IC_FOLDER },
    { "Çöp Kutusu", "/root/.cop", IC_TRASH },
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
    snprintf(title, sizeof title, "Dosyalar — %s", fo_in_trash(st->cwd) ? "Çöp Kutusu" : (base && base[1]) ? base + 1 : "/");
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

static IconId ent_icon(Ent *e) { return fo_icon(e->name, e->dir, e->exec); }

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

/* ---- dosya işlemleri (ortak: fileops.c) ---- */
enum { FA_OPEN = 1, FA_RENAME, FA_COPY, FA_CUT, FA_TRASH, FA_PASTE, FA_NEW_DIR, FA_NEW_TEXT, FA_NEW_AXS,
       FA_NEW_GUI, FA_RESTORE, FA_EMPTY, FA_TERMINAL, FA_DESKTOP, FA_EDIT };

static void select_path(FilesSt *st, const char *path)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    for (int i = 0; i < st->n; i++)
        if (!strcmp(st->ents[i].name, b))
            st->sel = i;
}

static void notify_err(const char *what)
{
    wm_notify(what, strerror(errno), IC_FILES);
}

static int new_kind;
static void new_done(const char *text, int id)
{
    Win *w = wm_find_id(id);
    if (!w || !text)
        return;
    FilesSt *st = w->st;
    char out[PATH_MAX];
    if (fo_create(st->cwd, text[0] ? text : NULL, new_kind, out, sizeof out) < 0) {
        notify_err("Oluşturulamadı");
        return;
    }
    load(w, st->cwd);
    select_path(st, out);
    if (new_kind == FO_AXS || new_kind == FO_AXS_GUI)
        wm_open(&APP_STUDIO, out);
}

static void ask_new(Win *w, int kind)
{
    static const char *titles[] = { "Yeni klasör", "Yeni metin belgesi", "Yeni Axs programı",
                                    "Yeni pencereli program (kolay arayüz)" };
    new_kind = kind;
    wm_prompt(titles[kind], "Adını yaz:", fo_default_name(kind), "Oluştur", 1, new_done, w->id);
}

static char ren_path[PATH_MAX];
static void rename_done(const char *text, int id)
{
    Win *w = wm_find_id(id);
    if (!w || !text || !text[0])
        return;
    FilesSt *st = w->st;
    char out[PATH_MAX];
    if (fo_rename(ren_path, text, out, sizeof out) < 0) {
        notify_err("Yeniden adlandırılamadı");
        return;
    }
    load(w, st->cwd);
    select_path(st, out);
}

static void empty_done(const char *text, int id)
{
    Win *w = wm_find_id(id);
    if (!text)
        return;
    fo_empty_trash();
    if (w)
        load(w, ((FilesSt *)w->st)->cwd);
}

static void file_action(int a, int id)
{
    Win *w = wm_find_id(id);
    if (!w)
        return;
    FilesSt *st = w->st;
    int i = st->sel;
    char p[PATH_MAX + 256], out[PATH_MAX];
    int has = i >= 0 && i < st->n;
    if (has)
        full_path(st, i, p, sizeof p);
    switch (a) {
    case FA_OPEN: open_ent(w, i); return;
    case FA_EDIT: if (has) wm_open(&APP_STUDIO, p); return;
    case FA_RENAME:
        if (!has)
            return;
        snprintf(ren_path, sizeof ren_path, "%s", p);
        wm_prompt("Yeniden adlandır", "Yeni ad:", st->ents[i].name, "Tamam", 1, rename_done, w->id);
        return;
    case FA_COPY: case FA_CUT:
        if (has) {
            fo_clip_set(p, a == FA_CUT);
            snprintf(st->status, sizeof st->status, "%s: %s", a == FA_CUT ? "Kesildi" : "Kopyalandı", st->ents[i].name);
        }
        break;
    case FA_TRASH:
        if (has && fo_trash(p) < 0)
            notify_err("Silinemedi");
        load(w, st->cwd);
        st->sel = mini(i, st->n - 1);
        return;
    case FA_RESTORE:
        if (has && fo_restore(p) < 0)
            notify_err("Geri yüklenemedi");
        else if (has)
            wm_notify("Geri yüklendi", st->ents[i].name, IC_FILES);
        load(w, st->cwd);
        return;
    case FA_EMPTY:
        wm_prompt("Çöp Kutusu'nu boşalt", "İçindeki her şey kalıcı olarak silinecek.", NULL, "Boşalt", 0, empty_done, w->id);
        return;
    case FA_PASTE:
        if (fo_paste(st->cwd, out, sizeof out) < 0) {
            notify_err("Yapıştırılamadı");
            return;
        }
        load(w, st->cwd);
        select_path(st, out);
        break;
    case FA_NEW_DIR: ask_new(w, FO_DIR); return;
    case FA_NEW_TEXT: ask_new(w, FO_TEXT); return;
    case FA_NEW_AXS: ask_new(w, FO_AXS); return;
    case FA_NEW_GUI: ask_new(w, FO_AXS_GUI); return;
    case FA_TERMINAL: {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "cd '%s' && exec bash", st->cwd);
        wm_run_in_terminal(cmd, "Terminal");
        return;
    }
    case FA_DESKTOP:
        if (has) {
            mkdir(desktop_dir(), 0755);
            fo_clip_set(p, 0);
            if (fo_paste(desktop_dir(), out, sizeof out) < 0)
                notify_err("Masaüstüne kopyalanamadı");
            else
                wm_notify("Masaüstüne kopyalandı", st->ents[i].name, IC_HOME);
        }
        return;
    }
    w->dirty = 1;
}

static void new_menu(Win *w, int x, int y)
{
    static const MenuItem m[] = {
        { "Klasör", FA_NEW_DIR, IC_FOLDER },
        { "Metin belgesi", FA_NEW_TEXT, IC_FILE },
        { "Axs programı", FA_NEW_AXS, IC_AXSFILE },
        { "Pencereli program (kolay arayüz)", FA_NEW_GUI, IC_STUDIO },
    };
    wm_menu(x, y, m, 4, file_action, w->id);
}

static void context_menu(Win *w, int x, int y, int on_item)
{
    FilesSt *st = w->st;
    MenuItem m[MENU_MAX];
    int n = 0, trash = fo_in_trash(st->cwd);
    if (on_item) {
        Ent *e = &st->ents[st->sel];
        m[n++] = (MenuItem){ "Aç", FA_OPEN, e->dir ? IC_FOLDER : IC_PLAY };
        if (!e->dir && e->size < 1024 * 1024)
            m[n++] = (MenuItem){ "Axs Stüdyo ile düzenle", FA_EDIT, IC_STUDIO };
        if (trash) {
            m[n++] = (MenuItem){ "Geri yükle", FA_RESTORE, IC_REFRESH };
            m[n++] = (MenuItem){ "Kalıcı olarak sil", FA_TRASH, IC_TRASH };
        } else {
            m[n++] = (MenuItem){ "Yeniden adlandır  (F2)", FA_RENAME, IC_NEW };
            m[n++] = (MenuItem){ "Kopyala  (Ctrl+C)", FA_COPY, IC_FILE };
            m[n++] = (MenuItem){ "Kes  (Ctrl+X)", FA_CUT, IC_FILE };
            m[n++] = (MenuItem){ "Masaüstüne kopyala", FA_DESKTOP, IC_HOME };
            m[n++] = (MenuItem){ "Çöpe taşı  (Delete)", FA_TRASH, IC_TRASH };
        }
    } else if (trash) {
        if (st->n)
            m[n++] = (MenuItem){ "Çöpü boşalt", FA_EMPTY, IC_TRASH };
    } else {
        m[n++] = (MenuItem){ "Yeni klasör", FA_NEW_DIR, IC_FOLDER };
        m[n++] = (MenuItem){ "Yeni metin belgesi", FA_NEW_TEXT, IC_FILE };
        m[n++] = (MenuItem){ "Yeni Axs programı", FA_NEW_AXS, IC_AXSFILE };
        m[n++] = (MenuItem){ "Yeni pencereli program", FA_NEW_GUI, IC_STUDIO };
        if (fo_clip_has())
            m[n++] = (MenuItem){ "Yapıştır  (Ctrl+V)", FA_PASTE, IC_FILES };
        m[n++] = (MenuItem){ "Burada terminal aç", FA_TERMINAL, IC_TERMINAL };
    }
    if (n)
        wm_menu(x, y, m, n, file_action, w->id);
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
    for (int i = 0, row = 0; i < N_PLACES; i++) {
        if (i > 0 && access(PLACES[i].path, F_OK) != 0)
            continue; /* yalnızca var olan yerler (ör. kalıcı disk etkinse) */
        Rect r = { 8, 38 + row++ * 36, SIDE_W - 16, 32 };
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
    if (fo_in_trash(st->cwd)) {
        if (ui_button(s, u, (Rect){ W - 140, 10, 128, 34 }, "Çöpü boşalt", BTN_DANGER) && st->n)
            file_action(FA_EMPTY, w->id);
    } else if (ui_button(s, u, (Rect){ W - 140, 10, 128, 34 }, "+ Yeni", BTN_PRIMARY)) {
        Rect cr = wm_content_rect(w);
        new_menu(w, cr.x + W - 260, cr.y + 48);
    }

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
        snprintf(stt, sizeof stt, "%s   •   Seçili: %s   •   Çift tık: aç, sağ tık: işlemler", st->status, st->ents[st->sel].name);
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
    if (e->kind == M_DOWN && e->button == 2 && e->x > SIDE_W) {
        /* sağ tık: satırdaysa seç ve öğe menüsü, değilse klasör menüsü */
        int ly = 80, H = w->content.h, vis = (H - ly - 30) / ROW_H;
        int row = (e->y - ly) / ROW_H, idx = row + st->scroll;
        int on = e->y >= ly && row < vis && idx < st->n;
        if (on)
            st->sel = idx;
        w->dirty = 1;
        Rect cr = wm_content_rect(w);
        context_menu(w, cr.x + e->x, cr.y + e->y, on);
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
    case KEY_F2: file_action(FA_RENAME, w->id); return;
    case KEY_DELETE: file_action(FA_TRASH, w->id); return;
    case KEY_C: if (e->mods & MOD_CTRL) file_action(FA_COPY, w->id); return;
    case KEY_X: if (e->mods & MOD_CTRL) file_action(FA_CUT, w->id); return;
    case KEY_V: if (e->mods & MOD_CTRL) file_action(FA_PASTE, w->id); return;
    case KEY_N: if (e->mods & MOD_CTRL) ask_new(w, (e->mods & MOD_SHIFT) ? FO_DIR : FO_TEXT); return;
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
