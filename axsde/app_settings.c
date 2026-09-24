/* AxsDE - Ayarlar: görünüm, klavye, saat, ağ, sistem */
#include "axsde.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

typedef struct {
    UiState ui;
    int page;
    Surf thumbs[N_WALLPAPERS];
    int thumb_accent;   /* önizlemeler hangi vurgu rengiyle üretildi */
    TextField test;
} SetSt;

static const char *PAGES[] = { "Görünüm", "Klavye", "Tarih ve saat", "Ağ", "Sistem" };
static const IconId PAGE_IC[] = { IC_SETTINGS, IC_KEYBOARD, IC_CLOCK, IC_NETWORK, IC_CPU };
#define N_PAGES 5

static void make_thumbs(SetSt *st)
{
    for (int i = 0; i < N_WALLPAPERS; i++) {
        surf_resize(&st->thumbs[i], 150, 94);
        wallpaper_render(&st->thumbs[i], i);
    }
    st->thumb_accent = accent_idx;
}

static void s_init(Win *w, const char *arg)
{
    (void)arg;
    SetSt *st = calloc(1, sizeof *st);
    st->ui.cur_hot = -1;
    st->thumb_accent = -1;
    w->st = st;
}

static void section(Surf *s, int x, int y, const char *t, const char *sub)
{
    draw_text(s, F_UI_BOLD, 16, x, y, T.text, t);
    if (sub)
        draw_text(s, F_UI, 12, x, y + 24, T.muted, sub);
}

static void page_look(Surf *s, SetSt *st, UiState *u, Rect a)
{
    if (st->thumb_accent != accent_idx)
        make_thumbs(st);
    int y = a.y;
    section(s, a.x, y, "Duvar kâğıdı", "Masaüstü arka planı");
    y += 50;
    for (int i = 0; i < N_WALLPAPERS; i++) {
        int col = i % 3, row = i / 3;
        Rect r = { a.x + col * 166, y + row * 132, 150, 94 };
        int sel = i == wallpaper_idx;
        int hov = ui_hover(u, r);
        if (sel)
            fill_rrect(s, rect_inset(r, -4), 14, T.accent);
        else if (hov)
            fill_rrect(s, rect_inset(r, -3), 13, T.overlay);
        blit_rrect(s, r.x, r.y, &st->thumbs[i], (Rect){ 0, 0, 150, 94 }, 10, 15);
        draw_text_center(s, sel ? F_UI_BOLD : F_UI, 12, (Rect){ r.x, r.y + 98, r.w, 20 }, sel ? T.text : T.subtext, WALLPAPER_NAMES[i]);
        if (ui_clicked(u, r) && !sel) {
            wallpaper_idx = i;
            config_save();
            wm_wallpaper_changed();
        }
    }
    y += 2 * 132 + 10;
    section(s, a.x, y, "Vurgu rengi", "Düğmeler, seçimler ve simgeler");
    y += 52;
    for (int i = 0; i < N_ACCENTS; i++) {
        float cx = a.x + 22 + i * 64.f, cy = y + 20.f;
        Rect r = { (int)cx - 22, (int)cy - 22, 44, 44 };
        int sel = i == accent_idx;
        if (sel)
            stroke_circle(s, cx, cy, 21, 3, T.text);
        else if (ui_hover(u, r))
            stroke_circle(s, cx, cy, 21, 2, T.overlay);
        fill_circle(s, cx, cy, 15, ACCENTS[i][0]);
        fill_circle(s, cx - 4, cy - 5, 5, ALPHA(HEX(0xFFFFFF), 60));
        draw_text_center(s, F_UI, 11, (Rect){ (int)cx - 32, (int)cy + 26, 64, 16 }, sel ? T.text : T.muted, ACCENT_NAMES[i]);
        if (ui_clicked(u, r) && !sel) {
            theme_set_accent(i);
            config_save();
            wm_wallpaper_changed();
        }
    }
}

static void page_keyboard(Surf *s, SetSt *st, UiState *u, Rect a)
{
    int y = a.y;
    section(s, a.x, y, "Klavye düzeni", "Üst çubuktaki TR/US düğmesiyle de değiştirebilirsin");
    y += 56;
    for (int i = 0; i < 2; i++) {
        Rect r = { a.x, y + i * 76, a.w, 64 };
        int sel = i == kb_layout;
        fill_rrect(s, r, 14, sel ? ALPHA(T.accent, 60) : T.surface2);
        stroke_rrect(s, r, 14, sel ? 2 : 1, sel ? T.accent : T.border);
        draw_icon(s, IC_KEYBOARD, r.x + 18, r.y + 16, 32);
        draw_text(s, F_UI_BOLD, 15, r.x + 66, r.y + 12, T.text, LAYOUT_NAMES[i]);
        draw_text(s, F_UI, 12, r.x + 66, r.y + 36, T.subtext, i == 0 ? "ğ ü ş ı ö ç — Türkçe Q klavye" : "QWERTY — US English");
        if (sel)
            ui_badge(s, r.x + r.w - 80, r.y + 22, "Etkin", T.accent, HEX(0xFFFFFF));
        ui_hover(u, r);
        if (ui_clicked(u, r) && !sel) {
            kb_layout = i;
            config_save();
        }
    }
    y += 170;
    section(s, a.x, y, "Deneme alanı", "Tıkla ve yaz: ÇĞİÖŞÜ çğıöşü");
    y += 50;
    Rect tf = { a.x, y, a.w, 42 };
    if (ui_clicked(u, tf))
        st->test.focus = 1;
    tf_draw(s, &st->test, tf, "Buraya yazmayı dene…");
}

static void page_time(Surf *s, SetSt *st, UiState *u, Rect a)
{
    (void)st;
    int y = a.y;
    section(s, a.x, y, "Saat dilimi", "Donanım saati UTC kabul edilir");
    y += 60;
    time_t now = time(NULL) + tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&now, &tm);
    char big[32], tz[32];
    snprintf(big, sizeof big, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    draw_text(s, F_UI_BOLD, 44, a.x, y, T.text, big);
    int h = tz_offset_min / 60, m = abs(tz_offset_min % 60);
    snprintf(tz, sizeof tz, "UTC%+d%s", h, m ? ":30" : "");
    if (tz_offset_min < 0 && h == 0)
        snprintf(tz, sizeof tz, "UTC-0:30");
    y += 70;
    Rect minus = { a.x, y, 44, 40 }, plus = { a.x + 170, y, 44, 40 };
    if (ui_button(s, u, minus, "−", BTN_NORMAL)) {
        tz_offset_min = maxi(tz_offset_min - 60, -12 * 60);
        config_save();
    }
    draw_text_center(s, F_UI_BOLD, 16, (Rect){ a.x + 44, y, 126, 40 }, T.accent2, tz);
    if (ui_button(s, u, plus, "+", BTN_NORMAL)) {
        tz_offset_min = mini(tz_offset_min + 60, 14 * 60);
        config_save();
    }
    if (ui_button(s, u, (Rect){ a.x + 240, y, 190, 40 }, "Türkiye (UTC+3)", tz_offset_min == 180 ? BTN_PRIMARY : BTN_NORMAL)) {
        tz_offset_min = 180;
        config_save();
    }
}

/* Arka planda komut çalıştır (beklemeden; çift fork, zombi kalmaz) */
static void spawn_bg(char *const argv[])
{
    pid_t p = fork();
    if (p == 0) {
        if (fork() == 0) {
            setsid();
            int nul = open("/dev/null", O_RDWR);
            if (nul >= 0) {
                dup2(nul, 0);
                dup2(nul, 1);
                dup2(nul, 2);
            }
            for (int fd = 3; fd < 1024; fd++)
                close(fd);
            execv(argv[0], argv);
            _exit(127);
        }
        _exit(0);
    }
    if (p > 0)
        waitpid(p, NULL, 0);
}

static void sysfs_str(const char *ifname, const char *attr, char *out, size_t n)
{
    char p[160];
    snprintf(p, sizeof p, "/sys/class/net/%s/%s", ifname, attr);
    out[0] = 0;
    if (read_file(p, out, n) > 0)
        out[strcspn(out, "\n")] = 0;
}

/* Ağ: gerçek ağ kartları (sürücü, bağlantı, adres), ağ geçidi, DNS */
static void page_network(Surf *s, SetSt *st, UiState *u, Rect a)
{
    (void)st;
    int y = a.y;
    section(s, a.x, y, "Ağ bağlantıları", "Kablolu (Ethernet) ve sanal makine ağ kartları; adres DHCP ile alınır");
    y += 52;
    DIR *d = opendir("/sys/class/net");
    struct dirent *e;
    int cards = 0;
    while (d && (e = readdir(d)) && y < a.y + a.h - 150) {
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo"))
            continue;
        char p[160], drv[128], oper[32], carrier[8], mac[32], speed[16], ip[64] = "adres yok";
        snprintf(p, sizeof p, "/sys/class/net/%s/device/driver", e->d_name);
        char link[256];
        ssize_t k = readlink(p, link, sizeof link - 1);
        if (k <= 0)
            continue; /* sanal arayüz (sit0, tünel vb.): gerçek bir kart değil */
        link[k] = 0;
        snprintf(drv, sizeof drv, "%s", strrchr(link, '/') ? strrchr(link, '/') + 1 : link);
        sysfs_str(e->d_name, "operstate", oper, sizeof oper);
        sysfs_str(e->d_name, "carrier", carrier, sizeof carrier);
        sysfs_str(e->d_name, "address", mac, sizeof mac);
        sysfs_str(e->d_name, "speed", speed, sizeof speed);
        struct ifaddrs *ifs = NULL;
        if (getifaddrs(&ifs) == 0) {
            for (struct ifaddrs *ia = ifs; ia; ia = ia->ifa_next)
                if (ia->ifa_addr && ia->ifa_addr->sa_family == AF_INET && !strcmp(ia->ifa_name, e->d_name))
                    inet_ntop(AF_INET, &((struct sockaddr_in *)ia->ifa_addr)->sin_addr, ip, sizeof ip);
            freeifaddrs(ifs);
        }
        int up = !strcmp(carrier, "1") && strcmp(ip, "adres yok");
        Rect r = { a.x, y, a.w, 92 };
        fill_rrect(s, r, 12, T.surface2);
        draw_icon(s, IC_NETWORK, r.x + 16, r.y + 16, 28);
        char title[96];
        snprintf(title, sizeof title, "%s  —  %s", e->d_name, drv);
        draw_text(s, F_UI_BOLD, 15, r.x + 56, r.y + 14, T.text, title);
        const char *state = up ? "Bağlı" : !strcmp(carrier, "1") ? "Adres bekleniyor" : "Kablo takılı değil";
        ui_badge(s, r.x + r.w - 150, r.y + 14, state, ALPHA(up ? T.green : T.yellow, 50), up ? T.green : T.yellow);
        char l1[160], l2[160];
        snprintf(l1, sizeof l1, "IPv4: %s", ip);
        long sp = atol(speed);
        char spd[32] = "-";
        if (sp >= 1000)
            snprintf(spd, sizeof spd, "%ld Gbit/sn", sp / 1000);
        else if (sp > 0)
            snprintf(spd, sizeof spd, "%ld Mbit/sn", sp);
        snprintf(l2, sizeof l2, "MAC: %s   •   Hız: %s", mac, spd);
        draw_text(s, F_MONO, 13, r.x + 56, r.y + 42, up ? T.text : T.subtext, l1);
        draw_text(s, F_UI, 12, r.x + 56, r.y + 64, T.muted, l2);
        y += 102;
        cards++;
    }
    if (d)
        closedir(d);
    if (!cards) {
        draw_text(s, F_UI, 14, a.x, y, T.subtext, "Ağ kartı bulunamadı.");
        y += 30;
    }
    /* ağ geçidi ve DNS */
    char gw[64] = "-", dns[160] = "", buf[4096];
    if (read_file("/proc/net/route", buf, sizeof buf) > 0) {
        for (char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n")) {
            char ifn[32];
            unsigned dst, g;
            if (sscanf(ln, "%31s %x %x", ifn, &dst, &g) == 3 && dst == 0 && g) {
                struct in_addr ga = { g };
                inet_ntop(AF_INET, &ga, gw, sizeof gw);
                break;
            }
        }
    }
    if (read_file("/etc/resolv.conf", buf, sizeof buf) > 0)
        for (char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n"))
            if (!strncmp(ln, "nameserver ", 11) && strlen(dns) + strlen(ln + 11) + 3 < sizeof dns) {
                if (dns[0])
                    strcat(dns, ", ");
                strcat(dns, ln + 11);
            }
    char info[256];
    snprintf(info, sizeof info, "Ağ geçidi: %s     DNS: %s", gw, dns[0] ? dns : "-");
    draw_text(s, F_UI, 13, a.x, y + 4, T.subtext, info);
    y += 34;
    if (ui_button(s, u, (Rect){ a.x, y, 170, 38 }, "Yeniden bağlan", BTN_PRIMARY)) {
        char *argv[] = { "/sbin/ag", "yenile", NULL };
        spawn_bg(argv);
        wm_notify("Ağ", "DHCP ile yeniden adres isteniyor...", IC_NETWORK);
    }
    if (ui_button(s, u, (Rect){ a.x + 180, y, 190, 38 }, "Bağlantıyı sına", BTN_NORMAL))
        wm_run_in_terminal("ag durum; echo; ag test", "Ağ testi");
    draw_text_wrap(s, F_UI, 11, a.x, y + 52, a.w, 2, 15, T.muted,
                   "Kablosuz (Wi-Fi) kartlar henüz desteklenmiyor. Elle adres: terminalde \"ag sabit eth0 192.168.1.50/24 192.168.1.1\"");
}

static void page_system(Surf *s, SetSt *st, UiState *u, Rect a)
{
    (void)st;
    int y = a.y;
    section(s, a.x, y, "Sistem bilgisi", NULL);
    y += 40;
    struct utsname un;
    uname(&un);
    char res[32], up[64], mem[64] = "?";
    snprintf(res, sizeof res, "%d × %d", SCREEN_W, SCREEN_H);
    char buf[2048];
    long upt = 0;
    if (read_file("/proc/uptime", buf, sizeof buf) > 0)
        upt = atol(buf);
    snprintf(up, sizeof up, "%ld sa %ld dk %ld sn", upt / 3600, (upt / 60) % 60, upt % 60);
    if (read_file("/proc/meminfo", buf, sizeof buf) > 0) {
        long tot = 0, av = 0;
        char *p = strstr(buf, "MemTotal:");
        if (p) sscanf(p, "MemTotal: %ld", &tot);
        p = strstr(buf, "MemAvailable:");
        if (p) sscanf(p, "MemAvailable: %ld", &av);
        snprintf(mem, sizeof mem, "%ld MB / %ld MB kullanımda", (tot - av) / 1024, tot / 1024);
    }
    char kern[160];
    snprintf(kern, sizeof kern, "%s %s (%s)", un.sysname, un.release, un.machine);
    struct { const char *k, *v; } rows[] = {
        { "Bilgisayar adı", un.nodename }, { "Çekirdek", kern }, { "Ekran", res },
        { "Bellek", mem }, { "Açık kalma süresi", up }, { "Masaüstü", "AxsDE " AXSDE_VERSION },
    };
    for (int i = 0; i < 6; i++) {
        Rect r = { a.x, y + i * 44, a.w, 40 };
        fill_rrect(s, r, 10, i % 2 ? T.surface : T.surface2);
        draw_text(s, F_UI, 13, r.x + 14, r.y + 11, T.subtext, rows[i].k);
        draw_text_fit(s, F_UI_BOLD, 13, r.x + 180, r.y + 11, r.w - 190, T.text, rows[i].v);
    }
    y += 6 * 44 + 20;
    if (ui_button(s, u, (Rect){ a.x, y, 210, 40 }, "Metin konsoluna geç", BTN_NORMAL))
        wm_quit(0);
}

static void s_draw(Win *w, Surf *s)
{
    SetSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    fill_rect(s, (Rect){ 0, 0, s->w, s->h }, T.surface);
    /* kenar çubuğu */
    Rect side = { 0, 0, 200, s->h };
    fill_rect(s, side, T.base);
    fill_rect(s, (Rect){ side.w - 1, 0, 1, s->h }, T.border);
    for (int i = 0; i < N_PAGES; i++) {
        Rect r = { 10, 14 + i * 44, side.w - 20, 38 };
        int sel = i == st->page;
        int hov = ui_hover(u, r);
        if (sel)
            fill_rrect(s, r, 9, ALPHA(T.accent, 90));
        else if (hov)
            fill_rrect(s, r, 9, T.surface2);
        draw_icon(s, PAGE_IC[i], r.x + 10, r.y + 9, 20);
        draw_text(s, sel ? F_UI_BOLD : F_UI, 14, r.x + 40, r.y + (r.h - font_height(F_UI, 14)) / 2, sel ? T.text : T.subtext, PAGES[i]);
        if (ui_clicked(u, r))
            st->page = i;
    }
    Rect a = { side.w + 28, 24, s->w - side.w - 56, s->h - 48 };
    Rect old = s->clip;
    surf_clip(s, (Rect){ side.w, 0, s->w - side.w, s->h });
    switch (st->page) {
    case 0: page_look(s, st, u, a); break;
    case 1: page_keyboard(s, st, u, a); break;
    case 2: page_time(s, st, u, a); break;
    case 3: page_network(s, st, u, a); break;
    case 4: page_system(s, st, u, a); break;
    }
    s->clip = old;
}

static void s_mouse(Win *w, MouseEv *e)
{
    SetSt *st = w->st;
    if (e->kind == M_DOWN)
        st->test.focus = 0;
    ui_dispatch(w, &st->ui, e);
}

static void s_key(Win *w, KeyEv *e)
{
    SetSt *st = w->st;
    if (st->page == 1 && st->test.focus && tf_key(&st->test, e))
        w->dirty = 1;
}

static void s_tick(Win *w)
{
    SetSt *st = w->st;
    static int n;
    if ((st->page == 2 || st->page == 3 || st->page == 4) && ++n % 4 == 0) /* saniyede bir */
        w->dirty = 1;
}

static void s_close(Win *w)
{
    SetSt *st = w->st;
    for (int i = 0; i < N_WALLPAPERS; i++)
        surf_free(&st->thumbs[i]);
    free(st);
}

const App APP_SETTINGS = {
    .id = "ayarlar", .name = "Ayarlar", .desc = "Görünüm, klavye, saat, ağ",
    .icon = IC_SETTINGS, .w = 760, .h = 520, .single = 1,
    .init = s_init, .draw = s_draw, .mouse = s_mouse, .key = s_key, .tick = s_tick, .close = s_close,
};
