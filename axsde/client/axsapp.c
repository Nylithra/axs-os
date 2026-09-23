/* libaxsapp - AxsDE istemci kütüphanesi (ADP protokolü) */
#define _GNU_SOURCE
#include "axsapp.h"
#include "../adp.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ui.c'nin yapılandırma kodunun beklediği masaüstü değişkenleri (burada yalnızca okunur) */
int wallpaper_idx;
int tz_offset_min = 180;
int kb_layout;

static int sock = -1;
static Surf surf;
static size_t maplen;
static int focused = 1;
static int dirty = 1, quit_req;
static Rect drect; /* kısmi yeniden çizim bölgesi (dirty == 0 iken) */
static int interval;

static void send_m(AdpMsg *m)
{
    if (sock >= 0 && send(sock, m, sizeof *m, MSG_NOSIGNAL) < 0) { /* sunucu gitti */ }
}

static void send_t(uint32_t type, int a, int b, int c, int d, const char *text)
{
    AdpMsg m = { type, a, b, c, d, 0, 0, "" };
    if (text)
        snprintf(m.text, sizeof m.text, "%s", text);
    send_m(&m);
}

/* Bir ileti al; CONFIGURE ile gelen memfd'yi eşle */
static int recv_msg(AdpMsg *m)
{
    struct iovec iov = { m, sizeof *m };
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr mh = { 0 };
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = cbuf;
    mh.msg_controllen = sizeof cbuf;
    ssize_t n = recvmsg(sock, &mh, MSG_CMSG_CLOEXEC);
    if (n != (ssize_t)sizeof *m)
        return -1;
    if (m->type == ADP_CONFIGURE) {
        int fd = -1;
        for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
            if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
                memcpy(&fd, CMSG_DATA(c), sizeof(int));
        if (fd < 0)
            return 0;
        if (surf.px)
            munmap(surf.px, maplen);
        maplen = (size_t)m->c * m->b * 4;
        void *p = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p == MAP_FAILED)
            return -1;
        surf = (Surf){ p, m->a, m->b, m->c, { 0, 0, m->a, m->b } };
    }
    return 0;
}

int axsapp_open(const char *id, const char *title, int w, int h)
{
    if (font_init() < 0)
        return -1;
    config_load(); /* vurgu rengi ve klavye düzeni masaüstüyle aynı olsun */
    const char *path = getenv("AXSDE_SOCKET");
    sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path ? path : ADP_SOCKET);
    if (sock < 0 || connect(sock, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "%s: AxsDE masaüstüne bağlanılamadı (%s)\n", id, a.sun_path);
        return -1;
    }
    const char *eid = getenv("AXSDE_APP_ID");
    send_t(ADP_HELLO, w, h, 0, 0, eid && *eid ? eid : id);
    if (title)
        send_t(ADP_TITLE, 0, 0, 0, 0, title);
    /* ilk ortak tamponu bekle */
    for (;;) {
        AdpMsg m;
        if (recv_msg(&m) < 0)
            return -1;
        if (m.type == ADP_CONFIGURE && surf.px)
            break;
    }
    return 0;
}

Surf *axsapp_surf(void) { return &surf; }
int axsapp_fd(void) { return sock; }
int axsapp_focused(void) { return focused; }

void axsapp_commit(Rect r) { send_t(ADP_COMMIT, r.x, r.y, r.w, r.h, NULL); }
void axsapp_title(const char *t) { send_t(ADP_TITLE, 0, 0, 0, 0, t); }
void axsapp_request_size(int w, int h) { send_t(ADP_RESIZE, w, h, 0, 0, NULL); }

void axsapp_close(void)
{
    send_t(ADP_QUIT, 0, 0, 0, 0, NULL);
    close(sock);
    sock = -1;
}

int axsapp_next(AxsEvent *ev, int timeout_ms)
{
    memset(ev, 0, sizeof *ev);
    struct pollfd p = { sock, POLLIN, 0 };
    int r = poll(&p, 1, timeout_ms);
    if (r == 0)
        return 0;
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    AdpMsg m;
    if (recv_msg(&m) < 0)
        return -1;
    switch (m.type) {
    case ADP_CONFIGURE:
        ev->type = AE_RESIZE;
        break;
    case ADP_KEY:
        ev->type = AE_KEY;
        ev->key = (KeyEv){ m.a, (uint32_t)m.b, m.c, m.d };
        break;
    case ADP_MOUSE:
        ev->type = AE_MOUSE;
        ev->mouse = (MouseEv){ (MouseKind)m.a, m.b, m.c, m.d, m.e, m.f };
        break;
    case ADP_FOCUS:
        ev->type = AE_FOCUS;
        focused = ev->focused = m.a;
        break;
    case ADP_CLOSE:
        ev->type = AE_CLOSE;
        break;
    default:
        ev->type = AE_NONE;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Kolay döngü                                                         */
/* ------------------------------------------------------------------ */

void axsapp_redraw(void) { dirty = 1; }
void axsapp_redraw_rect(Rect r)
{
    if (rect_empty(r))
        return;
    drect = rect_empty(drect) ? r : rect_union(drect, r);
}
void axsapp_quit(void) { quit_req = 1; }
void axsapp_set_interval(int ms) { interval = ms; }

static long ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int axsapp_run(const char *id, const char *title, int w, int h, const AxsAppFuncs *f)
{
    if (axsapp_open(id, title, w, h) < 0)
        return 1;
    UiState u;
    memset(&u, 0, sizeof u);
    u.cur_hot = -1;
    interval = f->interval_ms;
    long next = ms_now() + interval;
    dirty = 1;
    while (!quit_req) {
        if (dirty) {
            dirty = 0;
            int clicked = u.pressed || u.released || u.wheel;
            ui_begin(&u);
            surf_noclip(&surf);
            f->draw(&surf, &u);
            ui_end(&u);
            axsapp_commit((Rect){ 0, 0, 0, 0 });
            if (clicked)
                dirty = 1; /* tıklamanın sonucu görünsün */
            drect = (Rect){ 0, 0, 0, 0 };
            if (quit_req)
                break;
        } else if (!rect_empty(drect)) {
            /* yalnızca değişen bölgeyi çiz ve gönder */
            Rect r = rect_isect(drect, (Rect){ 0, 0, surf.w, surf.h });
            drect = (Rect){ 0, 0, 0, 0 };
            if (!rect_empty(r)) {
                ui_begin(&u);
                surf_clip(&surf, r);
                f->draw(&surf, &u);
                surf_noclip(&surf);
                ui_end(&u);
                axsapp_commit(r);
            }
        }
        int timeout = -1;
        if (interval > 0) {
            timeout = (int)(next - ms_now());
            if (timeout < 0)
                timeout = 0;
        }
        AxsEvent ev;
        int r = axsapp_next(&ev, timeout);
        if (r < 0)
            break;
        if (interval > 0 && ms_now() >= next) {
            next = ms_now() + interval;
            if (f->timer)
                f->timer();
        }
        if (r == 0)
            continue;
        switch (ev.type) {
        case AE_KEY:
            if (f->key)
                f->key(&ev.key);
            dirty = 1;
            break;
        case AE_MOUSE:
            if (f->mouse)
                f->mouse(&ev.mouse);
            if (ev.mouse.kind == M_MOVE) {
                if (ui_move(&u, &ev.mouse))
                    dirty = 1;
            } else {
                ui_mouse(&u, &ev.mouse);
                dirty = 1;
            }
            break;
        case AE_RESIZE:
            if (f->resized)
                f->resized(surf.w, surf.h);
            dirty = 1;
            break;
        case AE_FOCUS:
            dirty = 1;
            break;
        case AE_CLOSE:
            quit_req = 1;
            break;
        default:
            break;
        }
    }
    axsapp_close();
    return 0;
}
