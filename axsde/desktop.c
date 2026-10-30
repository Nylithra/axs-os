/* AxsDE - masaüstü simgeleri
 *
 * ~/Masaüstü klasörünün içeriği duvar kâğıdının üzerinde simge olarak görünür (ızgara, sütun
 * sütun). Çift tık açar; sağ tık menüsü: yeni klasör / belge / Axs programı / pencereli program,
 * yapıştır; simgede aç, yeniden adlandır, kopyala, kes, çöpe taşı. Simgeler sürüklenip başka
 * hücreye, bir klasörün ya da Çöp Kutusu'nun üzerine bırakılabilir. Konumlar
 * ~/.config/axsde-masaustu dosyasında saklanır. "<ad>.kisayol" dosyaları (uygulama=<kimlik>)
 * uygulama kısayoludur. Klavye: Enter aç, F2 yeniden adlandır, Delete çöpe, Ctrl+C/X/V, oklar.
 */
#define _GNU_SOURCE
#include "axsde.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define CELL_W   96
#define CELL_H   100
#define ICON     48
#define TOP      48      /* üst çubuğun altı */
#define LEFT     12
#define BOTTOM   104     /* dock için boş bırakılan alt kısım */
#define MAXI     160
#define TRASH_ID ":cop"

typedef struct {
    char name[256];           /* dosya adı (çöp için TRASH_ID) */
    char label[256];          /* görünen ad */
    int dir, exec, img;
    char app[64];             /* kısayolsa uygulama kimliği */
    int col, row, placed;
} DItem;

static DItem items[MAXI];
static int nitems;
static char ddir[PATH_MAX];
static int sel = -1, hover = -1, focus;
static time_t dir_mtime, trash_mtime;
static int trash_full;

/* sürükleme */
static int press_i = -1, press_x, press_y, dragging, drag_x, drag_y;

static int rows(void) { return maxi(1, (SCREEN_H - TOP - BOTTOM) / CELL_H); }
static int cols(void) { return maxi(1, (SCREEN_W - LEFT * 2) / CELL_W); }

static Rect cell_rect(int col, int row) { return (Rect){ LEFT + col * CELL_W, TOP + row * CELL_H, CELL_W, CELL_H }; }
static Rect item_rect(int i) { return cell_rect(items[i].col, items[i].row); }

const char *desktop_dir(void)
{
    if (!ddir[0])
        snprintf(ddir, sizeof ddir, "%s/Masaüstü", getenv("HOME") ? getenv("HOME") : "/root");
    return ddir;
}

static void item_path(int i, char *out, size_t n)
{
    if (!strcmp(items[i].name, TRASH_ID))
        snprintf(out, n, "%s", fo_trash_dir());
    else
        snprintf(out, n, "%s/%s", desktop_dir(), items[i].name);
}

static int is_trash(int i) { return i >= 0 && !strcmp(items[i].name, TRASH_ID); }

/* ---------------------------------------------------------------- */
/* Konumlar                                                           */
/* ---------------------------------------------------------------- */

static void pos_path(char *out, size_t n)
{
    snprintf(out, n, "%s/.config/axsde-masaustu", getenv("HOME") ? getenv("HOME") : "/root");
}

static void save_positions(void)
{
    char p[PATH_MAX];
    pos_path(p, sizeof p);
    FILE *f = fopen(p, "w");
    if (!f)
        return;
    for (int i = 0; i < nitems; i++)
        fprintf(f, "%d %d %s\n", items[i].col, items[i].row, items[i].name);
    fclose(f);
}

static int occupied(int col, int row, int except)
{
    for (int i = 0; i < nitems; i++)
        if (i != except && items[i].placed && items[i].col == col && items[i].row == row)
            return i;
    return -1;
}

static void place_rest(void)
{
    int R = rows(), C = cols();
    for (int i = 0; i < nitems; i++) {
        if (items[i].placed && items[i].col < C && items[i].row < R && occupied(items[i].col, items[i].row, i) < 0)
            continue;
        items[i].placed = 0;
        for (int c = 0; c < C && !items[i].placed; c++)
            for (int r = 0; r < R && !items[i].placed; r++)
                if (occupied(c, r, i) < 0) {
                    items[i].col = c;
                    items[i].row = r;
                    items[i].placed = 1;
                }
        if (!items[i].placed) {   /* yer kalmadı: üst üste */
            items[i].col = C - 1;
            items[i].row = R - 1;
            items[i].placed = 1;
        }
    }
}

/* ---------------------------------------------------------------- */
/* Klasörü okuma                                                      */
/* ---------------------------------------------------------------- */

static int ends_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}

static void read_shortcut(const char *path, char *app, size_t n)
{
    app[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    char line[256];
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "uygulama=", 9)) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(app, n, "%s", line + 9);
        }
    fclose(f);
}

/* varsayılan sıra: kısayollar, klasörler, dosyalar; her grup ada göre */
static int item_cmp(const void *a, const void *b)
{
    const DItem *x = a, *y = b;
    int gx = x->app[0] ? 0 : x->dir ? 1 : 2, gy = y->app[0] ? 0 : y->dir ? 1 : 2;
    if (gx != gy)
        return gx - gy;
    return strcasecmp(x->label, y->label);
}

static void scan(void)
{
    char keep[256] = "";
    if (sel >= 0 && sel < nitems)
        snprintf(keep, sizeof keep, "%s", items[sel].name);
    nitems = 0;
    /* çöp kutusu her zaman ilk */
    DItem *t = &items[nitems++];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, TRASH_ID);
    snprintf(t->label, sizeof t->label, "Çöp Kutusu");
    trash_full = fo_trash_count() > 0;

    DIR *d = opendir(desktop_dir());
    struct dirent *e;
    while (d && (e = readdir(d)) && nitems < MAXI) {
        if (e->d_name[0] == '.')
            continue;
        DItem *x = &items[nitems];
        memset(x, 0, sizeof *x);
        snprintf(x->name, sizeof x->name, "%s", e->d_name);
        char full[PATH_MAX + 256];
        snprintf(full, sizeof full, "%s/%s", desktop_dir(), e->d_name);
        struct stat sb;
        if (stat(full, &sb) < 0)
            continue;
        x->dir = S_ISDIR(sb.st_mode);
        x->exec = !x->dir && (sb.st_mode & 0111);
        x->img = !x->dir && sb.st_size < 6 * 1024 * 1024 &&
                 (ends_ci(x->name, ".png") || ends_ci(x->name, ".jpg") || ends_ci(x->name, ".jpeg"));
        memcpy(x->label, x->name, sizeof x->label);
        if (!x->dir && ends_ci(x->name, ".kisayol")) {
            read_shortcut(full, x->app, sizeof x->app);
            x->label[strlen(x->label) - 8] = 0;
        }
        nitems++;
    }
    if (d)
        closedir(d);
    qsort(items + 1, nitems - 1, sizeof(DItem), item_cmp);
    /* kayıtlı konumlar */
    char p[PATH_MAX];
    pos_path(p, sizeof p);
    FILE *f = fopen(p, "r");
    if (f) {
        char line[400];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            int c, r, off = 0;
            if (sscanf(line, "%d %d %n", &c, &r, &off) < 2 || !off)
                continue;
            for (int i = 0; i < nitems; i++)
                if (!items[i].placed && !strcmp(items[i].name, line + off)) {
                    items[i].col = c;
                    items[i].row = r;
                    items[i].placed = 1;
                }
        }
        fclose(f);
    }
    place_rest();
    sel = -1;
    for (int i = 0; keep[0] && i < nitems; i++)
        if (!strcmp(items[i].name, keep))
            sel = i;
    hover = -1;
    struct stat sb;
    dir_mtime = stat(desktop_dir(), &sb) == 0 ? sb.st_mtime : 0;
    trash_mtime = stat(fo_trash_dir(), &sb) == 0 ? sb.st_mtime : 0;
    wm_damage((Rect){ 0, TOP - 8, SCREEN_W, SCREEN_H - TOP + 8 });
}

static void mkshortcut(const char *id, const char *label)
{
    char p[PATH_MAX + 300];
    snprintf(p, sizeof p, "%s/%s.kisayol", desktop_dir(), label);
    if (access(p, F_OK) == 0)
        return;
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "uygulama=%s\n", id);
        fclose(f);
    }
}

int desktop_add_shortcut(const char *app_id)
{
    AppEntry *a = apps_find(app_id);
    if (!a)
        return -1;
    mkdir(desktop_dir(), 0755);
    mkshortcut(a->id, a->name);
    scan();
    return 0;
}

void desktop_init(void)
{
    const char *home = getenv("HOME") ? getenv("HOME") : "/root";
    char mark[PATH_MAX];
    snprintf(mark, sizeof mark, "%s/.config/axsde-masaustu-hazir", home);
    if (access(mark, F_OK) != 0) {
        /* ilk açılış: birkaç kısayol ve kısa bir rehber */
        mkdir(desktop_dir(), 0755);
        mkshortcut("dosyalar", "Dosyalar");
        mkshortcut("terminal", "Terminal");
        mkshortcut("paketler", "Uygulama Marketi");
        mkshortcut("studyo", "Axs Stüdyo");
        mkshortcut("ayarlar", "Ayarlar");
        char p[PATH_MAX + 64];
        snprintf(p, sizeof p, "%s/Beni oku.txt", desktop_dir());
        FILE *f = fopen(p, "w");
        if (f) {
            fputs("AxsOS masaüstüne hoş geldin!\n\n"
                  "• Masaüstünde boş bir yere sağ tıkla: yeni klasör, metin belgesi, Axs programı ya da\n"
                  "  pencereli (kolay arayüz) Axs programı oluştur.\n"
                  "• Bir simgeye sağ tıkla: aç, yeniden adlandır (F2), kopyala, kes, çöpe taşı (Delete).\n"
                  "• Simgeleri sürükleyerek yerini değiştir; klasörün ya da Çöp Kutusu'nun üzerine bırak.\n"
                  "• Uygulamaları başlatıcıdan (Super ya da Ctrl+Boşluk) sağ tıkla \"Masaüstüne ekle\".\n"
                  "• Windows programları (.exe, .msi): Uygulama Marketi'nden \"WTA\" paketini kur,\n"
                  "  sonra .exe dosyasına çift tıkla.\n"
                  "• Kalıcı depolama açıksa masaüstün yeniden başlatınca korunur (Ayarlar > Depolama).\n", f);
            fclose(f);
        }
        mkdir(fo_trash_dir(), 0700);
        char cfg[PATH_MAX];
        snprintf(cfg, sizeof cfg, "%s/.config", home);
        mkdir(cfg, 0755);
        FILE *m = fopen(mark, "w");
        if (m)
            fclose(m);
    }
    mkdir(desktop_dir(), 0755);
    scan();
}

void desktop_tick(void)
{
    static long last;
    long t = now_ms();
    if (t - last < 1000 || dragging)
        return;
    last = t;
    struct stat sb;
    time_t m = stat(desktop_dir(), &sb) == 0 ? sb.st_mtime : 0;
    time_t tm = stat(fo_trash_dir(), &sb) == 0 ? sb.st_mtime : 0;
    if (m != dir_mtime || tm != trash_mtime)
        scan();
}

/* ---------------------------------------------------------------- */
/* Çizim                                                              */
/* ---------------------------------------------------------------- */

/* ad: en fazla iki satır, ortalanmış (seçiliyse dört satır) */
static void draw_label(Surf *s, Rect r, const char *txt, int maxl, int selected)
{
    const int FS = 12, LH = 15, MW = CELL_W - 8;
    char lines[4][256];
    int nl = 0;
    const char *p = txt;
    while (*p && nl < maxl) {
        const char *q = p, *brk = NULL;
        int w = 0;
        while (*q) {
            const char *nq = q;
            utf8_next(&nq);
            int cw = text_width_n(F_UI, FS, p, (int)(nq - p));
            if (cw > MW)
                break;
            w = cw;
            if (*q == ' ' || *q == '-' || *q == '_' || *q == '.')
                brk = nq;
            q = nq;
        }
        (void)w;
        if (*q && brk && nl < maxl - 1)
            q = brk;
        int len = (int)(q - p);
        if (len <= 0) {          /* tek karakter bile sığmıyor */
            const char *nq = p;
            utf8_next(&nq);
            len = (int)(nq - p);
        }
        if (nl == maxl - 1 && p[len]) {   /* son satır: … ile bitir */
            snprintf(lines[nl], sizeof lines[nl], "%s", p);
            nl++;
            break;
        }
        snprintf(lines[nl++], sizeof lines[nl], "%.*s", len, p);
        p += len;
        while (*p == ' ')
            p++;
    }
    int y = r.y + 6 + ICON + 6;
    if (selected) {
        int bw = 0;
        for (int i = 0; i < nl; i++)
            bw = maxi(bw, mini(text_width(F_UI, FS, lines[i]), MW));
        fill_rrect(s, (Rect){ r.x + (r.w - bw) / 2 - 5, y - 2, bw + 10, nl * LH + 5 }, 6, ALPHA(T.accent, 230));
    }
    for (int i = 0; i < nl; i++) {
        int tw = mini(text_width(F_UI, FS, lines[i]), MW);
        int x = r.x + (r.w - tw) / 2;
        if (!selected)
            draw_text_fit(s, F_UI, FS, x + 1, y + 1 + i * LH, MW, ALPHA(HEX(0x000000), 200), lines[i]);
        draw_text_fit(s, F_UI, FS, x, y + i * LH, MW, HEX(0xFFFFFF), lines[i]);
    }
}

static void draw_item_icon(Surf *s, int i, int x, int y)
{
    DItem *it = &items[i];
    if (is_trash(i)) {
        draw_icon(s, IC_TRASH, x, y, ICON);
        if (trash_full)
            fill_circle(s, x + ICON - 6, y + 8, 6, T.accent2);
        return;
    }
    if (it->app[0]) {
        AppEntry *a = apps_find(it->app);
        if (a)
            app_draw_icon(s, a, x, y, ICON);
        else
            draw_icon(s, IC_EXEC, x, y, ICON);
        /* kısayol oku */
        fill_circle(s, x + 8, y + ICON - 8, 8, ALPHA(HEX(0xFFFFFF), 235));
        draw_line(s, x + 4.5f, y + ICON - 4.5f, x + 11, y + ICON - 11, 2, HEX(0x1C1C2C));
        draw_line(s, x + 6.5f, y + ICON - 11, x + 11, y + ICON - 11, 2, HEX(0x1C1C2C));
        draw_line(s, x + 11, y + ICON - 11, x + 11, y + ICON - 6.5f, 2, HEX(0x1C1C2C));
        return;
    }
    if (it->img) {
        char p[PATH_MAX];
        item_path(i, p, sizeof p);
        const Surf *th = icon_png(p, ICON);
        if (th) {
            int tx = x + (ICON - th->w) / 2, ty = y + (ICON - th->h) / 2;
            fill_rect(s, (Rect){ tx - 2, ty - 2, th->w + 4, th->h + 4 }, HEX(0xFFFFFF));
            blit_alpha(s, tx, ty, th);
            return;
        }
    }
    draw_icon(s, fo_icon(it->name, it->dir, it->exec), x, y, ICON);
}

void desktop_draw(Surf *s, Rect clip)
{
    for (int i = 0; i < nitems; i++) {
        Rect r = item_rect(i);
        if (rect_empty(rect_isect(r, clip)))
            continue;
        int seli = i == sel, hov = i == hover;
        if (seli || hov)
            fill_rrect(s, rect_inset(r, 3), 10, ALPHA(HEX(0xFFFFFF), seli ? (focus ? 60 : 38) : 26));
        int a = dragging && i == press_i;
        if (!a)
            draw_item_icon(s, i, r.x + (r.w - ICON) / 2, r.y + 6);
        draw_label(s, r, items[i].label, seli && focus ? 4 : 2, seli && focus && !a);
    }
    if (dragging && press_i >= 0 && press_i < nitems) {
        /* bırakılacak hücre ve sürüklenen simge */
        int c = clampi((drag_x - LEFT) / CELL_W, 0, cols() - 1), rr = clampi((drag_y - TOP) / CELL_H, 0, rows() - 1);
        Rect tr = cell_rect(c, rr);
        if (!rect_empty(rect_isect(tr, clip)))
            stroke_rrect(s, rect_inset(tr, 4), 10, 2, ALPHA(HEX(0xFFFFFF), 120));
        Rect g = { drag_x - ICON / 2, drag_y - ICON / 2, ICON, ICON };
        if (!rect_empty(rect_isect(g, clip)))
            draw_item_icon(s, press_i, g.x, g.y);
    }
}

/* ---------------------------------------------------------------- */
/* Eylemler                                                           */
/* ---------------------------------------------------------------- */

enum { DA_OPEN = 1, DA_RENAME, DA_COPY, DA_CUT, DA_TRASH, DA_PASTE, DA_NEW_DIR, DA_NEW_TEXT,
       DA_NEW_AXS, DA_NEW_GUI, DA_TERMINAL, DA_WALLPAPER, DA_SETTINGS, DA_EMPTY_TRASH, DA_REFRESH,
       DA_EDIT, DA_OPEN_TERM };

static void damage_item(int i)
{
    if (i >= 0 && i < nitems) {
        Rect r = item_rect(i);
        wm_damage((Rect){ r.x, r.y, r.w, r.h + 40 });   /* seçili ad aşağı taşabilir */
    }
}

static void set_sel(int i)
{
    if (i == sel)
        return;
    damage_item(sel);
    sel = i;
    damage_item(sel);
}

static void notify_err(const char *what)
{
    char m[200];
    snprintf(m, sizeof m, "%s", strerror(errno));
    wm_notify(what, m, IC_FILES);
}

static void open_item(int i)
{
    if (i < 0 || i >= nitems)
        return;
    if (is_trash(i)) {
        mkdir(fo_trash_dir(), 0700);
        wm_open(&APP_FILES, fo_trash_dir());
        return;
    }
    if (items[i].app[0]) {
        AppEntry *a = apps_find(items[i].app);
        if (a)
            app_launch(a, NULL);
        else
            wm_notify("Uygulama bulunamadı", "Bu kısayolun uygulaması kurulu değil (Uygulama Marketi)", IC_PACKAGES);
        return;
    }
    char p[PATH_MAX];
    item_path(i, p, sizeof p);
    wm_open_file(p);
}

static void select_name(const char *path)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    for (int i = 0; i < nitems; i++)
        if (!strcmp(items[i].name, b))
            set_sel(i);
}

static int new_kind;
static void new_done(const char *text, int ctx)
{
    (void)ctx;
    if (!text)
        return;
    char out[PATH_MAX];
    if (fo_create(desktop_dir(), text[0] ? text : NULL, new_kind, out, sizeof out) < 0) {
        notify_err("Oluşturulamadı");
        return;
    }
    scan();
    focus = 1;
    select_name(out);
    if (new_kind == FO_AXS || new_kind == FO_AXS_GUI)
        wm_open(&APP_STUDIO, out);
}

static void ask_new(int kind)
{
    static const char *titles[] = { "Yeni klasör", "Yeni metin belgesi", "Yeni Axs programı",
                                    "Yeni pencereli program (kolay arayüz)" };
    new_kind = kind;
    char init[64];
    snprintf(init, sizeof init, "%s", fo_default_name(kind));
    wm_prompt(titles[kind], "Adını yaz:", init, "Oluştur", 1, new_done, 0);
}

static char ren_path[PATH_MAX];
static void rename_done(const char *text, int ctx)
{
    if (!text || !text[0])
        return;
    char nn[300], out[PATH_MAX];
    snprintf(nn, sizeof nn, "%s", text);
    /* kısayolların uzantısı gizli: geri ekle */
    if (ctx == 1 && !ends_ci(nn, ".kisayol"))
        strncat(nn, ".kisayol", sizeof nn - strlen(nn) - 1);
    if (fo_rename(ren_path, nn, out, sizeof out) < 0) {
        notify_err("Yeniden adlandırılamadı");
        return;
    }
    /* konumu yeni ada taşı */
    const char *ob = strrchr(ren_path, '/') + 1;
    for (int i = 0; i < nitems; i++)
        if (!strcmp(items[i].name, ob)) {
            snprintf(items[i].name, sizeof items[i].name, "%s", nn);
            save_positions();
        }
    scan();
    select_name(out);
}

static void ask_rename(int i)
{
    if (i < 0 || is_trash(i))
        return;
    item_path(i, ren_path, sizeof ren_path);
    wm_prompt("Yeniden adlandır", "Yeni ad:", items[i].label, "Tamam", 1, rename_done, items[i].app[0] ? 1 : 0);
}

static void empty_done(const char *text, int ctx)
{
    (void)ctx;
    if (!text)
        return;
    fo_empty_trash();
    scan();
    wm_notify("Çöp Kutusu boşaltıldı", NULL, IC_TRASH);
}

static void trash_item(int i)
{
    if (i < 0 || i >= nitems || is_trash(i))
        return;
    char p[PATH_MAX];
    item_path(i, p, sizeof p);
    if (fo_trash(p) < 0) {
        notify_err("Çöpe taşınamadı");
        return;
    }
    sel = -1;
    scan();
}

static void paste_here(void)
{
    char out[PATH_MAX];
    if (fo_paste(desktop_dir(), out, sizeof out) < 0) {
        notify_err("Yapıştırılamadı");
        return;
    }
    scan();
    select_name(out);
}

static void menu_cb(int a, int i)
{
    char p[PATH_MAX];
    switch (a) {
    case DA_OPEN: open_item(i); break;
    case DA_EDIT:
        item_path(i, p, sizeof p);
        wm_open(&APP_STUDIO, p);
        break;
    case DA_OPEN_TERM: {
        item_path(i, p, sizeof p);
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "cd '%s' && exec bash", p);
        wm_run_in_terminal(cmd, items[i].label);
        break;
    }
    case DA_RENAME: ask_rename(i); break;
    case DA_COPY: case DA_CUT:
        item_path(i, p, sizeof p);
        fo_clip_set(p, a == DA_CUT);
        wm_notify(a == DA_CUT ? "Kesildi" : "Kopyalandı", "Yapıştırmak için sağ tık > Yapıştır (Ctrl+V)", IC_FILES);
        break;
    case DA_TRASH: trash_item(i); break;
    case DA_PASTE: paste_here(); break;
    case DA_NEW_DIR: ask_new(FO_DIR); break;
    case DA_NEW_TEXT: ask_new(FO_TEXT); break;
    case DA_NEW_AXS: ask_new(FO_AXS); break;
    case DA_NEW_GUI: ask_new(FO_AXS_GUI); break;
    case DA_TERMINAL: {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "cd '%s' && exec bash", desktop_dir());
        wm_run_in_terminal(cmd, "Terminal");
        break;
    }
    case DA_WALLPAPER:
        wallpaper_idx = (wallpaper_idx + 1) % N_WALLPAPERS;
        config_save();
        wm_wallpaper_changed();
        break;
    case DA_SETTINGS: wm_open(&APP_SETTINGS, NULL); break;
    case DA_EMPTY_TRASH:
        if (fo_trash_count())
            wm_prompt("Çöp Kutusu'nu boşalt", "İçindeki her şey kalıcı olarak silinecek.", NULL, "Boşalt", 0, empty_done, 0);
        break;
    case DA_REFRESH: scan(); break;
    }
}

static void item_menu(int i, int x, int y)
{
    MenuItem m[MENU_MAX];
    int n = 0;
    if (is_trash(i)) {
        m[n++] = (MenuItem){ "Aç", DA_OPEN, IC_FILES };
        if (fo_trash_count())
            m[n++] = (MenuItem){ "Çöpü boşalt", DA_EMPTY_TRASH, IC_TRASH };
    } else {
        m[n++] = (MenuItem){ "Aç", DA_OPEN, items[i].dir ? IC_FOLDER : IC_PLAY };
        size_t l = strlen(items[i].name);
        if (!items[i].dir && !items[i].app[0] && !items[i].img && l > 4 &&
            (ends_ci(items[i].name, ".axs") || ends_ci(items[i].name, ".txt") || ends_ci(items[i].name, ".sh") ||
             ends_ci(items[i].name, ".md") || ends_ci(items[i].name, ".bat")))
            m[n++] = (MenuItem){ "Axs Stüdyo ile düzenle", DA_EDIT, IC_STUDIO };
        if (items[i].dir)
            m[n++] = (MenuItem){ "Terminalde aç", DA_OPEN_TERM, IC_TERMINAL };
        m[n++] = (MenuItem){ "Yeniden adlandır  (F2)", DA_RENAME, IC_NEW };
        m[n++] = (MenuItem){ "Kopyala  (Ctrl+C)", DA_COPY, IC_FILE };
        m[n++] = (MenuItem){ "Kes  (Ctrl+X)", DA_CUT, IC_FILE };
        m[n++] = (MenuItem){ "Çöpe taşı  (Delete)", DA_TRASH, IC_TRASH };
    }
    wm_menu(x, y, m, n, menu_cb, i);
}

static void desk_menu(int x, int y)
{
    MenuItem m[MENU_MAX];
    int n = 0;
    m[n++] = (MenuItem){ "Yeni klasör", DA_NEW_DIR, IC_FOLDER };
    m[n++] = (MenuItem){ "Yeni metin belgesi", DA_NEW_TEXT, IC_FILE };
    m[n++] = (MenuItem){ "Yeni Axs programı", DA_NEW_AXS, IC_AXSFILE };
    m[n++] = (MenuItem){ "Yeni pencereli program", DA_NEW_GUI, IC_STUDIO };
    if (fo_clip_has())
        m[n++] = (MenuItem){ "Yapıştır  (Ctrl+V)", DA_PASTE, IC_FILES };
    m[n++] = (MenuItem){ "Burada terminal aç", DA_TERMINAL, IC_TERMINAL };
    m[n++] = (MenuItem){ "Yenile", DA_REFRESH, IC_REFRESH };
    m[n++] = (MenuItem){ "Duvar kâğıdını değiştir", DA_WALLPAPER, IC_REFRESH };
    m[n++] = (MenuItem){ "Ayarlar", DA_SETTINGS, IC_SETTINGS };
    wm_menu(x, y, m, n, menu_cb, -1);
}

/* ---------------------------------------------------------------- */
/* Girdi                                                              */
/* ---------------------------------------------------------------- */

static int item_at(int x, int y)
{
    for (int i = nitems - 1; i >= 0; i--)
        if (rect_has(rect_inset(item_rect(i), 4), x, y))
            return i;
    return -1;
}

int desktop_grabbing(void) { return press_i >= 0; }
int desktop_focused(void) { return focus; }

void desktop_blur(void)
{
    if (!focus)
        return;
    focus = 0;
    damage_item(sel);
}

static void damage_drag(void)
{
    wm_damage((Rect){ drag_x - ICON / 2 - 4, drag_y - ICON / 2 - 4, ICON + 8, ICON + 8 });
    int c = clampi((drag_x - LEFT) / CELL_W, 0, cols() - 1), r = clampi((drag_y - TOP) / CELL_H, 0, rows() - 1);
    wm_damage(cell_rect(c, r));
}

static void drop(void)
{
    int i = press_i;
    int c = clampi((drag_x - LEFT) / CELL_W, 0, cols() - 1), r = clampi((drag_y - TOP) / CELL_H, 0, rows() - 1);
    int o = occupied(c, r, i);
    if (o >= 0 && o != i) {
        char p[PATH_MAX];
        item_path(i, p, sizeof p);
        if (is_trash(o)) {
            if (!is_trash(i))
                trash_item(i);
            return;
        }
        if (items[o].dir && !is_trash(i)) {
            char d[PATH_MAX];
            item_path(o, d, sizeof d);
            if (fo_move_into(p, d) < 0)
                notify_err("Taşınamadı");
            else
                wm_notify("Taşındı", items[o].label, IC_FOLDER);
            scan();
            return;
        }
        /* dolu hücre: yer değiştir */
        items[o].col = items[i].col;
        items[o].row = items[i].row;
    }
    items[i].col = c;
    items[i].row = r;
    save_positions();
    wm_damage((Rect){ 0, TOP - 8, SCREEN_W, SCREEN_H - TOP + 8 });
}

int desktop_mouse(MouseEv *e)
{
    if (press_i >= 0) {
        if (e->kind == M_MOVE) {
            if (!dragging && (abs(e->x - press_x) > 5 || abs(e->y - press_y) > 5)) {
                dragging = 1;
                damage_item(press_i);
            }
            if (dragging) {
                damage_drag();
                drag_x = e->x;
                drag_y = e->y;
                damage_drag();
            }
        } else if (e->kind == M_UP) {
            int i = press_i;
            press_i = -1;
            if (dragging) {
                dragging = 0;
                damage_drag();
                damage_item(i);
                drag_x = e->x;
                drag_y = e->y;
                press_i = i;
                drop();
                press_i = -1;
            }
        }
        return 1;
    }
    int i = item_at(e->x, e->y);
    if (e->kind == M_MOVE) {
        if (i != hover) {
            damage_item(hover);
            hover = i;
            damage_item(hover);
        }
        return 1;
    }
    if (e->kind != M_DOWN)
        return 1;
    if (!focus) {
        focus = 1;
        damage_item(sel);
    }
    if (e->button == 2) {
        if (i >= 0) {
            set_sel(i);
            item_menu(i, e->x, e->y);
        } else {
            set_sel(-1);
            desk_menu(e->x, e->y);
        }
        return 1;
    }
    if (e->button != 1)
        return 1;
    set_sel(i);
    if (i >= 0) {
        if (e->clicks == 2) {
            open_item(i);
            return 1;
        }
        press_i = i;
        press_x = drag_x = e->x;
        press_y = drag_y = e->y;
        dragging = 0;
    }
    return 1;
}

static int nearest(int dc, int dr)
{
    if (sel < 0)
        return nitems ? 0 : -1;
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < nitems; i++) {
        int c = items[i].col - items[sel].col, r = items[i].row - items[sel].row;
        if (i == sel || (dc && c * dc <= 0) || (dr && r * dr <= 0))
            continue;
        int d = (dc ? abs(r) * 100 + abs(c) : abs(c) * 100 + abs(r));
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best >= 0 ? best : sel;
}

void desktop_key(KeyEv *e)
{
    if (!e->down)
        return;
    int ctrl = e->mods & MOD_CTRL;
    char p[PATH_MAX];
    switch (e->code) {
    case KEY_ENTER: case KEY_KPENTER: open_item(sel); break;
    case KEY_F2: ask_rename(sel); break;
    case KEY_DELETE: trash_item(sel); break;
    case KEY_F5: scan(); break;
    case KEY_UP: set_sel(nearest(0, -1)); break;
    case KEY_DOWN: set_sel(nearest(0, 1)); break;
    case KEY_LEFT: set_sel(nearest(-1, 0)); break;
    case KEY_RIGHT: set_sel(nearest(1, 0)); break;
    case KEY_ESC: set_sel(-1); break;
    case KEY_C: case KEY_X:
        if (ctrl && sel >= 0 && !is_trash(sel)) {
            item_path(sel, p, sizeof p);
            fo_clip_set(p, e->code == KEY_X);
        }
        break;
    case KEY_V:
        if (ctrl && fo_clip_has())
            paste_here();
        break;
    case KEY_N:
        if (ctrl)
            ask_new((e->mods & MOD_SHIFT) ? FO_DIR : FO_TEXT);
        break;
    }
}
