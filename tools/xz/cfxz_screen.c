/* cfxz-screen - Centrifuge screen prototype for the XDJ-XZ (read-only except framebuffer pixels).
 *
 * Draws the Centrifuge UI to the framebuffer with a live spinning platter + sweeping playhead. Writes to every
 * buffer page of /dev/fb0 (the stock app may double-buffer), and logs the framebuffer geometry so we can see what
 * the hardware actually reports. Touches nothing but screen pixels; RAM-only.
 *
 * usage: cfxz-screen <background.rgb565> [logfile]
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
#include <sys/mman.h>
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

int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"usage: cfxz-screen <bg.rgb565> [log]\n"); return 2; }
    FILE *log = argc>2 ? fopen(argv[2],"w") : NULL;
    #define LOG(...) do{ if(log){fprintf(log,__VA_ARGS__);fflush(log);} }while(0)
    LOG("cfxz-screen starting\n");

    int bfd=open(argv[1],O_RDONLY);
    static uint16_t bg[W*H];
    if(bfd<0||read(bfd,bg,sizeof bg)!=(ssize_t)sizeof bg){ LOG("background read failed\n"); return 1; }
    close(bfd);

    int fb=-1;
    for(int tries=0; tries<120 && fb<0; tries++){ fb=open("/dev/fb0",O_RDWR); if(fb<0){ struct timespec t={0,500000000L}; nanosleep(&t,0);} }
    if(fb<0){ LOG("could not open /dev/fb0\n"); return 1; }

    struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
    memset(&var,0,sizeof var); memset(&fix,0,sizeof fix);
    ioctl(fb,FBIOGET_VSCREENINFO,&var); ioctl(fb,FBIOGET_FSCREENINFO,&fix);
    int stride = fix.line_length ? fix.line_length : W*2;
    unsigned yv = var.yres_virtual ? var.yres_virtual : var.yres;
    if(yv<var.yres||yv==0) yv=var.yres?var.yres:H;
    unsigned nbuf = var.yres ? (yv/var.yres) : 1; if(nbuf<1) nbuf=1; if(nbuf>4) nbuf=4;
    LOG("fb0 xres=%u yres=%u xvirt=%u yvirt=%u bpp=%u line_length=%u xoff=%u yoff=%u smem_len=%u nbuf=%u\n",
        var.xres,var.yres,var.xres_virtual,var.yres_virtual,var.bits_per_pixel,fix.line_length,
        var.xoffset,var.yoffset,fix.smem_len,nbuf);

    size_t maplen=(size_t)stride*yv; if(fix.smem_len>maplen) maplen=fix.smem_len;
    uint8_t *fbm=mmap(0,maplen,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);
    if(fbm==MAP_FAILED){ LOG("mmap failed\n"); return 1; }
    LOG("mmap ok (%lu bytes). drawing.\n",(unsigned long)maplen);

    buf=malloc(sizeof(uint16_t)*W*H);
    uint16_t cyan=RGB565(0x2e,0xe6,0xd6), amber=RGB565(0xff,0xd3,0x39);
    int vr=var.yres?(int)var.yres:H;

    /* try to show buffer 0 */
    var.yoffset=0; var.xoffset=0; ioctl(fb,FBIOPAN_DISPLAY,&var);

    double theta=0; int phase=0;
    for(;;){
        memcpy(buf,bg,sizeof bg);
        int nx=CX+(int)(cos(theta)*PR), ny=CY+(int)(sin(theta)*PR);
        int hx=CX+(int)(cos(theta)*HUBR), hy=CY+(int)(sin(theta)*HUBR);
        line2(hx,hy,nx,ny,cyan);
        px(CX-(int)(cos(theta)*HUBR),CY-(int)(sin(theta)*HUBR),cyan);
        int phx=WAVE_X0+(phase%(WAVE_X1-WAVE_X0));
        vline(phx,WAVE_Y0,WAVE_Y1,amber);
        /* write into every buffer page, so whichever is scanned out shows Centrifuge */
        for(unsigned b=0;b<nbuf;b++){
            uint8_t *base=fbm+(size_t)b*vr*stride;
            for(int y=0;y<H&&y<vr;y++) memcpy(base+(size_t)y*stride, buf+y*W, W*2);
        }
        theta+=0.14; if(theta>6.2831853) theta-=6.2831853;
        phase+=5;
        struct timespec ts={0,10000000L}; nanosleep(&ts,0);
    }
}
