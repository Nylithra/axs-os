/* AxsDE - Paket Merkezi: axpkg için grafik arayüz */
#include "axsde.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPO "/var/lib/axpkg/repo"
#define DB "/var/lib/axpkg/db"
#define MAXPKG 64

typedef struct {
    char name[64], version[32], desc[256], depends[128], file[256];
    char inst_version[32];
    int installed;
} Pkg;

typedef struct {
    UiState ui;
    Pkg pk[MAXPKG];
    int n, scroll;
    Proc job;
    int busy;
    char busy_name[64];
    int busy_install;
    char log[4096];
    int filter; /* 0 tümü, 1 kurulu */
    int spin;
} PkgSt;

static void parse_manifest(const char *txt, Pkg *p, int into_inst)
{
    char line[512];
    const char *s = txt;
    while (*s) {
        size_t n = strcspn(s, "\n");
        snprintf(line, sizeof line, "%.*s", (int)(n < 511 ? n : 511), s);
        s += n + (s[n] == '\n');
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char *v = eq + 1;
        if (into_inst) {
            if (!strcmp(line, "version"))
                snprintf(p->inst_version, sizeof p->inst_version, "%s", v);
            continue;
        }
        if (!strcmp(line, "name")) snprintf(p->name, sizeof p->name, "%s", v);
        else if (!strcmp(line, "version")) snprintf(p->version, sizeof p->version, "%s", v);
        else if (!strcmp(line, "description")) snprintf(p->desc, sizeof p->desc, "%s", v);
        else if (!strcmp(line, "depends")) snprintf(p->depends, sizeof p->depends, "%s", v);
    }
}

static void reload(PkgSt *st)
{
    st->n = 0;
    DIR *d = opendir(REPO);
    struct dirent *e;
    while (d && (e = readdir(d)) && st->n < MAXPKG) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".axp"))
            continue;
        Pkg *p = &st->pk[st->n];
        memset(p, 0, sizeof *p);
        snprintf(p->file, sizeof p->file, REPO "/%s", e->d_name);
        char out[2048] = "";
        char *argv[] = { "tar", "-xzOf", p->file, "MANIFEST", NULL };
        if (run_capture(argv, out, sizeof out) != 0 || !strstr(out, "name=")) {
            char *argv2[] = { "tar", "-xzOf", p->file, "./MANIFEST", NULL };
            run_capture(argv2, out, sizeof out);
        }
        parse_manifest(out, p, 0);
        if (!p->name[0])
            continue;
        char mp[256], buf[2048];
        snprintf(mp, sizeof mp, DB "/%s/MANIFEST", p->name);
        if (read_file(mp, buf, sizeof buf) > 0) {
            p->installed = 1;
            parse_manifest(buf, p, 1);
        }
        st->n++;
    }
    if (d)
        closedir(d);
    /* ada göre sırala */
    for (int i = 1; i < st->n; i++) {
        Pkg k = st->pk[i];
        int j = i - 1;
        while (j >= 0 && strcmp(st->pk[j].name, k.name) > 0) {
            st->pk[j + 1] = st->pk[j];
            j--;
        }
        st->pk[j + 1] = k;
    }
}

static void p_init(Win *w, const char *arg)
{
    (void)arg;
    PkgSt *st = calloc(1, sizeof *st);
    st->ui.cur_hot = -1;
    st->job.fd = -1;
    w->st = st;
    reload(st);
    snprintf(st->log, sizeof st->log, "Hazır. %d paket depoda.", st->n);
}

static void start_job(Win *w, Pkg *p, int install)
{
    PkgSt *st = w->st;
    if (st->busy)
        return;
    char *argv[] = { "axpkg", install ? "install" : "remove", p->name, NULL };
    if (proc_start(&st->job, argv) < 0) {
        wm_notify("Paket Merkezi", "axpkg çalıştırılamadı", IC_PACKAGES);
        return;
    }
    st->busy = 1;
    st->busy_install = install;
    snprintf(st->busy_name, sizeof st->busy_name, "%s", p->name);
    snprintf(st->log, sizeof st->log, "$ axpkg %s %s\n", install ? "install" : "remove", p->name);
    w->dirty = 1;
}

static void strip_ansi(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '\033') {
            while (*p && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
                p++;
            if (!*p)
                break;
            continue;
        }
        *o++ = *p;
    }
    *o = 0;
}

static void p_draw(Win *w, Surf *s)
{
    PkgSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.surface);

    /* başlık */
    for (int y = 0; y < 96; y++)
        fill_rect(s, (Rect){ 0, y, W, 1 }, mix(HEX(0x3B2A0A), T.surface, y * 255 / 96));
    draw_icon(s, IC_PACKAGES, 24, 20, 56);
    draw_text(s, F_UI_BOLD, 22, 96, 22, T.text, "Paket Merkezi");
    int ninst = 0;
    for (int i = 0; i < st->n; i++)
        ninst += st->pk[i].installed;
    char sub[96];
    snprintf(sub, sizeof sub, "%d paket mevcut  •  %d kurulu  •  axpkg ile güçlendirildi", st->n, ninst);
    draw_text(s, F_UI, 13, 97, 56, T.subtext, sub);
    /* süzgeç sekmeleri */
    const char *tabs[] = { "Tümü", "Kurulu" };
    for (int i = 0; i < 2; i++) {
        Rect r = { W - 250 + i * 96, 30, 88, 32 };
        int sel = st->filter == i;
        int hov = ui_hover(u, r);
        fill_rrect(s, r, 16, sel ? T.accent : hov ? T.overlay : T.surface2);
        draw_text_center(s, F_UI_BOLD, 13, r, sel ? HEX(0xFFFFFF) : T.subtext, tabs[i]);
        if (ui_clicked(u, r)) {
            st->filter = i;
            st->scroll = 0;
        }
    }
    if (ui_icon_button(s, u, (Rect){ W - 56, 29, 34, 34 }, IC_REFRESH, "Yenile") && !st->busy) {
        reload(st);
        snprintf(st->log, sizeof st->log, "Depo yenilendi: %d paket.", st->n);
    }

    /* kartlar */
    int log_h = 110;
    Rect area = { 20, 110, W - 40, H - 110 - log_h - 20 };
    int cols = W > 700 ? 2 : 1;
    int cw = (area.w - (cols - 1) * 14) / cols, ch = 118;
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, area));
    int shown = 0;
    for (int i = 0; i < st->n; i++) {
        Pkg *p = &st->pk[i];
        if (st->filter == 1 && !p->installed)
            continue;
        int idx = shown++;
        int row = idx / cols, col = idx % cols;
        Rect r = { area.x + col * (cw + 14), area.y + row * (ch + 14) - st->scroll, cw, ch };
        if (r.y + r.h < area.y || r.y > area.y + area.h)
            continue;
        fill_rrect(s, r, 16, T.surface2);
        stroke_rrect(s, r, 16, 1, p->installed ? ALPHA(T.green, 90) : ALPHA(HEX(0xFFFFFF), 14));
        draw_icon(s, IC_PACKAGE, r.x + 16, r.y + 18, 40);
        draw_text(s, F_UI_BOLD, 15, r.x + 70, r.y + 16, T.text, p->name);
        int nx = r.x + 78 + text_width(F_UI_BOLD, 15, p->name);
        ui_badge(s, nx, r.y + 16, p->version, T.overlay, T.subtext);
        if (p->installed)
            ui_badge(s, nx + text_width(F_UI_BOLD, 11, p->version) + 22, r.y + 16, "Kurulu", ALPHA(T.green, 60), T.green);
        draw_text_wrap(s, F_UI, 13, r.x + 70, r.y + 42, r.w - 86, 2, 18, T.subtext, p->desc);
        if (p->depends[0]) {
            char dep[160];
            snprintf(dep, sizeof dep, "Bağımlılık: %s", p->depends);
            draw_text_fit(s, F_UI, 11, r.x + 70, r.y + r.h - 34, r.w - 200, T.muted, dep);
        }
        Rect b = { r.x + r.w - 120, r.y + r.h - 46, 104, 34 };
        if (st->busy && !strcmp(st->busy_name, p->name)) {
            fill_rrect(s, b, 8, T.overlay);
            static const char *sp[] = { "◐", "◓", "◑", "◒" };
            (void)sp;
            char lab[48];
            snprintf(lab, sizeof lab, "%s%.*s", st->busy_install ? "Kuruluyor" : "Kaldırılıyor", st->spin % 4, "...");
            draw_text_center(s, F_UI_BOLD, 12, b, T.text, lab);
        } else if (p->installed) {
            if (ui_button(s, u, b, "Kaldır", BTN_NORMAL) && !st->busy)
                start_job(w, p, 0);
        } else {
            if (ui_button(s, u, b, "Kur", BTN_PRIMARY) && !st->busy)
                start_job(w, p, 1);
        }
    }
    if (!shown)
        draw_text_center(s, F_UI, 14, (Rect){ area.x, area.y + 40, area.w, 30 }, T.muted,
                         st->filter ? "Henüz kurulu paket yok" : "Depoda paket yok");
    s->clip = oc;
    int rows = (shown + cols - 1) / cols;
    int total_h = rows * (ch + 14);
    st->scroll = clampi(st->scroll, 0, maxi(0, total_h - area.h));
    ui_scrollbar(s, (Rect){ W - 12, area.y, 5, area.h }, total_h, area.h, st->scroll);

    /* günlük */
    Rect lg = { 20, H - log_h - 10, W - 40, log_h };
    fill_rrect(s, lg, 12, HEX(0x13131D));
    draw_text(s, F_UI_BOLD, 11, lg.x + 14, lg.y + 10, T.muted, "İŞLEM GÜNLÜĞÜ");
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s", st->log);
    strip_ansi(tmp);
    /* son 4 satır */
    char *lines[64];
    int nl = 0;
    for (char *t = strtok(tmp, "\n"); t && nl < 64; t = strtok(NULL, "\n"))
        lines[nl++] = t;
    int start = maxi(0, nl - 4);
    for (int i = start; i < nl; i++)
        draw_text_fit(s, F_MONO, 12, lg.x + 14, lg.y + 30 + (i - start) * 18, lg.w - 28, T.text, lines[i]);
}

static void p_mouse(Win *w, MouseEv *e)
{
    PkgSt *st = w->st;
    if (e->kind == M_WHEEL) {
        st->scroll -= e->wheel * 40;
        w->dirty = 1;
        return;
    }
    ui_dispatch(w, &st->ui, e);
}

static int p_fds(Win *w, int *fds, int max)
{
    PkgSt *st = w->st;
    if (st->busy && st->job.fd >= 0 && max > 0) {
        fds[0] = st->job.fd;
        return 1;
    }
    return 0;
}

static void p_io(Win *w, int fd)
{
    (void)fd;
    PkgSt *st = w->st;
    int done = proc_read(&st->job);
    if (st->job.out) {
        size_t l = strlen(st->log);
        snprintf(st->log + l, sizeof st->log - l, "%s", st->job.out);
        st->job.len = 0;
        st->job.out[0] = 0;
    }
    if (done) {
        char msg[128];
        int ok = st->job.status == 0;
        snprintf(msg, sizeof msg, "%s %s", st->busy_name,
                 ok ? (st->busy_install ? "kuruldu" : "kaldırıldı") : "işlemi başarısız");
        wm_notify(ok ? "Paket Merkezi" : "Paket Merkezi — hata", msg, IC_PACKAGES);
        proc_free(&st->job);
        st->busy = 0;
        reload(st);
    }
    w->dirty = 1;
}

static void p_tick(Win *w)
{
    PkgSt *st = w->st;
    if (st->busy) {
        st->spin++;
        w->dirty = 1;
    }
}

static void p_close(Win *w)
{
    PkgSt *st = w->st;
    proc_free(&st->job);
    free(st);
}

const App APP_PACKAGES = {
    .id = "paketler", .name = "Paket Merkezi", .desc = "Uygulama kur ve kaldır",
    .icon = IC_PACKAGES, .w = 800, .h = 560, .single = 1,
    .init = p_init, .draw = p_draw, .mouse = p_mouse, .fds = p_fds, .io = p_io, .tick = p_tick, .close = p_close,
};
