/*
 * AxsOS init (PID 1)
 *
 * - /proc, /sys, /dev, /dev/pts, /tmp, /run bağlar
 * - hostname'i "axsos" yapar
 * - AxsOS açılış logosunu basar
 * - /etc/rc varsa çalıştırır (ağ vb. açılış işleri)
 * - her etkin konsolda (ör. ekran tty1 + seri ttyS0) logo basıp shell başlatır
 *   (/bin/axsh, yoksa /bin/sh); shell kapanınca yeniden açar
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
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define HOSTNAME "axsos"
#define CONSOLE  "/dev/console"

static const char *SHELLS[] = { "/bin/axsh", "/bin/sh", NULL };

#define MAX_TTYS 4
static struct {
    char dev[64];
    pid_t pid;
    time_t last_spawn;
} ttys[MAX_TTYS];
static int nttys;

static volatile sig_atomic_t shutdown_req; /* 0 veya RB_* komutu */

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
    if (mount(src, dst, type, flags, data) < 0 && errno != EBUSY)
        msg("UYARI: %s bağlanamadı: %s", dst, strerror(errno));
}

static void mount_all(void)
{
    do_mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    do_mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL);
    do_mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755");
    do_mount("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC, "gid=5,mode=620,ptmxmode=666");
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
        ttys[nttys++].pid = -1;
    }
    if (!nttys) {
        snprintf(ttys[0].dev, sizeof ttys[0].dev, "%s", CONSOLE);
        ttys[0].pid = -1;
        nttys = 1;
    }
}

static pid_t spawn_shell(const char *tty)
{
    const char *sh = NULL;
    for (int i = 0; SHELLS[i]; i++)
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
        print_logo();
        setenv("SHELL", sh, 1);
        chdir("/root");
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
    mount_all();
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
            ttys[i].pid = spawn_shell(ttys[i].dev);
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
                if (!shutdown_req)
                    msg("%s üzerindeki shell kapandı, yeniden başlatılıyor...", ttys[i].dev);
            }
    }
}
