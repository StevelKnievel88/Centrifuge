/* cfxz-screen - Centrifuge screen prototype for the XDJ-XZ (writes framebuffer pixels only).
 *
 * Draws the Centrifuge UI to /dev/fb0 with a live spinning platter + sweeping playhead. Uses plain writes
 * (lseek + write, the same way `cat > /dev/fb0` works on this display) rather than mmap, which this i.MX driver
 * does not scan out. Writes to every buffer page.
 *
 * The stock player (rbp) redraws the same single BG buffer continuously, which makes our screen flicker
 * (full-screen alternation between rbp and Centrifuge). The FG overlay plane (/dev/fb1) only has memory for
 * 800x288, so a full-screen coexisting overlay is impossible. To take full-screen clean ownership we pause
 * rbp's drawing with SIGSTOP. But rbp also handles the power button and USB eject, so while it is frozen the
 * deck cannot power off. We therefore run a power-button watcher: on a power press we sync, resume rbp
 * (SIGCONT) and stop drawing so rbp's own clean shutdown runs, with a forced power-off as a safety net.
 *
 * Everything is RAM-only and reversible: power off (now works) or pull power and the deck is back to stock.
 * Pass --no-freeze to leave rbp running (will flicker, but power/eject work normally).
 *
 * usage: cfxz-screen <background.rgb565> [logfile] [--no-freeze]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <ctype.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <linux/fb.h>
#include <linux/input.h>

#define W 800
#define H 480
#define RGB565(r,g,b) ((uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
#define CX 128
#define CY 270
#define PR 88
#define HUBR 26
#define WAVE_X0 15
#define WAVE_X1 785
#define WAVE_Y0 59
#define WAVE_Y1 149

static FILE *g_log=NULL;
#define LOG(...) do{ if(g_log){fprintf(g_log,__VA_ARGS__);fflush(g_log);} }while(0)

static uint16_t *buf;
static void px(int x,int y,uint16_t c){ if((unsigned)x<W&&(unsigned)y<H) buf[y*W+x]=c; }
static void vline(int x,int y0,int y1,uint16_t c){ for(int y=y0;y<=y1;y++) px(x,y,c); }
static void line2(int x0,int y0,int x1,int y1,uint16_t c){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;){ px(x0,y0,c); px(x0+1,y0,c); px(x0,y0+1,c); if(x0==x1&&y0==y1)break;
        int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}

/* --- find the player program (rbp) by scanning /proc/<pid>/comm --- */
static int find_rbp(void){
    DIR *d=opendir("/proc"); if(!d) return -1;
    struct dirent *e; int found=-1;
    while((e=readdir(d))){
        if(!isdigit((unsigned char)e->d_name[0])) continue;
        char path[64]; snprintf(path,sizeof path,"/proc/%s/comm",e->d_name);
        FILE *f=fopen(path,"r"); if(!f) continue;
        char name[64]={0}; if(fgets(name,sizeof name,f)){ char *nl=strchr(name,'\n'); if(nl)*nl=0;
            if(strcmp(name,"rbp")==0) found=atoi(e->d_name); }
        fclose(f);
        if(found>0) break;
    }
    closedir(d);
    return found;
}

/* --- log rbp's threads, so a future build can pause only the drawing thread (deck stays fully alive) --- */
static void log_rbp_threads(int pid){
    char dir[64]; snprintf(dir,sizeof dir,"/proc/%d/task",pid);
    DIR *d=opendir(dir); if(!d){ LOG("rbp threads: (none)\n"); return; }
    struct dirent *e; LOG("rbp threads (tid:comm):");
    while((e=readdir(d))){
        if(!isdigit((unsigned char)e->d_name[0])) continue;
        char p[96]; snprintf(p,sizeof p,"%s/%s/comm",dir,e->d_name);
        FILE *f=fopen(p,"r"); if(!f) continue;
        char nm[64]={0}; if(fgets(nm,sizeof nm,f)){ char *nl=strchr(nm,'\n'); if(nl)*nl=0; LOG(" %s:%s",e->d_name,nm); }
        fclose(f);
    }
    LOG("\n"); closedir(d);
}

/* --- power button watcher: find input devices that report KEY_POWER --- */
#define BPL (int)(sizeof(long)*8)
#define NB(x) (((x)-1)/BPL+1)
static int has_bit(const unsigned long*a,int b){ return (a[b/BPL]>>(b%BPL))&1UL; }

static int g_powfds[16]; static int g_npow=0;
static void scan_power_inputs(void){
    LOG("input devices:");
    for(int i=0;i<32;i++){
        char p[32]; snprintf(p,sizeof p,"/dev/input/event%d",i);
        int fd=open(p,O_RDONLY|O_NONBLOCK); if(fd<0) continue;
        char name[128]={0}; ioctl(fd,EVIOCGNAME(sizeof name),name);
        unsigned long keys[NB(KEY_MAX+1)]; memset(keys,0,sizeof keys);
        ioctl(fd,EVIOCGBIT(EV_KEY,sizeof keys),keys);
        int pw = has_bit(keys,KEY_POWER);
        LOG(" [event%d:%s%s]",i,name[0]?name:"?",pw?":POWER":"");
        if(pw && g_npow<16) g_powfds[g_npow++]=fd; else close(fd);
    }
    LOG("\n");
    LOG("power-capable inputs: %d\n",g_npow);
}

static int g_rbp=-1;
static void resume_and_exit(int sig){ (void)sig; if(g_rbp>0) kill(g_rbp,SIGCONT); _exit(0); }

/* child: wait for a power-button press, then hand shutdown back to rbp (with a forced power-off fallback) */
static void power_watch_loop(int parent){
    if(g_npow==0){ LOG("power watcher: no KEY_POWER input found; power button will not work while frozen\n"); _exit(0); }
    struct pollfd pf[16];
    for(int i=0;i<g_npow;i++){ pf[i].fd=g_powfds[i]; pf[i].events=POLLIN; }
    for(;;){
        int r=poll(pf,g_npow,-1); if(r<=0) continue;
        for(int i=0;i<g_npow;i++){
            if(!(pf[i].revents&POLLIN)) continue;
            struct input_event ev; ssize_t n;
            while((n=read(pf[i].fd,&ev,sizeof ev))==(ssize_t)sizeof ev){
                if(ev.type==EV_KEY && ev.code==KEY_POWER && ev.value==1){
                    LOG("power pressed - resuming rbp for clean shutdown\n");
                    sync();
                    if(g_rbp>0) kill(g_rbp,SIGCONT);   /* rbp wakes, reads the buffered power event, shuts down */
                    kill(parent,SIGTERM);              /* stop our renderer so rbp owns the screen */
                    for(int s=0;s<8;s++){ struct timespec t={1,0}; nanosleep(&t,0); }  /* give rbp ~8s */
                    sync(); reboot(RB_POWER_OFF);       /* safety net if rbp didn't power off */
                    _exit(0);
                }
            }
        }
    }
}

int main(int argc,char**argv){
    const char *bgpath=NULL, *logpath=NULL; int freeze=1;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--no-freeze")==0) freeze=0;
        else if(!bgpath) bgpath=argv[i];
        else if(!logpath) logpath=argv[i];
    }
    if(!bgpath){ fprintf(stderr,"usage: cfxz-screen <bg.rgb565> [log] [--no-freeze]\n"); return 2; }
    g_log = logpath ? fopen(logpath,"w") : NULL;
    LOG("cfxz-screen (write mode, freeze=%d) starting\n",freeze);

    int bfd=open(bgpath,O_RDONLY);
    static uint16_t bg[W*H];
    if(bfd<0||read(bfd,bg,sizeof bg)!=(ssize_t)sizeof bg){ LOG("background read failed\n"); return 1; }
    close(bfd);

    /* probe the FG overlay plane (fb1): confirms 800x288 budget */
    {
        int o=open("/dev/fb1",O_RDWR);
        if(o<0){ LOG("fb1 probe: open failed\n"); }
        else{
            struct fb_var_screeninfo v; struct fb_fix_screeninfo fx;
            memset(&v,0,sizeof v); memset(&fx,0,sizeof fx);
            ioctl(o,FBIOGET_VSCREENINFO,&v); ioctl(o,FBIOGET_FSCREENINFO,&fx);
            LOG("fb1 probe: xres=%u yres=%u yvirt=%u bpp=%u smem_len=%u (max 800-wide rows=%u)\n",
                v.xres,v.yres,v.yres_virtual,v.bits_per_pixel,fx.smem_len, fx.smem_len?fx.smem_len/1600u:0u);
            close(o);
        }
    }

    if(freeze){
        scan_power_inputs();                 /* must open the power input BEFORE rbp is frozen is fine either way */
        int pid=find_rbp();
        if(pid>0){
            log_rbp_threads(pid);
            if(kill(pid,SIGSTOP)==0){ g_rbp=pid; LOG("paused rbp pid=%d (SIGSTOP) - display is ours\n",pid); }
            else LOG("found rbp pid=%d but SIGSTOP failed\n",pid);
        } else LOG("rbp not found; drawing anyway (may flicker)\n");
        signal(SIGTERM,resume_and_exit); signal(SIGINT,resume_and_exit); signal(SIGHUP,resume_and_exit);
        /* fork the power watcher */
        int parent=getpid();
        pid_t w=fork();
        if(w==0){ power_watch_loop(parent); _exit(0); }
    }

    int fb=-1;
    for(int t=0;t<120 && fb<0;t++){ fb=open("/dev/fb0",O_RDWR); if(fb<0){ struct timespec s={0,500000000L}; nanosleep(&s,0);} }
    if(fb<0){ LOG("could not open /dev/fb0\n"); if(g_rbp>0)kill(g_rbp,SIGCONT); return 1; }

    struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
    memset(&var,0,sizeof var); memset(&fix,0,sizeof fix);
    ioctl(fb,FBIOGET_VSCREENINFO,&var); ioctl(fb,FBIOGET_FSCREENINFO,&fix);
    int stride = fix.line_length ? (int)fix.line_length : W*2;
    int yres = var.yres ? (int)var.yres : H;
    unsigned yv = var.yres_virtual>=(unsigned)yres ? var.yres_virtual : (unsigned)yres;
    int pages = yres ? (int)(yv/yres) : 1; if(pages<1)pages=1; if(pages>4)pages=4;
    LOG("fb0 xres=%u yres=%d yvirt=%u bpp=%u line_length=%u smem_len=%u pages=%d\n",
        var.xres,yres,var.yres_virtual,var.bits_per_pixel,fix.line_length,fix.smem_len,pages);

    buf=malloc(sizeof(uint16_t)*W*H);
    int pagebytes = stride*yres;
    uint8_t *page = malloc(pagebytes);
    uint16_t cyan=RGB565(0x2e,0xe6,0xd6), amber=RGB565(0xff,0xd3,0x39);

    double theta=0; int phase=0; int logged=0;
    for(;;){
        memcpy(buf,bg,sizeof bg);
        int nx=CX+(int)(cos(theta)*PR), ny=CY+(int)(sin(theta)*PR);
        int hx=CX+(int)(cos(theta)*HUBR), hy=CY+(int)(sin(theta)*HUBR);
        line2(hx,hy,nx,ny,cyan);
        px(CX-(int)(cos(theta)*HUBR),CY-(int)(sin(theta)*HUBR),cyan);
        int phx=WAVE_X0+(phase%(WAVE_X1-WAVE_X0));
        vline(phx,WAVE_Y0,WAVE_Y1,amber);
        for(int y=0;y<H && y<yres;y++) memcpy(page+(size_t)y*stride, buf+y*W, W*2);
        for(int p=0;p<pages;p++){
            if(lseek(fb,(off_t)p*pagebytes,SEEK_SET)==(off_t)-1){ if(!logged)LOG("lseek failed\n"); }
            ssize_t wr=write(fb,page,pagebytes);
            if(!logged){ LOG("first write: %ld bytes (page=%d, pagebytes=%d)\n",(long)wr,p,pagebytes); }
        }
        logged=1;
        theta+=0.14; if(theta>6.2831853) theta-=6.2831853;
        phase+=5;
        struct timespec ts = freeze ? (struct timespec){0,33000000L} : (struct timespec){0,16000000L};
        nanosleep(&ts,0);
    }
}
