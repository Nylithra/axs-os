/* AxsDE - dış grafik uygulamalar (ADP sunucusu): soket + memfd ortak bellek */
#define _GNU_SOURCE
#include "axsde.h"
#include "adp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_CLIENTS 16

typedef struct {
    int fd;
    Win *win;          /* HELLO sonrası */
    int memfd;
    uint32_t *map;
    size_t maplen;
    int w, h;          /* ortak tamponun boyutu */
    int want_tick;
    int focused;
    int req_w, req_h;  /* HELLO'daki istenen boyut */
    char id[64];
    AppEntry entry;    /* kayıttan kopya: apps_scan APPS dizisini yeniden düzenleyebilir */
} Client;

static int lfd = -1;
static Client clients[MAX_CLIENTS];

int ext_init(void)
{
    unlink(ADP_SOCKET);
    lfd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (lfd < 0)
        return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", ADP_SOCKET);
    if (bind(lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(lfd, 8) < 0) {
        close(lfd);
        lfd = -1;
        return -1;
    }
    chmod(ADP_SOCKET, 0666);
    for (int i = 0; i < MAX_CLIENTS; i++)
        clients[i].fd = -1;
    return 0;
}

int ext_fds(int *fds, int max)
{
    int n = 0;
    if (lfd >= 0 && n < max)
        fds[n++] = lfd;
    for (int i = 0; i < MAX_CLIENTS && n < max; i++)
        if (clients[i].fd >= 0)
            fds[n++] = clients[i].fd;
    return n;
}

static void send_msg(Client *c, AdpMsg *m, int passfd)
{
    struct iovec iov = { m, sizeof *m };
    struct msghdr mh = { 0 };
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    char cbuf[CMSG_SPACE(sizeof(int))];
    if (passfd >= 0) {
        memset(cbuf, 0, sizeof cbuf);
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof cbuf;
        struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &passfd, sizeof(int));
    }
    if (sendmsg(c->fd, &mh, MSG_NOSIGNAL | MSG_DONTWAIT) < 0) { /* istemci yavaşsa olay düşebilir */ }
}

static void send_simple(Client *c, uint32_t type, int a, int b, int cc, int d, int e, int f)
{
    AdpMsg m = { type, a, b, cc, d, e, f, "" };
    send_msg(c, &m, -1);
}

/* Pencere içerik boyutunda yeni ortak tampon oluştur ve istemciye gönder */
static void configure(Client *c, int w, int h)
{
    if (c->map)
        munmap(c->map, c->maplen);
    if (c->memfd >= 0)
        close(c->memfd);
    c->map = NULL;
    c->memfd = memfd_create("axsde-pencere", MFD_CLOEXEC);
    if (c->memfd < 0)
        return;
    c->maplen = (size_t)w * h * 4;
    if (ftruncate(c->memfd, (off_t)c->maplen) < 0)
        return;
    c->map = mmap(NULL, c->maplen, PROT_READ | PROT_WRITE, MAP_SHARED, c->memfd, 0);
    if (c->map == MAP_FAILED) {
        c->map = NULL;
        return;
    }
    c->w = w;
    c->h = h;
    AdpMsg m = { ADP_CONFIGURE, w, h, w, 0, 0, 0, "" };
    send_msg(c, &m, c->memfd);
}

static Client *client_of(Win *w)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd >= 0 && clients[i].win == w)
            return &clients[i];
    return NULL;
}

static void drop(Client *c)
{
    if (c->win) {
        c->win->closing = 1;
        c->win->st = NULL;
        c->win->entry = NULL;
    }
    if (c->map)
        munmap(c->map, c->maplen);
    if (c->memfd >= 0)
        close(c->memfd);
    close(c->fd);
    memset(c, 0, sizeof *c);
    c->fd = -1;
    c->memfd = -1;
}

static void commit(Client *c, Rect r)
{
    Win *w = c->win;
    if (!w || !c->map)
        return;
    Surf src = { c->map, c->w, c->h, c->w, { 0, 0, c->w, c->h } };
    if (rect_empty(r))
        r = (Rect){ 0, 0, c->w, c->h };
    r = rect_isect(r, (Rect){ 0, 0, mini(c->w, w->content.w), mini(c->h, w->content.h) });
    if (rect_empty(r))
        return;
    surf_noclip(&w->content);
    /* ortak bellekteki pikseller opak kabul edilir */
    for (int y = r.y; y < r.y + r.h; y++) {
        uint32_t *d = &w->content.px[y * w->content.stride + r.x];
        const uint32_t *s = &src.px[y * src.stride + r.x];
        for (int x = 0; x < r.w; x++)
            d[x] = s[x] | 0xFF000000u;
    }
    if (!w->minimized) {
        Rect cr = wm_content_rect(w);
        wm_damage((Rect){ cr.x + r.x, cr.y + r.y, r.w, r.h });
    }
}

static void handle(Client *c, AdpMsg *m)
{
    m->text[ADP_TEXT - 1] = 0;
    switch (m->type) {
    case ADP_HELLO: {
        if (c->win)
            break;
        snprintf(c->id, sizeof c->id, "%s", m->text);
        int cw = clampi(m->a, 160, SCREEN_W), ch = clampi(m->b, 100, SCREEN_H - 120);
        Win *w = wm_open_sized(&APP_EXTERNAL, NULL, cw, ch);
        if (!w) {
            drop(c);
            return;
        }
        w->st = c;
        c->win = w;
        const AppEntry *e = apps_find(c->id);
        if (e) {
            c->entry = *e;
            w->entry = &c->entry;
            snprintf(w->app_name, sizeof w->app_name, "%s", e->name);
            wm_set_title(w, e->name);
        }
        w->min_w = 200;
        w->min_h = 120;
        fill_rect(&w->content, (Rect){ 0, 0, w->content.w, w->content.h }, T.surface);
        configure(c, w->content.w, w->content.h);
        break;
    }
    case ADP_TITLE:
        if (c->win)
            wm_set_title(c->win, m->text);
        break;
    case ADP_COMMIT:
        commit(c, (Rect){ m->a, m->b, m->c, m->d });
        break;
    case ADP_RESIZE:
        if (c->win && !c->win->maximized)
            wm_resize_content(c->win, clampi(m->a, 160, SCREEN_W), clampi(m->b, 100, SCREEN_H - 120));
        break;
    case ADP_WANT_TICK:
        c->want_tick = m->a;
        break;
    case ADP_QUIT:
        drop(c);
        break;
    }
}

void ext_io(int fd)
{
    if (fd == lfd) {
        for (;;) {
            int cfd = accept4(lfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
            if (cfd < 0)
                break;
            Client *c = NULL;
            for (int i = 0; i < MAX_CLIENTS; i++)
                if (clients[i].fd < 0) {
                    c = &clients[i];
                    break;
                }
            if (!c) {
                close(cfd);
                continue;
            }
            memset(c, 0, sizeof *c);
            c->fd = cfd;
            c->memfd = -1;
        }
        return;
    }
    Client *c = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd == fd)
            c = &clients[i];
    if (!c)
        return;
    for (int k = 0; k < 64; k++) {
        AdpMsg m;
        ssize_t n = recv(fd, &m, sizeof m, MSG_DONTWAIT);
        if (n == (ssize_t)sizeof m) {
            handle(c, &m);
            if (c->fd < 0)
                return;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        drop(c); /* EOF ya da bozuk ileti */
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Dış uygulama pencereleri için App geri çağrıları                    */
/* ------------------------------------------------------------------ */

static void x_draw(Win *w, Surf *s)
{
    (void)s;
    Client *c = client_of(w);
    if (!c)
        return;
    int f = wm_focused() == w;
    if (f != c->focused) {
        c->focused = f;
        send_simple(c, ADP_FOCUS, f, 0, 0, 0, 0, 0);
    }
    /* içerik ortak bellekten COMMIT ile gelir: burada çizilecek bir şey yok */
    w->dmg = (Rect){ 0, 0, 1, 1 };
}

static void x_key(Win *w, KeyEv *e)
{
    Client *c = client_of(w);
    if (c)
        send_simple(c, ADP_KEY, e->code, (int)e->ch, e->mods, e->down, 0, 0);
}

static void x_mouse(Win *w, MouseEv *e)
{
    Client *c = client_of(w);
    if (c)
        send_simple(c, ADP_MOUSE, e->kind, e->x, e->y, e->button, e->wheel, e->clicks);
}

static void x_tick(Win *w)
{
    Client *c = client_of(w);
    if (c && c->want_tick)
        send_simple(c, ADP_TICK, 0, 0, 0, 0, 0, 0);
}

static void x_resize(Win *w)
{
    Client *c = client_of(w);
    fill_rect(&w->content, (Rect){ 0, 0, w->content.w, w->content.h }, T.surface);
    if (c)
        configure(c, w->content.w, w->content.h);
}

static void x_close(Win *w)
{
    Client *c = client_of(w);
    if (!c)
        return;
    send_simple(c, ADP_CLOSE, 0, 0, 0, 0, 0, 0);
    c->win = NULL;
    drop(c);
}

const App APP_EXTERNAL = {
    .id = "dis", .name = "Uygulama", .desc = "", .icon = IC_EXEC, .w = 480, .h = 360,
    .draw = x_draw, .key = x_key, .mouse = x_mouse, .tick = x_tick, .resize = x_resize, .close = x_close,
};
