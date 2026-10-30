/*
 * AxsOS init (PID 1)
 *
 * - kalıcı depolama: "AXSOS-VERI" etiketli ext4 disk varsa (e2fsck ile denetleyip)
 *   kök dizinin üzerine overlay kurar: alt katman ISO'daki sistem (salt okunur, RAM),
 *   üst katman disk. Kurulan paketler, dosyalar, ayarlar yeniden başlatınca korunur.
 *   Disk /mnt/kalici'de görünür. axs.kalici=0 ile kapatılır.
 * - /proc, /sys, /dev, /dev/pts, /tmp, /run bağlar
 * - hostname'i "axsos" yapar
 * - AxsOS açılış logosunu basar
 * - /etc/rc varsa çalıştırır (ağ vb. açılış işleri)
 * - her etkin konsolda (ör. ekran tty1 + seri ttyS0) logo basıp shell başlatır
 *   (/bin/bash, yoksa /bin/axsh, yoksa /bin/sh); shell kapanınca yeniden açar
 * - ekran konsolunda (tty1) framebuffer varsa AxsDE masaüstünü başlatır
 *   (çekirdek parametresi axs.gui=0 ile kapatılır; masaüstünden "metin
 *   konsoluna geç" denirse ya da art arda çökerse o konsolda shell açılır)
 * - yetim süreçleri toplar
 * - poweroff / reboot / halt sinyallerini işler (BusyBox ile uyumlu:
 *   SIGUSR2 = kapat, SIGTERM = yeniden başlat, SIGUSR1 = durdur)
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <dirent.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HOSTNAME "axsos"
#define CONSOLE  "/dev/console"

static const char *SHELLS[] = { "/bin/bash", "/bin/axsh", "/bin/sh", NULL };

#define MAX_TTYS 4
#define DESKTOP  "/usr/bin/axsde"

static struct {
    char dev[300];
    pid_t pid;
    time_t last_spawn;
    int gui;        /* bu konsolda masaüstü çalışsın */
    int is_gui;     /* şu an çalışan süreç masaüstü mü */
    int gui_fails;  /* hızlı çöküş sayısı */
} ttys[MAX_TTYS];
static int nttys;

static volatile sig_atomic_t shutdown_req; /* 0 veya RB_* komutu */

#define KALICI_ETIKET "AXSOS-VERI"
#define KALICI_YER    "/mnt/kalici"
static char kalici_dev[300];   /* etkinse kalıcı depolama aygıtı */

static void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("\033[1;36m[init]\033[0m ", stdout);
    vprintf(fmt, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);
}

static void do_mount(const char *src, const char *dst, const char *type,
                     unsigned long flags, const char *data)
{
    mkdir(dst, 0755);
    /* zaten bağlıysa (kalıcı depolama aşamasında taşındıysa) tekrar bağlama */
    char parent[128];
    snprintf(parent, sizeof parent, "%s/..", dst);
    struct stat a, b;
    if (stat(dst, &a) == 0 && stat(parent, &b) == 0 && a.st_dev != b.st_dev)
        return;
    if (mount(src, dst, type, flags, data) < 0 && errno != EBUSY)
        msg("UYARI: %s bağlanamadı: %s", dst, strerror(errno));
}

static int cmdline_has(const char *opt)
{
    char cmd[1024] = "";
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f)
        return 0;
    if (!fgets(cmd, sizeof cmd, f))
        cmd[0] = 0;
    fclose(f);
    size_t n = strlen(opt);
    for (char *p = cmd; (p = strstr(p, opt)); p += n)
        if ((p == cmd || p[-1] == ' ') && (p[n] == ' ' || p[n] == '\n' || p[n] == 0))
            return 1;
    return 0;
}

/* ext2/3/4 süper bloğundaki etiket (aygıt ext değilse -1) */
static int ext_label(const char *dev, char *label, size_t n)
{
    int fd = open(dev, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    unsigned char sb[256];
    ssize_t k = pread(fd, sb, sizeof sb, 1024);
    close(fd);
    if (k != (ssize_t)sizeof sb || sb[0x38] != 0x53 || sb[0x39] != 0xEF)
        return -1;
    snprintf(label, n, "%.16s", (const char *)sb + 0x78);
    return 0;
}

static int find_kalici(char *out, size_t n)
{
    DIR *d = opendir("/sys/class/block");
    struct dirent *e;
    int found = 0;
    while (d && !found && (e = readdir(d))) {
        const char *nm = e->d_name;
        if (nm[0] == '.' || !strncmp(nm, "loop", 4) || !strncmp(nm, "ram", 3) || !strncmp(nm, "zram", 4) ||
            !strncmp(nm, "sr", 2) || !strncmp(nm, "fd", 2))
            continue;
        char dev[300], label[32];
        snprintf(dev, sizeof dev, "/dev/%s", nm);
        if (ext_label(dev, label, sizeof label) == 0 && !strcmp(label, KALICI_ETIKET)) {
            snprintf(out, n, "%s", dev);
            found = 1;
        }
    }
    if (d)
        closedir(d);
    return found;
}

static int run_wait(char *const argv[])
{
    pid_t pid = fork();
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    while (pid > 0 && waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    return pid > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Kalıcı depolama: overlay kökü kur ve içine geç. Başarısızsa canlı (RAM) kipte devam eder. */
static void kalici_baslat(void)
{
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/dev", 0755);
    mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    if (cmdline_has("axs.kalici=0"))
        return;
    char dev[300];
    int found = 0;
    for (int i = 0; i < 10 && !(found = find_kalici(dev, sizeof dev)); i++)
        usleep(100000); /* USB/SATA diskler biraz geç görünebilir */
    if (!found)
        return;
    msg("Kalıcı depolama: %s", dev);
    if (access("/sbin/e2fsck", X_OK) == 0) {
        char *fsck[] = { "/sbin/e2fsck", "-p", dev, NULL };
        int r = run_wait(fsck);
        if (r >= 4)
            msg("UYARI: %s üzerinde düzeltilemeyen dosya sistemi hataları var (e2fsck %d)", dev, r);
    }
    mkdir("/.kalici", 0755);
    if (mount(dev, "/.kalici", "ext4", MS_NOATIME, NULL) < 0) {
        msg("UYARI: %s bağlanamadı (%s); canlı kipte devam ediliyor", dev, strerror(errno));
        return;
    }
    if (access("/.kalici/SIFIRLA", F_OK) == 0) { /* fabrika ayarlarına dönüş istendi */
        msg("Kalıcı depolama sıfırlanıyor (tüm değişiklikler siliniyor)...");
        char *rm[] = { "/bin/rm", "-rf", "/.kalici/sistem", NULL };
        run_wait(rm);
        unlink("/.kalici/SIFIRLA");
    }
    mkdir("/.kalici/sistem", 0755);
    mkdir("/.kalici/sistem/ust", 0755);
    mkdir("/.kalici/sistem/is", 0755);
    mkdir("/.yeni", 0755);
    if (mount("overlay", "/.yeni", "overlay", 0,
              "lowerdir=/,upperdir=/.kalici/sistem/ust,workdir=/.kalici/sistem/is") < 0) {
        msg("UYARI: overlay kurulamadı (%s); canlı kipte devam ediliyor", strerror(errno));
        umount("/.kalici");
        return;
    }
    static const char *MOVE[] = { "/proc", "/sys", "/dev" };
    for (int i = 0; i < 3; i++) {
        char to[64];
        snprintf(to, sizeof to, "/.yeni%s", MOVE[i]);
        mkdir(to, 0755);
        mount(MOVE[i], to, NULL, MS_MOVE, NULL);
    }
    mkdir("/.yeni/mnt", 0755);
    mkdir("/.yeni" KALICI_YER, 0755);
    mount("/.kalici", "/.yeni" KALICI_YER, NULL, MS_MOVE, NULL);
    /* yeni köke geç (switch_root gibi, ama alt katman olan initramfs silinmez) */
    if (chdir("/.yeni") == 0 && mount(".", "/", NULL, MS_MOVE, NULL) == 0 && chroot(".") == 0) {
        if (chdir("/") < 0) { /* yok say */ }
        snprintf(kalici_dev, sizeof kalici_dev, "%s", dev);
    } else {
        msg("UYARI: kalıcı köke geçilemedi (%s)", strerror(errno));
    }
}

static void mount_all(void)
{
    /* Kök dosya sistemi (initramfs, tmpfs) varsayılan olarak belleğin yarısıyla sınırlı;
     * büyük paketler (tarayıcılar) için sınırı yükselt. */
    if (!kalici_dev[0])
        mount("rootfs", "/", NULL, MS_REMOUNT, "size=90%");
    do_mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    do_mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    do_mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    do_mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "gid=5,mode=620,ptmxmode=666");
    /* POSIX paylaşımlı bellek (glibc shm_open; Firefox/Chrome buna ihtiyaç duyar) */
    do_mount("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    do_mount("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV, "mode=1777");
    do_mount("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV, "mode=0755");
}

static void setup_console(void)
{
    int fd = open(CONSOLE, O_RDWR | O_NOCTTY);
    if (fd < 0)
        return;
    dup2(fd, 0);
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2)
        close(fd);
}

static void set_hostname(void)
{
    if (sethostname(HOSTNAME, strlen(HOSTNAME)) < 0)
        msg("UYARI: hostname ayarlanamadı: %s", strerror(errno));
    FILE *f = fopen("/etc/hostname", "w");
    if (f) {
        fputs(HOSTNAME "\n", f);
        fclose(f);
    }
}

static void print_logo(void)
{
    static const char *logo =
        "\033[1;35m"
        "     _              ___  ____  \n"
        "    / \\   __  _____/ _ \\/ ___| \n"
        "   / _ \\  \\ \\/ / __| | | \\___ \\ \n"
        "  / ___ \\  >  <\\__ \\ |_| |___) |\n"
        " /_/   \\_\\/_/\\_\\___/\\___/|____/ \n"
        "\033[0m";
    char kver[128] = "?";
    FILE *f = fopen("/proc/sys/kernel/osrelease", "r");
    if (f) {
        if (fgets(kver, sizeof kver, f))
            kver[strcspn(kver, "\n")] = 0;
        fclose(f);
    }
    printf("\n%s\n", logo);
    printf("  \033[1mAxsOS\033[0m - Axs dilinin evi  |  Linux %s\n", kver);
    printf("  Çıkmak için: \033[1mpoweroff\033[0m   Yardım: \033[1mhelp\033[0m\n\n");
    fflush(stdout);
}

static void on_signal(int sig)
{
    switch (sig) {
    case SIGUSR2: shutdown_req = RB_POWER_OFF; break;
    case SIGTERM: shutdown_req = RB_AUTOBOOT;  break;
    case SIGUSR1: shutdown_req = RB_HALT_SYSTEM; break;
    case SIGINT:  shutdown_req = RB_AUTOBOOT;  break; /* Ctrl-Alt-Del */
    }
}

static void setup_signals(void)
{
    struct sigaction sa = { 0 };
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    reboot(RB_DISABLE_CAD); /* Ctrl-Alt-Del -> SIGINT */
}

static void default_env(void)
{
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin", 1);
    setenv("HOME", "/root", 1);
    setenv("TERM", "linux", 0);
    setenv("SHELL", "/bin/sh", 1);
    setenv("USER", "root", 1);
    setenv("LANG", "C.UTF-8", 1);
}

/* Çocuk sürecin sinyallerini varsayılana döndür. */
static void child_reset(void)
{
    sigset_t all;
    sigemptyset(&all);
    sigprocmask(SIG_SETMASK, &all, NULL);
    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
}

static void run_rc(void)
{
    if (access("/etc/rc", X_OK) != 0)
        return;
    pid_t pid = fork();
    if (pid == 0) {
        child_reset();
        execl("/bin/sh", "sh", "/etc/rc", (char *)NULL);
        _exit(127);
    }
    int st;
    while (pid > 0 && waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
}

/* Masaüstü kullanılabilir mi: framebuffer + axsde var, axs.gui=0 verilmemiş */
static int gui_wanted(void)
{
    char cmd[1024] = "";
    FILE *f = fopen("/proc/cmdline", "r");
    if (f) {
        if (!fgets(cmd, sizeof cmd, f))
            cmd[0] = 0;
        fclose(f);
    }
    if (strstr(cmd, "axs.gui=0"))
        return 0;
    return access("/dev/fb0", R_OK | W_OK) == 0 && access(DESKTOP, X_OK) == 0;
}

/* Etkin konsolları bul: "tty0 ttyS0" -> /dev/tty1, /dev/ttyS0 */
static void find_consoles(void)
{
    char buf[256] = "";
    FILE *f = fopen("/sys/class/tty/console/active", "r");
    if (f) {
        if (!fgets(buf, sizeof buf, f))
            buf[0] = 0;
        fclose(f);
    }
    for (char *t = strtok(buf, " \n"); t && nttys < MAX_TTYS; t = strtok(NULL, " \n")) {
        char dev[64];
        /* tty0 "şu anki sanal konsol" demektir; ilk sanal konsolu kullan */
        snprintf(dev, sizeof dev, "/dev/%s", strcmp(t, "tty0") ? t : "tty1");
        if (access(dev, R_OK | W_OK) != 0)
            continue;
        snprintf(ttys[nttys].dev, sizeof ttys[nttys].dev, "%s", dev);
        ttys[nttys].gui = !strcmp(dev, "/dev/tty1") && gui_wanted();
        ttys[nttys++].pid = -1;
    }
    if (!nttys) {
        snprintf(ttys[0].dev, sizeof ttys[0].dev, "%s", CONSOLE);
        ttys[0].pid = -1;
        nttys = 1;
    }
}

static pid_t spawn_shell(const char *tty, int gui)
{
    const char *sh = NULL;
    if (gui)
        sh = DESKTOP;
    for (int i = 0; !sh && SHELLS[i]; i++)
        if (access(SHELLS[i], X_OK) == 0) {
            sh = SHELLS[i];
            break;
        }
    if (!sh) {
        msg("HATA: çalıştırılabilir shell bulunamadı");
        return -1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        child_reset();
        setsid();
        /* Konsolu kontrol terminali yap ki Ctrl-C çalışsın. */
        int fd = open(tty, O_RDWR);
        if (fd >= 0) {
            ioctl(fd, TIOCSCTTY, 1);
            dup2(fd, 0);
            dup2(fd, 1);
            dup2(fd, 2);
            if (fd > 2)
                close(fd);
        }
        tcsetpgrp(0, getpid());
        if (gui) {
            if (chdir("/root") < 0) { /* yok say */ }
            execl(sh, "axsde", (char *)NULL);
            _exit(127);
        }
        print_logo();
        setenv("SHELL", sh, 1);
        if (chdir("/root") < 0) { /* yok say */ }
        const char *base = strrchr(sh, '/') + 1;
        char argv0[64];
        snprintf(argv0, sizeof argv0, "-%s", base); /* login shell */
        execl(sh, argv0, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void shutdown_system(int how)
{
    const char *what = how == RB_POWER_OFF ? "kapatılıyor" :
                       how == RB_AUTOBOOT ? "yeniden başlatılıyor" : "durduruluyor";
    msg("Sistem %s...", what);
    kill(-1, SIGTERM);
    sleep(1);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    sync();
    umount2("/tmp", MNT_DETACH);
    umount2("/run", MNT_DETACH);
    if (kalici_dev[0]) {
        /* takası kapat, diski salt okunur yap: güç kesilse bile tutarlı kalsın */
        swapoff(KALICI_YER "/takas");
        sync();
        if (mount(NULL, KALICI_YER, NULL, MS_REMOUNT | MS_RDONLY, NULL) < 0)
            msg("UYARI: kalıcı disk salt okunur yapılamadı (%s); veriler yazıldı (sync)", strerror(errno));
        sync();
    }
    reboot(how);
    /* reboot başarısız olursa sonsuza kadar bekle (PID 1 çıkmamalı). */
    for (;;)
        pause();
}

int main(void)
{
    if (getpid() != 1) {
        fprintf(stderr, "init: yalnızca PID 1 olarak çalışır\n");
        return 1;
    }
    kalici_baslat();
    mount_all();
    if (kalici_dev[0]) { /* masaüstü ve araçlar için: kalıcı depolama etkin */
        FILE *f = fopen("/run/axs-kalici", "w");
        if (f) {
            fprintf(f, "%s\n", kalici_dev);
            fclose(f);
        }
    }
    setup_console();
    setup_signals();
    default_env();
    set_hostname();
    run_rc();
    find_consoles();

    for (;;) {
        if (shutdown_req)
            shutdown_system(shutdown_req);
        for (int i = 0; i < nttys; i++) {
            if (ttys[i].pid > 0)
                continue;
            /* Shell çok hızlı ölüyorsa (ör. bozuk) CPU'yu yakma. */
            if (time(NULL) - ttys[i].last_spawn < 2)
                sleep(2);
            ttys[i].last_spawn = time(NULL);
            ttys[i].is_gui = ttys[i].gui;
            ttys[i].pid = spawn_shell(ttys[i].dev, ttys[i].gui);
        }
        int st;
        pid_t pid = wait(&st);
        if (pid < 0) {
            if (errno == ECHILD)
                sleep(1);
            continue; /* EINTR: sinyal geldi, döngü başında kontrol et */
        }
        for (int i = 0; i < nttys; i++)
            if (pid == ttys[i].pid) {
                ttys[i].pid = -1;
                if (shutdown_req)
                    continue;
                if (ttys[i].is_gui) {
                    int quick = time(NULL) - ttys[i].last_spawn < 10;
                    int clean = WIFEXITED(st) && WEXITSTATUS(st) == 0;
                    if (clean) {
                        ttys[i].gui = 0; /* kullanıcı metin konsolunu seçti */
                    } else if (quick && ++ttys[i].gui_fails >= 3) {
                        msg("Masaüstü açılamadı, %s üzerinde kabuk başlatılıyor", ttys[i].dev);
                        ttys[i].gui = 0;
                    }
                } else {
                    msg("%s üzerindeki shell kapandı, yeniden başlatılıyor...", ttys[i].dev);
                }
            }
    }
}
