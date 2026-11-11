/* cfxz-peek v2 - Centrifuge XZ reconnaissance helper (read-only).
 *
 * Finds the XDJ-XZ player (rbp) and repeatedly reads the mixer-communication object AND the buffers it points to,
 * so turning the channel knobs shows up as changing bytes we can locate offline. It only READS the player's memory
 * (ptrace attach/detach + /proc/<pid>/mem); it never writes to the player, the device or the firmware. Each sample
 * briefly pauses the player (a few ms); nothing persists.
 *
 * Pointer chain for firmware 1.26 (rbp is ET_EXEC, fixed addresses):
 *     global 0x01c8a308  ->  UiObjManager*  ,  +0x50  ->  MixerCpuCom*
 * The mixer frame that carries the knob ADCs may be inside the object or in a buffer it points to, so we dump the
 * object head and also follow the heap pointers in its first bytes.
 *
 * usage: cfxz-peek <logfile> [seconds] [interval_ms]
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
#define OBJ_DUMP     0x400          /* object head bytes to dump each sample */
#define PTR_SCAN     0x60           /* scan this many bytes of the object head for heap pointers */
#define PTR_DUMP     0x100          /* bytes to dump from each pointed-to buffer */
#define MAX_PTRS     16

static int read_mem(int memfd, uint32_t addr, void *out, size_t n)
{
    return pread(memfd, out, n, (off_t) addr) == (ssize_t) n ? 0 : -1;
}

static pid_t find_rbp(void)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    struct dirent *e; pid_t found = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        char path[64], comm[64] = {0};
        snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
        int fd = open(path, O_RDONLY); if (fd < 0) continue;
        ssize_t n = read(fd, comm, sizeof comm - 1); close(fd);
        if (n > 0) { if (comm[n-1]=='\n') comm[n-1]=0; if (!strcmp(comm,"rbp")) { found=(pid_t)atoi(e->d_name); break; } }
    }
    closedir(d); return found;
}

static int looks_heap(uint32_t p) { return p >= 0x09000000u && p < 0x50000000u && (p & 3) == 0; }

static void hexdump(FILE *log, const char *tag, uint32_t addr, const unsigned char *buf, uint32_t n)
{
    fprintf(log, "%s @0x%08x:\n", tag, addr);
    for (uint32_t i = 0; i < n; i += 32) {
        fprintf(log, "%04x  ", i);
        for (uint32_t j = 0; j < 32 && i + j < n; j++) fprintf(log, "%02x", buf[i + j]);
        fputc('\n', log);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: cfxz-peek <logfile> [seconds] [interval_ms]\n"); return 2; }
    int seconds = argc > 2 ? atoi(argv[2]) : 120;
    long interval_ms = argc > 3 ? atol(argv[3]) : 700;

    FILE *log = fopen(argv[1], "w");
    if (!log) { fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    fprintf(log, "cfxz-peek v2: mixer object + pointed buffers (read-only)\n");

    pid_t pid = find_rbp();
    if (!pid) { fprintf(log, "player process 'rbp' not found\n"); fclose(log); return 1; }
    fprintf(log, "rbp pid = %d\n", (int) pid);

    char mempath[64];
    snprintf(mempath, sizeof mempath, "/proc/%d/mem", (int) pid);
    unsigned char obj[OBJ_DUMP], pbuf[PTR_DUMP];

    time_t start = time(0);
    int sample = 0;
    while (time(0) - start < seconds) {
        if (ptrace(PTRACE_ATTACH, pid, 0, 0) == 0) {
            int st; waitpid(pid, &st, 0);
            int memfd = open(mempath, O_RDONLY);
            uint32_t uiobj = 0, mixer = 0, ptrs[MAX_PTRS]; int nptr = 0, ok = 0;
            if (memfd >= 0) {
                if (read_mem(memfd, GLOBAL_UIOBJ, &uiobj, 4) == 0 && looks_heap(uiobj) &&
                    read_mem(memfd, uiobj + OFF_MIXER, &mixer, 4) == 0 && looks_heap(mixer) &&
                    read_mem(memfd, mixer, obj, OBJ_DUMP) == 0) {
                    ok = 1;
                    for (uint32_t o = 0; o + 4 <= PTR_SCAN && nptr < MAX_PTRS; o += 4) {
                        uint32_t p; memcpy(&p, obj + o, 4);
                        if (looks_heap(p)) ptrs[nptr++] = p;
                    }
                }
            }
            fprintf(log, "\n#%d t=%ld uiobj=0x%08x mixer=0x%08x %s\n",
                    sample, (long)(time(0)-start), uiobj, mixer, ok ? "" : "(read failed)");
            if (ok) {
                hexdump(log, "obj", mixer, obj, OBJ_DUMP);
                for (int i = 0; i < nptr; i++) {
                    if (read_mem(memfd, ptrs[i], pbuf, PTR_DUMP) == 0) {
                        char tag[32]; snprintf(tag, sizeof tag, "ptr[+0x%02x]", i * 4);
                        hexdump(log, tag, ptrs[i], pbuf, PTR_DUMP);
                    }
                }
            }
            if (memfd >= 0) close(memfd);
            ptrace(PTRACE_DETACH, pid, 0, 0);
            fflush(log); sample++;
        } else { fprintf(log, "ptrace attach failed (retry)\n"); fflush(log); }
        struct timespec ts = { interval_ms / 1000, (interval_ms % 1000) * 1000000L };
        nanosleep(&ts, 0);
    }
    fprintf(log, "\ndone: %d samples\n", sample);
    fclose(log);
    return 0;
}
