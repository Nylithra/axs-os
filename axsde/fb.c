/* AxsDE - ekran çıkışı: DRM/KMS (tercih) ya da fbdev (/dev/fb0), konsolu grafik kipine alma
 *
 * DRM "dumb buffer" doğrudan ekran belleğine eşlenir: her kare ek kopya ve sayfa hatası
 * olmadan görünür. fbdev öykünmesi (deferred I/O) her sayfa yazımında sayfa hatası
 * ürettiği için yazılım emülasyonunda (KVM'siz QEMU) çok yavaştır; yalnızca yedektir.
 */
#include "axsde.h"

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int SCREEN_W, SCREEN_H;

static int fbfd = -1;
static uint8_t *fbmem;
static size_t fblen;
static int line_len, bpp;
static int roff, goff, boff;
static int ttyfd = -1;
static int old_kbmode = -1;

/* DRM durumu */
static int drmfd = -1;
static uint32_t drm_fb_id, drm_handle, drm_crtc, drm_conn;
static struct drm_mode_crtc drm_saved;
static int drm_saved_ok;
static struct drm_clip_rect drm_clips[64];
static int drm_nclips;

static void console_graphics(void)
{
    /* Metin konsolunun ekrana yazmasını ve klavyeyi okumasını durdur. */
    ttyfd = isatty(0) ? 0 : open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (ttyfd >= 0) {
        ioctl(ttyfd, KDSETMODE, KD_GRAPHICS);
        if (ioctl(ttyfd, KDGKBMODE, &old_kbmode) == 0)
            ioctl(ttyfd, KDSKBMODE, K_OFF);
    }
}

#define U64P(p) ((__u64)(uintptr_t)(p))

/* fbdev'in şu anki çözünürlüğü (çekirdeğe video= ile verilen) — DRM kipi seçerken tercih edilir */
static void fbdev_size(int *w, int *h)
{
    *w = *h = 0;
    int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return;
    struct fb_var_screeninfo v;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0) {
        *w = (int)v.xres;
        *h = (int)v.yres;
    }
    close(fd);
}

static int drm_open(void)
{
    if (getenv("AXSDE_FBDEV"))
        return -1;
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct drm_mode_card_res res = { 0 };
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || !res.count_connectors || !res.count_crtcs)
        goto fail;
    uint32_t conns[16], crtcs[16], encs[16], fbs[16];
    res.count_connectors = res.count_connectors > 16 ? 16 : res.count_connectors;
    res.count_crtcs = res.count_crtcs > 16 ? 16 : res.count_crtcs;
    res.count_encoders = res.count_encoders > 16 ? 16 : res.count_encoders;
    res.count_fbs = res.count_fbs > 16 ? 16 : res.count_fbs;
    res.connector_id_ptr = U64P(conns);
    res.crtc_id_ptr = U64P(crtcs);
    res.encoder_id_ptr = U64P(encs);
    res.fb_id_ptr = U64P(fbs);
    if (ioctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        goto fail;

    int want_w, want_h;
    fbdev_size(&want_w, &want_h);
    struct drm_mode_modeinfo mode = { 0 };
    uint32_t enc_id = 0;
    int found = 0;
    for (uint32_t c = 0; c < res.count_connectors && !found; c++) {
        struct drm_mode_get_connector gc = { .connector_id = conns[c] };
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc) < 0 || gc.connection != 1 || !gc.count_modes)
            continue;
        struct drm_mode_modeinfo *modes = calloc(gc.count_modes, sizeof *modes);
        uint32_t *props = calloc(gc.count_props + 1, 4), *encids = calloc(gc.count_encoders + 1, 4);
        uint64_t *pvals = calloc(gc.count_props + 1, 8);
        gc.modes_ptr = U64P(modes);
        gc.props_ptr = U64P(props);
        gc.prop_values_ptr = U64P(pvals);
        gc.encoders_ptr = U64P(encids);
        if (ioctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &gc) == 0 && gc.count_modes) {
            int pick = -1;
            for (uint32_t m = 0; m < gc.count_modes; m++)
                if (modes[m].hdisplay == want_w && modes[m].vdisplay == want_h) {
                    pick = (int)m;
                    break;
                }
            for (uint32_t m = 0; pick < 0 && m < gc.count_modes; m++)
                if (modes[m].type & DRM_MODE_TYPE_PREFERRED)
                    pick = (int)m;
            if (pick < 0)
                pick = 0;
            mode = modes[pick];
            enc_id = gc.encoder_id ? gc.encoder_id : (gc.count_encoders ? encids[0] : 0);
            drm_conn = conns[c];
            found = 1;
        }
        free(modes);
        free(props);
        free(pvals);
        free(encids);
    }
    if (!found)
        goto fail;
    drm_crtc = crtcs[0];
    if (enc_id) {
        struct drm_mode_get_encoder ge = { .encoder_id = enc_id };
        if (ioctl(fd, DRM_IOCTL_MODE_GETENCODER, &ge) == 0 && ge.crtc_id)
            drm_crtc = ge.crtc_id;
    }
    struct drm_mode_create_dumb cd = { .width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32 };
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0)
        goto fail;
    drm_handle = cd.handle;
    struct drm_mode_fb_cmd fbc = { .width = cd.width, .height = cd.height, .pitch = cd.pitch,
                                   .bpp = 32, .depth = 24, .handle = cd.handle };
    if (ioctl(fd, DRM_IOCTL_MODE_ADDFB, &fbc) < 0)
        goto fail;
    drm_fb_id = fbc.fb_id;
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0)
        goto fail;
    uint8_t *map = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)md.offset);
    if (map == MAP_FAILED)
        goto fail;
    memset(map, 0, cd.size);
    drm_saved.crtc_id = drm_crtc;
    drm_saved_ok = ioctl(fd, DRM_IOCTL_MODE_GETCRTC, &drm_saved) == 0;
    struct drm_mode_crtc sc = { .crtc_id = drm_crtc, .fb_id = drm_fb_id, .set_connectors_ptr = U64P(&drm_conn),
                                .count_connectors = 1, .mode = mode, .mode_valid = 1 };
    if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &sc) < 0) {
        munmap(map, cd.size);
        goto fail;
    }
    drmfd = fd;
    fbmem = map;
    fblen = cd.size;
    SCREEN_W = mode.hdisplay;
    SCREEN_H = mode.vdisplay;
    bpp = 32;
    line_len = (int)cd.pitch;
    roff = 16;
    goff = 8;
    boff = 0;
    fprintf(stderr, "axsde: DRM %dx%d\n", SCREEN_W, SCREEN_H);
    return 0;
fail:
    close(fd);
    return -1;
}

int fb_open(void)
{
    if (drm_open() == 0) {
        console_graphics();
        return 0;
    }
    fbfd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (fbfd < 0) {
        perror("axsde: /dev/fb0");
        return -1;
    }
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &v) < 0 || ioctl(fbfd, FBIOGET_FSCREENINFO, &f) < 0) {
        perror("axsde: fb ioctl");
        return -1;
    }
    SCREEN_W = (int)v.xres;
    SCREEN_H = (int)v.yres;
    bpp = (int)v.bits_per_pixel;
    line_len = (int)f.line_length;
    roff = (int)v.red.offset;
    goff = (int)v.green.offset;
    boff = (int)v.blue.offset;
    if (bpp != 32 && bpp != 24 && bpp != 16) {
        fprintf(stderr, "axsde: desteklenmeyen renk derinliği: %d bpp\n", bpp);
        return -1;
    }
    fblen = f.smem_len;
    fbmem = mmap(NULL, fblen, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbmem == MAP_FAILED) {
        perror("axsde: mmap");
        return -1;
    }
    console_graphics();
    return 0;
}

void fb_close(void)
{
    if (ttyfd >= 0) {
        if (old_kbmode >= 0)
            ioctl(ttyfd, KDSKBMODE, old_kbmode);
        ioctl(ttyfd, KDSETMODE, KD_TEXT);
        /* ekranı temizle ve imleci göster */
        if (write(ttyfd, "\033[H\033[2J\033[?25h", 13) < 0) { /* yok say */ }
    }
    if (fbmem && fbmem != MAP_FAILED)
        munmap(fbmem, fblen);
    if (fbfd >= 0)
        close(fbfd);
    if (drmfd >= 0) {
        /* önceki görüntüyü (konsol) geri getir */
        if (drm_saved_ok && drm_saved.fb_id) {
            drm_saved.set_connectors_ptr = U64P(&drm_conn);
            drm_saved.count_connectors = 1;
            ioctl(drmfd, DRM_IOCTL_MODE_SETCRTC, &drm_saved);
        }
        ioctl(drmfd, DRM_IOCTL_MODE_RMFB, &drm_fb_id);
        struct drm_mode_destroy_dumb dd = { .handle = drm_handle };
        ioctl(drmfd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        close(drmfd);
    }
}

/* Bu karede değişen bölgeleri sürücüye bildir (gölge tamponlu sürücüler için gerekli) */
void fb_flush(void)
{
    if (drmfd < 0 || !drm_nclips)
        return;
    struct drm_mode_fb_dirty_cmd dc = { .fb_id = drm_fb_id, .num_clips = (uint32_t)drm_nclips,
                                        .clips_ptr = U64P(drm_clips) };
    ioctl(drmfd, DRM_IOCTL_MODE_DIRTYFB, &dc); /* desteklemeyen sürücüde ENOSYS: sorun değil */
    drm_nclips = 0;
}

void fb_present(Surf *back, Rect r)
{
    r = rect_isect(r, (Rect){ 0, 0, SCREEN_W, SCREEN_H });
    if (rect_empty(r) || !fbmem)
        return;
    if (drmfd >= 0 && drm_nclips < 64)
        drm_clips[drm_nclips++] = (struct drm_clip_rect){ (unsigned short)r.x, (unsigned short)r.y,
                                                          (unsigned short)(r.x + r.w), (unsigned short)(r.y + r.h) };
    if (bpp == 32 && roff == 16 && goff == 8 && boff == 0) {
        for (int y = r.y; y < r.y + r.h; y++)
            memcpy(fbmem + (size_t)y * line_len + (size_t)r.x * 4, &back->px[y * back->stride + r.x], (size_t)r.w * 4);
        return;
    }
    for (int y = r.y; y < r.y + r.h; y++) {
        const uint32_t *s = &back->px[y * back->stride + r.x];
        uint8_t *d = fbmem + (size_t)y * line_len + (size_t)r.x * (bpp / 8);
        for (int x = 0; x < r.w; x++) {
            uint32_t p = s[x];
            uint32_t R = (p >> 16) & 255, G = (p >> 8) & 255, B = p & 255;
            if (bpp == 32) {
                ((uint32_t *)d)[x] = (R << roff) | (G << goff) | (B << boff);
            } else if (bpp == 24) {
                uint32_t v = (R << roff) | (G << goff) | (B << boff);
                d[x * 3] = (uint8_t)v;
                d[x * 3 + 1] = (uint8_t)(v >> 8);
                d[x * 3 + 2] = (uint8_t)(v >> 16);
            } else {
                ((uint16_t *)d)[x] = (uint16_t)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
            }
        }
    }
}
