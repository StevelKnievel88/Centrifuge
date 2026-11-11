/* cfxz-screen - Centrifuge screen prototype for the XDJ-XZ (writes framebuffer pixels only).
 *
 * Draws the Centrifuge UI to /dev/fb0 with a live spinning platter + sweeping playhead. Uses plain writes
 * (lseek + write, the same way `cat > /dev/fb0` works on this display) rather than mmap, which this i.MX driver
 * does not scan out. Writes to every buffer page.
 *
 * The stock player (rbp) redraws the same single BG buffer continuously, which makes our screen flicker
 * (full-screen alternation between rbp and Centrifuge). To take clean ownership of the display we pause rbp's
 * drawing with SIGSTOP while our screen is up, and SIGCONT it again on exit. This is RAM-only and reversible:
 * a power cycle (or exit) restores the stock deck. Pass --no-freeze to leave rbp running (will flicker).
 *
 * We also probe the FG overlay plane (/dev/fb1) once and log its memory budget, so we can tell whether a
 * true coexisting overlay (Centrifuge on top, rbp still running underneath) is feasible later.
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
#include <sys/ioctl.h>
#include <linux/fb.h>

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
            if(strcmp(name,"rbp")==0){ found=atoi(e->d_name); } }
        fclose(f);
        if(found>0) break;
    }
    closedir(d);
    return found;
}

static int g_rbp=-1;      /* paused rbp pid, for the signal handler to resume */
static void resume_and_exit(int sig){ (void)sig; if(g_rbp>0) kill(g_rbp,SIGCONT); _exit(0); }

int main(int argc,char**argv){
    const char *bgpath=NULL, *logpath=NULL; int freeze=1;
    for(int i=1;i<argc;i++){
        if(strcmp(argv[i],"--no-freeze")==0) freeze=0;
        else if(!bgpath) bgpath=argv[i];
        else if(!logpath) logpath=argv[i];
    }
    if(!bgpath){ fprintf(stderr,"usage: cfxz-screen <bg.rgb565> [log] [--no-freeze]\n"); return 2; }
    FILE *log = logpath ? fopen(logpath,"w") : NULL;
    #define LOG(...) do{ if(log){fprintf(log,__VA_ARGS__);fflush(log);} }while(0)
    LOG("cfxz-screen (write mode, freeze=%d) starting\n",freeze);

    int bfd=open(bgpath,O_RDONLY);
    static uint16_t bg[W*H];
    if(bfd<0||read(bfd,bg,sizeof bg)!=(ssize_t)sizeof bg){ LOG("background read failed\n"); return 1; }
    close(bfd);

    /* --- probe the FG overlay plane (fb1) once: is a coexisting overlay feasible? --- */
    {
        int o=open("/dev/fb1",O_RDWR);
        if(o<0){ LOG("fb1 probe: open failed\n"); }
        else{
            struct fb_var_screeninfo v; struct fb_fix_screeninfo fx;
            memset(&v,0,sizeof v); memset(&fx,0,sizeof fx);
            ioctl(o,FBIOGET_VSCREENINFO,&v); ioctl(o,FBIOGET_FSCREENINFO,&fx);
            unsigned need=800u*480u*2u;
            LOG("fb1 probe: xres=%u yres=%u xvirt=%u yvirt=%u bpp=%u line_length=%u smem_len=%u  (800x480x2 needs %u -> %s)\n",
                v.xres,v.yres,v.xres_virtual,v.yres_virtual,v.bits_per_pixel,fx.line_length,fx.smem_len,need,
                fx.smem_len>=need?"FITS full-screen overlay":"too small for full-screen; max rows at 800w = see below");
            if(fx.smem_len>0) LOG("fb1 probe: max 800-wide rows that fit = %u (of 480)\n", fx.smem_len/(800u*2u));
            close(o);
        }
    }

    /* --- take ownership of the display: pause rbp so it stops redrawing fb0 --- */
    if(freeze){
        int pid=find_rbp();
        if(pid>0){ if(kill(pid,SIGSTOP)==0){ g_rbp=pid; LOG("paused rbp pid=%d (SIGSTOP) - display is ours\n",pid); }
                   else LOG("found rbp pid=%d but SIGSTOP failed (errno)\n",pid); }
        else LOG("rbp not found; drawing anyway (may flicker)\n");
        signal(SIGTERM,resume_and_exit); signal(SIGINT,resume_and_exit); signal(SIGHUP,resume_and_exit);
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
    LOG("fb0 xres=%u yres=%d xvirt=%u yvirt=%u bpp=%u line_length=%u yoff=%u smem_len=%u pages=%d\n",
        var.xres,yres,var.xres_virtual,var.yres_virtual,var.bits_per_pixel,fix.line_length,var.yoffset,fix.smem_len,pages);

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
        /* build one page with the device stride, then write it to each buffer page */
        for(int y=0;y<H && y<yres;y++) memcpy(page+(size_t)y*stride, buf+y*W, W*2);
        for(int p=0;p<pages;p++){
            if(lseek(fb,(off_t)p*pagebytes,SEEK_SET)==(off_t)-1){ if(!logged)LOG("lseek failed\n"); }
            ssize_t wr=write(fb,page,pagebytes);
            if(!logged){ LOG("first write: %ld bytes (page=%d, pagebytes=%d)\n",(long)wr,p,pagebytes); }
        }
        logged=1;
        /* when rbp is frozen nothing overwrites us, so we can idle slower (less CPU, no tearing) */
        theta+=0.14; if(theta>6.2831853) theta-=6.2831853;
        phase+=5;
        struct timespec ts = freeze ? (struct timespec){0,33000000L} : (struct timespec){0,16000000L};
        nanosleep(&ts,0);
    }
}
