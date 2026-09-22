/*
 * axpkg - AxsOS paket yöneticisi
 *
 * Paket biçimi (.axp): gzip'li tar arşivi
 *     MANIFEST            anahtar=değer satırları:
 *                           name=merhaba
 *                           version=1.0
 *                           description=Axs ile yazılmış selam programı
 *                           depends=diger-paket baska-paket   (isteğe bağlı)
 *     files/...           kök dizine (/) kopyalanacak dosya ağacı
 *     post-install        (isteğe bağlı) kurulumdan sonra çalışır
 *     pre-remove          (isteğe bağlı) kaldırmadan önce çalışır
 *
 * Veritabanı: /var/lib/axpkg/db/<ad>/{MANIFEST,FILES,pre-remove}
 * Depo:       /var/lib/axpkg/repo içindeki .axp dosyaları (ada göre kurulum için)
 *
 * Komutlar:
 *   axpkg install <paket.axp | ad>...   kur (bağımlılıkları depodan çözer)
 *   axpkg remove <ad>...                kaldır
 *   axpkg list                          kurulu paketler
 *   axpkg available                     depodaki paketler
 *   axpkg info <ad>                     paket bilgisi
 *   axpkg files <ad>                    paketin dosyaları
 *   axpkg owner <yol>                   dosya hangi pakete ait
 *   axpkg create <dizin> [çıktı.axp]    dizinden paket oluştur
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define VERSION "1.0"

static const char *root = "";                 /* AXPKG_ROOT: sahte kök (test için) */
static char db_dir[PATH_MAX];
static char repo_dir[PATH_MAX];

#define C_OK   "\033[1;32m"
#define C_ERR  "\033[1;31m"
#define C_INFO "\033[1;36m"
#define C_DIM  "\033[2m"
#define C_OFF  "\033[0m"

static void info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf(C_INFO "==>" C_OFF " ");
    vprintf(fmt, ap);
    putchar('\n');
    va_end(ap);
    fflush(stdout);
}

static int fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, C_ERR "axpkg: hata:" C_OFF " ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Yardımcılar                                                         */
/* ------------------------------------------------------------------ */

static int run(char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "axpkg: %s çalıştırılamadı: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR)
            return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int rm_rf(const char *path)
{
    char *argv[] = { "rm", "-rf", (char *)path, NULL };
    return run(argv);
}

static int copy_file(const char *src, const char *dst, mode_t mode)
{
    int in = open(src, O_RDONLY);
    if (in < 0)
        return -1;
    /* Çalışan bir ikiliyi değiştirebilmek için önce sil, sonra yeniden oluştur. */
    unlink(dst);
    int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (out < 0) {
        close(in);
        return -1;
    }
    char buf[65536];
    ssize_t n;
    int rc = 0;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write(out, buf, n) != n) {
            rc = -1;
            break;
        }
    }
    if (n < 0)
        rc = -1;
    close(in);
    if (close(out) < 0)
        rc = -1;
    chmod(dst, mode & 07777);
    return rc;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int is_valid_name(const char *s)
{
    if (!*s)
        return 0;
    for (; *s; s++)
        if (!(( *s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.'))
            return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Manifest                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[128];
    char version[64];
    char description[512];
    char depends[1024];
} Manifest;

static int manifest_read(const char *path, Manifest *m)
{
    memset(m, 0, sizeof *m);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '#' || !line[0])
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "name"))             snprintf(m->name, sizeof m->name, "%s", v);
        else if (!strcmp(k, "version"))     snprintf(m->version, sizeof m->version, "%s", v);
        else if (!strcmp(k, "description")) snprintf(m->description, sizeof m->description, "%s", v);
        else if (!strcmp(k, "depends"))     snprintf(m->depends, sizeof m->depends, "%s", v);
    }
    fclose(f);
    if (!m->name[0] || !m->version[0])
        return -2;
    return 0;
}

static int is_installed(const char *name)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/%s/MANIFEST", db_dir, name);
    return access(p, F_OK) == 0;
}

/* ------------------------------------------------------------------ */
/* Dosya sahipliği                                                     */
/* ------------------------------------------------------------------ */

/* path'in sahibi olan paketi owner'a yazar (except hariç). Bulursa 1. */
static int find_owner(const char *path, const char *except, char *owner, size_t n)
{
    DIR *d = opendir(db_dir);
    if (!d)
        return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) {
        if (e->d_name[0] == '.' || (except && !strcmp(e->d_name, except)))
            continue;
        char fp[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/%s/FILES", db_dir, e->d_name);
        FILE *f = fopen(fp, "r");
        if (!f)
            continue;
        char line[PATH_MAX];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            if (!strcmp(line, path)) {
                snprintf(owner, n, "%s", e->d_name);
                found = 1;
                break;
            }
        }
        fclose(f);
    }
    closedir(d);
    return found;
}

/* ------------------------------------------------------------------ */
/* Kurulum                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *pkg;
    FILE *list;      /* kurulan dosyaların listesi */
    int conflicts;
    int check_only;
    int count;
} CopyCtx;

/* src ağacını (files/) root+rel altına kopyalar. */
static int copy_tree(const char *src, const char *rel, CopyCtx *cx)
{
    DIR *d = opendir(src);
    if (!d)
        return fail("%s açılamadı: %s", src, strerror(errno));
    struct dirent *e;
    int rc = 0;
    while (!rc && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char s[PATH_MAX], r[PATH_MAX], dst[PATH_MAX];
        snprintf(s, sizeof s, "%s/%s", src, e->d_name);
        snprintf(r, sizeof r, "%s/%s", rel, e->d_name);
        snprintf(dst, sizeof dst, "%s%s", root, r);
        struct stat st;
        if (lstat(s, &st) < 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (!cx->check_only) {
                if (mkdir_p(dst, st.st_mode & 07777) < 0) {
                    rc = fail("%s oluşturulamadı: %s", dst, strerror(errno));
                    break;
                }
            }
            rc = copy_tree(s, r, cx);
            continue;
        }
        if (cx->check_only) {
            char owner[256];
            if (find_owner(r, cx->pkg, owner, sizeof owner)) {
                fprintf(stderr, C_ERR "  çakışma:" C_OFF " %s zaten '%s' paketine ait\n", r, owner);
                cx->conflicts++;
            }
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            char target[PATH_MAX];
            ssize_t n = readlink(s, target, sizeof target - 1);
            if (n < 0)
                continue;
            target[n] = 0;
            unlink(dst);
            if (symlink(target, dst) < 0) {
                rc = fail("%s bağlantısı oluşturulamadı: %s", dst, strerror(errno));
                break;
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copy_file(s, dst, st.st_mode) < 0) {
                rc = fail("%s kopyalanamadı: %s", dst, strerror(errno));
                break;
            }
        } else {
            continue;
        }
        fprintf(cx->list, "%s\n", r);
        cx->count++;
    }
    closedir(d);
    return rc;
}

static int run_hook(const char *script, const char *pkg)
{
    if (access(script, F_OK) != 0)
        return 0;
    chmod(script, 0755);
    info("%s: %s betiği çalıştırılıyor", pkg, strrchr(script, '/') + 1);
    if (root[0]) /* sahte kökte betik çalıştırma */
        return 0;
    char *argv[] = { "/bin/sh", (char *)script, NULL };
    FILE *f = fopen(script, "r");
    char first[256] = "";
    if (f) {
        if (!fgets(first, sizeof first, f))
            first[0] = 0;
        fclose(f);
    }
    /* Betik "#!" ile başlıyorsa doğrudan çalıştır (ör. #!/usr/bin/axs) */
    if (!strncmp(first, "#!", 2))
        argv[0] = (char *)script, argv[1] = NULL;
    int st = run(argv);
    if (st != 0)
        fprintf(stderr, C_ERR "  uyarı:" C_OFF " betik %d koduyla çıktı\n", st);
    return 0;
}

static int install_one(const char *arg, int force, int depth);

/* Depoda ada göre paket dosyası bul (en son sürüm = alfabetik son). */
static int repo_find(const char *name, char *out, size_t n)
{
    DIR *d = opendir(repo_dir);
    if (!d)
        return 0;
    struct dirent *e;
    size_t nl = strlen(name);
    out[0] = 0;
    char best[NAME_MAX + 1] = "";
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 5 || strcmp(e->d_name + l - 4, ".axp"))
            continue;
        /* ad-sürüm.axp ya da ad.axp */
        if (strncmp(e->d_name, name, nl))
            continue;
        char c = e->d_name[nl];
        if (c != '-' && c != '.')
            continue;
        if (c == '-' && !(e->d_name[nl + 1] >= '0' && e->d_name[nl + 1] <= '9'))
            continue;
        if (strcmp(e->d_name, best) > 0)
            snprintf(best, sizeof best, "%s", e->d_name);
    }
    closedir(d);
    if (!best[0])
        return 0;
    snprintf(out, n, "%s/%s", repo_dir, best);
    return 1;
}

static int install_file(const char *file, int force, int depth)
{
    char tmpl[] = "/tmp/axpkg.XXXXXX";
    char *work = mkdtemp(tmpl);
    if (!work)
        return fail("geçici dizin oluşturulamadı: %s", strerror(errno));

    int rc = 1;
    char *tar[] = { "tar", "-xzf", (char *)file, "-C", work, NULL };
    if (run(tar) != 0) {
        fail("%s açılamadı (geçerli bir .axp/tar.gz değil)", file);
        goto out;
    }
    char mpath[PATH_MAX];
    snprintf(mpath, sizeof mpath, "%s/MANIFEST", work);
    Manifest m;
    int mr = manifest_read(mpath, &m);
    if (mr == -1) {
        fail("%s: MANIFEST yok", file);
        goto out;
    }
    if (mr == -2 || !is_valid_name(m.name)) {
        fail("%s: MANIFEST geçersiz (name ve version zorunlu, ad: a-z 0-9 - _ .)", file);
        goto out;
    }

    char dbp[PATH_MAX];
    snprintf(dbp, sizeof dbp, "%s/%s", db_dir, m.name);
    Manifest old;
    int upgrade = 0;
    char oldm[PATH_MAX];
    snprintf(oldm, sizeof oldm, "%s/MANIFEST", dbp);
    if (manifest_read(oldm, &old) == 0) {
        if (!strcmp(old.version, m.version) && !force) {
            info("%s %s zaten kurulu", m.name, m.version);
            rc = 0;
            goto out;
        }
        upgrade = 1;
    }

    /* Bağımlılıklar */
    if (m.depends[0]) {
        char deps[1024];
        snprintf(deps, sizeof deps, "%s", m.depends);
        for (char *t = strtok(deps, " ,"); t; t = strtok(NULL, " ,")) {
            if (is_installed(t))
                continue;
            if (depth > 16) {
                fail("bağımlılık zinciri çok derin (döngü?)");
                goto out;
            }
            info("%s için bağımlılık kuruluyor: %s", m.name, t);
            if (install_one(t, 0, depth + 1) != 0) {
                fail("%s: '%s' bağımlılığı kurulamadı", m.name, t);
                goto out;
            }
        }
    }

    char files[PATH_MAX];
    snprintf(files, sizeof files, "%s/files", work);
    int has_files = access(files, F_OK) == 0;

    /* Çakışma denetimi */
    if (has_files && !force) {
        CopyCtx chk = { .pkg = m.name, .check_only = 1 };
        copy_tree(files, "", &chk);
        if (chk.conflicts) {
            fail("%d dosya çakışması; zorlamak için: axpkg install -f", chk.conflicts);
            goto out;
        }
    }

    info("%s %s %s", upgrade ? "Güncelleniyor:" : "Kuruluyor:", m.name, m.version);
    if (mkdir_p(dbp, 0755) < 0) {
        fail("%s oluşturulamadı: %s", dbp, strerror(errno));
        goto out;
    }
    /* Güncellemede eski dosya listesini sakla, yenide olmayanları sil */
    char flist[PATH_MAX], oldlist[PATH_MAX];
    snprintf(flist, sizeof flist, "%s/FILES", dbp);
    snprintf(oldlist, sizeof oldlist, "%s/FILES.old", dbp);
    if (upgrade)
        rename(flist, oldlist);

    FILE *lf = fopen(flist, "w");
    if (!lf) {
        fail("%s yazılamadı: %s", flist, strerror(errno));
        goto out;
    }
    CopyCtx cx = { .pkg = m.name, .list = lf };
    int crc = has_files ? copy_tree(files, "", &cx) : 0;
    fclose(lf);
    if (crc != 0)
        goto out;

    if (upgrade) {
        /* Eski sürümde olup yenide olmayan dosyaları sil */
        FILE *of = fopen(oldlist, "r");
        FILE *nf = fopen(flist, "r");
        char **cur = NULL;
        size_t nc = 0;
        char line[PATH_MAX];
        while (nf && fgets(line, sizeof line, nf)) {
            line[strcspn(line, "\n")] = 0;
            cur = realloc(cur, sizeof(char *) * (nc + 1));
            cur[nc++] = strdup(line);
        }
        qsort(cur, nc, sizeof(char *), cmp_str);
        while (of && fgets(line, sizeof line, of)) {
            line[strcspn(line, "\n")] = 0;
            char *key = line;
            if (!bsearch(&key, cur, nc, sizeof(char *), cmp_str)) {
                char p[PATH_MAX];
                snprintf(p, sizeof p, "%s%s", root, line);
                unlink(p);
            }
        }
        for (size_t k = 0; k < nc; k++)
            free(cur[k]);
        free(cur);
        if (of)
            fclose(of);
        if (nf)
            fclose(nf);
        unlink(oldlist);
    }

    copy_file(mpath, oldm, 0644);
    char src[PATH_MAX], dst[PATH_MAX];
    snprintf(src, sizeof src, "%s/pre-remove", work);
    snprintf(dst, sizeof dst, "%s/pre-remove", dbp);
    unlink(dst);
    if (access(src, F_OK) == 0)
        copy_file(src, dst, 0755);

    snprintf(src, sizeof src, "%s/post-install", work);
    run_hook(src, m.name);
    printf(C_OK "  ✓" C_OFF " %s %s kuruldu (%d dosya)\n", m.name, m.version, cx.count);
    rc = 0;
out:
    rm_rf(work);
    return rc;
}

static int install_one(const char *arg, int force, int depth)
{
    size_t l = strlen(arg);
    if (strchr(arg, '/') || (l > 4 && !strcmp(arg + l - 4, ".axp")) ||
        (l > 7 && !strcmp(arg + l - 7, ".tar.gz"))) {
        if (access(arg, R_OK) != 0)
            return fail("%s: %s", arg, strerror(errno));
        return install_file(arg, force, depth);
    }
    if (is_installed(arg) && !force && depth > 0)
        return 0;
    char path[PATH_MAX];
    if (!repo_find(arg, path, sizeof path))
        return fail("'%s' paketi depoda yok (%s). Mevcutlar: axpkg available", arg, repo_dir);
    return install_file(path, force, depth);
}

/* ------------------------------------------------------------------ */
/* Kaldırma                                                            */
/* ------------------------------------------------------------------ */

static int cmp_rev(const void *a, const void *b)
{
    return strcmp(*(char *const *)b, *(char *const *)a);
}

static int remove_one(const char *name, int force)
{
    if (!is_valid_name(name) || !is_installed(name))
        return fail("'%s' kurulu değil", name);

    /* Ters bağımlılık denetimi */
    if (!force) {
        DIR *d = opendir(db_dir);
        struct dirent *e;
        int blocked = 0;
        while (d && (e = readdir(d))) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, name))
                continue;
            char mp[PATH_MAX];
            snprintf(mp, sizeof mp, "%s/%s/MANIFEST", db_dir, e->d_name);
            Manifest m;
            if (manifest_read(mp, &m) != 0)
                continue;
            char deps[1024];
            snprintf(deps, sizeof deps, "%s", m.depends);
            for (char *t = strtok(deps, " ,"); t; t = strtok(NULL, " ,"))
                if (!strcmp(t, name)) {
                    fprintf(stderr, C_ERR "  engel:" C_OFF " '%s' bu pakete bağımlı\n", m.name);
                    blocked = 1;
                }
        }
        if (d)
            closedir(d);
        if (blocked)
            return fail("'%s' kaldırılamaz; zorlamak için: axpkg remove -f", name);
    }

    char dbp[PATH_MAX], p[PATH_MAX];
    snprintf(dbp, sizeof dbp, "%s/%s", db_dir, name);
    snprintf(p, sizeof p, "%s/pre-remove", dbp);
    run_hook(p, name);

    info("Kaldırılıyor: %s", name);
    snprintf(p, sizeof p, "%s/FILES", dbp);
    FILE *f = fopen(p, "r");
    int count = 0;
    if (f) {
        char **list = NULL;
        size_t n = 0;
        char line[PATH_MAX];
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            if (!line[0])
                continue;
            list = realloc(list, sizeof(char *) * (n + 1));
            list[n++] = strdup(line);
        }
        fclose(f);
        for (size_t i = 0; i < n; i++) {
            char full[PATH_MAX];
            snprintf(full, sizeof full, "%s%s", root, list[i]);
            if (unlink(full) == 0)
                count++;
        }
        /* Boşalan dizinleri temizle (en derinden başlayarak) */
        char **dirs = NULL;
        size_t nd = 0;
        for (size_t i = 0; i < n; i++) {
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof tmp, "%s", list[i]);
            char *s;
            while ((s = strrchr(tmp, '/')) && s != tmp) {
                *s = 0;
                dirs = realloc(dirs, sizeof(char *) * (nd + 1));
                dirs[nd++] = strdup(tmp);
            }
        }
        qsort(dirs, nd, sizeof(char *), cmp_rev);
        for (size_t i = 0; i < nd; i++) {
            char full[PATH_MAX];
            snprintf(full, sizeof full, "%s%s", root, dirs[i]);
            rmdir(full); /* boş değilse başarısız olur, sorun değil */
            free(dirs[i]);
        }
        free(dirs);
        for (size_t i = 0; i < n; i++)
            free(list[i]);
        free(list);
    }
    rm_rf(dbp);
    printf(C_OK "  ✓" C_OFF " %s kaldırıldı (%d dosya silindi)\n", name, count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Listeleme                                                           */
/* ------------------------------------------------------------------ */

static int cmd_list(void)
{
    DIR *d = opendir(db_dir);
    if (!d) {
        puts("Kurulu paket yok.");
        return 0;
    }
    char **names = NULL;
    size_t n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        names = realloc(names, sizeof(char *) * (n + 1));
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    if (!n) {
        puts("Kurulu paket yok.");
        return 0;
    }
    qsort(names, n, sizeof(char *), cmp_str);
    printf("\033[1m%-20s %-12s %s\033[0m\n", "PAKET", "SÜRÜM", "AÇIKLAMA");
    for (size_t i = 0; i < n; i++) {
        char mp[PATH_MAX];
        snprintf(mp, sizeof mp, "%s/%s/MANIFEST", db_dir, names[i]);
        Manifest m;
        if (manifest_read(mp, &m) == 0)
            printf("%-20s %-10s %s\n", m.name, m.version, m.description);
        free(names[i]);
    }
    free(names);
    return 0;
}

static int cmd_available(void)
{
    DIR *d = opendir(repo_dir);
    if (!d) {
        printf("Depo yok: %s\n", repo_dir);
        return 0;
    }
    char **files = NULL;
    size_t n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l > 4 && !strcmp(e->d_name + l - 4, ".axp")) {
            files = realloc(files, sizeof(char *) * (n + 1));
            files[n++] = strdup(e->d_name);
        }
    }
    closedir(d);
    qsort(files, n, sizeof(char *), cmp_str);
    printf("\033[1m%-3s %-20s %-12s %s\033[0m\n", "", "PAKET", "SÜRÜM", "AÇIKLAMA");
    for (size_t i = 0; i < n; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", repo_dir, files[i]);
        /* Yalnızca MANIFEST'i çıkararak oku */
        char cmd[PATH_MAX * 2];
        snprintf(cmd, sizeof cmd, "tar -xzOf '%s' MANIFEST 2>/dev/null || tar -xzOf '%s' ./MANIFEST", path, path);
        FILE *p = popen(cmd, "r");
        Manifest m = { 0 };
        if (p) {
            char line[2048];
            while (fgets(line, sizeof line, p)) {
                line[strcspn(line, "\n")] = 0;
                char *eq = strchr(line, '=');
                if (!eq)
                    continue;
                *eq = 0;
                if (!strcmp(line, "name"))             snprintf(m.name, sizeof m.name, "%s", eq + 1);
                else if (!strcmp(line, "version"))     snprintf(m.version, sizeof m.version, "%s", eq + 1);
                else if (!strcmp(line, "description")) snprintf(m.description, sizeof m.description, "%s", eq + 1);
            }
            pclose(p);
        }
        if (m.name[0])
            printf("%s %-20s %-10s %s\n", is_installed(m.name) ? C_OK "[k]" C_OFF : "   ", m.name, m.version, m.description);
        free(files[i]);
    }
    free(files);
    printf(C_DIM "[k] = kurulu.  Kurmak için: axpkg install <ad>" C_OFF "\n");
    return 0;
}

static int cmd_info(const char *name)
{
    char mp[PATH_MAX];
    snprintf(mp, sizeof mp, "%s/%s/MANIFEST", db_dir, name);
    Manifest m;
    if (!is_valid_name(name) || manifest_read(mp, &m) != 0)
        return fail("'%s' kurulu değil", name);
    printf("Ad:          %s\nSürüm:       %s\nAçıklama:    %s\nBağımlılık:  %s\n",
           m.name, m.version, m.description, m.depends[0] ? m.depends : "-");
    char fp[PATH_MAX];
    snprintf(fp, sizeof fp, "%s/%s/FILES", db_dir, name);
    FILE *f = fopen(fp, "r");
    int n = 0;
    char line[PATH_MAX];
    while (f && fgets(line, sizeof line, f))
        n++;
    if (f)
        fclose(f);
    printf("Dosya sayısı: %d\n", n);
    return 0;
}

static int cmd_files(const char *name)
{
    char fp[PATH_MAX];
    snprintf(fp, sizeof fp, "%s/%s/FILES", db_dir, name);
    FILE *f = is_valid_name(name) ? fopen(fp, "r") : NULL;
    if (!f)
        return fail("'%s' kurulu değil", name);
    char line[PATH_MAX];
    while (fgets(line, sizeof line, f))
        fputs(line, stdout);
    fclose(f);
    return 0;
}

static int cmd_owner(const char *path)
{
    char owner[256];
    if (find_owner(path, NULL, owner, sizeof owner)) {
        printf("%s -> %s\n", path, owner);
        return 0;
    }
    return fail("%s hiçbir pakete ait değil", path);
}

static int cmd_create(const char *dir, const char *out)
{
    char mp[PATH_MAX];
    snprintf(mp, sizeof mp, "%s/MANIFEST", dir);
    Manifest m;
    int r = manifest_read(mp, &m);
    if (r == -1)
        return fail("%s bulunamadı", mp);
    if (r == -2 || !is_valid_name(m.name))
        return fail("%s geçersiz (name ve version zorunlu)", mp);
    char def[PATH_MAX];
    if (!out) {
        snprintf(def, sizeof def, "%s-%s.axp", m.name, m.version);
        out = def;
    }
    char abs_out[PATH_MAX];
    if (out[0] != '/') {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd))
            return fail("getcwd: %s", strerror(errno));
        snprintf(abs_out, sizeof abs_out, "%s/%s", cwd, out);
    } else {
        snprintf(abs_out, sizeof abs_out, "%s", out);
    }
    char *argv[16];
    int a = 0;
    argv[a++] = "tar";
    argv[a++] = "-czf";
    argv[a++] = abs_out;
    argv[a++] = "-C";
    argv[a++] = (char *)dir;
    argv[a++] = "MANIFEST";
    const char *opt[] = { "files", "post-install", "pre-remove" };
    for (int i = 0; i < 3; i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s/%s", dir, opt[i]);
        if (access(p, F_OK) == 0)
            argv[a++] = (char *)opt[i];
    }
    argv[a] = NULL;
    if (run(argv) != 0)
        return fail("arşiv oluşturulamadı");
    printf(C_OK "  ✓" C_OFF " %s oluşturuldu\n", out);
    return 0;
}

static void usage(void)
{
    printf("\033[1maxpkg %s\033[0m - AxsOS paket yöneticisi\n\n"
           "Kullanım:\n"
           "  axpkg install [-f] <paket.axp | ad>...   paket kur (-f: zorla/yeniden kur)\n"
           "  axpkg remove  [-f] <ad>...               paket kaldır\n"
           "  axpkg list                               kurulu paketler\n"
           "  axpkg available                          depodaki paketler\n"
           "  axpkg info <ad>                          paket bilgisi\n"
           "  axpkg files <ad>                         paketin dosyaları\n"
           "  axpkg owner <yol>                        dosyanın sahibi olan paket\n"
           "  axpkg create <dizin> [çıktı.axp]         dizinden paket oluştur\n\n"
           "Paket (.axp = tar.gz): MANIFEST (name=, version=, description=, depends=)\n"
           "                       files/ (kök dizine kopyalanır), post-install, pre-remove\n"
           "Depo: %s\n", VERSION, repo_dir);
}

int main(int argc, char **argv)
{
    const char *r = getenv("AXPKG_ROOT");
    if (r && *r && strcmp(r, "/"))
        root = r;
    snprintf(db_dir, sizeof db_dir, "%s/var/lib/axpkg/db", root);
    const char *rd = getenv("AXPKG_REPO");
    if (rd && *rd)
        snprintf(repo_dir, sizeof repo_dir, "%s", rd);
    else
        snprintf(repo_dir, sizeof repo_dir, "%s/var/lib/axpkg/repo", root);

    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        usage();
        return argc < 2;
    }
    const char *cmd = argv[1];
    if (!strcmp(cmd, "-v") || !strcmp(cmd, "--version")) {
        printf("axpkg %s\n", VERSION);
        return 0;
    }

    int force = 0, i = 2;
    if (argc > 2 && !strcmp(argv[2], "-f")) {
        force = 1;
        i = 3;
    }

    if (!strcmp(cmd, "list") || !strcmp(cmd, "ls"))
        return cmd_list();
    if (!strcmp(cmd, "available") || !strcmp(cmd, "search"))
        return cmd_available();
    if (!strcmp(cmd, "create")) {
        if (argc < 3)
            return fail("kullanım: axpkg create <dizin> [çıktı.axp]");
        return cmd_create(argv[2], argc > 3 ? argv[3] : NULL);
    }
    if (i >= argc) {
        usage();
        return 1;
    }
    if (!strcmp(cmd, "info"))
        return cmd_info(argv[i]);
    if (!strcmp(cmd, "files"))
        return cmd_files(argv[i]);
    if (!strcmp(cmd, "owner"))
        return cmd_owner(argv[i]);

    int install = !strcmp(cmd, "install") || !strcmp(cmd, "add");
    int remove = !strcmp(cmd, "remove") || !strcmp(cmd, "rm") || !strcmp(cmd, "uninstall");
    if (!install && !remove) {
        fail("bilinmeyen komut: %s", cmd);
        usage();
        return 1;
    }
    if (mkdir_p(db_dir, 0755) < 0)
        return fail("%s oluşturulamadı: %s", db_dir, strerror(errno));
    int rc = 0;
    for (; i < argc; i++)
        rc |= install ? install_one(argv[i], force, 0) : remove_one(argv[i], force);
    return rc;
}
