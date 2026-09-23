/* AxsDE - framebuffer (/dev/fb0) ve konsolu grafik kipine alma */
#include "axsde.h"

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

int fb_open(void)
{
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

    /* Metin konsolunun ekrana yazmasını ve klavyeyi okumasını durdur. */
    ttyfd = isatty(0) ? 0 : open("/dev/tty1", O_RDWR | O_CLOEXEC);
    if (ttyfd >= 0) {
        ioctl(ttyfd, KDSETMODE, KD_GRAPHICS);
        if (ioctl(ttyfd, KDGKBMODE, &old_kbmode) == 0)
            ioctl(ttyfd, KDSKBMODE, K_OFF);
    }
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
}

void fb_present(Surf *back, Rect r)
{
    r = rect_isect(r, (Rect){ 0, 0, SCREEN_W, SCREEN_H });
    if (rect_empty(r) || !fbmem)
        return;
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
