/* AxsDE - hesap ve oturum: ilk açılışta hesap oluşturma, giriş ekranı, kilit ekranı
 *
 * AxsOS tek kullanıcılı bir sistemdir: hesap yönetici yetkilidir (Windows'taki ilk hesap gibi).
 * /etc/passwd'de root'tan önce "<ad>:x:0:0:<Ad Soyad>:/root:/bin/bash" satırı olarak durur;
 * böylece kabukta, `whoami`de ve masaüstünde kullanıcının adı görünür, uygulamalar ve paket
 * yöneticisi eskisi gibi çalışır. Parola /etc/shadow'da SHA-512 (crypt $6$) özetidir; root
 * için de aynısı yazılır (su, login, passwd aynı parolayı kullanır).
 *
 * Kipler: KURULUM (hesap yok), GIRIS (açılışta), KILIT (Super+L / güç menüsü).
 */
#define _GNU_SOURCE
#include "axsde.h"

#include <ctype.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { L_YOK, L_KURULUM, L_GIRIS, L_KILIT };

static int mode = L_YOK;
static Surf bg;                    /* bulanık duvar kâğıdı */
static char user[33], fullname[96];
static TextField f_ad, f_kul, f_p1, f_p2, f_pw;
static int focus;                  /* kurulumda odaktaki alan 0..3 */
static char err[160];
static long shake_until;
static int hover = -1;
static int kul_elle;               /* kullanıcı adı elle değiştirildi */
static void (*on_done)(void);
static int last_min = -1;

#define PASSWD "/etc/passwd"
#define SHADOW "/etc/shadow"

/* ---------------------------------------------------------------- */
/* Hesap dosyaları                                                    */
/* ---------------------------------------------------------------- */

/* root dışındaki uid 0 hesabı (bizim hesabımız) */
static int read_account(void)
{
    FILE *f = fopen(PASSWD, "r");
    if (!f)
        return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *a[7] = { 0 };
        int n = 0;
        char *p = line;
        for (; n < 7; n++) {
            a[n] = p;
            char *c = strchr(p, ':');
            if (!c)
                break;
            *c = 0;
            p = c + 1;
        }
        if (n >= 4 && strcmp(a[0], "root") && !strcmp(a[2], "0")) {
            snprintf(user, sizeof user, "%s", a[0]);
            snprintf(fullname, sizeof fullname, "%s", a[4] && *a[4] ? a[4] : a[0]);
            fullname[strcspn(fullname, "\n")] = 0;
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static const char *stored_hash(void)
{
    static char h[200];
    FILE *f = fopen(SHADOW, "r");
    if (!f)
        return NULL;
    char line[512];
    const char *res = NULL;
    while (fgets(line, sizeof line, f)) {
        char *c = strchr(line, ':');
        if (!c)
            continue;
        *c = 0;
        if (!strcmp(line, user)) {
            char *e = strchr(c + 1, ':');
            if (e)
                *e = 0;
            snprintf(h, sizeof h, "%s", c + 1);
            res = h;
            break;
        }
    }
    fclose(f);
    return res;
}

static int check_password(const char *pw)
{
    const char *h = stored_hash();
    if (!h || !h[0])
        return 1;   /* parolasız hesap */
    const char *c = crypt(pw, h);
    return c && !strcmp(c, h);
}

static void make_hash(const char *pw, char *out, size_t n)
{
    static const char abc[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    unsigned char r[16] = { 0 };
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        if (read(fd, r, sizeof r) < 0) { /* yok say */ }
        close(fd);
    }
    char salt[32] = "$6$";
    for (int i = 0; i < 16; i++)
        salt[3 + i] = abc[r[i] % 64];
    salt[19] = '$';
    salt[20] = 0;
    const char *c = crypt(pw, salt);
    snprintf(out, n, "%s", c ? c : "");
}

/* passwd ve shadow'u yeniden yaz: <ad> (uid 0, ilk satır) + root; ikisinde aynı parola */
static int write_account(const char *u, const char *name, const char *pw)
{
    char hash[200];
    make_hash(pw, hash, sizeof hash);
    if (!hash[0])
        return -1;
    /* passwd: eski uid 0 takma adlarını at, yenisini en başa koy */
    FILE *in = fopen(PASSWD, "r"), *out = fopen(PASSWD ".yeni", "w");
    if (!out) {
        if (in)
            fclose(in);
        return -1;
    }
    fprintf(out, "%s:x:0:0:%s:/root:/bin/bash\n", u, name);
    char line[512];
    int has_root = 0;
    while (in && fgets(line, sizeof line, in)) {
        char cp[512];
        snprintf(cp, sizeof cp, "%s", line);
        char *c1 = strchr(cp, ':'), *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (c1 && c2) {
            *c1 = 0;
            int uid0 = !strncmp(c2 + 1, "0:", 2);
            if (uid0 && strcmp(cp, "root"))
                continue;
            if (!strcmp(cp, "root"))
                has_root = 1;
        }
        fputs(line, out);
    }
    if (!has_root)
        fputs("root:x:0:0:root:/root:/bin/bash\n", out);
    if (in)
        fclose(in);
    fclose(out);
    rename(PASSWD ".yeni", PASSWD);
    long days = (long)(time(NULL) / 86400);
    FILE *s = fopen(SHADOW ".yeni", "w");
    if (!s)
        return -1;
    fchmod(fileno(s), 0600);
    fprintf(s, "%s:%s:%ld:0:99999:7:::\nroot:%s:%ld:0:99999:7:::\n", u, hash, days, hash, days);
    fclose(s);
    rename(SHADOW ".yeni", SHADOW);
    sync();
    snprintf(user, sizeof user, "%s", u);
    snprintf(fullname, sizeof fullname, "%s", name);
    return 0;
}

int login_set_password(const char *old, const char *pw)
{
    if (!check_password(old))
        return -1;
    return write_account(user, fullname, pw);
}

int login_set_name(const char *name)
{
    /* parolayı koru: shadow'daki özeti yeniden yazmadan yalnızca passwd'yi değiştir */
    FILE *in = fopen(PASSWD, "r"), *out = fopen(PASSWD ".yeni", "w");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        return -1;
    }
    char line[512];
    size_t ul = strlen(user);
    while (fgets(line, sizeof line, in)) {
        if (!strncmp(line, user, ul) && line[ul] == ':')
            fprintf(out, "%s:x:0:0:%s:/root:/bin/bash\n", user, name);
        else
            fputs(line, out);
    }
    fclose(in);
    fclose(out);
    rename(PASSWD ".yeni", PASSWD);
    snprintf(fullname, sizeof fullname, "%s", name);
    return 0;
}

const char *login_user(void) { return user[0] ? user : "root"; }
const char *login_fullname(void) { return fullname[0] ? fullname : login_user(); }

/* "Ayşe Yılmaz" -> "ayse" */
static void suggest_user(const char *name, char *out, size_t n)
{
    static const struct { const char *tr, *a; } M[] = {
        { "ç", "c" }, { "Ç", "c" }, { "ğ", "g" }, { "Ğ", "g" }, { "ı", "i" }, { "İ", "i" },
        { "ö", "o" }, { "Ö", "o" }, { "ş", "s" }, { "Ş", "s" }, { "ü", "u" }, { "Ü", "u" },
    };
    size_t k = 0;
    for (const char *p = name; *p && k + 1 < n;) {
        if (*p == ' ')
            break;   /* yalnızca ilk ad */
        int done = 0;
        for (size_t i = 0; i < sizeof M / sizeof M[0]; i++) {
            size_t l = strlen(M[i].tr);
            if (!strncmp(p, M[i].tr, l)) {
                out[k++] = M[i].a[0];
                p += l;
                done = 1;
                break;
            }
        }
        if (done)
            continue;
        unsigned char c = (unsigned char)*p++;
        if (isalnum(c))
            out[k++] = (char)tolower(c);
    }
    out[k] = 0;
    if (k && isdigit((unsigned char)out[0]))
        out[0] = 'k';
}

static int valid_user(const char *u)
{
    if (!u[0] || strlen(u) > 32 || !strcmp(u, "root") || !islower((unsigned char)u[0]))
        return 0;
    for (const char *p = u; *p; p++)
        if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p) && *p != '_' && *p != '-')
            return 0;
    return 1;
}

/* ---------------------------------------------------------------- */
/* Yerleşim                                                           */
/* ---------------------------------------------------------------- */

static Rect card(void)
{
    int w = mode == L_KURULUM ? 480 : 380, h = mode == L_KURULUM ? 470 : 300;
    return (Rect){ (SCREEN_W - w) / 2, mode == L_KURULUM ? (SCREEN_H - h) / 2 : SCREEN_H / 2 - 70, w, h };
}

static Rect field(int i)   /* kurulum alanları */
{
    Rect c = card();
    return (Rect){ c.x + 32, c.y + 112 + i * 66, c.w - 64, 40 };
}

static Rect pw_field(void)
{
    Rect c = card();
    return (Rect){ c.x + 40, c.y + 186, c.w - 80 - 48, 42 };
}

static Rect go_btn(void)
{
    if (mode == L_KURULUM) {
        Rect c = card();
        return (Rect){ c.x + 32, c.y + c.h - 64, c.w - 64, 44 };
    }
    Rect f = pw_field();
    return (Rect){ f.x + f.w + 8, f.y, 42, 42 };
}

static Rect live_btn(void)
{
    Rect c = card();
    return (Rect){ c.x + 32, c.y + c.h + 14, c.w - 64, 32 };
}

static Rect power_btn(int i) { return (Rect){ SCREEN_W - 60 - i * 52, SCREEN_H - 64, 44, 44 }; }
static Rect layout_btn(void) { return (Rect){ 24, SCREEN_H - 60, 52, 36 }; }

static int live_mode(void) { return access("/run/axs-kalici", F_OK) != 0; }

/* ---------------------------------------------------------------- */
/* Çizim                                                              */
/* ---------------------------------------------------------------- */

static void make_bg(void)
{
    surf_resize(&bg, SCREEN_W, SCREEN_H);
    wallpaper_render(&bg, wallpaper_idx);
    surf_noclip(&bg);
    /* güçlü bulanıklık: küçük yarıçapı birkaç kez */
    for (int i = 0; i < 3; i++)
        blur_region(&bg, (Rect){ 0, 0, SCREEN_W, SCREEN_H }, 14);
    fill_rect(&bg, (Rect){ 0, 0, SCREEN_W, SCREEN_H }, ALPHA(HEX(0x07070E), 110));
}

static void draw_secret(Surf *s, TextField *t, Rect r, const char *ph)
{
    TextField d = *t;
    int n = 0;
    for (const char *p = t->buf; *p;) {
        utf8_next(&p);
        n++;
    }
    d.buf[0] = 0;
    for (int i = 0; i < n && i < 120; i++)
        strcat(d.buf, "•");
    d.len = (int)strlen(d.buf);
    int cn = 0;
    for (const char *p = t->buf; p < t->buf + t->cur;) {
        utf8_next(&p);
        cn++;
    }
    d.cur = cn * 3;
    tf_draw(s, &d, r, ph);
}

static void draw_avatar(Surf *s, int cx, int cy, int r, const char *name)
{
    fill_circle(s, cx, cy + 3, r, ALPHA(HEX(0x000000), 80));
    fill_circle(s, cx, cy, r, T.accent);
    stroke_circle(s, cx, cy, r, 2, ALPHA(HEX(0xFFFFFF), 70));
    char ini[8] = "";
    const char *p = name;
    if (*p) {
        const char *q = p;
        utf8_next(&q);
        snprintf(ini, sizeof ini, "%.*s", (int)(q - p), p);
        const char *sp = strchr(name, ' ');
        if (sp && sp[1]) {
            const char *a = sp + 1, *b = a;
            utf8_next(&b);
            strncat(ini, a, (size_t)(b - a));
        }
    }
    draw_text_center(s, F_UI_BOLD, r * 4 / 5, (Rect){ cx - r, cy - r, 2 * r, 2 * r }, HEX(0xFFFFFF), ini);
}

static void draw_power(Surf *s, Rect r, int i, int hov)
{
    fill_rrect(s, r, 22, ALPHA(HEX(0xFFFFFF), hov ? 50 : 22));
    draw_icon(s, i == 0 ? IC_POWER : IC_REFRESH, r.x + 12, r.y + 12, 20);
}

static const char *GUNLER[] = { "Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi" };
static const char *AYLAR[] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos",
                               "Eylül", "Ekim", "Kasım", "Aralık" };

void login_draw(Surf *s, Rect clip)
{
    if (mode == L_YOK)
        return;
    blit(s, clip.x, clip.y, &bg, clip);
    time_t t = time(NULL) + tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    long now = now_ms();
    int dx = 0;
    if (now < shake_until)   /* yanlış parola: titret */
        dx = (int)(10 * ((shake_until - now) / 40 % 2 ? 1 : -1) * (shake_until - now) / 400);

    if (mode != L_KURULUM) {
        char hm[16], dt[64];
        snprintf(hm, sizeof hm, "%02d:%02d", tm.tm_hour, tm.tm_min);
        snprintf(dt, sizeof dt, "%d %s %s", tm.tm_mday, AYLAR[tm.tm_mon], GUNLER[tm.tm_wday]);
        draw_text_center(s, F_UI_BOLD, 96, (Rect){ 0, 70, SCREEN_W, 120 }, HEX(0xFFFFFF), hm);
        draw_text_center(s, F_UI, 22, (Rect){ 0, 190, SCREEN_W, 34 }, ALPHA(HEX(0xFFFFFF), 220), dt);

        Rect c = card();
        c.x += dx;
        draw_avatar(s, c.x + c.w / 2, c.y + 60, 52, login_fullname());
        draw_text_center(s, F_UI_BOLD, 22, (Rect){ c.x, c.y + 124, c.w, 32 }, HEX(0xFFFFFF), login_fullname());
        if (mode == L_KILIT)
            draw_text_center(s, F_UI, 13, (Rect){ c.x, c.y + 154, c.w, 22 }, ALPHA(HEX(0xFFFFFF), 170), "Kilitli");
        Rect f = pw_field();
        f.x += dx;
        draw_secret(s, &f_pw, f, "Parola");
        Rect g = go_btn();
        g.x += dx;
        fill_rrect(s, g, 12, hover == 100 ? lighten(T.accent, 25) : T.accent);
        draw_line(s, g.x + 14, g.y + 21, g.x + 28, g.y + 21, 2.5f, HEX(0xFFFFFF));
        draw_line(s, g.x + 22, g.y + 14, g.x + 29, g.y + 21, 2.5f, HEX(0xFFFFFF));
        draw_line(s, g.x + 22, g.y + 28, g.x + 29, g.y + 21, 2.5f, HEX(0xFFFFFF));
        if (err[0])
            draw_text_center(s, F_UI, 13, (Rect){ c.x, f.y + 52, c.w, 22 }, HEX(0xFCA5A5), err);
        else
            draw_text_center(s, F_UI, 12, (Rect){ c.x, f.y + 52, c.w, 22 }, ALPHA(HEX(0xFFFFFF), 130),
                             "Enter ile giriş yap");
    } else {
        Rect c = card();
        draw_shadow(s, c, 22, 40, 14, 160);
        fill_rrect(s, c, 22, ALPHA(HEX(0x161624), 238));
        stroke_rrect(s, c, 22, 1, ALPHA(HEX(0xFFFFFF), 30));
        draw_logo(s, c.x + 52, c.y + 50, 40, T.accent, T.accent2);
        draw_text(s, F_UI_BOLD, 22, c.x + 84, c.y + 26, HEX(0xFFFFFF), "AxsOS'a hoş geldin!");
        draw_text(s, F_UI, 13, c.x + 84, c.y + 56, T.subtext, "Başlamak için hesabını oluştur.");
        static const char *labels[] = { "Adın", "Kullanıcı adı", "Parola", "Parola (tekrar)" };
        static const char *ph[] = { "Ad Soyad", "kullanıcı adı", "en az 4 karakter", "parolayı yeniden yaz" };
        TextField *fs[] = { &f_ad, &f_kul, &f_p1, &f_p2 };
        for (int i = 0; i < 4; i++) {
            Rect r = field(i);
            draw_text(s, F_UI_BOLD, 12, r.x + 2, r.y - 20, T.subtext, labels[i]);
            fs[i]->focus = focus == i;
            if (i >= 2)
                draw_secret(s, fs[i], r, ph[i]);
            else
                tf_draw(s, fs[i], r, ph[i]);
        }
        if (err[0])
            draw_text_center(s, F_UI, 13, (Rect){ c.x, c.y + c.h - 92, c.w, 20 }, HEX(0xFCA5A5), err);
        Rect g = go_btn();
        fill_rrect(s, g, 12, hover == 100 ? lighten(T.accent, 25) : T.accent);
        draw_text_center(s, F_UI_BOLD, 15, g, HEX(0xFFFFFF), "Hesabı oluştur");
        if (live_mode()) {
            Rect l = live_btn();
            draw_text_center(s, F_UI, 13, l, hover == 101 ? HEX(0xFFFFFF) : ALPHA(HEX(0xFFFFFF), 170),
                             "Canlı kip: hesapsız devam et  →");
        }
    }
    /* alt köşeler: klavye düzeni, güç */
    Rect lb = layout_btn();
    fill_rrect(s, lb, 10, ALPHA(HEX(0xFFFFFF), hover == 200 ? 50 : 22));
    draw_text_center(s, F_UI_BOLD, 13, lb, HEX(0xFFFFFF), kb_layout ? "US" : "TR");
    for (int i = 0; i < 2; i++)
        draw_power(s, power_btn(i), i, hover == 300 + i);
}

/* ---------------------------------------------------------------- */
/* Etkileşim                                                          */
/* ---------------------------------------------------------------- */

static void damage(void) { wm_damage_all(); }

/* durum seri konsola da yazılır (otomatik testler hangi ekranda olduğunu buradan anlar) */
static void report(const char *st)
{
    int fd = open("/dev/ttyS0", O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return;
    char b[64];
    int n = snprintf(b, sizeof b, "\r\n[axsde] durum: %s\r\n", st);
    if (write(fd, b, n) < 0) { /* yok say */ }
    close(fd);
}

static void finish(void)
{
    mode = L_YOK;
    report("masaustu");
    surf_free(&bg);
    tf_set(&f_pw, "");
    tf_set(&f_p1, "");
    tf_set(&f_p2, "");
    err[0] = 0;
    damage();
    if (on_done) {
        void (*cb)(void) = on_done;
        on_done = NULL;
        cb();
    }
}

static void submit(void)
{
    if (mode == L_KURULUM) {
        const char *ad = f_ad.buf, *u = f_kul.buf;
        while (*ad == ' ')
            ad++;
        if (!*ad) {
            snprintf(err, sizeof err, "Adını yaz");
            focus = 0;
        } else if (!valid_user(u)) {
            snprintf(err, sizeof err, "Kullanıcı adı küçük harf, rakam, - ve _ olmalı (root olamaz)");
            focus = 1;
        } else if (strlen(f_p1.buf) < 4) {
            snprintf(err, sizeof err, "Parola en az 4 karakter olmalı");
            focus = 2;
        } else if (strcmp(f_p1.buf, f_p2.buf)) {
            snprintf(err, sizeof err, "Parolalar aynı değil");
            tf_set(&f_p2, "");
            focus = 3;
        } else if (strchr(ad, ':')) {
            snprintf(err, sizeof err, "Adda ':' olamaz");
            focus = 0;
        } else if (write_account(u, ad, f_p1.buf) < 0) {
            snprintf(err, sizeof err, "Hesap kaydedilemedi");
        } else {
            finish();
            wm_notify("Hesap oluşturuldu", login_fullname(), IC_ABOUT);
            return;
        }
        damage();
        return;
    }
    if (check_password(f_pw.buf)) {
        finish();
    } else {
        snprintf(err, sizeof err, "Parola yanlış, yeniden dene");
        tf_set(&f_pw, "");
        shake_until = now_ms() + 400;
        damage();
    }
}

int login_active(void) { return mode != L_YOK; }

static void power(int i)
{
    mode = L_YOK;
    surf_free(&bg);
    wm_shutdown(i == 1);
}

void login_mouse(MouseEv *e)
{
    int h = -1;
    if (rect_has(go_btn(), e->x, e->y))
        h = 100;
    else if (mode == L_KURULUM && live_mode() && rect_has(live_btn(), e->x, e->y))
        h = 101;
    else if (rect_has(layout_btn(), e->x, e->y))
        h = 200;
    for (int i = 0; i < 2; i++)
        if (rect_has(power_btn(i), e->x, e->y))
            h = 300 + i;
    if (e->kind == M_MOVE) {
        if (h != hover) {
            hover = h;
            damage();
        }
        return;
    }
    if (e->kind != M_DOWN || e->button != 1)
        return;
    if (h == 100) {
        submit();
    } else if (h == 101) {
        finish();
    } else if (h == 200) {
        kb_layout = !kb_layout;
        config_save();
        damage();
    } else if (h >= 300) {
        power(h - 300);
    } else if (mode == L_KURULUM) {
        for (int i = 0; i < 4; i++)
            if (rect_has(field(i), e->x, e->y)) {
                focus = i;
                damage();
            }
    }
}

void login_key(KeyEv *e)
{
    if (!e->down)
        return;
    if (e->code == KEY_ENTER || e->code == KEY_KPENTER) {
        if (mode == L_KURULUM && focus < 3) {
            focus++;
            damage();
            return;
        }
        submit();
        return;
    }
    if (mode == L_KURULUM) {
        if (e->code == KEY_TAB) {
            focus = (focus + ((e->mods & MOD_SHIFT) ? 3 : 1)) % 4;
            damage();
            return;
        }
        TextField *fs[] = { &f_ad, &f_kul, &f_p1, &f_p2 };
        if (focus >= 2 && (e->mods & MOD_CTRL) && (e->code == KEY_C || e->code == KEY_X))
            return;   /* parolayı panoya kopyalama */
        if (tf_key(fs[focus], e)) {
            if (focus == 0 && !kul_elle) {
                char sug[33];
                suggest_user(f_ad.buf, sug, sizeof sug);
                tf_set(&f_kul, sug);
            }
            if (focus == 1)
                kul_elle = 1;
            err[0] = 0;
            damage();
        }
        return;
    }
    if ((e->mods & MOD_CTRL) && (e->code == KEY_C || e->code == KEY_X))
        return;
    if (tf_key(&f_pw, e)) {
        err[0] = 0;
        damage();
    }
}

void login_tick(void)
{
    if (mode == L_YOK)
        return;
    time_t t = time(NULL);
    int m = (int)(t / 60);
    if (m != last_min) {
        last_min = m;
        damage();
    }
    if (shake_until && now_ms() < shake_until + 100)
        damage();
}

static void enter(int m)
{
    mode = m;
    report(m == L_KURULUM ? "kurulum" : m == L_GIRIS ? "giris" : "kilit");
    make_bg();
    tf_set(&f_pw, "");
    f_pw.focus = 1;
    err[0] = 0;
    hover = -1;
    focus = 0;
    damage();
}

/* açılışta: hesap varsa giriş ekranı, yoksa kurulum; bitince cb (oturum başlar) */
void login_start(void (*cb)(void))
{
    on_done = cb;
    if (read_account()) {
        const char *h = stored_hash();
        if (h && !h[0]) {   /* parolasız hesap: doğrudan oturum */
            finish();
            return;
        }
        enter(L_GIRIS);
    } else {
        tf_set(&f_ad, "");
        tf_set(&f_kul, "");
        kul_elle = 0;
        enter(L_KURULUM);
    }
}

void login_lock(void)
{
    if (mode != L_YOK)
        return;
    if (!read_account()) {
        wm_notify("Kilitlenemedi", "Önce bir hesap oluştur (Ayarlar > Hesap)", IC_ABOUT);
        return;
    }
    on_done = NULL;
    enter(L_KILIT);
}

void login_new_account(void)
{
    if (mode != L_YOK)
        return;
    on_done = NULL;
    tf_set(&f_ad, "");
    tf_set(&f_kul, "");
    kul_elle = 0;
    enter(L_KURULUM);
}

int login_has_account(void) { return read_account(); }
