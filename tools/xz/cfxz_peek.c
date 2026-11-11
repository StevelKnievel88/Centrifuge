/* cfxz-peek - Centrifuge XZ reconnaissance helper (read-only).
 *
 * Finds the XDJ-XZ player process (rbp) and, over a short window, repeatedly reads the mixer-communication object and
 * logs its bytes, so turning the channel knobs shows up as changing values we can locate offline. It only READS the
 * player's memory (via ptrace attach/detach + /proc/<pid>/mem); it never writes to the player, the device, or the
 * firmware. A brief attach pauses the player for a few milliseconds per sample; nothing persists.
 *
 * Pointer chain for firmware 1.26 (rbp is ET_EXEC, fixed addresses):
 *     global 0x01c8a308  ->  UiObjManager*  ,  +0x50  ->  MixerCpuCom*
 * We dump a window at the MixerCpuCom object; the decoded channel knob values live inside it.
 *
 * usage: cfxz-peek <logfile> [seconds] [dumpbytes]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/types.h>

#define GLOBAL_UIOBJ 0x01c8a308u
#define OFF_MIXER    0x50

static int read_mem(int memfd, uint32_t addr, void *out, size_t n)
{
    ssize_t got = pread(memfd, out, n, (off_t) addr);
    return got == (ssize_t) n ? 0 : -1;
}

static pid_t find_rbp(void)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e;
    pid_t found = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[64], comm[64] = {0};
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, comm, sizeof comm - 1);
        close(fd);
        if (n > 0) {
            if (comm[n - 1] == '\n') comm[n - 1] = 0;
            if (strcmp(comm, "rbp") == 0) { found = (pid_t) atoi(e->d_name); break; }
        }
    }
    closedir(d);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: cfxz-peek <logfile> [seconds] [dumpbytes]\n"); return 2; }
    const char *logpath = argv[1];
    int seconds = argc > 2 ? atoi(argv[2]) : 45;
    uint32_t dump = argc > 3 ? (uint32_t) strtoul(argv[3], 0, 0) : 0x1000;
    if (dump > 0x4000) dump = 0x4000;

    FILE *log = fopen(logpath, "w");
    if (!log) { fprintf(stderr, "cannot write %s\n", logpath); return 1; }
    fprintf(log, "cfxz-peek: reconnaissance of the mixer object (read-only)\n");

    pid_t pid = find_rbp();
    if (!pid) { fprintf(log, "player process 'rbp' not found\n"); fclose(log); return 1; }
    fprintf(log, "rbp pid = %d\n", (int) pid);

    char mempath[64];
    snprintf(mempath, sizeof mempath, "/proc/%d/mem", (int) pid);

    unsigned char *buf = malloc(dump);
    if (!buf) { fclose(log); return 1; }

    time_t start = time(0);
    int sample = 0;
    while (time(0) - start < seconds) {
        if (ptrace(PTRACE_ATTACH, pid, 0, 0) == 0) {
            int st;
            waitpid(pid, &st, 0);
            int memfd = open(mempath, O_RDONLY);
            uint32_t uiobj = 0, mixer = 0;
            int ok = 0;
            if (memfd >= 0) {
                if (read_mem(memfd, GLOBAL_UIOBJ, &uiobj, 4) == 0 && uiobj >= 0x8000 &&
                    read_mem(memfd, uiobj + OFF_MIXER, &mixer, 4) == 0 && mixer >= 0x8000 &&
                    read_mem(memfd, mixer, buf, dump) == 0)
                    ok = 1;
                close(memfd);
            }
            ptrace(PTRACE_DETACH, pid, 0, 0);

            fprintf(log, "\n#%d t=%ld uiobj=0x%08x mixer=0x%08x %s\n",
                    sample, (long) (time(0) - start), uiobj, mixer, ok ? "" : "(read failed)");
            if (ok) {
                for (uint32_t i = 0; i < dump; i += 32) {
                    fprintf(log, "%04x  ", i);
                    for (uint32_t j = 0; j < 32 && i + j < dump; j++) fprintf(log, "%02x", buf[i + j]);
                    fputc('\n', log);
                }
            }
            fflush(log);
            sample++;
        } else {
            fprintf(log, "ptrace attach failed (will retry)\n");
            fflush(log);
        }
        struct timespec ts = { 1, 500000000L };   /* 1.5 s between samples */
        nanosleep(&ts, 0);
    }
    fprintf(log, "\ndone: %d samples\n", sample);
    fclose(log);
    free(buf);
    return 0;
}
