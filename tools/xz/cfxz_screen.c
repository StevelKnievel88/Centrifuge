/* cfxz-screen - Centrifuge screen prototype for the XDJ-XZ.
 *
 * Draws the Centrifuge UI to the framebuffer (/dev/fb0, 800x480 RGB565): a pre-rendered background with a live
 * spinning platter needle and a sweeping playhead drawn over it. It only writes pixels to the screen; it does not
 * touch audio, the mixer, the player or the firmware, and nothing persists (RAM-only; power off to get the stock
 * screen back). The background image is read from the path given as argv[1].
 *
 * usage: cfxz-screen <background.rgb565>
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

/* geometry taken from the rendered screen */
#define CX 128
#define CY 270
#define PR 88          /* needle length */
#define HUBR 26
#define WAVE_X0 15
#define WAVE_X1 785
#define WAVE_Y0 59
#define WAVE_Y1 149

static uint16_t *buf;   /* working frame */

static void px(int x, int y, uint16_t c){ if((unsigned)x<W && (unsigned)y<H) buf[y*W+x]=c; }
static void vline(int x, int y0, int y1, uint16_t c){ for(int y=y0;y<=y1;y++) px(x,y,c); }
static void thickline(int x0,int y0,int x1,int y1,uint16_t c,int t){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    for(;;){ for(int o=-t;o<=t;o++){ px(x0+o,y0,c); px(x0,y0+o,c);} if(x0==x1&&y0==y1)break;
        int e2=2*e; if(e2>=dy){e+=dy;x0+=sx;} if(e2<=dx){e+=dx;y0+=sy;} }
}

int main(int argc, char **argv){
    if(argc<2){ fprintf(stderr,"usage: cfxz-screen <background.rgb565>\n"); return 2; }
    int bfd=open(argv[1],O_RDONLY);
    if(bfd<0){ perror("background"); return 1; }
    static uint16_t bg[W*H];
    if(read(bfd,bg,sizeof bg)!=(ssize_t)sizeof bg){ fprintf(stderr,"bad background size\n"); return 1; }
    close(bfd);

    int fb=open("/dev/fb0",O_RDWR);
    if(fb<0){ perror("/dev/fb0"); return 1; }
    struct fb_var_screeninfo var; struct fb_fix_screeninfo fix;
    ioctl(fb,FBIOGET_VSCREENINFO,&var); ioctl(fb,FBIOGET_FSCREENINFO,&fix);
    int stride = fix.line_length ? fix.line_length : W*2;      /* bytes per row on the device */
    size_t maplen = (size_t)stride*var.yres_virtual;
    uint8_t *fbm = mmap(0,maplen,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);
    if(fbm==MAP_FAILED){ perror("mmap"); return 1; }

    buf=malloc(sizeof(uint16_t)*W*H);
    uint16_t cyan=RGB565(0x2e,0xe6,0xd6), amber=RGB565(0xff,0xd3,0x39), amb2=RGB565(0x6a,0x58,0x18);

    double theta=0; int phase=0;
    for(;;){
        memcpy(buf,bg,sizeof bg);
        /* spinning needle (cyan) over the platter */
        int nx=CX+(int)(cos(theta)*(PR)), ny=CY+(int)(sin(theta)*(PR));
        int hx=CX+(int)(cos(theta)*HUBR), hy=CY+(int)(sin(theta)*HUBR);
        thickline(hx,hy,nx,ny,cyan,1);
        /* a little counterweight dot */
        px(CX-(int)(cos(theta)*HUBR),CY-(int)(sin(theta)*HUBR),cyan);
        /* sweeping playhead on the waveform */
        int phx=WAVE_X0 + (phase % (WAVE_X1-WAVE_X0));
        vline(phx,WAVE_Y0,WAVE_Y1,amber);
        vline(phx-1,WAVE_Y0,WAVE_Y1,amb2); vline(phx+1,WAVE_Y0,WAVE_Y1,amb2);
        /* present: copy to the framebuffer row by row (respect the device stride) */
        for(int y=0;y<H && y<(int)var.yres;y++)
            memcpy(fbm + (size_t)y*stride, buf + y*W, W*2);

        theta += 0.14; if(theta>6.2831853) theta-=6.2831853;
        phase += 5;
        struct timespec ts={0,40000000L}; nanosleep(&ts,0);   /* ~25 fps */
    }
}
