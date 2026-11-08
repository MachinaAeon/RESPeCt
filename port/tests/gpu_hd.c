/* Asset-free tests of the production rasterizers, including D3D11 shader execution.
 * This supplies decoded images at the texpack_attach boundary; matching/loading
 * is tested separately in the game. No original game data is required. */
#include "render.h"
#include "render_gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
uint16_t g_vram[512][1024];
uint32_t g_frame;
static uint32_t pixels[16*16], pixels2[64*32];
static HdImg image={16,16,pixels}, image2={64,32,pixels2};
static int use_hd=1, which;
static int syncs;
void rt_log(const char *fmt, ...) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); fputc('\n',stderr); va_end(a); }
void texpack_attach(RPrim *p,int x,int y,int w,int h) {
    p->hd=NULL;
    if(use_hd){p->hd=which?&image2:&image; p->hd_kx=which?4:2; p->hd_ky=2; p->hd_ox=-4; p->hd_oy=-4;}
}
void texpack_fill(int x,int y,int w,int h) {}
void texpack_copy(int x,int y,int a,int b,int w,int h) {}
void texpack_upload(int x,int y,int w,int h) {}
void texpack_frame(void) { syncs++; }
void texpack_reload(void) {}
int display_holes(void){return 0;}
extern int g_true_color, g_smooth_tex;
#define ROI 64
#define MAXROI (ROI*4)
static uint16_t cpu[MAXROI*MAXROI], cpu_low[MAXROI*MAXROI], native[512][1024], original[512][1024];
static unsigned color(uint16_t hi,uint16_t lo,int k){int c=(hi>>(k*5))&31;return (lo&0x8000)?(c<<3)|((lo>>(k*3))&7):(c<<3)|(c>>2);}
static RPrim prim(int rect){
    RPrim p={0};p.kind=rect?RP_RECT:RP_TRI;p.textured=1;p.raw=1;p.depth=2;p.tx=512;p.ty=256;
    p.dx2=p.dy2=ROI-1;p.w=p.h=8;
    p.v[0]=(RVtx){4,4,128,128,128,2,2,100};p.v[1]=(RVtx){28,6,200,96,32,10,2,160};p.v[2]=(RVtx){6,29,32,192,240,2,10,220};
    return p;
}
static void scene(int scenario){
    render_fill(0,0,ROI,ROI,0x4210);render_zclear(0,0,ROI,ROI);
    for(int y=256;y<288;y++)for(int x=512;x<544;x++)g_vram[y][x]=(uint16_t)(0x1f|((x&1)?0x8000:0));
    RPrim p=prim(scenario<7); which=0;
    if(scenario==1){p.raw=0;p.v[0].r=64;p.v[0].g=200;p.v[0].b=96;}
    if(scenario==2){p.flipx=p.flipy=1;p.v[0].u=p.v[0].v=9;}
    if(scenario==3){p.sqx0=4.25f;p.sqx1=17.75f;}
    if(scenario==4){p.semi=1;p.abr=0;}
    if(scenario==5){p.semi=1;p.abr=2;p.mask_set=1;}
    if(scenario==6){p.v[0].u=p.v[0].v=0;}
    if(scenario>=8){p.zt=2;}
    if(scenario==9){p.gouraud=1;p.raw=0;}
    if(scenario==10){p.semi=1;p.abr=1;}
    if(scenario==11){p.semi=1;p.abr=3;}
    render_prim(&p);
    if(scenario==12){which=1;p.v[0].x+=30;p.v[1].x+=30;p.v[2].x+=30;render_prim(&p);which=0;p.v[0].y+=30;p.v[1].y+=30;p.v[2].y+=30;render_prim(&p);}
    render_sync();
}
static int run(int scenario,int scale,int tc,int hd){
    use_hd=hd;g_true_color=tc;
    render_set_gpu(0);render_set_scale(scale);scene(scenario);
    int st;const uint16_t *hi=render_hires(&st),*lo=render_low();int side=ROI*scale;
    for(int y=0;y<side;y++){memcpy(cpu+y*side,hi+y*st,side*2);memcpy(cpu_low+y*side,lo+y*st,side*2);}
    memcpy(native,g_vram,sizeof native);
    if(!hd)memcpy(original,g_vram,sizeof original);
    if(!render_set_gpu(1)||!render_on_gpu()){fprintf(stderr,"UNAVAILABLE: D3D11\n");return 2;}
    scene(scenario);render_want(0,0,side,side);hi=render_hires(&st);lo=render_low();
    int mismatches=0,max_error=0,alpha_errors=0;
    for(int y=0;y<side;y++)for(int x=0;x<side;x++){
        int a=y*side+x,b=y*st+x;if(cpu[a]==hi[b]&&cpu_low[a]==lo[b])continue;
        mismatches++;if((cpu[a]^hi[b])&0x8000)alpha_errors++;
        for(int k=0;k<3;k++){int e=abs((int)color(cpu[a],cpu_low[a],k)-(int)color(hi[b],lo[b],k));if(e>max_error)max_error=e;}
    }
    int native_ok=!memcmp(native,g_vram,sizeof native)&&(!hd||!memcmp(original,g_vram,sizeof original));
    /* CPU and shader can round a filtered float on opposite sides of a half.
     * Permit one 8-bit colour step only; 5-bit output and STP remain exact. */
    int ok=native_ok&&!alpha_errors&&(tc?max_error<=1:mismatches==0);
    printf("%s case=%d scale=%d true_color=%d hd=%d mismatches=%d max_rgb_error=%d stp_errors=%d native_equal=%d\n",ok?"PASS":"FAIL",scenario,scale,tc,hd,mismatches,max_error,alpha_errors,native_ok);
    return ok?0:1;
}
int main(void){
    for(int y=0;y<16;y++)for(int x=0;x<16;x++)pixels[y*16+x]=0xff000000u|(x*16u)|((y*16u)<<8)|(((x^y)*16u)<<16);
    for(int y=0;y<32;y++)for(int x=0;x<64;x++)pixels2[y*64+x]=0xff000000u|((x*3u)<<8)|(y*7u);
    /* Alpha class thresholds, including the exact boundary values. */
    const unsigned a[8]={0,63,64,128,191,192,254,255};
    for(int y=0;y<16;y++)for(int x=0;x<16;x++)pixels[y*16+x]=(pixels[y*16+x]&0xffffff)|(a[(x/2)]<<24);
    render_init(2,1);
    for(int s=2;s<=4;s++)for(int tc=0;tc<=1;tc++)for(int i=0;i<13;i++){
        int r=run(i,s,tc,0);if(r)return r;r=run(i,s,tc,1);if(r)return r;
    }
    /* A repeated source is shared inside the queue; renewed after the
     * synchronized cache boundary even if a freed object's address is reused. */
    uint32_t a0,a1,b0;
    if(!rgpu_hd_add(&image,&a0)||!rgpu_hd_add(&image,&a1)||a0!=a1||!rgpu_hd_add(&image2,&b0)||b0==a0)return 1;
    rgpu_hd_reset();
    for(int i=0;i<256;i++)pixels[i]=0xff803020;
    if(run(0,2,1,0)||run(0,2,1,1))return 1;
    HdImg invalid={0,1,pixels},huge={0x7fffffff,0x7fffffff,pixels};
    if(rgpu_hd_add(&invalid,&a0)||rgpu_hd_add(&huge,&a0))return 1;
    puts("PASS cache sharing, lifetime renewal and invalid-image fallback");
    printf("PASS complete: 158 CPU/GPU comparisons, native VRAM invariant, %d synchronization boundaries\n",syncs);
    return 0;
}
