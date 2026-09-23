/* AxsDE - Sistem İzleyici: işlemci, bellek, süreçler */
#include "axsde.h"

#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST 90
#define MAXP 256

typedef struct { int pid; char name[40]; char state; long rss_kb; } PInfo;

typedef struct {
    UiState ui;
    float cpu[HIST], mem[HIST];
    int nh;
    unsigned long long last_total, last_idle;
    long mem_total, mem_used;
    PInfo procs[MAXP];
    int np, sel_pid, scroll;
    int ticks;
    char load[64], uptime[64];
} MonSt;

static void sample(MonSt *st)
{
    char buf[8192];
    if (read_file("/proc/stat", buf, sizeof buf) > 0) {
        unsigned long long u, n, s, idle, io, irq, sirq, steal;
        if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &io, &irq, &sirq, &steal) == 8) {
            unsigned long long tot = u + n + s + idle + io + irq + sirq + steal, id = idle + io;
            float v = 0;
            if (st->last_total && tot > st->last_total)
                v = 1.0f - (float)(id - st->last_idle) / (float)(tot - st->last_total);
            st->last_total = tot;
            st->last_idle = id;
            memmove(st->cpu, st->cpu + 1, sizeof(float) * (HIST - 1));
            st->cpu[HIST - 1] = v < 0 ? 0 : v > 1 ? 1 : v;
        }
    }
    if (read_file("/proc/meminfo", buf, sizeof buf) > 0) {
        long tot = 0, av = 0;
        char *p = strstr(buf, "MemTotal:");
        if (p) sscanf(p, "MemTotal: %ld", &tot);
        p = strstr(buf, "MemAvailable:");
        if (p) sscanf(p, "MemAvailable: %ld", &av);
        st->mem_total = tot;
        st->mem_used = tot - av;
        memmove(st->mem, st->mem + 1, sizeof(float) * (HIST - 1));
        st->mem[HIST - 1] = tot ? (float)(tot - av) / tot : 0;
    }
    if (st->nh < HIST)
        st->nh++;
    if (read_file("/proc/loadavg", buf, sizeof buf) > 0) {
        float a, b, c;
        if (sscanf(buf, "%f %f %f", &a, &b, &c) == 3)
            snprintf(st->load, sizeof st->load, "%.2f  %.2f  %.2f", a, b, c);
    }
    if (read_file("/proc/uptime", buf, sizeof buf) > 0) {
        long up = atol(buf);
        snprintf(st->uptime, sizeof st->uptime, "%ld:%02ld:%02ld", up / 3600, (up / 60) % 60, up % 60);
    }
    /* süreçler */
    st->np = 0;
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d && (e = readdir(d)) && st->np < MAXP) {
        if (!isdigit((unsigned char)e->d_name[0]))
            continue;
        char path[64];
        snprintf(path, sizeof path, "/proc/%s/stat", e->d_name);
        if (read_file(path, buf, sizeof buf) <= 0)
            continue;
        char *l = strchr(buf, '('), *r = strrchr(buf, ')');
        if (!l || !r)
            continue;
        PInfo *p = &st->procs[st->np];
        p->pid = atoi(buf);
        int nl = (int)(r - l - 1);
        snprintf(p->name, sizeof p->name, "%.*s", nl < 39 ? nl : 39, l + 1);
        p->state = r[2];
        /* ')' sonrasındaki alanlar: 3=state, 9=flags, 23=vsize, 24=rss */
        char *f = r + 2;
        long rss = 0;
        unsigned long flags = 0, vsize = 0;
        for (int i = 3; i <= 24 && f; i++) {
            if (i == 9)
                flags = strtoul(f, NULL, 10);
            else if (i == 23)
                vsize = strtoul(f, NULL, 10);
            else if (i == 24)
                rss = atol(f);
            f = strchr(f, ' ');
            if (f)
                f++;
        }
        if (flags & 0x00200000) /* PF_KTHREAD: çekirdek iş parçacığı */
            continue;
        /* bu çekirdek yapılandırmasında RSS 0 olabilir: o zaman sanal boyut */
        p->rss_kb = rss > 0 ? rss * 4 : (long)(vsize / 1024);
        st->np++;
    }
    if (d)
        closedir(d);
    /* belleğe göre sırala */
    for (int i = 1; i < st->np; i++) {
        PInfo k = st->procs[i];
        int j = i - 1;
        while (j >= 0 && st->procs[j].rss_kb < k.rss_kb) {
            st->procs[j + 1] = st->procs[j];
            j--;
        }
        st->procs[j + 1] = k;
    }
}

static void m_init(Win *w, const char *arg)
{
    (void)arg;
    MonSt *st = calloc(1, sizeof *st);
    st->ui.cur_hot = -1;
    w->st = st;
    sample(st);
}

static void chart(Surf *s, Rect r, const float *v, int n, uint32_t c)
{
    fill_rrect(s, r, 10, T.base);
    for (int i = 1; i < 4; i++)
        fill_rect(s, (Rect){ r.x + 8, r.y + r.h * i / 4, r.w - 16, 1 }, ALPHA(HEX(0xFFFFFF), 10));
    if (n < 2)
        return;
    float step = (float)(r.w - 16) / (HIST - 1);
    int start = HIST - n;
    Rect old = s->clip;
    surf_clip(s, rect_isect(old, rect_inset(r, 2)));
    /* dolgu alanı: dikey şeritler */
    for (int i = start; i < HIST; i++) {
        float x = r.x + 8 + i * step;
        int top = r.y + r.h - 6 - (int)(v[i] * (r.h - 12));
        fill_rect(s, (Rect){ (int)x, top, (int)step + 1, r.y + r.h - 6 - top }, ALPHA(c, 40));
    }
    for (int i = start + 1; i < HIST; i++) {
        float x0 = r.x + 8 + (i - 1) * step, x1 = r.x + 8 + i * step;
        float y0 = r.y + r.h - 6 - v[i - 1] * (r.h - 12), y1 = r.y + r.h - 6 - v[i] * (r.h - 12);
        draw_line(s, x0, y0, x1, y1, 2.2f, c);
    }
    s->clip = old;
}

static void stat_card(Surf *s, Rect r, IconId ic, const char *title, const char *value, const char *sub)
{
    fill_rrect(s, r, 14, T.surface2);
    stroke_rrect(s, r, 14, 1, ALPHA(HEX(0xFFFFFF), 12));
    draw_icon(s, ic, r.x + 14, r.y + 14, 22);
    draw_text(s, F_UI, 12, r.x + 44, r.y + 17, T.subtext, title);
    draw_text(s, F_UI_BOLD, 24, r.x + 16, r.y + 44, T.text, value);
    if (sub)
        draw_text_fit(s, F_UI, 12, r.x + 16, r.y + 80, r.w - 24, T.muted, sub);
}

static void m_draw(Win *w, Surf *s)
{
    MonSt *st = w->st;
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.surface);
    int pad = 20;
    int cw = (W - pad * 2 - 16) / 2;
    char v1[32], v2[32], sub2[64];
    snprintf(v1, sizeof v1, "%%%d", (int)(st->cpu[HIST - 1] * 100 + 0.5f));
    snprintf(v2, sizeof v2, "%%%d", (int)(st->mem[HIST - 1] * 100 + 0.5f));
    snprintf(sub2, sizeof sub2, "%ld / %ld MB", st->mem_used / 1024, st->mem_total / 1024);
    char sub1[96];
    snprintf(sub1, sizeof sub1, "Yük: %s", st->load);
    Rect c1 = { pad, pad, cw, 210 }, c2 = { pad + cw + 16, pad, cw, 210 };
    stat_card(s, c1, IC_CPU, "İşlemci", v1, sub1);
    stat_card(s, c2, IC_MEMORY, "Bellek", v2, sub2);
    chart(s, (Rect){ c1.x + 12, c1.y + 104, c1.w - 24, 94 }, st->cpu, st->nh, T.accent2);
    chart(s, (Rect){ c2.x + 12, c2.y + 104, c2.w - 24, 94 }, st->mem, st->nh, HEX(0x34D399));
    char upt[96];
    snprintf(upt, sizeof upt, "Açık kalma: %s", st->uptime);
    draw_text(s, F_UI, 12, c1.x + 130, c1.y + 50, T.muted, upt);

    /* süreç tablosu */
    int ty = pad + 226;
    draw_text(s, F_UI_BOLD, 15, pad, ty, T.text, "Süreçler");
    char cnt[32];
    snprintf(cnt, sizeof cnt, "%d", st->np);
    ui_badge(s, pad + 80, ty, cnt, T.overlay, T.text);
    Rect kill_r = { W - pad - 130, ty - 6, 130, 32 };
    if (st->sel_pid > 1) {
        if (ui_button(s, u, kill_r, "Sonlandır", BTN_DANGER)) {
            if (kill(st->sel_pid, SIGTERM) == 0) {
                char msg[80];
                snprintf(msg, sizeof msg, "PID %d sonlandırıldı", st->sel_pid);
                wm_notify("Sistem İzleyici", msg, IC_MONITOR);
            }
            st->sel_pid = 0;
        }
    }
    ty += 36;
    Rect hdr = { pad, ty, W - 2 * pad, 28 };
    fill_rrect(s, hdr, 8, T.base);
    int colx[4] = { hdr.x + 12, hdr.x + 90, hdr.x + hdr.w - 260, hdr.x + hdr.w - 130 };
    const char *hn[4] = { "PID", "Ad", "Durum", "Bellek" };
    for (int i = 0; i < 4; i++)
        draw_text(s, F_UI_BOLD, 12, colx[i], ty + 7, T.subtext, hn[i]);
    ty += 32;
    int rh = 28, vis = (H - ty - 10) / rh;
    st->scroll = clampi(st->scroll, 0, maxi(0, st->np - vis));
    for (int i = 0; i < vis && i + st->scroll < st->np; i++) {
        PInfo *p = &st->procs[i + st->scroll];
        Rect r = { pad, ty + i * rh, W - 2 * pad, rh - 2 };
        int sel = p->pid == st->sel_pid;
        int hov = ui_hover(u, r);
        if (sel)
            fill_rrect(s, r, 7, ALPHA(T.accent, 90));
        else if (hov)
            fill_rrect(s, r, 7, T.surface2);
        char pid[16], mem[32];
        snprintf(pid, sizeof pid, "%d", p->pid);
        fmt_size(mem, sizeof mem, (long long)p->rss_kb * 1024);
        const char *stn = p->state == 'R' ? "Çalışıyor" : p->state == 'S' ? "Uyuyor" : p->state == 'Z' ? "Zombi" :
                          p->state == 'T' ? "Durdu" : "Bekliyor";
        draw_text(s, F_MONO, 13, colx[0], r.y + 5, T.subtext, pid);
        draw_text_fit(s, F_UI_BOLD, 13, colx[1], r.y + 5, colx[2] - colx[1] - 10, T.text, p->name);
        draw_text(s, F_UI, 12, colx[2], r.y + 6, p->state == 'R' ? T.green : T.muted, stn);
        draw_text(s, F_UI, 13, colx[3], r.y + 5, T.text, mem);
        if (ui_clicked(u, r))
            st->sel_pid = p->pid;
    }
    ui_scrollbar(s, (Rect){ W - 10, ty, 5, vis * rh }, st->np, vis, st->scroll);
}

static void m_mouse(Win *w, MouseEv *e)
{
    MonSt *st = w->st;
    if (e->kind == M_WHEEL) {
        st->scroll -= e->wheel * 3;
        w->dirty = 1;
        return;
    }
    ui_dispatch(w, &st->ui, e);
}

static void m_tick(Win *w)
{
    MonSt *st = w->st;
    if (++st->ticks >= 4) {
        st->ticks = 0;
        sample(st);
        w->dirty = 1;
    }
}

static void m_close(Win *w) { free(w->st); }

const App APP_MONITOR = {
    .id = "izleyici", .name = "Sistem İzleyici", .desc = "İşlemci, bellek ve süreçler",
    .icon = IC_MONITOR, .w = 720, .h = 560, .single = 1,
    .init = m_init, .draw = m_draw, .mouse = m_mouse, .tick = m_tick, .close = m_close,
};
