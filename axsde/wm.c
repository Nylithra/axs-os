/* AxsDE - pencere yöneticisi, birleştirici, üst çubuk, dock, başlatıcı, menüler */
#define _GNU_SOURCE
#include "axsde.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <arpa/inet.h>
#include <math.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BAR_H      34
#define DOCK_ICON  50
#define DOCK_PAD   10
#define DOCK_H     (DOCK_ICON + 2 * DOCK_PAD + 4)
#define WIN_RAD    12
#define SHADOW     28
#define MAX_WINS   24
#define MAX_DMG    24

const App *ALL_APPS[] = { &APP_TERMINAL, &APP_FILES, &APP_STUDIO, &APP_PACKAGES,
                          &APP_MONITOR, &APP_SETTINGS, &APP_ABOUT };
int N_APPS = (int)(sizeof ALL_APPS / sizeof ALL_APPS[0]);

int wallpaper_idx;
int tz_offset_min = 180; /* Türkiye: UTC+3 */
const char *WALLPAPER_NAMES[N_WALLPAPERS] = { "Axs Gecesi", "Derin Okyanus", "Gün Batımı", "Kuzey Işıkları", "Sade Grafit" };

static Surf screen, wall, barbg;
static Win *wins[MAX_WINS]; /* z sırası: 0 en alt */
static int nwins, next_id = 1;
static Rect dmg[MAX_DMG];
static int ndmg;
static int mouse_x, mouse_y;
static int quit_code = -1;
static char clock_text[64];
static char net_text[64];

/* sürükleme / boyutlandırma / yakalama */
static Win *drag_win, *resize_win, *grab_win;
static Rect resize_box; /* boyutlandırma önizlemesi (ekran koordinatı) */
static int drag_dx, drag_dy;
static int title_btn_win = -1, title_btn = -1; /* basılan başlık düğmesi */
static int hover_title_win = -1, hover_title_btn = -1;
static long last_click_ms;
static int last_click_x, last_click_y, last_click_btn;

/* dock */
static int dock_hover = -1;
static const App *DOCK[] = { &APP_TERMINAL, &APP_FILES, &APP_STUDIO, &APP_PACKAGES,
                             &APP_MONITOR, &APP_SETTINGS, &APP_ABOUT };
#define N_DOCK ((int)(sizeof DOCK / sizeof DOCK[0]))

/* başlatıcı */
static int launcher_open;
static TextField lq;
static int lsel;
static Surf launcher_bg;
static int launcher_hover = -1;

/* menüler */
typedef struct { const char *label; int action; IconId icon; } MenuItem;
static MenuItem menu_items[8];
static int menu_n, menu_open, menu_hover = -1;
static Rect menu_rect;
enum { ACT_NONE, ACT_POWEROFF, ACT_REBOOT, ACT_CONSOLE, ACT_TERMINAL, ACT_WALLPAPER,
       ACT_SETTINGS, ACT_ABOUT, ACT_FILES, ACT_STUDIO };

/* bildirimler */
typedef struct { char title[64], body[160]; IconId icon; long until; } Notif;
static Notif notifs[4];
static int nnotif;

/* ------------------------------------------------------------------ */
/* Hasar (yeniden çizilecek bölgeler)                                  */
/* ------------------------------------------------------------------ */

void wm_damage(Rect r)
{
    r = rect_isect(r, (Rect){ 0, 0, SCREEN_W, SCREEN_H });
    if (rect_empty(r))
        return;
    for (int i = 0; i < ndmg; i++) {
        /* kesişen ya da çok yakın bölgeleri birleştir */
        Rect u = rect_union(dmg[i], r);
        long ua = (long)u.w * u.h, sa = (long)dmg[i].w * dmg[i].h + (long)r.w * r.h;
        if (ua <= sa + 8192) {
            dmg[i] = u;
            return;
        }
    }
    if (ndmg < MAX_DMG) {
        dmg[ndmg++] = r;
    } else {
        dmg[0] = rect_union(dmg[0], r);
    }
}

void wm_damage_all(void) { wm_damage((Rect){ 0, 0, SCREEN_W, SCREEN_H }); }

static Rect win_outer(Win *w)
{
    return (Rect){ w->r.x - SHADOW, w->r.y - SHADOW + 6, w->r.w + 2 * SHADOW, w->r.h + 2 * SHADOW + 6 };
}

Rect wm_content_rect(Win *w)
{
    return (Rect){ w->r.x, w->r.y + TITLE_H, w->r.w, w->r.h - TITLE_H };
}

static Rect work_area(void)
{
    return (Rect){ 0, BAR_H, SCREEN_W, SCREEN_H - BAR_H - DOCK_H - 8 };
}

static Rect dock_rect(void)
{
    int w = N_DOCK * (DOCK_ICON + 12) + 2 * DOCK_PAD - 12 + 20;
    return (Rect){ (SCREEN_W - w) / 2, SCREEN_H - DOCK_H - 8, w, DOCK_H };
}

/* ------------------------------------------------------------------ */
/* Duvar kâğıtları                                                     */
/* ------------------------------------------------------------------ */

typedef struct { float x, y, r; uint32_t c; float k; } Glow;

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

void wallpaper_render(Surf *dst, int idx)
{
    uint32_t c0, c1, c2;
    Glow g[4];
    int ng = 0, stars = 0;
    uint32_t ac = T.accent;
    switch (idx) {
    default:
    case 0:
        c0 = HEX(0x0B0A1A); c1 = HEX(0x1A1240); c2 = HEX(0x0E1030);
        g[ng++] = (Glow){ .18f, .22f, .55f, ac, .55f };
        g[ng++] = (Glow){ .85f, .80f, .50f, HEX(0x2563EB), .40f };
        g[ng++] = (Glow){ .70f, .15f, .30f, HEX(0xEC4899), .22f };
        stars = 1;
        break;
    case 1:
        c0 = HEX(0x051923); c1 = HEX(0x0B3142); c2 = HEX(0x0F4C5C);
        g[ng++] = (Glow){ .80f, .25f, .55f, HEX(0x22D3EE), .35f };
        g[ng++] = (Glow){ .15f, .85f, .60f, HEX(0x0EA5E9), .30f };
        break;
    case 2:
        c0 = HEX(0x120C2B); c1 = HEX(0x3B1E54); c2 = HEX(0x9B3D5A);
        g[ng++] = (Glow){ .50f, 1.05f, .65f, HEX(0xFB923C), .60f };
        g[ng++] = (Glow){ .15f, .20f, .40f, HEX(0x7C3AED), .30f };
        stars = 1;
        break;
    case 3:
        c0 = HEX(0x030B14); c1 = HEX(0x06202A); c2 = HEX(0x071A24);
        g[ng++] = (Glow){ .30f, .30f, .45f, HEX(0x10B981), .45f };
        g[ng++] = (Glow){ .65f, .20f, .40f, HEX(0x22D3EE), .35f };
        g[ng++] = (Glow){ .80f, .45f, .30f, HEX(0xA855F7), .25f };
        stars = 1;
        break;
    case 4:
        c0 = HEX(0x1C1C24); c1 = HEX(0x22222C); c2 = HEX(0x15151B);
        g[ng++] = (Glow){ .50f, .45f, .70f, ac, .10f };
        break;
    }
    float W = (float)dst->w, H = (float)dst->h;
    float asp = W / H;
    for (int y = 0; y < dst->h; y++) {
        uint32_t *row = &dst->px[y * dst->stride];
        float fy = y / H;
        for (int x = 0; x < dst->w; x++) {
            float fx = x / W;
            float t = fx * 0.35f + fy * 0.65f;
            uint32_t c = t < 0.5f ? mix(c0, c1, (int)(t * 2 * 255)) : mix(c1, c2, (int)((t - .5f) * 2 * 255));
            for (int i = 0; i < ng; i++) {
                float dx = (fx - g[i].x) * asp, dy = fy - g[i].y;
                float d2 = (dx * dx + dy * dy) / (g[i].r * g[i].r);
                if (d2 < 1) {
                    float k = (1 - d2);
                    k = k * k * g[i].k;
                    c = mix(c, g[i].c, (int)(k * 255));
                }
            }
            /* renk bantlanmasını önlemek için hafif gürültü */
            uint32_t h = hash32((uint32_t)(y * 7919 + x));
            int n = (int)(h & 3) - 1;
            int r = clampi((int)((c >> 16) & 255) + n, 0, 255);
            int gg = clampi((int)((c >> 8) & 255) + n, 0, 255);
            int b = clampi((int)(c & 255) + n, 0, 255);
            if (stars && (h >> 8) % 2400 == 0) {
                int a = 80 + (int)((h >> 20) & 127);
                r = r + (255 - r) * a / 255;
                gg = gg + (255 - gg) * a / 255;
                b = b + (255 - b) * a / 255;
            }
            row[x] = RGB(r, gg, b);
        }
    }
    /* ortada büyük, silik Axs logosu */
    surf_noclip(dst);
    draw_logo(dst, W / 2, H / 2 - H * 0.02f, fminf(W, H) * 0.34f, ALPHA(HEX(0xFFFFFF), 14), ALPHA(HEX(0xFFFFFF), 4));
}

static void make_wallpaper(void)
{
    surf_resize(&wall, SCREEN_W, SCREEN_H);
    wallpaper_render(&wall, wallpaper_idx);

    /* üst çubuğun buzlu cam arka planı */
    surf_resize(&barbg, SCREEN_W, BAR_H);
    for (int y = 0; y < BAR_H; y++)
        memcpy(&barbg.px[y * SCREEN_W], &wall.px[y * SCREEN_W], (size_t)SCREEN_W * 4);
    blur_region(&barbg, (Rect){ 0, 0, SCREEN_W, BAR_H }, 12);
    surf_noclip(&barbg);
    fill_rect(&barbg, (Rect){ 0, 0, SCREEN_W, BAR_H }, ALPHA(HEX(0x0A0A12), 120));
    fill_rect(&barbg, (Rect){ 0, BAR_H - 1, SCREEN_W, 1 }, ALPHA(HEX(0xFFFFFF), 18));
}

void wm_wallpaper_changed(void)
{
    make_wallpaper();
    for (int i = 0; i < nwins; i++)
        wins[i]->dirty = 1;
    wm_damage_all();
}

/* ------------------------------------------------------------------ */
/* Pencereler                                                          */
/* ------------------------------------------------------------------ */

Win *wm_focused(void)
{
    for (int i = nwins - 1; i >= 0; i--)
        if (!wins[i]->minimized)
            return wins[i];
    return NULL;
}

Win *wm_find(const App *app)
{
    for (int i = nwins - 1; i >= 0; i--)
        if (wins[i]->app == app)
            return wins[i];
    return NULL;
}

static int win_index(Win *w)
{
    for (int i = 0; i < nwins; i++)
        if (wins[i] == w)
            return i;
    return -1;
}

static void damage_win(Win *w) { wm_damage(win_outer(w)); }

static void damage_bar(void) { wm_damage((Rect){ 0, 0, SCREEN_W, BAR_H }); }

/* Dikdörtgenin yalnızca kenar şeritlerini hasarla (içini yeniden çizme) */
static void damage_outline(Rect r)
{
    if (rect_empty(r))
        return;
    int t = 4;
    wm_damage((Rect){ r.x - t, r.y - t, r.w + 2 * t, 2 * t });
    wm_damage((Rect){ r.x - t, r.y + r.h - t, r.w + 2 * t, 2 * t });
    wm_damage((Rect){ r.x - t, r.y - t, 2 * t, r.h + 2 * t });
    wm_damage((Rect){ r.x + r.w - t, r.y - t, 2 * t, r.h + 2 * t });
}

void wm_focus(Win *w)
{
    int i = win_index(w);
    if (i < 0)
        return;
    /* önceki odak: w dışındaki en üst görünür pencere */
    Win *old = NULL;
    for (int k = nwins - 1; k >= 0 && !old; k--)
        if (wins[k] != w && !wins[k]->minimized)
            old = wins[k];
    w->minimized = 0;
    memmove(&wins[i], &wins[i + 1], sizeof(Win *) * (nwins - i - 1));
    wins[nwins - 1] = w;
    if (old && old != w) {
        old->dirty = 1;
        damage_win(old);
    }
    w->dirty = 1;
    damage_win(w);
    damage_bar();
    wm_damage(dock_rect());
}

void wm_win_dirty(Win *w) { w->dirty = 1; }

void wm_set_title(Win *w, const char *title)
{
    snprintf(w->title, sizeof w->title, "%s", title);
    wm_damage((Rect){ w->r.x, w->r.y, w->r.w, TITLE_H });
    damage_bar();
}

static void win_resize(Win *w, int cw, int ch)
{
    cw = maxi(cw, w->min_w);
    ch = maxi(ch, w->min_h);
    Rect wa = work_area();
    cw = mini(cw, SCREEN_W);
    ch = mini(ch, wa.h + DOCK_H - TITLE_H);
    damage_win(w);
    w->r.w = cw;
    w->r.h = ch + TITLE_H;
    surf_resize(&w->content, cw, ch);
    if (w->app->resize)
        w->app->resize(w);
    w->dirty = 1;
    damage_win(w);
}

Win *wm_open(const App *app, const char *arg)
{
    if (app->single) {
        Win *e = wm_find(app);
        if (e) {
            wm_focus(e);
            return e;
        }
    }
    if (nwins >= MAX_WINS) {
        wm_notify("Çok fazla pencere", "Yeni pencere açmadan önce birini kapatın.", IC_ABOUT);
        return NULL;
    }
    Win *w = calloc(1, sizeof *w);
    w->id = next_id++;
    w->app = app;
    snprintf(w->title, sizeof w->title, "%s", app->name);
    w->min_w = 320;
    w->min_h = 200;
    Rect wa = work_area();
    int cw = mini(app->w, wa.w - 40), ch = mini(app->h, wa.h - TITLE_H - 20);
    /* basamaklı yerleşim */
    int off = (nwins % 6) * 32;
    int x = (SCREEN_W - cw) / 2 - 80 + off;
    int y = wa.y + maxi(16, (wa.h - ch - TITLE_H) / 2 - 60) + off;
    w->r = (Rect){ clampi(x, 8, SCREEN_W - cw - 8), clampi(y, BAR_H + 8, SCREEN_H - 120), cw, ch + TITLE_H };
    w->content = surf_new(cw, ch);
    w->dirty = 1;
    wins[nwins++] = w;
    if (app->init)
        app->init(w, arg);
    wm_focus(w);
    return w;
}

void wm_close(Win *w)
{
    int i = win_index(w);
    if (i < 0)
        return;
    damage_win(w);
    if (w->app->close)
        w->app->close(w);
    if (drag_win == w) drag_win = NULL;
    if (resize_win == w) resize_win = NULL;
    if (grab_win == w) grab_win = NULL;
    surf_free(&w->content);
    surf_free(&w->chrome);
    memmove(&wins[i], &wins[i + 1], sizeof(Win *) * (nwins - i - 1));
    nwins--;
    free(w);
    Win *f = wm_focused();
    if (f) {
        f->dirty = 1;
        damage_win(f);
    }
    damage_bar();
    wm_damage(dock_rect());
}

static void toggle_max(Win *w)
{
    damage_win(w);
    if (w->maximized) {
        w->maximized = 0;
        w->r.x = w->restore.x;
        w->r.y = w->restore.y;
        win_resize(w, w->restore.w, w->restore.h - TITLE_H);
    } else {
        w->restore = w->r;
        w->maximized = 1;
        Rect wa = work_area(); /* üst çubuk ile dock arası */
        w->r.x = 0;
        w->r.y = wa.y;
        win_resize(w, SCREEN_W, wa.h - TITLE_H);
    }
    damage_win(w);
}

static void minimize(Win *w)
{
    damage_win(w);
    w->minimized = 1;
    /* en alta taşı */
    int i = win_index(w);
    if (i > 0) {
        memmove(&wins[1], &wins[0], sizeof(Win *) * (size_t)i);
        wins[0] = w;
    }
    Win *f = wm_focused();
    if (f) {
        f->dirty = 1;
        damage_win(f);
    }
    damage_bar();
    wm_damage(dock_rect());
}

static Win *win_at(int x, int y)
{
    for (int i = nwins - 1; i >= 0; i--) {
        Win *w = wins[i];
        if (!w->minimized && rect_has(w->r, x, y))
            return w;
    }
    return NULL;
}

/* başlık düğmeleri: 0 kapat, 1 küçült, 2 büyüt */
static Rect title_btn_rect(Win *w, int i)
{
    return (Rect){ w->r.x + 14 + i * 22, w->r.y + (TITLE_H - 14) / 2, 14, 14 };
}

/* ------------------------------------------------------------------ */
/* Bildirimler ve menüler                                              */
/* ------------------------------------------------------------------ */

static Rect notif_rect(int i) { return (Rect){ SCREEN_W - 380, BAR_H + 12 + i * 86, 364, 74 }; }

void wm_notify(const char *title, const char *body, IconId icon)
{
    if (nnotif == 4) {
        memmove(&notifs[0], &notifs[1], sizeof(Notif) * 3);
        nnotif--;
    }
    Notif *n = &notifs[nnotif++];
    snprintf(n->title, sizeof n->title, "%s", title);
    snprintf(n->body, sizeof n->body, "%s", body ? body : "");
    n->icon = icon;
    n->until = now_ms() + 4500;
    for (int i = 0; i < 4; i++)
        wm_damage(notif_rect(i));
}

static void open_menu(int x, int y, const MenuItem *items, int n)
{
    menu_n = n;
    memcpy(menu_items, items, sizeof(MenuItem) * n);
    int w = 230;
    for (int i = 0; i < n; i++)
        w = maxi(w, text_width(F_UI, 14, items[i].label) + 70);
    int h = n * 36 + 12;
    menu_rect = (Rect){ clampi(x, 8, SCREEN_W - w - 8), clampi(y, BAR_H + 4, SCREEN_H - h - 8), w, h };
    menu_open = 1;
    menu_hover = -1;
    wm_damage(rect_inset(menu_rect, -30));
}

static void close_menu(void)
{
    if (!menu_open)
        return;
    menu_open = 0;
    wm_damage(rect_inset(menu_rect, -30));
}

static void spawn_detached(char *const argv[])
{
    pid_t p = fork();
    if (p == 0) {
        if (fork() == 0) {
            setsid();
            execvp(argv[0], argv);
            _exit(127);
        }
        _exit(0);
    }
    if (p > 0)
        waitpid(p, NULL, 0);
}

static void do_action(int a)
{
    switch (a) {
    case ACT_POWEROFF: {
        char *argv[] = { "poweroff", NULL };
        wm_notify("Kapatılıyor", "AxsOS kapanıyor, hoşça kal!", IC_POWER);
        spawn_detached(argv);
        break;
    }
    case ACT_REBOOT: {
        char *argv[] = { "reboot", NULL };
        spawn_detached(argv);
        break;
    }
    case ACT_CONSOLE: wm_quit(0); break;
    case ACT_TERMINAL: wm_open(&APP_TERMINAL, NULL); break;
    case ACT_WALLPAPER:
        wallpaper_idx = (wallpaper_idx + 1) % N_WALLPAPERS;
        config_save();
        wm_wallpaper_changed();
        break;
    case ACT_SETTINGS: wm_open(&APP_SETTINGS, NULL); break;
    case ACT_ABOUT: wm_open(&APP_ABOUT, NULL); break;
    case ACT_FILES: wm_open(&APP_FILES, NULL); break;
    case ACT_STUDIO: wm_open(&APP_STUDIO, NULL); break;
    }
}

/* ------------------------------------------------------------------ */
/* Başlatıcı                                                           */
/* ------------------------------------------------------------------ */

static Rect launcher_rect(void)
{
    int w = 660, h = 470;
    return (Rect){ (SCREEN_W - w) / 2, maxi(BAR_H + 20, (SCREEN_H - h) / 2 - 40), w, h };
}

static int ci_contains(const char *hay, const char *needle)
{
    /* ASCII için büyük/küçük harf duyarsız arama; Türkçe harfler olduğu gibi */
    size_t n = strlen(needle);
    if (!n)
        return 1;
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b)
                break;
            i++;
        }
        if (i == n)
            return 1;
    }
    return 0;
}

static int launcher_matches(const App **out)
{
    int n = 0;
    if (lq.buf[0] == '>')
        return 0;
    for (int i = 0; i < N_APPS; i++)
        if (ci_contains(ALL_APPS[i]->name, lq.buf) || ci_contains(ALL_APPS[i]->desc, lq.buf) ||
            ci_contains(ALL_APPS[i]->id, lq.buf))
            out[n++] = ALL_APPS[i];
    return n;
}

static void render_scene(Rect clip, int with_overlays);

static void open_launcher(void)
{
    if (launcher_open)
        return;
    close_menu();
    launcher_open = 1;
    tf_set(&lq, "");
    lq.focus = 1;
    lsel = 0;
    /* arkadaki sahneyi bir kez çizip bulanıklaştır: yazarken tekrar gerekmez */
    Rect lr = launcher_rect();
    render_scene((Rect){ 0, 0, SCREEN_W, SCREEN_H }, 0);
    surf_resize(&launcher_bg, lr.w, lr.h);
    Rect full = { 0, 0, SCREEN_W, SCREEN_H };
    surf_noclip(&launcher_bg);
    blit(&launcher_bg, 0, 0, &screen, lr);
    (void)full;
    blur_region(&launcher_bg, (Rect){ 0, 0, lr.w, lr.h }, 14);
    wm_damage_all();
}

static void close_launcher(void)
{
    if (!launcher_open)
        return;
    launcher_open = 0;
    lq.focus = 0;
    wm_damage_all();
}

static void launcher_run(void)
{
    if (lq.buf[0] == '>') {
        const char *cmd = lq.buf + 1;
        while (*cmd == ' ')
            cmd++;
        if (*cmd)
            wm_run_in_terminal(cmd, cmd);
        close_launcher();
        return;
    }
    const App *m[16];
    int n = launcher_matches(m);
    if (n) {
        const App *a = m[clampi(lsel, 0, n - 1)];
        close_launcher();
        wm_open(a, NULL);
    }
}

static Rect launcher_tile(int i)
{
    Rect lr = launcher_rect();
    int cols = 4, tw = 146, th = 118;
    int gx = lr.x + (lr.w - cols * tw) / 2;
    return (Rect){ gx + (i % cols) * tw, lr.y + 92 + (i / cols) * (th + 8), tw - 8, th };
}

static void draw_launcher(Surf *s)
{
    fill_rect(s, (Rect){ 0, 0, SCREEN_W, SCREEN_H }, ALPHA(HEX(0x05050A), 110));
    Rect lr = launcher_rect();
    draw_shadow(s, lr, 22, 40, 12, 150);
    /* buzlu cam */
    blit_rrect(s, lr.x, lr.y, &launcher_bg, (Rect){ 0, 0, lr.w, lr.h }, 22, 15);
    fill_rrect(s, lr, 22, ALPHA(HEX(0x16162A), 175));
    stroke_rrect(s, lr, 22, 1, ALPHA(HEX(0xFFFFFF), 40));

    Rect sf = { lr.x + 24, lr.y + 24, lr.w - 48, 46 };
    fill_rrect(s, sf, 14, ALPHA(HEX(0x000000), 90));
    stroke_rrect(s, sf, 14, 2, T.accent);
    draw_icon(s, IC_SEARCH, sf.x + 14, sf.y + 12, 22);
    int ty = sf.y + (sf.h - font_height(F_UI, 17)) / 2;
    if (!lq.len)
        draw_text(s, F_UI, 17, sf.x + 48, ty, T.muted, "Uygulama ara ya da > komut yaz…");
    else
        draw_text(s, F_UI, 17, sf.x + 48, ty, T.text, lq.buf);
    int cx = sf.x + 48 + text_width_n(F_UI, 17, lq.buf, lq.cur);
    fill_rect(s, (Rect){ cx, ty + 2, 2, font_height(F_UI, 17) - 4 }, T.accent2);

    if (lq.buf[0] == '>') {
        Rect hint = { lr.x + 40, lr.y + 110, lr.w - 80, 120 };
        fill_rrect(s, hint, 16, ALPHA(HEX(0xFFFFFF), 12));
        draw_icon(s, IC_TERMINAL, hint.x + 24, hint.y + 28, 64);
        draw_text(s, F_UI_BOLD, 17, hint.x + 110, hint.y + 34, T.text, "Terminalde çalıştır");
        draw_text_fit(s, F_MONO, 15, hint.x + 110, hint.y + 64, hint.w - 130, T.accent2, lq.buf + 1);
    } else {
        const App *m[16];
        int n = launcher_matches(m);
        if (!n)
            draw_text_center(s, F_UI, 15, (Rect){ lr.x, lr.y + 160, lr.w, 40 }, T.subtext, "Eşleşen uygulama yok");
        for (int i = 0; i < n; i++) {
            Rect t = launcher_tile(i);
            int sel = i == lsel, hov = i == launcher_hover;
            if (sel || hov)
                fill_rrect(s, t, 16, sel ? ALPHA(T.accent, 110) : ALPHA(HEX(0xFFFFFF), 22));
            draw_icon(s, m[i]->icon, t.x + (t.w - 60) / 2, t.y + 12, 60);
            draw_text_center(s, F_UI_BOLD, 13, (Rect){ t.x, t.y + 78, t.w, 18 }, T.text, m[i]->name);
            draw_text_center(s, F_UI, 11, (Rect){ t.x + 4, t.y + 96, t.w - 8, 16 }, T.subtext, m[i]->id);
        }
    }
    draw_text_center(s, F_UI, 12, (Rect){ lr.x, lr.y + lr.h - 36, lr.w, 20 }, T.muted,
                     "Enter: aç   ↑↓←→: seç   Esc: kapat   > komut: terminalde çalıştır");
}

/* ------------------------------------------------------------------ */
/* Çizim                                                               */
/* ------------------------------------------------------------------ */

static const char *GUN[] = { "Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt" };
static const char *AY[] = { "Oca", "Şub", "Mar", "Nis", "May", "Haz", "Tem", "Ağu", "Eyl", "Eki", "Kas", "Ara" };

static void update_clock(void)
{
    time_t now = time(NULL) + tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&now, &tm);
    char buf[64];
    snprintf(buf, sizeof buf, "%s %d %s  %02d:%02d", GUN[tm.tm_wday], tm.tm_mday, AY[tm.tm_mon], tm.tm_hour, tm.tm_min);
    if (strcmp(buf, clock_text)) {
        snprintf(clock_text, sizeof clock_text, "%s", buf);
        damage_bar();
    }
}

static void update_net(void)
{
    /* eth0'ın IPv4 adresini doğrudan çekirdekten sor (alt süreç yok -> donma yok) */
    char buf[64] = "";
    int sk = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (sk >= 0) {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof ifr);
        snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "eth0");
        if (ioctl(sk, SIOCGIFADDR, &ifr) == 0)
            inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, buf, sizeof buf);
        close(sk);
    }
    if (strcmp(buf, net_text)) {
        snprintf(net_text, sizeof net_text, "%s", buf);
        damage_bar();
    }
}

static Rect bar_logo_rect(void) { return (Rect){ 8, 3, 118, BAR_H - 6 }; }
static Rect bar_power_rect(void) { return (Rect){ SCREEN_W - 44, 3, 36, BAR_H - 6 }; }
static Rect bar_layout_rect(void) { return (Rect){ SCREEN_W - 88, 5, 38, BAR_H - 10 }; }

static void draw_bar(Surf *s)
{
    blit(s, 0, 0, &barbg, (Rect){ 0, 0, SCREEN_W, BAR_H });
    Rect lg = bar_logo_rect();
    if (rect_has(lg, mouse_x, mouse_y) || launcher_open)
        fill_rrect(s, lg, 8, ALPHA(HEX(0xFFFFFF), 30));
    draw_logo(s, lg.x + 20, lg.y + lg.h / 2.0f, 20, T.accent2, T.accent);
    draw_text(s, F_UI_BOLD, 14, lg.x + 38, lg.y + (lg.h - font_height(F_UI_BOLD, 14)) / 2, T.text, "AxsOS");
    Win *f = wm_focused();
    if (f)
        draw_text(s, F_UI_BOLD, 13, lg.x + lg.w + 16, (BAR_H - font_height(F_UI_BOLD, 13)) / 2, T.subtext, f->app->name);

    draw_text_center(s, F_UI_BOLD, 13, (Rect){ 0, 0, SCREEN_W, BAR_H }, T.text, clock_text);

    Rect pw = bar_power_rect();
    if (rect_has(pw, mouse_x, mouse_y))
        fill_rrect(s, pw, 8, ALPHA(HEX(0xFFFFFF), 30));
    draw_icon(s, IC_POWER, pw.x + 8, pw.y + 4, 20);

    Rect kl = bar_layout_rect();
    fill_rrect(s, kl, 6, ALPHA(HEX(0xFFFFFF), rect_has(kl, mouse_x, mouse_y) ? 45 : 22));
    draw_text_center(s, F_UI_BOLD, 12, kl, T.text, kb_layout == 0 ? "TR" : "US");

    int nx = kl.x - 12;
    const char *nt = net_text[0] ? net_text : "Çevrimdışı";
    int nw = text_width(F_UI, 12, nt);
    nx -= nw;
    draw_text(s, F_UI, 12, nx, (BAR_H - font_height(F_UI, 12)) / 2, net_text[0] ? T.subtext : T.muted, nt);
    draw_icon(s, IC_NETWORK, nx - 22, 9, 16);
}

static void draw_dock(Surf *s)
{
    Rect d = dock_rect();
    draw_shadow(s, d, 20, 24, 8, 120);
    fill_rrect(s, d, 22, ALPHA(HEX(0x14141F), 200));
    stroke_rrect(s, d, 22, 1, ALPHA(HEX(0xFFFFFF), 34));
    for (int i = 0; i < N_DOCK; i++) {
        int x = d.x + DOCK_PAD + 10 + i * (DOCK_ICON + 12);
        int y = d.y + DOCK_PAD;
        int sz = DOCK_ICON;
        if (i == dock_hover) {
            sz += 8;
            x -= 4;
            y -= 10;
        }
        draw_icon(s, DOCK[i]->icon, x, y, sz);
        if (wm_find(DOCK[i]))
            fill_circle(s, d.x + DOCK_PAD + 10 + i * (DOCK_ICON + 12) + DOCK_ICON / 2.0f, d.y + d.h - 7, 2.5f, T.text);
    }
    if (dock_hover >= 0) {
        const char *nm = DOCK[dock_hover]->name;
        int tw = text_width(F_UI_BOLD, 13, nm) + 24;
        int cx = d.x + DOCK_PAD + 10 + dock_hover * (DOCK_ICON + 12) + DOCK_ICON / 2;
        Rect tip = { cx - tw / 2, d.y - 44, tw, 30 };
        fill_rrect(s, tip, 9, ALPHA(HEX(0x101018), 235));
        stroke_rrect(s, tip, 9, 1, ALPHA(HEX(0xFFFFFF), 30));
        draw_text_center(s, F_UI_BOLD, 13, tip, T.text, nm);
    }
}

static Rect dock_tip_rect(void)
{
    Rect d = dock_rect();
    return (Rect){ d.x - 60, d.y - 50, d.w + 120, 50 };
}

static uint32_t str_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* Başlık çubuğunu (düğmeler, simge, başlık) pencereye ait yüzeye çiz. */
static void render_chrome(Win *w, int focused, int hov_idx)
{
    Surf *c = &w->chrome;
    surf_resize(c, w->r.w, TITLE_H);
    surf_noclip(c);
    int W = c->w;
    fill_rect(c, (Rect){ 0, 0, W, TITLE_H }, focused ? T.title_focus : T.title_blur);
    fill_rect(c, (Rect){ 0, TITLE_H - 1, W, 1 }, ALPHA(HEX(0x000000), 60));
    static const uint32_t BC[3] = { 0xFFF87171, 0xFFFBBF24, 0xFF34D399 };
    for (int i = 0; i < 3; i++) {
        float cx = 14 + i * 22 + 7.f, cy = (TITLE_H - 14) / 2 + 7.f;
        fill_circle(c, cx, cy, 6.5f, focused || hov_idx >= 0 ? BC[i] : T.overlay);
        if (hov_idx >= 0) {
            uint32_t k = ALPHA(HEX(0x000000), 150);
            if (i == 0) {
                draw_line(c, cx - 2.5f, cy - 2.5f, cx + 2.5f, cy + 2.5f, 1.4f, k);
                draw_line(c, cx - 2.5f, cy + 2.5f, cx + 2.5f, cy - 2.5f, 1.4f, k);
            } else {
                draw_line(c, cx - 3, cy, cx + 3, cy, 1.4f, k);
                if (i == 2)
                    draw_line(c, cx, cy - 3, cx, cy + 3, 1.4f, k);
            }
        }
    }
    int tw = mini(text_width(F_UI_BOLD, 13, w->title), W - 160);
    draw_icon(c, w->app->icon, (W - tw) / 2 - 24, (TITLE_H - 18) / 2, 18);
    draw_text_fit(c, F_UI_BOLD, 13, (W - tw) / 2, (TITLE_H - font_height(F_UI_BOLD, 13)) / 2,
                  W - 160, focused ? T.text : T.muted, w->title);
}

static void draw_window(Surf *s, Win *w, int focused)
{
    Rect r = w->r;
    draw_shadow(s, r, WIN_RAD, SHADOW, 6, focused ? 150 : 90);
    int hov_idx = win_index(w) == hover_title_win ? hover_title_btn : -1;
    uint32_t key = str_hash(w->title) ^ ((uint32_t)r.w << 8) ^ (uint32_t)(focused ? 1 : 0) ^
                   ((uint32_t)(hov_idx + 2) << 4) ^ T.accent;
    if (!w->chrome.px || w->chrome.w != r.w || w->chrome_key != key) {
        render_chrome(w, focused, hov_idx);
        w->chrome_key = key;
    }
    blit_rrect(s, r.x, r.y, &w->chrome, (Rect){ 0, 0, r.w, TITLE_H }, WIN_RAD, 3);
    blit_rrect(s, r.x, r.y + TITLE_H, &w->content, (Rect){ 0, 0, w->content.w, w->content.h }, WIN_RAD, 12);
    stroke_rrect(s, r, WIN_RAD, 1, ALPHA(HEX(0xFFFFFF), focused ? 30 : 16));
}

static void draw_notifs(Surf *s)
{
    for (int i = 0; i < nnotif; i++) {
        Rect r = notif_rect(i);
        draw_shadow(s, r, 16, 20, 6, 110);
        fill_rrect(s, r, 16, ALPHA(HEX(0x1E1E2E), 240));
        stroke_rrect(s, r, 16, 1, ALPHA(HEX(0xFFFFFF), 30));
        draw_icon(s, notifs[i].icon, r.x + 14, r.y + 17, 40);
        draw_text_fit(s, F_UI_BOLD, 14, r.x + 68, r.y + 14, r.w - 84, T.text, notifs[i].title);
        draw_text_fit(s, F_UI, 13, r.x + 68, r.y + 38, r.w - 84, T.subtext, notifs[i].body);
    }
}

static void draw_menu(Surf *s)
{
    Rect r = menu_rect;
    draw_shadow(s, r, 14, 24, 8, 140);
    fill_rrect(s, r, 14, ALPHA(HEX(0x1C1C2C), 245));
    stroke_rrect(s, r, 14, 1, ALPHA(HEX(0xFFFFFF), 34));
    for (int i = 0; i < menu_n; i++) {
        Rect it = { r.x + 6, r.y + 6 + i * 36, r.w - 12, 34 };
        if (i == menu_hover)
            fill_rrect(s, it, 9, T.accent);
        draw_icon(s, menu_items[i].icon, it.x + 10, it.y + 7, 20);
        draw_text(s, F_UI, 14, it.x + 42, it.y + (it.h - font_height(F_UI, 14)) / 2,
                  i == menu_hover ? HEX(0xFFFFFF) : T.text, menu_items[i].label);
    }
}

static void render_scene(Rect clip, int with_overlays)
{
    surf_clip(&screen, clip);
    blit(&screen, clip.x, clip.y, &wall, clip);
    Win *f = wm_focused();
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (w->minimized)
            continue;
        if (rect_empty(rect_isect(win_outer(w), clip)))
            continue;
        draw_window(&screen, w, w == f && !launcher_open);
    }
    if (!rect_empty(rect_isect(clip, (Rect){ 0, 0, SCREEN_W, BAR_H })))
        draw_bar(&screen);
    if (!rect_empty(rect_isect(clip, rect_inset(dock_rect(), -40))) || !rect_empty(rect_isect(clip, dock_tip_rect())))
        draw_dock(&screen);
    if (!with_overlays)
        return;
    if (resize_win && !rect_empty(resize_box)) {
        stroke_rrect(&screen, resize_box, WIN_RAD, 3, T.accent2);
        stroke_rrect(&screen, rect_inset(resize_box, 3), WIN_RAD - 3, 1, ALPHA(HEX(0x000000), 120));
    }
    if (nnotif)
        draw_notifs(&screen);
    if (menu_open)
        draw_menu(&screen);
    if (launcher_open)
        draw_launcher(&screen);
}

static void compose(void)
{
    /* kirli pencere içeriklerini yeniden çiz */
    for (int i = 0; i < nwins; i++) {
        Win *w = wins[i];
        if (!w->dirty)
            continue;
        w->dirty = 0;
        w->dmg = (Rect){ 0, 0, 0, 0 };
        surf_noclip(&w->content);
        if (w->app->draw)
            w->app->draw(w, &w->content);
        if (w->minimized)
            continue;
        Rect cr = wm_content_rect(w);
        if (rect_empty(w->dmg))
            wm_damage(cr);
        else
            wm_damage((Rect){ cr.x + w->dmg.x, cr.y + w->dmg.y, w->dmg.w, w->dmg.h });
    }
    if (!ndmg)
        return;
    /* üst çubuğa değen hasar, tüm çubuğu kapsasın (cam efekti tutarlı kalsın) */
    for (int i = 0; i < ndmg; i++) {
        Rect d = dmg[i];
        int draw_cursor = !rect_empty(rect_isect(d, (Rect){ mouse_x - 2, mouse_y - 2, 26, 30 }));
        render_scene(d, 1);
        if (draw_cursor)
            draw_arrow_cursor(&screen, mouse_x, mouse_y);
        fb_present(&screen, d);
    }
    fb_flush();
    ndmg = 0;
}

/* ------------------------------------------------------------------ */
/* Girdi                                                               */
/* ------------------------------------------------------------------ */

static void damage_cursor(int x, int y) { wm_damage((Rect){ x - 2, y - 2, 26, 30 }); }

static int dock_index_at(int x, int y)
{
    Rect d = dock_rect();
    if (!rect_has(d, x, y))
        return -1;
    for (int i = 0; i < N_DOCK; i++) {
        Rect r = { d.x + DOCK_PAD + 10 + i * (DOCK_ICON + 12) - 6, d.y, DOCK_ICON + 12, d.h };
        if (rect_has(r, x, y))
            return i;
    }
    return -1;
}

static void dock_click(int i)
{
    const App *a = DOCK[i];
    Win *w = wm_find(a);
    if (!w) {
        wm_open(a, NULL);
    } else if (w == wm_focused()) {
        minimize(w);
    } else {
        wm_focus(w);
    }
}

static void to_content(Win *w, MouseEv *e, MouseEv *out)
{
    *out = *e;
    out->x = e->x - w->r.x;
    out->y = e->y - w->r.y - TITLE_H;
}

static void launcher_mouse(MouseEv *e)
{
    Rect lr = launcher_rect();
    if (e->kind == M_MOVE) {
        int h = -1;
        const App *m[16];
        int n = launcher_matches(m);
        for (int i = 0; i < n; i++)
            if (rect_has(launcher_tile(i), e->x, e->y))
                h = i;
        if (h != launcher_hover) {
            launcher_hover = h;
            wm_damage(lr);
        }
        return;
    }
    if (e->kind != M_DOWN)
        return;
    if (!rect_has(lr, e->x, e->y)) {
        close_launcher();
        return;
    }
    if (launcher_hover >= 0) {
        lsel = launcher_hover;
        launcher_run();
    }
}

void wm_on_mouse(MouseEv *e)
{
    if (e->kind == M_MOVE) {
        if (e->x == mouse_x && e->y == mouse_y)
            return;
        damage_cursor(mouse_x, mouse_y);
        int ox = mouse_x, oy = mouse_y;
        mouse_x = e->x;
        mouse_y = e->y;
        damage_cursor(mouse_x, mouse_y);
        /* üst çubuktaki hover efektleri */
        if (oy < BAR_H || mouse_y < BAR_H)
            if (rect_has(bar_logo_rect(), ox, oy) != rect_has(bar_logo_rect(), mouse_x, mouse_y) ||
                rect_has(bar_power_rect(), ox, oy) != rect_has(bar_power_rect(), mouse_x, mouse_y) ||
                rect_has(bar_layout_rect(), ox, oy) != rect_has(bar_layout_rect(), mouse_x, mouse_y))
                damage_bar();
    } else {
        mouse_x = e->x;
        mouse_y = e->y;
    }

    /* çift tıklama algılama */
    if (e->kind == M_DOWN) {
        long t = now_ms();
        if (t - last_click_ms < 420 && abs(e->x - last_click_x) < 5 && abs(e->y - last_click_y) < 5 && e->button == last_click_btn) {
            e->clicks = 2;
            last_click_ms = 0;
        } else {
            e->clicks = 1;
            last_click_ms = t;
        }
        last_click_x = e->x;
        last_click_y = e->y;
        last_click_btn = e->button;
    }

    if (launcher_open) {
        launcher_mouse(e);
        return;
    }

    if (menu_open) {
        if (e->kind == M_MOVE) {
            int h = -1;
            for (int i = 0; i < menu_n; i++)
                if (rect_has((Rect){ menu_rect.x + 6, menu_rect.y + 6 + i * 36, menu_rect.w - 12, 34 }, e->x, e->y))
                    h = i;
            if (h != menu_hover) {
                menu_hover = h;
                wm_damage(menu_rect);
            }
            return;
        }
        if (e->kind == M_DOWN) {
            int act = (menu_hover >= 0 && rect_has(menu_rect, e->x, e->y)) ? menu_items[menu_hover].action : ACT_NONE;
            close_menu();
            do_action(act);
        }
        return;
    }

    /* sürükleme / boyutlandırma */
    if (drag_win) {
        if (e->kind == M_MOVE) {
            damage_win(drag_win);
            drag_win->r.x = clampi(e->x - drag_dx, -drag_win->r.w + 80, SCREEN_W - 80);
            drag_win->r.y = clampi(e->y - drag_dy, BAR_H, SCREEN_H - 60);
            damage_win(drag_win);
        } else if (e->kind == M_UP) {
            drag_win = NULL;
        }
        return;
    }
    if (resize_win) {
        /* sürüklerken yalnızca çerçeve önizlemesi; gerçek boyut bırakınca uygulanır
           (her harekette pencere içeriğini yeniden oluşturmak yavaş makinede kasar) */
        Rect nb = resize_win->r;
        nb.w = clampi(e->x - nb.x, resize_win->min_w, SCREEN_W);
        nb.h = clampi(e->y - nb.y, resize_win->min_h + TITLE_H, SCREEN_H - BAR_H);
        if (e->kind == M_MOVE) {
            damage_outline(resize_box);
            resize_box = nb;
            damage_outline(resize_box);
        } else if (e->kind == M_UP) {
            damage_outline(resize_box);
            win_resize(resize_win, nb.w, nb.h - TITLE_H);
            resize_win = NULL;
            resize_box = (Rect){ 0, 0, 0, 0 };
        }
        return;
    }
    if (grab_win) {
        MouseEv ce;
        to_content(grab_win, e, &ce);
        if (grab_win->app->mouse)
            grab_win->app->mouse(grab_win, &ce);
        if (e->kind == M_UP)
            grab_win = NULL;
        return;
    }

    /* dock */
    int di = dock_index_at(e->x, e->y);
    Win *w = win_at(e->x, e->y);
    if (e->kind == M_MOVE) {
        int nh = (w && !rect_has(dock_rect(), e->x, e->y)) ? -1 : di;
        if (nh != dock_hover) {
            dock_hover = nh;
            wm_damage(dock_tip_rect());
            wm_damage(rect_inset(dock_rect(), -12));
        }
        /* başlık düğmesi hover */
        int hw = -1, hb = -1;
        if (w && e->y < w->r.y + TITLE_H)
            for (int i = 0; i < 3; i++)
                if (rect_has(rect_inset(title_btn_rect(w, i), -4), e->x, e->y)) {
                    hw = win_index(w);
                    hb = i;
                }
        if (hw != hover_title_win || hb != hover_title_btn) {
            if (hover_title_win >= 0 && hover_title_win < nwins)
                wm_damage((Rect){ wins[hover_title_win]->r.x, wins[hover_title_win]->r.y, 90, TITLE_H });
            hover_title_win = hw;
            hover_title_btn = hb;
            if (w)
                wm_damage((Rect){ w->r.x, w->r.y, 90, TITLE_H });
        }
        if (w && e->y >= w->r.y + TITLE_H && w->app->mouse) {
            MouseEv ce;
            to_content(w, e, &ce);
            w->app->mouse(w, &ce);
        }
        return;
    }

    if (e->kind == M_DOWN) {
        /* üst çubuk */
        if (e->y < BAR_H) {
            if (rect_has(bar_logo_rect(), e->x, e->y)) {
                open_launcher();
            } else if (rect_has(bar_power_rect(), e->x, e->y)) {
                static const MenuItem pm[] = {
                    { "Kapat", ACT_POWEROFF, IC_POWER },
                    { "Yeniden başlat", ACT_REBOOT, IC_REFRESH },
                    { "Metin konsoluna geç", ACT_CONSOLE, IC_TERMINAL },
                    { "AxsOS hakkında", ACT_ABOUT, IC_ABOUT },
                };
                open_menu(SCREEN_W - 250, BAR_H + 4, pm, 4);
            } else if (rect_has(bar_layout_rect(), e->x, e->y)) {
                kb_layout = !kb_layout;
                config_save();
                damage_bar();
                wm_notify("Klavye düzeni", LAYOUT_NAMES[kb_layout], IC_KEYBOARD);
            }
            return;
        }
        if (di >= 0 && e->button == 1) {
            dock_click(di);
            return;
        }
        /* bildirime tıklayınca kapat */
        for (int i = 0; i < nnotif; i++)
            if (rect_has(notif_rect(i), e->x, e->y)) {
                notifs[i].until = 0;
                return;
            }
        if (w) {
            if (w != wm_focused())
                wm_focus(w);
            if (e->y < w->r.y + TITLE_H) {
                for (int i = 0; i < 3; i++)
                    if (rect_has(rect_inset(title_btn_rect(w, i), -4), e->x, e->y)) {
                        title_btn_win = w->id;
                        title_btn = i;
                        return;
                    }
                if (e->clicks == 2) {
                    toggle_max(w);
                    return;
                }
                if (!w->maximized) {
                    drag_win = w;
                    drag_dx = e->x - w->r.x;
                    drag_dy = e->y - w->r.y;
                }
                return;
            }
            /* sağ-alt köşe: boyutlandır */
            if (!w->maximized && e->x >= w->r.x + w->r.w - 18 && e->y >= w->r.y + w->r.h - 18) {
                resize_win = w;
                resize_box = w->r;
                return;
            }
            grab_win = w;
            MouseEv ce;
            to_content(w, e, &ce);
            if (w->app->mouse)
                w->app->mouse(w, &ce);
            return;
        }
        /* masaüstü */
        if (e->button == 2) {
            static const MenuItem dm[] = {
                { "Terminal aç", ACT_TERMINAL, IC_TERMINAL },
                { "Dosyalar", ACT_FILES, IC_FILES },
                { "Axs Stüdyo", ACT_STUDIO, IC_STUDIO },
                { "Duvar kâğıdını değiştir", ACT_WALLPAPER, IC_REFRESH },
                { "Ayarlar", ACT_SETTINGS, IC_SETTINGS },
            };
            open_menu(e->x, e->y, dm, 5);
        }
        return;
    }

    if (e->kind == M_UP) {
        if (title_btn >= 0) {
            Win *tw = NULL;
            for (int i = 0; i < nwins; i++)
                if (wins[i]->id == title_btn_win)
                    tw = wins[i];
            int b = title_btn;
            title_btn = -1;
            if (tw && rect_has(rect_inset(title_btn_rect(tw, b), -4), e->x, e->y)) {
                if (b == 0) wm_close(tw);
                else if (b == 1) minimize(tw);
                else toggle_max(tw);
            }
        }
        return;
    }

    if (e->kind == M_WHEEL && w && w->app->mouse && e->y >= w->r.y + TITLE_H) {
        MouseEv ce;
        to_content(w, e, &ce);
        w->app->mouse(w, &ce);
    }
}

static void cycle_windows(void)
{
    if (nwins < 2)
        return;
    /* en alttaki pencereyi öne getir */
    wm_focus(wins[0]);
}

void wm_on_key(KeyEv *e)
{
    static int super_alone;
    if (e->code == KEY_LEFTMETA || e->code == KEY_RIGHTMETA) {
        if (e->down == 1 && !(e->mods & ~MOD_SUPER))
            super_alone = 1;
        else if (!e->down && super_alone) {
            super_alone = 0;
            if (launcher_open)
                close_launcher();
            else
                open_launcher();
        }
        return;
    }
    if (e->down)
        super_alone = 0;
    if (!e->down) {
        Win *f = wm_focused();
        if (f && f->app->key && !launcher_open)
            f->app->key(f, e);
        return;
    }
    int m = e->mods & ~(MOD_SHIFT);
    /* genel kısayollar */
    if ((m == MOD_CTRL && e->code == KEY_SPACE) || (m == MOD_ALT && e->code == KEY_F1)) {
        if (launcher_open)
            close_launcher();
        else
            open_launcher();
        return;
    }
    if (m == (MOD_CTRL | MOD_ALT) && e->code == KEY_T) {
        close_launcher();
        wm_open(&APP_TERMINAL, NULL);
        return;
    }
    if (m == (MOD_CTRL | MOD_ALT) && e->code == KEY_BACKSPACE) {
        wm_quit(0);
        return;
    }
    if (m == MOD_ALT && e->code == KEY_TAB) {
        cycle_windows();
        return;
    }
    if (m == MOD_ALT && e->code == KEY_F4) {
        Win *f = wm_focused();
        if (f)
            wm_close(f);
        return;
    }
    if (menu_open && e->code == KEY_ESC) {
        close_menu();
        return;
    }
    if (launcher_open) {
        const App *mm[16];
        int n = launcher_matches(mm);
        switch (e->code) {
        case KEY_ESC: close_launcher(); return;
        case KEY_ENTER: case KEY_KPENTER: launcher_run(); return;
        case KEY_RIGHT: if (lq.cur == lq.len) { lsel = mini(lsel + 1, maxi(n - 1, 0)); wm_damage(launcher_rect()); return; } break;
        case KEY_LEFT: if (lq.cur == lq.len && lsel > 0) { lsel--; wm_damage(launcher_rect()); return; } break;
        case KEY_DOWN: lsel = mini(lsel + 4, maxi(n - 1, 0)); wm_damage(launcher_rect()); return;
        case KEY_UP: lsel = maxi(lsel - 4, 0); wm_damage(launcher_rect()); return;
        }
        if (tf_key(&lq, e)) {
            lsel = 0;
            wm_damage(launcher_rect());
        }
        return;
    }
    Win *f = wm_focused();
    if (f && f->app->key)
        f->app->key(f, e);
}

/* ------------------------------------------------------------------ */
/* Yardımcılar (uygulamalar için)                                      */
/* ------------------------------------------------------------------ */

void wm_run_in_terminal(const char *cmd, const char *title)
{
    Win *w = wm_open(&APP_TERMINAL, cmd);
    if (w && title)
        wm_set_title(w, title);
}

static int ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcmp(s + a - b, suf);
}

void wm_open_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return;
    if (S_ISDIR(st.st_mode)) {
        wm_open(&APP_FILES, path);
        return;
    }
    static const char *text_ext[] = { ".axs", ".nyl", ".txt", ".md", ".sh", ".c", ".h", ".py", ".conf",
                                      ".json", ".csv", ".html", ".js", ".css", ".list", NULL };
    for (int i = 0; text_ext[i]; i++)
        if (ends_with(path, text_ext[i])) {
            wm_open(&APP_STUDIO, path);
            return;
        }
    if (ends_with(path, ".axp")) {
        char cmd[1024];
        snprintf(cmd, sizeof cmd, "axpkg install '%s'", path);
        wm_run_in_terminal(cmd, "Paket kurulumu");
        return;
    }
    if (st.st_mode & 0111) {
        char cmd[1024];
        snprintf(cmd, sizeof cmd, "'%s'", path);
        wm_run_in_terminal(cmd, path);
        return;
    }
    if (st.st_size < 1024 * 1024)
        wm_open(&APP_STUDIO, path);
}

void wm_quit(int code) { quit_code = code; }

/* ------------------------------------------------------------------ */
/* Ana döngü                                                           */
/* ------------------------------------------------------------------ */

static volatile sig_atomic_t got_term;
static void on_term(int sig) { (void)sig; got_term = 1; }

static void tick(void)
{
    update_clock();
    long t = now_ms();
    int changed = 0;
    for (int i = 0; i < nnotif; i++)
        if (notifs[i].until <= t) {
            memmove(&notifs[i], &notifs[i + 1], sizeof(Notif) * (nnotif - i - 1));
            nnotif--;
            i--;
            changed = 1;
        }
    if (changed)
        for (int i = 0; i < 5; i++)
            wm_damage(rect_inset(notif_rect(i), -24));
    for (int i = 0; i < nwins; i++)
        if (wins[i]->app->tick)
            wins[i]->app->tick(wins[i]);
}

/* ------------------------------------------------------------------ */
/* Ölçüm kipi: axsde --bench                                           */
/* Ekran olmadan gerçek olay yollarını çalıştırıp kare sürelerini yazar */
/* ------------------------------------------------------------------ */

static double bench_frame(void)
{
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    compose();
    clock_gettime(CLOCK_MONOTONIC, &b);
    return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

static void bench_report(const char *name, double total, int n)
{
    printf("  %-34s %7.2f ms/kare  (%d kare)\n", name, total / n, n);
    fflush(stdout);
}

static int bench_main(void)
{
    SCREEN_W = 1280;
    SCREEN_H = 800;
    if (font_init() < 0)
        return 1;
    config_load();
    screen = surf_new(SCREEN_W, SCREEN_H);
    mouse_x = 640;
    mouse_y = 400;
    make_wallpaper();
    wm_damage_all();
    compose();
    wm_open(&APP_ABOUT, "welcome");
    wm_open(&APP_MONITOR, NULL);
    wm_open(&APP_STUDIO, NULL);
    Win *term = wm_open(&APP_TERMINAL, NULL);
    compose();
    const char *shot = getenv("AXSDE_BENCH_SHOT");
    if (shot) {
        FILE *f = fopen(shot, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
            for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
                uint32_t p = screen.px[i];
                fputc((p >> 16) & 255, f);
                fputc((p >> 8) & 255, f);
                fputc(p & 255, f);
            }
            fclose(f);
        }
    }
    printf("AxsDE ölçüm (1280x800, 4 pencere):\n");

    double t = 0;
    int n = 10;
    for (int i = 0; i < n; i++) {
        wm_damage_all();
        t += bench_frame();
    }
    bench_report("tam ekran yeniden çizim", t, n);

    /* imleç: masaüstü üzerinde gezinme */
    t = 0;
    n = 200;
    for (int i = 0; i < n; i++) {
        MouseEv e = { M_MOVE, 1100 + (i % 40), 300 + (i % 60), 0, 0, 0 };
        wm_on_mouse(&e);
        t += bench_frame();
    }
    bench_report("fare hareketi (masaüstü)", t, n);

    /* imleç: pencere üzerinde gezinme */
    t = 0;
    for (int i = 0; i < n; i++) {
        MouseEv e = { M_MOVE, term->r.x + 100 + (i % 200), term->r.y + 120 + (i % 100), 0, 0, 0 };
        wm_on_mouse(&e);
        t += bench_frame();
    }
    bench_report("fare hareketi (pencere üstü)", t, n);

    /* pencere sürükleme */
    MouseEv d = { M_DOWN, term->r.x + 200, term->r.y + 15, 1, 0, 1 };
    wm_on_mouse(&d);
    t = 0;
    n = 100;
    for (int i = 0; i < n; i++) {
        MouseEv e = { M_MOVE, d.x + (i % 50) * 6, d.y + (i % 30) * 4, 0, 0, 0 };
        wm_on_mouse(&e);
        t += bench_frame();
    }
    MouseEv u = { M_UP, d.x, d.y, 1, 0, 1 };
    wm_on_mouse(&u);
    bench_report("pencere sürükleme", t, n);

    /* terminal çıktısı */
    t = 0;
    n = 100;
    wm_focus(term);
    compose();
    for (int i = 0; i < n; i++) {
        char line[64];
        snprintf(line, sizeof line, "satir %d: merhaba AxsOS\r\n", i);
        struct { Term *t; } *ts = term->st;
        term_feed(ts->t, line, (int)strlen(line));
        term->dirty = 1;
        t += bench_frame();
    }
    bench_report("terminal satır çıktısı", t, n);

    /* editörde yazma */
    Win *st = wm_find(&APP_STUDIO);
    wm_focus(st);
    compose();
    t = 0;
    n = 60;
    for (int i = 0; i < n; i++) {
        KeyEv k = { 30 /* KEY_A */, 'a', 0, 1 };
        wm_on_key(&k);
        t += bench_frame();
    }
    bench_report("Axs Stüdyo'da yazma", t, n);

    /* boyutlandırma */
    MouseEv rd = { M_DOWN, st->r.x + st->r.w - 5, st->r.y + st->r.h - 5, 1, 0, 1 };
    wm_on_mouse(&rd);
    t = 0;
    n = 30;
    for (int i = 0; i < n; i++) {
        MouseEv e = { M_MOVE, rd.x - (i % 15) * 8, rd.y - (i % 15) * 5, 0, 0, 0 };
        wm_on_mouse(&e);
        t += bench_frame();
    }
    MouseEv ru = { M_UP, rd.x, rd.y, 1, 0, 1 };
    wm_on_mouse(&ru);
    bench_frame();
    bench_report("pencere boyutlandırma", t, n);

    /* başlatıcı */
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    open_launcher();
    compose();
    clock_gettime(CLOCK_MONOTONIC, &b);
    bench_report("başlatıcıyı açma", (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6, 1);
    close_launcher();
    compose();
    while (nwins)
        wm_close(wins[nwins - 1]);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--bench"))
        return bench_main();
    if (argc > 1 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("axsde %s\n", AXSDE_VERSION);
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGHUP, on_term);
    setenv("HOME", getenv("HOME") ? getenv("HOME") : "/root", 0);
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 0);

    if (font_init() < 0)
        return 1;
    if (fb_open() < 0)
        return 1;
    config_load();
    screen = surf_new(SCREEN_W, SCREEN_H);
    mouse_x = SCREEN_W / 2;
    mouse_y = SCREEN_H / 2;
    make_wallpaper();
    if (input_init() < 0)
        fprintf(stderr, "axsde: giriş aygıtı bulunamadı\n");
    update_clock();
    update_net();
    wm_damage_all();
    compose();

    /* ilk açılışta karşılama penceresi */
    wm_open(&APP_ABOUT, "welcome");

    long next_tick = now_ms() + 250, next_net = now_ms() + 5000, next_scan = now_ms() + 3000;
    while (quit_code < 0 && !got_term) {
        compose();
        struct pollfd pfd[64];
        int kind[64]; /* -1 giriş, i >= 0: pencere indeksi */
        int n = 0;
        int ifd[16];
        int ni = input_fds(ifd, 16);
        for (int i = 0; i < ni; i++) {
            pfd[n] = (struct pollfd){ ifd[i], POLLIN, 0 };
            kind[n++] = -1;
        }
        for (int i = 0; i < nwins && n < 60; i++) {
            if (!wins[i]->app->fds)
                continue;
            int fds[4];
            int k = wins[i]->app->fds(wins[i], fds, 4);
            for (int j = 0; j < k && n < 60; j++) {
                pfd[n] = (struct pollfd){ fds[j], POLLIN, 0 };
                kind[n++] = wins[i]->id;
            }
        }
        long t = now_ms();
        int timeout = (int)maxi(0, (int)(next_tick - t));
        int r = poll(pfd, n, timeout);
        if (r < 0 && errno != EINTR)
            break;
        for (int i = 0; r > 0 && i < n; i++) {
            if (!(pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            if (kind[i] == -1) {
                input_read(pfd[i].fd);
            } else {
                for (int j = 0; j < nwins; j++)
                    if (wins[j]->id == kind[i] && wins[j]->app->io) {
                        wins[j]->app->io(wins[j], pfd[i].fd);
                        break;
                    }
            }
        }
        t = now_ms();
        if (t >= next_tick) {
            next_tick = t + 250;
            tick();
        }
        if (t >= next_net) {
            next_net = t + 10000;
            update_net();
        }
        if (t >= next_scan) {
            next_scan = t + 3000;
            input_rescan();
        }
        /* kapanması istenen pencereler */
        for (int i = 0; i < nwins; i++)
            if (wins[i]->closing) {
                wm_close(wins[i]);
                i--;
            }
    }
    while (nwins)
        wm_close(wins[nwins - 1]);
    fb_close();
    return quit_code < 0 ? 0 : quit_code;
}
