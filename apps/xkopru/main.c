/* xkopru - X uygulamalarını (Firefox, Chrome) AxsDE penceresinde gösteren köprü.
 *
 *   xkopru --display N --fb /tmp/xvfbN/Xvfb_screen0 --title "Firefox" [--pid P]
 *
 * Xvfb, ekran belleğini "-fbdir" ile bir XWD dosyası olarak paylaşır; xkopru bu dosyayı
 * eşler, değişen satırları AxsDE penceresine kopyalar. Klavye ve fare olayları X sunucusuna
 * XTEST uzantısıyla (ham X11 protokolü, Xlib'siz) iletilir. Pencere kapatılınca --pid ile
 * verilen tarayıcı sürecine SIGTERM gönderilir; X sunucusu kapanınca xkopru da çıkar.
 */
#include "axsapp.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int xfd = -1;
static uint32_t xroot;
static int xtest_op;
static const uint8_t *fb;
static size_t fb_len;
static int fb_w, fb_h, fb_stride, fb_off;
static pid_t child = -1;
static uint8_t keys_down[256];
static int buttons_down[8];

static int xwrite(const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t k = write(xfd, p, n);
        if (k < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                struct pollfd pf = { xfd, POLLOUT, 0 };
                poll(&pf, 1, 100);
                continue;
            }
            return -1;
        }
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static int xread_full(void *buf, size_t n, int timeout_ms)
{
    uint8_t *p = buf;
    long end = now_ms() + timeout_ms;
    while (n) {
        struct pollfd pf = { xfd, POLLIN, 0 };
        int left = (int)(end - now_ms());
        if (left <= 0 || poll(&pf, 1, left) <= 0)
            return -1;
        ssize_t k = read(xfd, p, n);
        if (k <= 0) {
            if (k < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            return -1;
        }
        p += k;
        n -= (size_t)k;
    }
    return 0;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

/* X sunucusuna bağlan, kök pencereyi ve XTEST işlem kodunu öğren */
static int x_connect(int display)
{
    xfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "/tmp/.X11-unix/X%d", display);
    if (connect(xfd, (struct sockaddr *)&a, sizeof a) < 0)
        return -1;
    uint8_t req[12] = { 'l', 0, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    if (xwrite(req, sizeof req) < 0)
        return -1;
    uint8_t hdr[8];
    if (xread_full(hdr, 8, 5000) < 0 || hdr[0] != 1) {
        fprintf(stderr, "xkopru: X bağlantısı reddedildi\n");
        return -1;
    }
    size_t len = rd16(hdr + 6) * 4u;
    uint8_t *d = malloc(len);
    if (!d || xread_full(d, len, 5000) < 0)
        return -1;
    uint16_t vlen = rd16(d + 16);
    uint8_t nformats = d[21];
    size_t off = 32 + ((vlen + 3u) & ~3u) + nformats * 8u;
    if (off + 4 > len)
        return -1;
    xroot = rd32(d + off);
    free(d);

    /* QueryExtension "XTEST" */
    const char *name = "XTEST";
    uint8_t q[16] = { 98, 0, 4, 0, 5, 0, 0, 0 };
    memcpy(q + 8, name, 5);
    if (xwrite(q, sizeof q) < 0)
        return -1;
    uint8_t rep[32];
    if (xread_full(rep, 32, 5000) < 0 || rep[0] != 1 || !rep[8]) {
        fprintf(stderr, "xkopru: X sunucusunda XTEST yok\n");
        return -1;
    }
    xtest_op = rep[9];
    fcntl(xfd, F_SETFL, O_NONBLOCK);
    return 0;
}

/* XTestFakeInput: type 2/3 tuş, 4/5 düğme, 6 hareket */
static void fake(int type, int detail, int x, int y)
{
    uint8_t r[36];
    memset(r, 0, sizeof r);
    r[0] = (uint8_t)xtest_op;
    r[1] = 2; /* X_XTestFakeInput */
    r[2] = 9; /* uzunluk: 36 bayt / 4 */
    r[4] = (uint8_t)type;
    r[5] = (uint8_t)detail;
    uint32_t root = type == 6 ? xroot : 0;
    memcpy(r + 12, &root, 4);
    int16_t rx = (int16_t)x, ry = (int16_t)y;
    memcpy(r + 24, &rx, 2);
    memcpy(r + 26, &ry, 2);
    xwrite(r, sizeof r);
}

/* Sunucudan gelen yanıt/hata/olayları boşalt; bağlantı kapandıysa -1 */
static int x_drain(void)
{
    uint8_t buf[4096];
    for (;;) {
        ssize_t k = read(xfd, buf, sizeof buf);
        if (k > 0)
            continue;
        if (k == 0)
            return -1;
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    }
}

/* Xvfb -fbdir dosyası: XWD başlığı + renk tablosu + pikseller */
static int fb_map(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size < 100) {
        close(fd);
        return -1;
    }
    fb_len = (size_t)st.st_size;
    fb = mmap(NULL, fb_len, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (fb == MAP_FAILED)
        return -1;
    /* XWD başlığı büyük sonlu 32 bitlik alanlardan oluşur */
#define H(i) ((uint32_t)fb[(i) * 4] << 24 | (uint32_t)fb[(i) * 4 + 1] << 16 | (uint32_t)fb[(i) * 4 + 2] << 8 | fb[(i) * 4 + 3])
    uint32_t header_size = H(0), width = H(4), height = H(5), bpp = H(11), bpl = H(12), ncolors = H(19);
#undef H
    if (bpp != 32 || !width || !height) {
        fprintf(stderr, "xkopru: desteklenmeyen ekran biçimi (%u bpp)\n", bpp);
        return -1;
    }
    fb_w = (int)width;
    fb_h = (int)height;
    fb_stride = (int)bpl;
    fb_off = (int)(header_size + ncolors * 12);
    if ((size_t)fb_off + (size_t)fb_stride * fb_h > fb_len)
        return -1;
    return 0;
}

/* Değişen satırları pencereye kopyala ve yalnızca o bölgeyi gönder */
static void sync_frame(int force)
{
    Surf *s = axsapp_surf();
    int w = mini(s->w, fb_w), h = mini(s->h, fb_h);
    int y0 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        const uint32_t *src = (const uint32_t *)(fb + fb_off + (size_t)y * fb_stride);
        uint32_t *dst = &s->px[y * s->stride];
        if (!force && !memcmp(src, dst, (size_t)w * 4))
            continue;
        memcpy(dst, src, (size_t)w * 4);
        if (y0 < 0)
            y0 = y;
        y1 = y;
    }
    if (y0 >= 0)
        axsapp_commit((Rect){ 0, y0, w, y1 - y0 + 1 });
}

static void release_all(void)
{
    for (int k = 0; k < 256; k++)
        if (keys_down[k]) {
            fake(3, k, 0, 0);
            keys_down[k] = 0;
        }
    for (int b = 1; b < 8; b++)
        if (buttons_down[b]) {
            fake(5, b, 0, 0);
            buttons_down[b] = 0;
        }
}

static void on_key(KeyEv *e)
{
    int kc = e->code + 8; /* evdev -> X anahtar kodu */
    if (kc < 8 || kc > 255)
        return;
    if (e->down) {
        fake(2, kc, 0, 0); /* basılı tutulunca tekrar eden basışlar X'te de tekrar eder */
        keys_down[kc] = 1;
    } else if (keys_down[kc]) {
        fake(3, kc, 0, 0);
        keys_down[kc] = 0;
    }
}

static void on_mouse(MouseEv *e)
{
    int x = clampi(e->x, 0, fb_w - 1), y = clampi(e->y, 0, fb_h - 1);
    if (e->kind == M_WHEEL) {
        int b = e->wheel > 0 ? 4 : 5;
        fake(6, 0, x, y);
        fake(4, b, 0, 0);
        fake(5, b, 0, 0);
        return;
    }
    fake(6, 0, x, y);
    if (e->kind == M_DOWN || e->kind == M_UP) {
        int b = e->button == 2 ? 3 : e->button == 3 ? 2 : 1;
        fake(e->kind == M_DOWN ? 4 : 5, b, 0, 0);
        buttons_down[b] = e->kind == M_DOWN;
    }
}

int main(int argc, char **argv)
{
    int display = 1;
    const char *fbpath = NULL, *title = "Tarayıcı";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--display")) display = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--fb")) fbpath = argv[i + 1];
        else if (!strcmp(argv[i], "--title")) title = argv[i + 1];
        else if (!strcmp(argv[i], "--pid")) child = (pid_t)atoi(argv[i + 1]);
    }
    char defpath[64];
    if (!fbpath) {
        snprintf(defpath, sizeof defpath, "/tmp/xvfb%d/Xvfb_screen0", display);
        fbpath = defpath;
    }
    /* X sunucusu ve ekran dosyası hazır olana kadar bekle (en fazla 30 sn) */
    long end = now_ms() + 30000;
    while (fb_map(fbpath) < 0 || x_connect(display) < 0) {
        if (fb && fb != MAP_FAILED)
            munmap((void *)fb, fb_len), fb = NULL;
        if (xfd >= 0)
            close(xfd), xfd = -1;
        if (now_ms() > end) {
            fprintf(stderr, "xkopru: X ekranı :%d açılamadı\n", display);
            return 1;
        }
        usleep(200000);
    }
    if (axsapp_open(getenv("AXSDE_APP_ID") ? getenv("AXSDE_APP_ID") : "xkopru", title, fb_w, fb_h) < 0)
        return 1;
    sync_frame(1);
    long next = now_ms();
    int quit = 0;
    while (!quit) {
        long wait = next - now_ms();
        struct pollfd pf[2] = { { axsapp_fd(), POLLIN, 0 }, { xfd, POLLIN, 0 } };
        int r = poll(pf, 2, wait > 0 ? (int)wait : 0);
        if (r < 0 && errno != EINTR)
            break;
        if (pf[1].revents & (POLLIN | POLLHUP | POLLERR))
            if (x_drain() < 0)
                break; /* tarayıcı kapandı, X sunucusu gitti */
        if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            AxsEvent ev;
            while (!quit) {
                int k = axsapp_next(&ev, 0);
                if (k < 0) {
                    quit = 1;
                    break;
                }
                if (k == 0)
                    break;
                switch (ev.type) {
                case AE_KEY: on_key(&ev.key); break;
                case AE_MOUSE: on_mouse(&ev.mouse); break;
                case AE_FOCUS: if (!ev.focused) release_all(); break;
                case AE_RESIZE: sync_frame(1); break;
                case AE_CLOSE: quit = 1; break;
                default: break;
                }
            }
        }
        if (now_ms() >= next) {
            sync_frame(0);
            next = now_ms() + 40; /* ~25 kare/sn; yalnızca değişen satırlar gönderilir */
        }
    }
    if (child > 0)
        kill(child, SIGTERM);
    axsapp_close();
    return 0;
}
