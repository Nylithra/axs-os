/* AxsDE - dosya işlemleri: yeni dosya/klasör, yeniden adlandırma, çöp kutusu, kopyala/kes/yapıştır
 * Masaüstü ve Dosyalar aynı işlevleri kullanır. Çöp kutusu: ~/.cop (dosyalar) + ~/.cop/.bilgi
 * (her dosyanın asıl yolu; geri yüklemek için). */
#define _GNU_SOURCE
#include "axsde.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int ends_with_ci(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && !strcasecmp(s + a - b, suf);
}

IconId fo_icon(const char *name, int dir, int exec)
{
    if (dir)
        return IC_FOLDER;
    if (ends_with_ci(name, ".axs") || ends_with_ci(name, ".nyl"))
        return IC_AXSFILE;
    if (ends_with_ci(name, ".axp"))
        return IC_PACKAGE;
    if (ends_with_ci(name, ".exe") || ends_with_ci(name, ".msi") || ends_with_ci(name, ".bat"))
        return IC_EXEC;
    if (exec)
        return IC_EXEC;
    return IC_FILE;
}

/* çalıştır ve bekle (cp/mv/rm); 0 = başarılı */
static int run_wait(char *const argv[])
{
    pid_t p = fork();
    if (p == 0) {
        for (int fd = 3; fd < 1024; fd++)
            close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    if (p < 0)
        return -1;
    int st;
    if (waitpid(p, &st, 0) < 0)
        return -1;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* "ad.uz" zaten varsa "ad (2).uz", "ad (3).uz" ... */
int fo_unique(const char *dir, const char *name, char *out, size_t n)
{
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name)
        dot = name + strlen(name);
    int stem = (int)(dot - name);
    snprintf(out, n, "%s/%s", dir, name);
    for (int i = 2; i < 1000 && access(out, F_OK) == 0; i++)
        snprintf(out, n, "%s/%.*s (%d)%s", dir, stem, name, i, dot);
    return access(out, F_OK) == 0 ? -1 : 0;
}

static int valid_name(const char *s)
{
    if (!s[0] || strchr(s, '/') || !strcmp(s, ".") || !strcmp(s, ".."))
        return 0;
    return 1;
}

const char *fo_default_name(int kind)
{
    switch (kind) {
    case FO_DIR: return "Yeni klasör";
    case FO_TEXT: return "Yeni belge.txt";
    case FO_AXS: return "Yeni program.axs";
    default: return "Yeni pencere.axs";
    }
}

static const char TPL_AXS[] =
    "# Yeni Axs programı — F5 ile çalıştır\n"
    "\n"
    "ad = \"dünya\"\n"
    "print: Merhaba ad;!\n";

static const char TPL_GUI[] =
    "# Kolay arayüz: pencereli Axs programı — F5 ile çalıştır\n"
    "# Belgeler: /usr/lib/axs/docs/arayuz.md\n"
    "use arayuz\n"
    "\n"
    "p = arayuz.pencere(\"Merhaba\", 420, 300)\n"
    "p.baslik(\"Merhaba!\")\n"
    "p.yazi(\"Adını yaz ve düğmeye bas:\")\n"
    "ad = p.giris(\"Adın\")\n"
    "sonuc = p.yazi(\"\")\n"
    "\n"
    "func selamla()\n"
    "  sonuc.ayarla(\"Merhaba \" + ad.deger() + \"!\")\n"
    "end\n"
    "\n"
    "p.dugme(\"Selamla\", selamla)\n"
    "p.calistir()\n";

int fo_create(const char *dir, const char *name, int kind, char *out, size_t n)
{
    if (!name || !name[0])
        name = fo_default_name(kind);
    if (!valid_name(name)) {
        errno = EINVAL;
        return -1;
    }
    char nm[300];
    snprintf(nm, sizeof nm, "%s", name);
    /* uzantı yazılmadıysa ekle */
    if (kind == FO_AXS || kind == FO_AXS_GUI) {
        if (!ends_with_ci(nm, ".axs"))
            strncat(nm, ".axs", sizeof nm - strlen(nm) - 1);
    } else if (kind == FO_TEXT && !strchr(nm, '.')) {
        strncat(nm, ".txt", sizeof nm - strlen(nm) - 1);
    }
    if (fo_unique(dir, nm, out, n) < 0) {
        errno = EEXIST;
        return -1;
    }
    if (kind == FO_DIR)
        return mkdir(out, 0755);
    FILE *f = fopen(out, "wx");
    if (!f)
        return -1;
    if (kind == FO_AXS)
        fputs(TPL_AXS, f);
    else if (kind == FO_AXS_GUI)
        fputs(TPL_GUI, f);
    return fclose(f);
}

int fo_rename(const char *path, const char *newname, char *out, size_t n)
{
    if (!valid_name(newname)) {
        errno = EINVAL;
        return -1;
    }
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    char *sl = strrchr(dir, '/');
    if (!sl) {
        errno = EINVAL;
        return -1;
    }
    *sl = 0;
    snprintf(out, n, "%s/%s", dir[0] ? dir : "", newname);
    if (!strcmp(out, path))
        return 0;
    if (access(out, F_OK) == 0) {
        errno = EEXIST;
        return -1;
    }
    return rename(path, out);
}

/* ---------------------------------------------------------------- */
/* Çöp kutusu                                                         */
/* ---------------------------------------------------------------- */

const char *fo_trash_dir(void)
{
    static char p[PATH_MAX];
    if (!p[0])
        snprintf(p, sizeof p, "%s/.cop", getenv("HOME") ? getenv("HOME") : "/root");
    return p;
}

int fo_in_trash(const char *path)
{
    const char *t = fo_trash_dir();
    size_t l = strlen(t);
    return !strncmp(path, t, l) && (path[l] == '/' || !path[l]);
}

static int move_path(const char *src, const char *dst)
{
    if (rename(src, dst) == 0)
        return 0;
    if (errno != EXDEV)
        return -1;
    char *argv[] = { "mv", "-f", (char *)src, (char *)dst, NULL };   /* başka dosya sistemi */
    return run_wait(argv);
}

int fo_trash(const char *path)
{
    if (fo_in_trash(path)) {     /* çöpteki dosya: kalıcı olarak sil */
        char *argv[] = { "rm", "-rf", (char *)path, NULL };
        int r = run_wait(argv);
        const char *base = strrchr(path, '/');
        char info[PATH_MAX + 300];
        snprintf(info, sizeof info, "%s/.bilgi/%s", fo_trash_dir(), base ? base + 1 : path);
        unlink(info);
        return r;
    }
    char info_dir[PATH_MAX + 16];
    snprintf(info_dir, sizeof info_dir, "%s/.bilgi", fo_trash_dir());
    mkdir(fo_trash_dir(), 0700);
    mkdir(info_dir, 0700);
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char dst[PATH_MAX];
    if (fo_unique(fo_trash_dir(), base, dst, sizeof dst) < 0)
        return -1;
    if (move_path(path, dst) < 0)
        return -1;
    const char *nb = strrchr(dst, '/') + 1;
    char info[PATH_MAX + 300];
    snprintf(info, sizeof info, "%s/%s", info_dir, nb);
    FILE *f = fopen(info, "w");
    if (f) {
        fprintf(f, "%s\n", path);
        fclose(f);
    }
    return 0;
}

int fo_restore(const char *tp)
{
    const char *base = strrchr(tp, '/');
    base = base ? base + 1 : tp;
    char info[PATH_MAX + 300], orig[PATH_MAX] = "";
    snprintf(info, sizeof info, "%s/.bilgi/%s", fo_trash_dir(), base);
    FILE *f = fopen(info, "r");
    if (f) {
        if (fgets(orig, sizeof orig, f))
            orig[strcspn(orig, "\n")] = 0;
        fclose(f);
    }
    if (!orig[0])
        snprintf(orig, sizeof orig, "%s/%s", desktop_dir(), base);
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", orig);
    char *sl = strrchr(dir, '/');
    if (sl)
        *sl = 0;
    if (access(dir, F_OK) != 0)
        snprintf(dir, sizeof dir, "%s", desktop_dir());
    const char *ob = strrchr(orig, '/');
    char dst[PATH_MAX];
    if (fo_unique(dir, ob ? ob + 1 : base, dst, sizeof dst) < 0)
        return -1;
    if (move_path(tp, dst) < 0)
        return -1;
    unlink(info);
    return 0;
}

void fo_empty_trash(void)
{
    DIR *d = opendir(fo_trash_dir());
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char p[PATH_MAX + 300];
        snprintf(p, sizeof p, "%s/%s", fo_trash_dir(), e->d_name);
        char *argv[] = { "rm", "-rf", p, NULL };
        run_wait(argv);
    }
    closedir(d);
}

int fo_trash_count(void)
{
    DIR *d = opendir(fo_trash_dir());
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            n++;
    closedir(d);
    return n;
}

/* ---------------------------------------------------------------- */
/* Pano: kopyala / kes / yapıştır                                     */
/* ---------------------------------------------------------------- */

static char clip_path[PATH_MAX];
static int clip_cut;

void fo_clip_set(const char *path, int cut)
{
    snprintf(clip_path, sizeof clip_path, "%s", path);
    clip_cut = cut;
}

int fo_clip_has(void) { return clip_path[0] && access(clip_path, F_OK) == 0; }

int fo_paste(const char *dir, char *out, size_t n)
{
    if (!fo_clip_has()) {
        errno = ENOENT;
        return -1;
    }
    size_t dl = strlen(dir);
    if (!strncmp(dir, clip_path, strlen(clip_path)) && (dl == strlen(clip_path) || dir[strlen(clip_path)] == '/')) {
        errno = EINVAL;   /* klasörü kendi içine */
        return -1;
    }
    const char *base = strrchr(clip_path, '/');
    base = base ? base + 1 : clip_path;
    if (fo_unique(dir, base, out, n) < 0)
        return -1;
    int r;
    if (clip_cut) {
        r = move_path(clip_path, out);
        if (r == 0)
            clip_path[0] = 0;
    } else {
        char *argv[] = { "cp", "-a", clip_path, out, NULL };
        r = run_wait(argv);
    }
    return r;
}

int fo_move_into(const char *path, const char *dir)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    if (!strncmp(dir, path, strlen(path)) && (dir[strlen(path)] == '/' || !dir[strlen(path)])) {
        errno = EINVAL;
        return -1;
    }
    char dst[PATH_MAX];
    if (fo_unique(dir, base, dst, sizeof dst) < 0)
        return -1;
    return move_path(path, dst);
}
