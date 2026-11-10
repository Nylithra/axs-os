/* AxsDE - pano (kopyala / yapıştır)
 *
 * Pano metni /run/pano dosyasındadır: AxsDE'nin uygulamaları, dış uygulamalar (xkopru ile
 * tarayıcılar ve Windows programları) ve kabuktaki `pano` komutu aynı panoyu kullanır.
 *
 * Sanal makinede QEMU'nun "qemu-vdagent" aygıtı varsa (virtio-serial bağlantı noktası
 * com.redhat.spice.0) pano ana makineyle eşitlenir: Windows/Linux'ta kopyalanan metin AxsOS'ta
 * yapıştırılır, AxsOS'ta kopyalanan ana makinede. Protokol: SPICE vdagent (VDIChunkHeader +
 * VDAgentMessage; ANNOUNCE_CAPABILITIES, CLIPBOARD_GRAB / REQUEST / CLIPBOARD).
 */
#define _GNU_SOURCE
#include "axsde.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PANO      "/run/pano"
#define PANO_MAX  (4 << 20)

/* vdagent */
enum { VD_PORT_CLIENT = 1 };
enum { VD_CLIPBOARD = 4, VD_ANNOUNCE = 6, VD_GRAB = 7, VD_REQUEST = 8, VD_RELEASE = 9 };
enum { CAP_CLIPBOARD_BY_DEMAND = 5, CAP_GUEST_LINEEND_LF = 8 };
enum { VD_UTF8_TEXT = 1 };
#define VD_CHUNK_MAX 2048

static char *txt;              /* panonun önbelleği */
static struct timespec txt_mt;
static off_t txt_sz = -1;
static int vfd = -1;
static long next_try;
static unsigned char *rbuf;    /* okunan bayt akışı */
static size_t rlen, rcap;
static unsigned char *msg;     /* birleştirilen ileti */
static size_t mlen, mneed, mcap;

/* ---------------------------------------------------------------- */
/* Dosya                                                              */
/* ---------------------------------------------------------------- */

static void load_file(void)
{
    struct stat sb;
    if (stat(PANO, &sb) != 0) {
        if (!txt)
            txt = strdup("");
        return;
    }
    if (txt && sb.st_size == txt_sz && sb.st_mtim.tv_sec == txt_mt.tv_sec && sb.st_mtim.tv_nsec == txt_mt.tv_nsec)
        return;
    size_t n = (size_t)mini((int)sb.st_size, PANO_MAX);
    char *b = malloc(n + 1);
    int fd = open(PANO, O_RDONLY);
    ssize_t r = fd >= 0 ? read(fd, b, n) : -1;
    if (fd >= 0)
        close(fd);
    b[r > 0 ? r : 0] = 0;
    free(txt);
    txt = b;
    txt_sz = sb.st_size;
    txt_mt = sb.st_mtim;
}

static void save_file(const char *t)
{
    char tmp[64];
    snprintf(tmp, sizeof tmp, PANO ".%d", (int)getpid());
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    size_t n = strlen(t), off = 0;
    while (off < n) {
        ssize_t w = write(fd, t + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
    close(fd);
    rename(tmp, PANO);
    struct stat sb;
    if (stat(PANO, &sb) == 0) {
        txt_sz = sb.st_size;
        txt_mt = sb.st_mtim;
    }
}

const char *clip_get(void)
{
    load_file();
    return txt ? txt : "";
}

/* ---------------------------------------------------------------- */
/* vdagent                                                            */
/* ---------------------------------------------------------------- */

/* hata ayıklama: /tmp/pano-debug varsa iletiler /tmp/pano.log'a yazılır */
static void dbg(const char *fmt, uint32_t a, uint32_t b)
{
    if (access("/tmp/pano-debug", F_OK) != 0)
        return;
    FILE *f = fopen("/tmp/pano.log", "a");
    if (f) {
        fprintf(f, fmt, a, b);
        fputc('\n', f);
        fclose(f);
    }
}

static void put32(unsigned char *p, uint32_t v) { memcpy(p, &v, 4); }   /* x86: küçük uçlu */
static uint32_t get32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void vd_close(void)
{
    if (vfd >= 0)
        close(vfd);
    vfd = -1;
    rlen = mlen = mneed = 0;
    next_try = now_ms() + 5000;
}

static void write_all(const unsigned char *p, size_t n)
{
    while (n && vfd >= 0) {
        ssize_t w = write(vfd, p, n);
        if (w < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                usleep(1000);
                continue;
            }
            vd_close();
            return;
        }
        p += w;
        n -= (size_t)w;
    }
}

/* VDAgentMessage'ı 2048 baytlık parçalar halinde gönder */
static void vd_send(uint32_t type, const void *data, size_t n)
{
    dbg("gönder tür=%u boyut=%u", type, (uint32_t)n);
    if (vfd < 0)
        return;
    size_t total = 20 + n;
    unsigned char *m = malloc(total);
    put32(m, 1);                 /* VD_AGENT_PROTOCOL */
    put32(m + 4, type);
    memset(m + 8, 0, 8);         /* opaque */
    put32(m + 16, (uint32_t)n);
    if (n)
        memcpy(m + 20, data, n);
    for (size_t off = 0; off < total && vfd >= 0; off += VD_CHUNK_MAX) {
        size_t part = total - off < VD_CHUNK_MAX ? total - off : VD_CHUNK_MAX;
        unsigned char h[8];
        put32(h, VD_PORT_CLIENT);
        put32(h + 4, (uint32_t)part);
        write_all(h, 8);
        write_all(m + off, part);
    }
    free(m);
}

static void vd_announce(int request)
{
    unsigned char a[8];
    put32(a, (uint32_t)request);
    put32(a + 4, (1u << CAP_CLIPBOARD_BY_DEMAND) | (1u << CAP_GUEST_LINEEND_LF));
    vd_send(VD_ANNOUNCE, a, sizeof a);
}

static void vd_grab(void)
{
    unsigned char t[4];
    put32(t, VD_UTF8_TEXT);
    vd_send(VD_GRAB, t, 4);
}

static void vd_message(uint32_t type, const unsigned char *d, size_t n)
{
    dbg("al tür=%u boyut=%u", type, (uint32_t)n);
    switch (type) {
    case VD_ANNOUNCE:
        if (n >= 4 && get32(d))
            vd_announce(0);
        if (txt && *txt)
            vd_grab();   /* bağlanmadan önce kopyalanan metin de ana makinede yapıştırılabilsin */
        break;
    case VD_GRAB:        /* ana makinede yeni bir şey kopyalandı: metinse iste */
        for (size_t i = 0; i + 4 <= n; i += 4)
            if (get32(d + i) == VD_UTF8_TEXT) {
                unsigned char t[4];
                put32(t, VD_UTF8_TEXT);
                vd_send(VD_REQUEST, t, 4);
                break;
            }
        break;
    case VD_CLIPBOARD:   /* istenen metin geldi */
        if (n >= 4 && get32(d) == VD_UTF8_TEXT) {
            size_t len = n - 4;
            char *s = malloc(len + 1), *o = s;
            for (size_t i = 0; i < len; i++) {   /* CRLF -> LF, NUL'lar atılır */
                char c = (char)d[4 + i];
                if (c == '\r' && i + 1 < len && d[5 + i] == '\n')
                    continue;
                if (c)
                    *o++ = c;
            }
            *o = 0;
            save_file(s);
            free(txt);
            txt = s;
        }
        break;
    case VD_REQUEST: {   /* ana makine bizim panomuzu istiyor */
        const char *t = clip_get();
        size_t len = strlen(t);
        unsigned char *b = malloc(4 + len);
        put32(b, VD_UTF8_TEXT);
        memcpy(b + 4, t, len);
        vd_send(VD_CLIPBOARD, b, 4 + len);
        free(b);
        break;
    }
    default:
        break;
    }
}

static int vd_open(void)
{
    DIR *d = opendir("/sys/class/virtio-ports");
    if (!d)
        return -1;
    struct dirent *e;
    char dev[300] = "";
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char p[300], name[64] = "";
        snprintf(p, sizeof p, "/sys/class/virtio-ports/%s/name", e->d_name);
        if (read_file(p, name, sizeof name) > 0 && !strncmp(name, "com.redhat.spice.0", 18))
            snprintf(dev, sizeof dev, "/dev/%s", e->d_name);
    }
    closedir(d);
    if (!dev[0])
        return -1;
    vfd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (vfd < 0)
        return -1;
    vd_announce(1);
    return 0;
}

int clip_fd(void) { return vfd; }

void clip_io(void)
{
    if (vfd < 0)
        return;
    for (;;) {
        if (rlen + 4096 > rcap) {
            rcap = rcap ? rcap * 2 : 16384;
            rbuf = realloc(rbuf, rcap);
        }
        ssize_t r = read(vfd, rbuf + rlen, rcap - rlen);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            break;
        if (r <= 0) {          /* ana makine tarafı kapandı */
            vd_close();
            return;
        }
        rlen += (size_t)r;
    }
    /* parçaları ayıkla, iletileri birleştir */
    size_t pos = 0;
    while (rlen - pos >= 8) {
        uint32_t size = get32(rbuf + pos + 4);
        if (size > 1 << 20) {  /* bozuk akış */
            vd_close();
            return;
        }
        if (rlen - pos < 8 + size)
            break;
        const unsigned char *p = rbuf + pos + 8;
        size_t left = size;
        while (left) {
            if (!mneed) {      /* yeni ileti: önce 20 baytlık başlık */
                size_t take = 20 - mlen < left ? 20 - mlen : left;
                if (mlen + take > mcap) {
                    mcap = mlen + take + 4096;
                    msg = realloc(msg, mcap);
                }
                memcpy(msg + mlen, p, take);
                mlen += take;
                p += take;
                left -= take;
                if (mlen == 20) {
                    mneed = 20 + get32(msg + 16);
                    if (mneed > PANO_MAX + 64) {
                        vd_close();
                        return;
                    }
                }
            }
            if (mneed) {
                size_t take = mneed - mlen < left ? mneed - mlen : left;
                if (mlen + take > mcap) {
                    mcap = mneed + 64;
                    msg = realloc(msg, mcap);
                }
                memcpy(msg + mlen, p, take);
                mlen += take;
                p += take;
                left -= take;
                if (mlen == mneed) {
                    vd_message(get32(msg + 4), msg + 20, mlen - 20);
                    mlen = mneed = 0;
                    if (vfd < 0)
                        return;
                }
            }
        }
        pos += 8 + size;
    }
    memmove(rbuf, rbuf + pos, rlen - pos);
    rlen -= pos;
}

/* ---------------------------------------------------------------- */

void clip_set(const char *t)
{
    if (!t)
        return;
    save_file(t);
    free(txt);
    txt = strdup(t);
    vd_grab();
}

static void ui_set(const char *t) { clip_set(t); }

void clip_init(void)
{
    ui_clip_get = clip_get;
    ui_clip_set = ui_set;
    load_file();
    vd_open();
    if (vfd < 0)
        next_try = now_ms() + 5000;
}

/* her ~250 ms: dışarıdan (pano komutu, tarayıcı köprüsü) değiştiyse ana makineye bildir */
void clip_tick(void)
{
    struct stat sb;
    if (stat(PANO, &sb) == 0 &&
        (sb.st_size != txt_sz || sb.st_mtim.tv_sec != txt_mt.tv_sec || sb.st_mtim.tv_nsec != txt_mt.tv_nsec)) {
        load_file();
        vd_grab();
    }
    if (vfd < 0 && now_ms() >= next_try) {
        if (vd_open() < 0)
            next_try = now_ms() + 10000;
    }
}
