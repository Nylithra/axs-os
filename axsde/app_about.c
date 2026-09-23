/* AxsDE - Hoş geldin / Hakkında */
#include "axsde.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

typedef struct {
    UiState ui;
    char kernel[64], cpu[96], mem[48], axs[64];
    Proc axsp;
    int welcome;
} AboutSt;

static void a_init(Win *w, const char *arg)
{
    AboutSt *st = calloc(1, sizeof *st);
    w->st = st;
    st->ui.cur_hot = -1;
    st->welcome = arg && !strcmp(arg, "welcome");
    wm_set_title(w, st->welcome ? "AxsOS'a hoş geldin" : "AxsOS hakkında");
    struct utsname u;
    if (uname(&u) == 0)
        snprintf(st->kernel, sizeof st->kernel, "Linux %s", u.release);
    char buf[4096];
    snprintf(st->cpu, sizeof st->cpu, "Bilinmiyor");
    if (read_file("/proc/cpuinfo", buf, sizeof buf) > 0) {
        char *p = strstr(buf, "model name");
        if (p && (p = strchr(p, ':'))) {
            p += 2;
            p[strcspn(p, "\n")] = 0;
            snprintf(st->cpu, sizeof st->cpu, "%s", p);
        }
    }
    if (read_file("/proc/meminfo", buf, sizeof buf) > 0) {
        long kb = 0;
        sscanf(buf, "MemTotal: %ld", &kb);
        fmt_size(st->mem, sizeof st->mem, (long long)kb * 1024);
    }
    snprintf(st->axs, sizeof st->axs, "yükleniyor…");
    char *argv[] = { "axs", "-s", NULL };
    if (proc_start(&st->axsp, argv) < 0)
        snprintf(st->axs, sizeof st->axs, "bulunamadı");
}

static void action_card(Surf *s, UiState *u, Rect r, IconId ic, const char *title, const char *sub, const App *app)
{
    int hov = ui_hover(u, r);
    fill_rrect(s, r, 14, hov ? T.overlay : T.surface2);
    stroke_rrect(s, r, 14, 1, hov ? ALPHA(T.accent, 200) : ALPHA(HEX(0xFFFFFF), 14));
    draw_icon(s, ic, r.x + 16, r.y + (r.h - 44) / 2, 44);
    draw_text(s, F_UI_BOLD, 14, r.x + 74, r.y + 18, T.text, title);
    draw_text_fit(s, F_UI, 12, r.x + 74, r.y + 40, r.w - 86, T.subtext, sub);
    if (ui_clicked(u, r))
        wm_open(app, NULL);
}

static void a_draw(Win *w, Surf *s)
{
    AboutSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.surface);

    /* kahraman bölümü */
    Rect hero = { 0, 0, W, 170 };
    for (int y = 0; y < hero.h; y++) {
        uint32_t c = mix(lighten(T.accent, -70), T.surface, y * 255 / hero.h);
        fill_rect(s, (Rect){ 0, y, W, 1 }, c);
    }
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, hero));
    fill_circle(s, W - 90.f, 40.f, 120.f, ALPHA(T.accent2, 40));
    fill_circle(s, W - 210.f, 150.f, 60.f, ALPHA(HEX(0x3B82F6), 30));
    s->clip = oc;
    fill_rrect_vgrad(s, (Rect){ 36, 36, 96, 96 }, 26, T.accent2, lighten(T.accent, -50));
    stroke_rrect(s, (Rect){ 36, 36, 96, 96 }, 26, 1, ALPHA(HEX(0xFFFFFF), 60));
    draw_logo(s, 84, 86, 62, HEX(0xFFFFFF), HEX(0xE9D5FF));
    draw_text(s, F_UI_BOLD, 28, 156, 44, HEX(0xFFFFFF), st->welcome ? "AxsOS'a hoş geldin!" : "AxsOS 0.1");
    draw_text(s, F_UI, 15, 158, 88, ALPHA(HEX(0xFFFFFF), 210), "Axs dilinin evi — Linux üzerinde, sıfırdan yazılmış bir sistem.");
    ui_badge(s, 158, 118, "AxsDE " AXSDE_VERSION, ALPHA(HEX(0xFFFFFF), 40), HEX(0xFFFFFF));
    ui_badge(s, 158 + 96, 118, st->kernel, ALPHA(HEX(0xFFFFFF), 40), HEX(0xFFFFFF));

    /* bilgi satırları */
    int y = 190;
    struct { IconId ic; const char *k; const char *v; } rows[] = {
        { IC_CPU, "İşlemci", st->cpu },
        { IC_MEMORY, "Bellek", st->mem },
        { IC_STUDIO, "Axs", st->axs },
    };
    for (int i = 0; i < 3; i++) {
        int cw3 = (W - 72 - 24) / 3;
        Rect r = { 36 + i * (cw3 + 12), y, cw3, 62 };
        fill_rrect(s, r, 12, T.base);
        draw_icon(s, rows[i].ic, r.x + 12, r.y + 17, 28);
        draw_text(s, F_UI, 11, r.x + 50, r.y + 12, T.muted, rows[i].k);
        draw_text_fit(s, F_UI_BOLD, 13, r.x + 50, r.y + 31, r.w - 60, T.text, rows[i].v);
    }

    y += 84;
    draw_text(s, F_UI_BOLD, 15, 36, y, T.text, "Hemen başla");
    y += 30;
    int cw = (W - 72 - 12) / 2;
    action_card(s, u, (Rect){ 36, y, cw, 72 }, IC_STUDIO, "Axs Stüdyo", "Axs kodu yaz ve tek tıkla çalıştır", &APP_STUDIO);
    action_card(s, u, (Rect){ 48 + cw, y, cw, 72 }, IC_TERMINAL, "Terminal", "axsh kabuğu: axs, axpkg, busybox", &APP_TERMINAL);
    y += 84;
    action_card(s, u, (Rect){ 36, y, cw, 72 }, IC_PACKAGES, "Paket Merkezi", "axpkg ile uygulama kur/kaldır", &APP_PACKAGES);
    action_card(s, u, (Rect){ 48 + cw, y, cw, 72 }, IC_FILES, "Dosyalar", "Axs örneklerine göz at", &APP_FILES);

    y += 96;
    if (y + 60 < H) {
        draw_text(s, F_UI_BOLD, 13, 36, y, T.subtext, "Kısayollar");
        const char *tips[] = { "Super / Ctrl+Boşluk  başlatıcı", "Ctrl+Alt+T  terminal", "Alt+Tab  pencere değiştir",
                               "Alt+F4  pencereyi kapat", "Çift tık başlık  büyüt", "Sağ tık masaüstü  menü" };
        for (int i = 0; i < 6; i++)
            draw_text(s, F_UI, 12, 36 + (i % 3) * ((W - 72) / 3), y + 24 + (i / 3) * 22, T.muted, tips[i]);
    }
}

static void a_mouse(Win *w, MouseEv *e)
{
    AboutSt *st = w->st;
    ui_dispatch(w, &st->ui, e);
}

static int a_fds(Win *w, int *fds, int max)
{
    AboutSt *st = w->st;
    if (st->axsp.fd >= 0 && !st->axsp.done && max > 0) {
        fds[0] = st->axsp.fd;
        return 1;
    }
    return 0;
}

static void a_io(Win *w, int fd)
{
    (void)fd;
    AboutSt *st = w->st;
    if (proc_read(&st->axsp)) {
        char *o = st->axsp.out ? st->axsp.out : "";
        o[strcspn(o, "\n")] = 0;
        snprintf(st->axs, sizeof st->axs, "%s", *o ? o : "bulunamadı");
        w->dirty = 1;
    }
}

static void a_close(Win *w)
{
    AboutSt *st = w->st;
    proc_free(&st->axsp);
    free(st);
}

const App APP_ABOUT = {
    .id = "hakkinda", .name = "Hoş Geldin", .desc = "AxsOS hakkında ve ilk adımlar",
    .icon = IC_ABOUT, .w = 760, .h = 560, .single = 1,
    .init = a_init, .draw = a_draw, .mouse = a_mouse, .fds = a_fds, .io = a_io, .close = a_close,
};
