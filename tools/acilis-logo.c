/* Açılış logosu üretici (host'ta çalışır): AxsDE'nin draw_logo'suyla AxsOS logosunu çizer ve
 * çekirdeğin önyükleme logosu için 224 renkli PPM (P3) yazar. Çekirdek bunu fbcon'da ekranın
 * ortasında gösterir (fbcon=logo-pos:center).
 *   acilis-logo <çıktı.ppm> [boyut]
 *   acilis-logo --png <çıktı.png> <genişlik> <yükseklik>   (GRUB arka planı: gradyan + logo)
 */
#include "axsde.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* gfx.c'nin istediği ama burada kullanılmayan ekran boyutları */
int SCREEN_W = 1280, SCREEN_H = 800;
Theme T;

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "--png")) {
        /* GRUB arka planı: koyu mor gradyan + ortada logo; P6 PPM olarak yazılır (dönüştürmek için) */
        int w = atoi(argv[3]), h = atoi(argv[4]);
        Surf s = surf_new(w, h);
        surf_noclip(&s);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                float fx = (float)x / w, fy = (float)y / h;
                int r = (int)(14 + 30 * (1 - fy) * (1 - fx)), g = (int)(10 + 8 * (1 - fy)), b = (int)(26 + 40 * fy * fx + 14);
                s.px[y * s.stride + x] = RGB(r, g, b);
            }
        draw_logo(&s, w / 2.0f, h / 2.0f - 20, h / 5.0f, HEX(0x8B5CF6), HEX(0xC084FC));
        FILE *f = fopen(argv[2], "wb");
        if (!f)
            return 1;
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) {
            uint32_t p = s.px[i];
            unsigned char c[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
            fwrite(c, 1, 3, f);
        }
        fclose(f);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "--ham")) {
        /* init'in açılış ekranı için: koyu zemin (#0B0B14) üzerinde logo, P6 */
        int sz = atoi(argv[3]);
        Surf s = surf_new(sz, sz);
        surf_noclip(&s);
        fill_rect(&s, (Rect){ 0, 0, sz, sz }, HEX(0x0B0B14));
        draw_logo(&s, sz / 2.0f, sz / 2.0f, sz * 0.8f, HEX(0x8B5CF6), HEX(0xC084FC));
        FILE *f = fopen(argv[2], "wb");
        if (!f)
            return 1;
        fprintf(f, "P6\n%d %d\n255\n", sz, sz);
        for (int i = 0; i < sz * sz; i++) {
            uint32_t p = s.px[i];
            unsigned char c[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p };
            fwrite(c, 1, 3, f);
        }
        fclose(f);
        return 0;
    }
    if (argc < 2)
        return 2;
    int sz = argc > 2 ? atoi(argv[2]) : 160;
    Surf s = surf_new(sz, sz);
    surf_noclip(&s);
    fill_rect(&s, (Rect){ 0, 0, sz, sz }, HEX(0x000000));
    draw_logo(&s, sz / 2.0f, sz / 2.0f, sz * 0.86f, HEX(0x8B5CF6), HEX(0xC084FC));
    FILE *f = fopen(argv[1], "w");
    if (!f)
        return 1;
    fprintf(f, "P3\n%d %d\n255\n", sz, sz);
    /* çekirdek en fazla 224 renk kabul eder: renkleri kademeli olarak kabalaştır */
    int shift = 0;
    for (; shift < 8; shift++) {
        static uint32_t seen[4096];
        int n = 0, ok = 1;
        for (int i = 0; i < sz * sz && ok; i++) {
            uint32_t p = s.px[i] & 0xFFFFFF, m = (0xFF >> shift) << shift, k = p & (m << 16 | m << 8 | m);
            int found = 0;
            for (int j = 0; j < n; j++)
                if (seen[j] == k) {
                    found = 1;
                    break;
                }
            if (!found) {
                if (n >= 224)
                    ok = 0;
                else
                    seen[n++] = k;
            }
        }
        if (ok)
            break;
    }
    uint32_t m = (0xFF >> shift) << shift;
    for (int y = 0; y < sz; y++) {
        for (int x = 0; x < sz; x++) {
            uint32_t p = s.px[y * s.stride + x];
            fprintf(f, "%u %u %u ", (p >> 16) & m, (p >> 8) & m, p & m);
        }
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}
