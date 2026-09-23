/* AxsDE - Uygulama Marketi: axpkg deposunu (INDEX) gezer, uygulama kurar/açar/kaldırır */
#include "axsde.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define REPO "/var/lib/axpkg/repo"
#define DB "/var/lib/axpkg/db"
#define MAXPKG 96
#define SB 214 /* kenar çubuğu genişliği */

typedef struct {
    char name[64], title[64], version[32], desc[200], about[600], category[32];
    char depends[128], file[128], app[64], run[128], open[160], icon[160];
    char inst_version[32];
    long size, isize;
    int installed;
} Pkg;

static const char *CATS[] = { "Keşfet", "Oyunlar", "Araçlar", "Grafik", "Axs", "Sistem", "Kurulu" };
#define N_CATS 7
#define CAT_INSTALLED 6

typedef struct {
    UiState ui;
    Pkg pk[MAXPKG];
    int n;
    int cat;          /* seçili kategori */
    int detail;       /* -1 = liste, yoksa paket indeksi */
    int scroll;
    TextField q;      /* arama */
    Proc job;
    int busy, busy_install, spin;
    char busy_name[64];
    char log[2048];
} MarketSt;

/* ---- depo ---- */

static void set_field(Pkg *p, const char *k, const char *v)
{
#define F(key, dst) else if (!strcmp(k, key)) snprintf(p->dst, sizeof p->dst, "%s", v)
    if (0) {}
    F("name", name); F("title", title); F("version", version); F("description", desc);
    F("about", about); F("category", category); F("depends", depends); F("file", file);
    F("app", app); F("run", run); F("open", open);
    else if (!strcmp(k, "size")) p->size = atol(v);
    else if (!strcmp(k, "installed_size")) p->isize = atol(v);
#undef F
}

static void reload(MarketSt *st)
{
    st->n = 0;
    FILE *f = fopen(REPO "/INDEX", "r");
    if (!f)
        return;
    char line[1024];
    Pkg cur;
    memset(&cur, 0, sizeof cur);
    for (;;) {
        char *r = fgets(line, sizeof line, f);
        if (r)
            line[strcspn(line, "\r\n")] = 0;
        if (!r || !line[0]) { /* kayıt sonu */
            if (cur.name[0] && st->n < MAXPKG) {
                if (!cur.title[0])
                    snprintf(cur.title, sizeof cur.title, "%s", cur.name);
                if (!cur.category[0])
                    snprintf(cur.category, sizeof cur.category, "Sistem");
                snprintf(cur.icon, sizeof cur.icon, REPO "/icons/%s.png", cur.name);
                char mp[160], buf[2048];
                snprintf(mp, sizeof mp, DB "/%s/MANIFEST", cur.name);
                if (read_file(mp, buf, sizeof buf) > 0) {
                    cur.installed = 1;
                    char *v = strstr(buf, "version=");
                    if (v)
                        snprintf(cur.inst_version, sizeof cur.inst_version, "%.*s", (int)strcspn(v + 8, "\n"), v + 8);
                }
                st->pk[st->n++] = cur;
            }
            memset(&cur, 0, sizeof cur);
            if (!r)
                break;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        set_field(&cur, line, eq + 1);
    }
    fclose(f);
    /* grafik uygulamalar önce, sonra başlığa göre */
    for (int i = 1; i < st->n; i++) {
        Pkg k = st->pk[i];
        int j = i - 1;
        while (j >= 0 && ((!st->pk[j].app[0] && k.app[0]) ||
                          (!st->pk[j].app[0] == !k.app[0] && strcasecmp(st->pk[j].title, k.title) > 0))) {
            st->pk[j + 1] = st->pk[j];
            j--;
        }
        st->pk[j + 1] = k;
    }
}

static int visible(MarketSt *st, Pkg *p)
{
    if (st->q.buf[0]) {
        const char *q = st->q.buf;
        return text_match(p->title, q) || text_match(p->name, q) || text_match(p->desc, q) || text_match(p->category, q);
    }
    if (st->cat == 0)
        return 1;
    if (st->cat == CAT_INSTALLED)
        return p->installed;
    return !strcmp(p->category, CATS[st->cat]);
}

static void pkg_icon(Surf *s, Pkg *p, int x, int y, int size)
{
    const Surf *ic = icon_png(p->icon, size);
    if (ic)
        blit_alpha(s, x, y, ic);
    else
        draw_icon(s, IC_PACKAGE, x, y, size);
}

/* ---- işlemler ---- */

static void start_job(Win *w, Pkg *p, int install)
{
    MarketSt *st = w->st;
    if (st->busy)
        return;
    char *argv[] = { "axpkg", install ? "install" : "remove", p->name, NULL };
    if (proc_start(&st->job, argv) < 0) {
        wm_notify("Uygulama Marketi", "axpkg çalıştırılamadı", IC_PACKAGES);
        return;
    }
    st->busy = 1;
    st->busy_install = install;
    snprintf(st->busy_name, sizeof st->busy_name, "%s", p->name);
    snprintf(st->log, sizeof st->log, "$ axpkg %s %s\n", install ? "install" : "remove", p->name);
    w->dirty = 1;
}

static void open_pkg(Pkg *p)
{
    if (p->app[0]) {
        apps_scan();
        AppEntry *e = apps_find(p->app);
        if (e)
            app_launch(e, NULL);
        else
            wm_notify(p->title, "Uygulama kaydı bulunamadı", IC_PACKAGES);
    } else if (p->run[0]) {
        wm_run_in_terminal(p->run, p->title);
    } else if (p->open[0]) {
        wm_open_file(p->open);
    }
}

static int can_open(Pkg *p) { return p->app[0] || p->run[0] || p->open[0]; }

/* Kur / Aç / Güncelle düğmesi (işlem sürüyorsa dönen gösterge) */
static void action_button(Win *w, Surf *s, UiState *u, Rect b, Pkg *p)
{
    MarketSt *st = w->st;
    if (st->busy && !strcmp(st->busy_name, p->name)) {
        fill_rrect(s, b, b.h / 2, T.overlay);
        /* dönen yay */
        float cx = b.x + 18.f, cy = b.y + b.h / 2.f;
        for (int i = 0; i < 8; i++) {
            float a = (st->spin * 2 + i) * 0.785f;
            fill_circle(s, cx + 6 * __builtin_cosf(a), cy + 6 * __builtin_sinf(a), 1.6f, ALPHA(T.text, 40 + i * 26));
        }
        draw_text_center(s, F_UI_BOLD, 12, (Rect){ b.x + 18, b.y, b.w - 18, b.h }, T.text,
                         st->busy_install ? "Kuruluyor" : "Kaldırılıyor");
        return;
    }
    int update = p->installed && strcmp(p->inst_version, p->version);
    if (p->installed && !update) {
        if (can_open(p)) {
            if (ui_button(s, u, b, "Aç", BTN_NORMAL))
                open_pkg(p);
        } else {
            fill_rrect(s, b, b.h / 2, ALPHA(T.green, 40));
            draw_text_center(s, F_UI_BOLD, 12, b, T.green, "Kurulu");
        }
    } else if (ui_button(s, u, b, update ? "Güncelle" : "Kur", BTN_PRIMARY) && !st->busy) {
        start_job(w, p, 1);
    }
}

/* ---- çizim ---- */

static void draw_sidebar(Win *w, Surf *s, UiState *u)
{
    MarketSt *st = w->st;
    int H = s->h;
    fill_rect(s, (Rect){ 0, 0, SB, H }, T.base);
    fill_rect(s, (Rect){ SB - 1, 0, 1, H }, T.border);
    draw_icon(s, IC_PACKAGES, 18, 18, 34);
    draw_text(s, F_UI_BOLD, 16, 62, 18, T.text, "Market");
    draw_text(s, F_UI, 11, 62, 38, T.muted, "AxsOS uygulamaları");
    Rect qr = { 14, 66, SB - 28, 34 };
    st->q.focus = 1;
    tf_draw(s, &st->q, qr, "Ara…");
    static const IconId ICS[N_CATS] = { IC_HOME, IC_PLAY, IC_CLOCK, IC_FILE, IC_AXSFILE, IC_CPU, IC_PACKAGE };
    int y = 116;
    for (int i = 0; i < N_CATS; i++) {
        if (i == CAT_INSTALLED) {
            fill_rect(s, (Rect){ 18, y + 4, SB - 36, 1 }, T.border);
            y += 12;
        }
        Rect r = { 10, y, SB - 20, 36 };
        int sel = st->cat == i && !st->q.buf[0];
        int hov = ui_hover(u, r);
        if (sel || hov)
            fill_rrect(s, r, 10, sel ? ALPHA(T.accent, 70) : T.surface);
        draw_icon(s, ICS[i], r.x + 10, r.y + 9, 18);
        draw_text(s, sel ? F_UI_BOLD : F_UI, 14, r.x + 38, r.y + 9, sel ? T.text : T.subtext, CATS[i]);
        int cnt = 0;
        for (int k = 0; k < st->n; k++)
            cnt += i == 0 ? 1 : i == CAT_INSTALLED ? st->pk[k].installed : !strcmp(st->pk[k].category, CATS[i]);
        char c[8];
        snprintf(c, sizeof c, "%d", cnt);
        draw_text(s, F_UI, 12, r.x + r.w - 12 - text_width(F_UI, 12, c), r.y + 11, T.muted, c);
        if (ui_clicked(u, r)) {
            st->cat = i;
            st->detail = -1;
            st->scroll = 0;
            tf_set(&st->q, "");
        }
        y += 40;
    }
    /* işlem günlüğünün son satırı */
    if (st->log[0]) {
        char tmp[2048];
        snprintf(tmp, sizeof tmp, "%s", st->log);
        char *last = NULL;
        for (char *t = strtok(tmp, "\n"); t; t = strtok(NULL, "\n"))
            last = t;
        if (last) {
            /* renk kodlarını at */
            char clean[256], *o = clean;
            for (char *p = last; *p && o < clean + 250; p++) {
                if (*p == '\033') {
                    while (*p && *p != 'm')
                        p++;
                    if (!*p)
                        break;
                    continue;
                }
                *o++ = *p;
            }
            *o = 0;
            fill_rrect(s, (Rect){ 10, H - 64, SB - 20, 52 }, 10, T.surface);
            draw_text(s, F_UI_BOLD, 10, 20, H - 56, T.muted, "SON İŞLEM");
            draw_text_wrap(s, F_UI, 11, 20, H - 40, SB - 40, 2, 14, T.subtext, clean);
        }
    }
}

static void draw_card(Win *w, Surf *s, UiState *u, Rect r, int i)
{
    MarketSt *st = w->st;
    Pkg *p = &st->pk[i];
    int hov = ui_hover(u, r);
    fill_rrect(s, r, 16, hov ? lighten(T.surface2, 10) : T.surface2);
    stroke_rrect(s, r, 16, 1, ALPHA(HEX(0xFFFFFF), hov ? 26 : 12));
    pkg_icon(s, p, r.x + 16, r.y + 16, 60);
    draw_text_fit(s, F_UI_BOLD, 15, r.x + 90, r.y + 20, r.w - 104, T.text, p->title);
    char sub[96];
    snprintf(sub, sizeof sub, "%s  •  %s", p->category, p->app[0] ? "Uygulama" : p->run[0] ? "Terminal" : "İçerik");
    draw_text_fit(s, F_UI, 12, r.x + 90, r.y + 44, r.w - 104, T.muted, sub);
    draw_text_wrap(s, F_UI, 13, r.x + 16, r.y + 88, r.w - 32, 2, 18, T.subtext, p->desc);
    Rect b = { r.x + r.w - 104, r.y + r.h - 46, 88, 32 };
    if (p->installed)
        ui_badge(s, r.x + 16, r.y + r.h - 38, "✓ Kurulu", ALPHA(T.green, 50), T.green);
    action_button(w, s, u, b, p);
    /* kartın geri kalanına tıklama: ayrıntı */
    if (ui_clicked(u, r) && !rect_has(b, u->mx, u->my)) {
        st->detail = i;
        st->scroll = 0;
    }
}

static void draw_hero(Win *w, Surf *s, UiState *u, Rect r)
{
    MarketSt *st = w->st;
    /* öne çıkan: kurulu olmayan ilk grafik uygulama (yoksa ilk uygulama) */
    int fi = -1;
    for (int i = 0; i < st->n && fi < 0; i++)
        if (st->pk[i].app[0] && !st->pk[i].installed)
            fi = i;
    for (int i = 0; i < st->n && fi < 0; i++)
        if (st->pk[i].app[0])
            fi = i;
    if (fi < 0)
        return;
    Pkg *p = &st->pk[fi];
    fill_rrect_vgrad(s, r, 20, lighten(T.accent, -10), lighten(T.accent2, -90));
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, r));
    /* köşelere değmeyen süs daireleri */
    fill_circle(s, r.x + r.w - 126.f, r.y + r.h / 2.f, 118, ALPHA(HEX(0xFFFFFF), 16));
    fill_circle(s, r.x + r.w - 300.f, r.y + r.h + 40.f, 90, ALPHA(HEX(0xFFFFFF), 10));
    s->clip = oc;
    pkg_icon(s, p, r.x + r.w - 190, r.y + (r.h - 128) / 2, 128);
    draw_text(s, F_UI_BOLD, 11, r.x + 28, r.y + 26, ALPHA(HEX(0xFFFFFF), 200), "ÖNE ÇIKAN");
    draw_text(s, F_UI_BOLD, 30, r.x + 28, r.y + 46, HEX(0xFFFFFF), p->title);
    draw_text_wrap(s, F_UI, 14, r.x + 28, r.y + 90, r.w - 260, 2, 20, ALPHA(HEX(0xFFFFFF), 220), p->desc);
    Rect b = { r.x + 28, r.y + r.h - 58, 110, 36 };
    action_button(w, s, u, b, p);
    Rect d = { b.x + 120, b.y, 110, 36 };
    if (ui_button(s, u, d, "Ayrıntılar", BTN_GHOST)) {
        st->detail = fi;
        st->scroll = 0;
    }
}

static void fmt_kb(char *out, size_t n, long b) { fmt_size(out, n, b); }

static void draw_detail(Win *w, Surf *s, UiState *u, Rect area)
{
    MarketSt *st = w->st;
    Pkg *p = &st->pk[st->detail];
    int x = area.x, y = area.y - st->scroll;
    if (ui_button(s, u, (Rect){ x, y, 84, 32 }, "‹  Geri", BTN_GHOST)) {
        st->detail = -1;
        st->scroll = 0;
        return;
    }
    y += 50;
    pkg_icon(s, p, x, y, 112);
    draw_text(s, F_UI_BOLD, 28, x + 136, y + 8, T.text, p->title);
    char sub[160];
    snprintf(sub, sizeof sub, "%s  •  sürüm %s  •  %s", p->category, p->version,
             p->app[0] ? "Grafik uygulama" : p->run[0] ? "Terminal programı" : "İçerik paketi");
    draw_text(s, F_UI, 13, x + 136, y + 48, T.muted, sub);
    Rect b = { x + 136, y + 74, 120, 36 };
    action_button(w, s, u, b, p);
    if (p->installed && !(st->busy && !strcmp(st->busy_name, p->name))) {
        if (ui_button(s, u, (Rect){ b.x + 130, b.y, 100, 36 }, "Kaldır", BTN_DANGER) && !st->busy)
            start_job(w, p, 0);
    }
    y += 136;
    fill_rect(s, (Rect){ x, y, area.w, 1 }, T.border);
    y += 20;
    int colw = area.w - 260;
    draw_text(s, F_UI_BOLD, 16, x, y, T.text, "Hakkında");
    draw_text_wrap(s, F_UI, 14, x, y + 30, colw, 10, 21, T.subtext, p->about[0] ? p->about : p->desc);
    /* bilgi kutusu */
    Rect ib = { x + area.w - 240, y, 240, 206 };
    fill_rrect(s, ib, 14, T.surface2);
    char sz[32], isz[32];
    fmt_kb(sz, sizeof sz, p->size);
    fmt_kb(isz, sizeof isz, p->isize);
    const char *rows[5][2] = {
        { "Paket", p->name }, { "Sürüm", p->version }, { "İndirme", sz }, { "Kurulu boyut", isz },
        { "Bağımlılık", p->depends[0] ? p->depends : "yok" },
    };
    for (int i = 0; i < 5; i++) {
        draw_text(s, F_UI, 12, ib.x + 16, ib.y + 16 + i * 36, T.muted, rows[i][0]);
        draw_text_fit(s, F_UI_BOLD, 13, ib.x + 16, ib.y + 32 + i * 36, ib.w - 32, T.text, rows[i][1]);
    }
    if (p->installed && p->inst_version[0] && strcmp(p->inst_version, p->version)) {
        char t[96];
        snprintf(t, sizeof t, "Kurulu sürüm %s — güncelleme var", p->inst_version);
        draw_text(s, F_UI, 12, x, ib.y + ib.h + 10, T.yellow, t);
    }
    if (p->run[0]) {
        char t[200];
        snprintf(t, sizeof t, "Terminalden çalıştır:  %s", p->run);
        fill_rrect(s, (Rect){ x, ib.y + ib.h + 34, colw, 36 }, 10, HEX(0x13131D));
        draw_text(s, F_MONO, 13, x + 14, ib.y + ib.h + 43, T.green, t);
    }
}

static void m_draw(Win *w, Surf *s)
{
    MarketSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ SB, 0, W - SB, H }, T.surface);
    draw_sidebar(w, s, u);

    Rect area = { SB + 28, 24, W - SB - 56, H - 24 };
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, (Rect){ SB, 0, W - SB, H }));
    if (st->detail >= 0 && st->detail < st->n) {
        draw_detail(w, s, u, area);
        s->clip = oc;
        return;
    }
    int y = area.y - st->scroll;
    const char *head = st->q.buf[0] ? "Arama sonuçları" : CATS[st->cat];
    draw_text(s, F_UI_BOLD, 26, area.x, y, T.text, head);
    y += 48;
    if (st->cat == 0 && !st->q.buf[0]) {
        draw_hero(w, s, u, (Rect){ area.x, y, area.w, 190 });
        y += 214;
        draw_text(s, F_UI_BOLD, 17, area.x, y, T.text, "Tüm uygulamalar");
        y += 34;
    }
    int cols = maxi(1, (area.w + 16) / 256);
    int cw = (area.w - (cols - 1) * 16) / cols, ch = 176;
    int shown = 0;
    for (int i = 0; i < st->n; i++) {
        if (!visible(st, &st->pk[i]))
            continue;
        int row = shown / cols, col = shown % cols;
        shown++;
        Rect r = { area.x + col * (cw + 16), y + row * (ch + 16), cw, ch };
        if (r.y + r.h < 0 || r.y > H)
            continue;
        draw_card(w, s, u, r, i);
    }
    if (!shown)
        draw_text(s, F_UI, 14, area.x, y + 10, T.muted,
                  st->q.buf[0] ? "Eşleşen uygulama yok" : st->cat == CAT_INSTALLED ? "Henüz kurulu uygulama yok"
                                                                              : "Bu kategoride uygulama yok");
    int total = (y + st->scroll) + ((shown + cols - 1) / cols) * (ch + 16) + 8;
    s->clip = oc;
    int maxs = maxi(0, total - H);
    if (st->scroll > maxs) {
        st->scroll = maxs;
        w->dirty = 1;
    }
    ui_scrollbar(s, (Rect){ W - 10, 8, 5, H - 16 }, total, H, st->scroll);
}

static void m_init(Win *w, const char *arg)
{
    (void)arg;
    MarketSt *st = calloc(1, sizeof *st);
    st->ui.cur_hot = -1;
    st->job.fd = -1;
    st->detail = -1;
    w->st = st;
    w->min_w = 760;
    w->min_h = 480;
    reload(st);
}

static void m_mouse(Win *w, MouseEv *e)
{
    MarketSt *st = w->st;
    if (e->kind == M_WHEEL) {
        st->scroll = maxi(0, st->scroll - e->wheel * 60);
        w->dirty = 1;
        return;
    }
    ui_dispatch(w, &st->ui, e);
}

static void m_key(Win *w, KeyEv *e)
{
    MarketSt *st = w->st;
    if (!e->down)
        return;
    if (e->code == KEY_ESC) {
        if (st->detail >= 0)
            st->detail = -1;
        else
            tf_set(&st->q, "");
        st->scroll = 0;
        w->dirty = 1;
        return;
    }
    if (e->code == KEY_PAGEDOWN || e->code == KEY_PAGEUP) {
        st->scroll = maxi(0, st->scroll + (e->code == KEY_PAGEDOWN ? 400 : -400));
        w->dirty = 1;
        return;
    }
    if (tf_key(&st->q, e)) {
        st->detail = -1;
        st->scroll = 0;
        w->dirty = 1;
    }
}

static int m_fds(Win *w, int *fds, int max)
{
    MarketSt *st = w->st;
    if (st->busy && st->job.fd >= 0 && max > 0) {
        fds[0] = st->job.fd;
        return 1;
    }
    return 0;
}

static void m_io(Win *w, int fd)
{
    (void)fd;
    MarketSt *st = w->st;
    int done = proc_read(&st->job);
    if (st->job.out) {
        size_t l = strlen(st->log);
        snprintf(st->log + l, sizeof st->log - l, "%s", st->job.out);
        st->job.len = 0;
        st->job.out[0] = 0;
    }
    if (done) {
        int ok = st->job.status == 0;
        char title[64] = "", msg[160];
        for (int i = 0; i < st->n; i++)
            if (!strcmp(st->pk[i].name, st->busy_name))
                snprintf(title, sizeof title, "%s", st->pk[i].title);
        snprintf(msg, sizeof msg, "%s %s", title[0] ? title : st->busy_name,
                 ok ? (st->busy_install ? "kuruldu. Başlatıcıda ve Market'te “Aç” ile başlat." : "kaldırıldı.")
                    : "işlemi başarısız oldu.");
        wm_notify(ok ? "Uygulama Marketi" : "Uygulama Marketi — hata", msg, IC_PACKAGES);
        proc_free(&st->job);
        st->busy = 0;
        reload(st);
        apps_scan(); /* başlatıcı yeni .app kayıtlarını görsün */
    }
    w->dirty = 1;
}

static void m_tick(Win *w)
{
    MarketSt *st = w->st;
    if (st->busy) {
        st->spin++;
        w->dirty = 1;
    }
}

static void m_close(Win *w)
{
    MarketSt *st = w->st;
    proc_free(&st->job);
    free(st);
}

const App APP_PACKAGES = {
    .id = "paketler", .name = "Uygulama Marketi", .desc = "Uygulama keşfet, kur ve kaldır",
    .icon = IC_PACKAGES, .w = 1000, .h = 660, .single = 1,
    .init = m_init, .draw = m_draw, .mouse = m_mouse, .key = m_key, .fds = m_fds, .io = m_io, .tick = m_tick,
    .close = m_close,
};
