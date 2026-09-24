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
 * Yerel depo: /var/lib/axpkg/repo içindeki .axp dosyaları + INDEX
 * Uzak depo:  /etc/axpkg/depolar dosyasındaki adresler (satır başına bir URL). "axpkg update"
 *             her deponun INDEX'ini /var/lib/axpkg/remote/ altına indirir; yerelde olmayan
 *             paket kurulurken .axp curl ile indirilir ve sha256 özeti doğrulanır.
 *
 * Komutlar:
 *   axpkg update                        uzak depo dizinlerini indir
 *   axpkg upgrade                       kurulu paketlerin yeni sürümlerini kur
 *   axpkg install <paket.axp | ad>...   kur (bağımlılıkları yerel/uzak depodan çözer)
 *   axpkg remove <ad>...                kaldır
 *   axpkg list                          kurulu paketler
 *   axpkg available                     depodaki paketler
 *   axpkg info <ad>                     paket bilgisi
 *   axpkg files <ad>                    paketin dosyaları
 *   axpkg owner <yol>                   dosya hangi pakete ait
 *   axpkg create <dizin> [çıktı.axp]    dizinden paket oluştur
 */
#define _GNU_SOURCE
#include <ctype.h>
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
static char remote_dir[PATH_MAX];   /* indirilen uzak INDEX'ler */
static char cache_dir[PATH_MAX];    /* indirilen .axp'ler (kurulunca silinir) */
static char repos_conf[PATH_MAX];

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

/* Çakışma denetimi için tüm kurulu dosyaların sıralı dizini ("yol\tpaket") */
typedef struct { char **v; size_t n; } OwnerIdx;

static int cmp_path_prefix(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    for (;; x++, y++) {
        char cx = *x == '\t' ? 0 : *x, cy = *y == '\t' ? 0 : *y;
        if (cx != cy)
            return (unsigned char)cx - (unsigned char)cy;
        if (!cx)
            return 0;
    }
}

static void owner_idx_load(OwnerIdx *ix, const char *except)
{
    ix->v = NULL;
    ix->n = 0;
    size_t cap = 0;
    DIR *d = opendir(db_dir);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || (except && !strcmp(e->d_name, except)))
            continue;
        char fp[PATH_MAX];
        snprintf(fp, sizeof fp, "%s/%s/FILES", db_dir, e->d_name);
        FILE *f = fopen(fp, "r");
        char line[PATH_MAX];
        while (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\n")] = 0;
            if (ix->n == cap) {
                cap = cap ? cap * 2 : 1024;
                ix->v = realloc(ix->v, cap * sizeof(char *));
            }
            char *ent = malloc(strlen(line) + strlen(e->d_name) + 2);
            sprintf(ent, "%s\t%s", line, e->d_name);
            ix->v[ix->n++] = ent;
        }
        if (f)
            fclose(f);
    }
    if (d)
        closedir(d);
    if (ix->n)
        qsort(ix->v, ix->n, sizeof(char *), cmp_path_prefix);
}

static const char *owner_idx_find(OwnerIdx *ix, const char *path)
{
    const char *key = path;
    char **hit = ix->n ? bsearch(&key, ix->v, ix->n, sizeof(char *), cmp_path_prefix) : NULL;
    return hit ? strchr(*hit, '\t') + 1 : NULL;
}

static void owner_idx_free(OwnerIdx *ix)
{
    for (size_t i = 0; i < ix->n; i++)
        free(ix->v[i]);
    free(ix->v);
}

/* ------------------------------------------------------------------ */
/* Kurulum                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *pkg;
    OwnerIdx *owners; /* çakışma denetimi */
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
            const char *owner = owner_idx_find(cx->owners, r);
            if (owner) {
                if (cx->conflicts < 20)
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
            unlink(dst);
            if (rename(s, dst) == 0) {
                chmod(dst, st.st_mode & 07777);
            } else if (copy_file(s, dst, st.st_mode) < 0) {
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
    return st;
}

static int install_one(const char *arg, int force, int depth);
static int remove_one(const char *name, int force);

/* ------------------------------------------------------------------ */
/* Depo dizinleri (INDEX): yerel + uzak                                */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[128], version[64], description[512], depends[1024];
    char file[256], sha256[80], provides[1024], url[512], icon[256];
    char base[512];   /* uzak deponun adresi (yerelse boş) */
    long size;
    int local;
} IndexEnt;

/* Bir INDEX dosyasındaki kayıtları cb ile gezer; cb 1 dönerse durur. */
static int index_walk(const char *path, const char *base, int local,
                      int (*cb)(IndexEnt *, void *), void *ud)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    IndexEnt e;
    memset(&e, 0, sizeof e);
    char line[4096];
    int stop = 0;
    for (;;) {
        char *r = fgets(line, sizeof line, f);
        if (r)
            line[strcspn(line, "\r\n")] = 0;
        if (!r || !line[0]) {
            if (e.name[0]) {
                snprintf(e.base, sizeof e.base, "%s", base ? base : "");
                e.local = local;
                if (cb(&e, ud)) {
                    stop = 1;
                    break;
                }
            }
            memset(&e, 0, sizeof e);
            if (!r)
                break;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
#define S(key, fld) else if (!strcmp(k, key)) snprintf(e.fld, sizeof e.fld, "%s", v)
        if (0) {}
        S("name", name); S("version", version); S("description", description); S("depends", depends);
        S("file", file); S("sha256", sha256); S("provides", provides); S("url", url);
        S("icon", icon);
        else if (!strcmp(k, "size")) e.size = atol(v);
#undef S
    }
    fclose(f);
    return stop;
}

/* Tüm dizinleri gez: önce yerel depo INDEX'i, sonra /var/lib/axpkg/remote/<n>.INDEX (+ <n>.URL) */
static int index_all(int (*cb)(IndexEnt *, void *), void *ud)
{
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/INDEX", repo_dir);
    if (index_walk(p, NULL, 1, cb, ud))
        return 1;
    for (int i = 0; i < 16; i++) {
        char ip[PATH_MAX], up[PATH_MAX], base[512] = "";
        snprintf(ip, sizeof ip, "%s/%d.INDEX", remote_dir, i);
        snprintf(up, sizeof up, "%s/%d.URL", remote_dir, i);
        if (access(ip, R_OK) != 0)
            continue;
        FILE *uf = fopen(up, "r");
        if (uf) {
            if (!fgets(base, sizeof base, uf))
                base[0] = 0;
            base[strcspn(base, "\r\n")] = 0;
            fclose(uf);
        }
        if (index_walk(ip, base, 0, cb, ud))
            return 1;
    }
    return 0;
}

/* Sürüm karşılaştırma: sayısal parçalara göre (1.10 > 1.9) */
static int vercmp(const char *a, const char *b)
{
    while (*a || *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
            if (x != y)
                return x < y ? -1 : 1;
        } else {
            if (*a != *b)
                return (unsigned char)*a - (unsigned char)*b;
            a++, b++;
        }
    }
    return 0;
}

typedef struct { const char *name; IndexEnt best; int found; } FindCtx;

static int find_cb(IndexEnt *e, void *ud)
{
    FindCtx *c = ud;
    if (strcmp(e->name, c->name))
        return 0;
    /* en yeni sürüm; eşitse yerel olan */
    if (!c->found || vercmp(e->version, c->best.version) > 0 ||
        (!vercmp(e->version, c->best.version) && e->local && !c->best.local)) {
        c->best = *e;
        c->found = 1;
    }
    return 0;
}

static int index_find(const char *name, IndexEnt *out)
{
    FindCtx c = { .name = name };
    index_all(find_cb, &c);
    if (c.found)
        *out = c.best;
    return c.found;
}

static int run_capture_line(char *const argv[], char *out, size_t n)
{
    int pfd[2];
    if (pipe(pfd) < 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pfd[1], 1);
        close(pfd[0]);
        close(pfd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(pfd[1]);
    ssize_t k = read(pfd[0], out, n - 1);
    out[k > 0 ? k : 0] = 0;
    close(pfd[0]);
    int st;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Uzak paketi önbelleğe indir, sha256'yı doğrula. 0 = tamam */
static int download_pkg(const IndexEnt *e, char *path, size_t n)
{
    char url[1024];
    if (e->url[0])
        snprintf(url, sizeof url, "%s", e->url);
    else
        snprintf(url, sizeof url, "%s/%s", e->base, e->file);
    if (mkdir_p(cache_dir, 0755) < 0)
        return fail("%s oluşturulamadı: %s", cache_dir, strerror(errno));
    snprintf(path, n, "%s/%s", cache_dir, e->file[0] ? e->file : "paket.axp");
    char part[PATH_MAX + 8];
    snprintf(part, sizeof part, "%s.part", path);
    char mb[32];
    snprintf(mb, sizeof mb, "%.1f MB", e->size / 1048576.0);
    info("İndiriliyor: %s %s (%s)", e->name, e->version, e->size ? mb : "boyut bilinmiyor");
    printf(C_DIM "    %s" C_OFF "\n", url);
    fflush(stdout);
    char *argv[] = { "curl", "-fL", "--retry", "3", "--connect-timeout", "20",
                     (isatty(1) || getenv("AXPKG_PROGRESS")) ? "--progress-bar" : "-sS", "-o", part, url, NULL };
    if (run(argv) != 0) {
        unlink(part);
        return fail("%s indirilemedi. İnternet bağlantısını denetleyin: ag test", e->name);
    }
    if (e->sha256[0]) {
        char out[256];
        char *sa[] = { "sha256sum", part, NULL };
        if (run_capture_line(sa, out, sizeof out) != 0 || strncmp(out, e->sha256, 64)) {
            unlink(part);
            return fail("%s: sha256 özeti tutmuyor (bozuk ya da değiştirilmiş indirme)", e->name);
        }
        printf(C_OK "  ✓" C_OFF " sha256 doğrulandı\n");
    }
    if (rename(part, path) < 0)
        return fail("%s: %s", path, strerror(errno));
    return 0;
}

typedef struct { const char *base; } IconCtx;

static int icon_cb(IndexEnt *e, void *ud)
{
    IconCtx *c = ud;
    if (!e->icon[0] || !is_valid_name(e->name))
        return 0;
    char dir[PATH_MAX], dst[PATH_MAX], url[1024];
    snprintf(dir, sizeof dir, "%s/icons", remote_dir);
    mkdir_p(dir, 0755);
    snprintf(dst, sizeof dst, "%s/%s.png", dir, e->name);
    snprintf(url, sizeof url, "%s/%s", c->base, e->icon);
    char *argv[] = { "curl", "-fsSL", "--max-time", "20", "-o", dst, url, NULL };
    if (run(argv) != 0)
        unlink(dst);
    return 0;
}

static int cmd_update(void)
{
    FILE *f = fopen(repos_conf, "r");
    const char *env = getenv("AXPKG_REMOTE");
    if (!f && !(env && *env))
        return fail("uzak depo tanımlı değil (%s)", repos_conf);
    if (mkdir_p(remote_dir, 0755) < 0)
        return fail("%s oluşturulamadı: %s", remote_dir, strerror(errno));
    /* eski dizinleri temizle */
    for (int i = 0; i < 16; i++) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s/%d.INDEX", remote_dir, i);
        unlink(p);
        snprintf(p, sizeof p, "%s/%d.URL", remote_dir, i);
        unlink(p);
    }
    char line[512];
    int idx = 0, ok = 0;
    char envbuf[2048];
    snprintf(envbuf, sizeof envbuf, "%s", env ? env : "");
    char *envp = envbuf[0] ? envbuf : NULL;
    for (;;) {
        if (envp) { /* AXPKG_REMOTE: boşlukla ayrılmış adresler (test için) */
            char *t = strtok(envp == envbuf ? envbuf : NULL, " ");
            envp = (char *)1;
            if (!t)
                break;
            snprintf(line, sizeof line, "%s", t);
        } else if (!fgets(line, sizeof line, f)) {
            break;
        }
        line[strcspn(line, "\r\n")] = 0;
        char *u = line;
        while (*u == ' ')
            u++;
        if (!*u || *u == '#' || idx >= 16)
            continue;
        size_t l = strlen(u);
        while (l && u[l - 1] == '/')
            u[--l] = 0;
        char ip[PATH_MAX], url[640];
        snprintf(ip, sizeof ip, "%s/%d.INDEX", remote_dir, idx);
        snprintf(url, sizeof url, "%s/INDEX", u);
        info("Depo: %s", u);
        char *argv[] = { "curl", "-fsSL", "--retry", "2", "--connect-timeout", "15", "-o", ip, url, NULL };
        if (run(argv) != 0) {
            fprintf(stderr, C_ERR "  erişilemedi" C_OFF " (internet bağlantısı? ag test)\n");
            unlink(ip);
            continue;
        }
        char up[PATH_MAX];
        snprintf(up, sizeof up, "%s/%d.URL", remote_dir, idx);
        FILE *uf = fopen(up, "w");
        if (uf) {
            fprintf(uf, "%s\n", u);
            fclose(uf);
        }
        int cnt = 0;
        FILE *ixf = fopen(ip, "r");
        char l2[4096];
        while (ixf && fgets(l2, sizeof l2, ixf))
            cnt += !strncmp(l2, "name=", 5);
        if (ixf)
            fclose(ixf);
        printf(C_OK "  ✓" C_OFF " %d paket\n", cnt);
        /* Market için simgeler: <uzak>/icon=... -> remote/icons/<ad>.png */
        IconCtx icx = { .base = u };
        index_walk(ip, u, 0, icon_cb, &icx);
        idx++;
        ok++;
    }
    if (f)
        fclose(f);
    return ok ? 0 : fail("hiçbir depoya erişilemedi");
}

typedef struct { char **names; size_t n; } NameList;

static int names_cb(IndexEnt *e, void *ud)
{
    NameList *l = ud;
    for (size_t i = 0; i < l->n; i++)
        if (!strcmp(l->names[i], e->name))
            return 0;
    l->names = realloc(l->names, sizeof(char *) * (l->n + 1));
    l->names[l->n++] = strdup(e->name);
    return 0;
}

static int cmd_names(void)
{
    NameList l = { 0 };
    index_all(names_cb, &l);
    qsort(l.names, l.n, sizeof(char *), cmp_str);
    for (size_t i = 0; i < l.n; i++) {
        puts(l.names[i]);
        free(l.names[i]);
    }
    free(l.names);
    return 0;
}

typedef struct { const char *cmd; char pkg[128]; } ProvCtx;

static int prov_cb(IndexEnt *e, void *ud)
{
    ProvCtx *c = ud;
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", e->provides);
    for (char *t = strtok(buf, " ,"); t; t = strtok(NULL, " ,"))
        if (!strcmp(t, c->cmd)) {
            snprintf(c->pkg, sizeof c->pkg, "%s", e->name);
            return 1;
        }
    return 0;
}

static int cmd_provides(const char *cmd)
{
    ProvCtx c = { .cmd = cmd };
    index_all(prov_cb, &c);
    if (!c.pkg[0])
        return 1;
    puts(c.pkg);
    return 0;
}

static int cmd_upgrade(void)
{
    DIR *d = opendir(db_dir);
    struct dirent *e;
    int n = 0, rc = 0;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char mp[PATH_MAX];
        snprintf(mp, sizeof mp, "%s/%s/MANIFEST", db_dir, e->d_name);
        Manifest m;
        IndexEnt ie;
        if (manifest_read(mp, &m) != 0 || !index_find(m.name, &ie))
            continue;
        if (vercmp(ie.version, m.version) > 0) {
            info("%s: %s -> %s", m.name, m.version, ie.version);
            rc |= install_one(m.name, 1, 0);
            n++;
        }
    }
    if (d)
        closedir(d);
    if (!n)
        info("Tüm paketler güncel");
    return rc;
}

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
    /* Çalışma dizini hedefle aynı dosya sisteminde: dosyalar kopyalanmadan taşınır (rename).
     * Canlı sistemde her şey RAM'de olduğundan büyük paketlerde bellek iki kat harcanmaz. */
    if (mkdir_p(cache_dir, 0755) < 0)
        return fail("%s oluşturulamadı: %s", cache_dir, strerror(errno));
    char tmpl[PATH_MAX];
    snprintf(tmpl, sizeof tmpl, "%s/is.XXXXXX", cache_dir);
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
        OwnerIdx ow;
        owner_idx_load(&ow, m.name);
        CopyCtx chk = { .pkg = m.name, .check_only = 1, .owners = &ow };
        copy_tree(files, "", &chk);
        owner_idx_free(&ow);
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
    if (run_hook(src, m.name) != 0) {
        /* Kurulum betiği başarısız (ör. tarayıcı indirilemedi): yarım kurulum bırakma */
        if (!upgrade) {
            fprintf(stderr, C_ERR "  geri alınıyor:" C_OFF " %s kurulumu tamamlanamadı\n", m.name);
            remove_one(m.name, 1);
        }
        fail("%s kurulamadı (kurulum betiği başarısız)", m.name);
        goto out;
    }
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
    IndexEnt ie;
    int have_ix = index_find(arg, &ie);
    if ((!have_ix || ie.local) && repo_find(arg, path, sizeof path))
        return install_file(path, force, depth);
    if (!have_ix)
        return fail("'%s' paketi bulunamadı. Depo dizinini yenilemek için: axpkg update", arg);
    if (download_pkg(&ie, path, sizeof path) != 0)
        return 1;
    int rc = install_file(path, force, depth);
    unlink(path); /* bellekte yer kaplamasın (canlı sistemde her şey RAM'de) */
    return rc;
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

typedef struct { const char *q; int n; NameList seen; } AvailCtx;

static int avail_cb(IndexEnt *e, void *ud)
{
    AvailCtx *c = ud;
    for (size_t i = 0; i < c->seen.n; i++)
        if (!strcmp(c->seen.names[i], e->name))
            return 0;
    if (c->q && !strcasestr(e->name, c->q) && !strcasestr(e->description, c->q))
        return 0;
    names_cb(e, &c->seen);
    char sz[32] = "";
    if (!e->local && e->size)
        snprintf(sz, sizeof sz, " (%.1f MB)", e->size / 1048576.0);
    printf("%s %-20s %-10s %s%s%s%s\n", is_installed(e->name) ? C_OK "[k]" C_OFF : e->local ? "   " : C_INFO "[i]" C_OFF,
           e->name, e->version, e->description, C_DIM, sz, C_OFF);
    c->n++;
    return 0;
}

static int cmd_available(const char *q)
{
    printf("\033[1m%-3s %-20s %-10s %s\033[0m\n", "", "PAKET", "SÜRÜM", "AÇIKLAMA");
    AvailCtx c = { .q = q };
    index_all(avail_cb, &c);
    for (size_t i = 0; i < c.seen.n; i++)
        free(c.seen.names[i]);
    free(c.seen.names);
    if (!c.n)
        puts(q ? "Eşleşen paket yok." : "Depoda paket yok.");
    printf(C_DIM "[k] = kurulu, [i] = internetten indirilir.  Kurmak için: axpkg install <ad>" C_OFF "\n");
    char p[PATH_MAX];
    snprintf(p, sizeof p, "%s/0.INDEX", remote_dir);
    if (access(p, F_OK) != 0)
        printf(C_DIM "Uzak depo dizini yok; internetteki paketleri görmek için: axpkg update" C_OFF "\n");
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
           "  axpkg update                             uzak depo dizinlerini internetten indir\n"
           "  axpkg upgrade                            kurulu paketleri güncelle\n"
           "  axpkg install [-f] <paket.axp | ad>...   paket kur (-f: zorla/yeniden kur)\n"
           "  axpkg remove  [-f] <ad>...               paket kaldır\n"
           "  axpkg list                               kurulu paketler\n"
           "  axpkg available | search [kelime]        depodaki paketler (yerel + uzak)\n"
           "  axpkg provides <komut>                   komutu sağlayan paket\n"
           "  axpkg info <ad>                          paket bilgisi\n"
           "  axpkg files <ad>                         paketin dosyaları\n"
           "  axpkg owner <yol>                        dosyanın sahibi olan paket\n"
           "  axpkg create <dizin> [çıktı.axp]         dizinden paket oluştur\n\n"
           "Paket (.axp = tar.gz): MANIFEST (name=, version=, description=, depends=)\n"
           "                       files/ (kök dizine kopyalanır), post-install, pre-remove\n"
           "Yerel depo: %s   Uzak depolar: %s\n", VERSION, repo_dir, repos_conf);
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
    snprintf(remote_dir, sizeof remote_dir, "%s/var/lib/axpkg/remote", root);
    snprintf(cache_dir, sizeof cache_dir, "%s/var/cache/axpkg", root);
    snprintf(repos_conf, sizeof repos_conf, "%s/etc/axpkg/depolar", root);

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
        return cmd_available(argc > 2 ? argv[2] : NULL);
    if (!strcmp(cmd, "update"))
        return cmd_update();
    if (!strcmp(cmd, "upgrade")) {
        if (mkdir_p(db_dir, 0755) < 0)
            return fail("%s oluşturulamadı: %s", db_dir, strerror(errno));
        return cmd_upgrade();
    }
    if (!strcmp(cmd, "names"))
        return cmd_names();
    if (!strcmp(cmd, "provides"))
        return argc > 2 ? cmd_provides(argv[2]) : 1;
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
