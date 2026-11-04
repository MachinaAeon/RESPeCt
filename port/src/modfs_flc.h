/* Bounded8-bit FLC image decoding. Format: compuphase.com/flic.htm.
 * Artwork timing is deliberately not interpreted here. The caller selects
 * a decoded frame from the original game's presentation state. */
#ifndef MODFS_FLC_H
#define MODFS_FLC_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
typedef struct { const uint8_t *p,*end; int ok; } ModFlcIn;
static unsigned flc_u8(ModFlcIn *s) {
    if(s->p==s->end){s->ok=0;return 0;}return *s->p++;
}
static unsigned flc_u16(ModFlcIn *s) { unsigned a=flc_u8(s);return a|(flc_u8(s)<<8); }
static uint32_t flc_le32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned flc_le16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static int flc_image(unsigned type,ModFlcIn *s,uint8_t *pixels,unsigned w,unsigned h,uint8_t pal[256][3]) {
    if(type==4 || type==11) {
        unsigned packets=flc_u16(s),index=0;
        for(unsigned i=0;i<packets && s->ok;i++) {
            index+=flc_u8(s);unsigned count=flc_u8(s);if(!count)count=256;
            if(index>256 || count>256-index)return 0;
            for(unsigned j=0;j<count;j++)for(unsigned c=0;c<3;c++) {
                unsigned v=flc_u8(s);if(type==11 && v>63)return 0;
                pal[index+j][c]=(uint8_t)(type==11?v<<2:v);
            }
            index+=count;
        }
    } else if(type==13)memset(pixels,0,(size_t)w*h);
    else if(type==16) {
        size_t n=(size_t)w*h;if((size_t)(s->end-s->p)!=n)return 0;
        memcpy(pixels,s->p,n);s->p+=n;
    } else if(type==15) {
        for(unsigned y=0;y<h && s->ok;y++) {
            (void)flc_u8(s);unsigned x=0;
            while(x<w && s->ok) {
                int count=(int8_t)flc_u8(s);unsigned run=(unsigned)(count<0?-count:count);
                if(!run || run>w-x)return 0;
                if(count>0){uint8_t v=(uint8_t)flc_u8(s);memset(pixels+(size_t)y*w+x,v,run);}
                else for(unsigned i=0;i<run;i++)pixels[(size_t)y*w+x+i]=(uint8_t)flc_u8(s);
                x+=run;
            }
        }
    } else if(type==12) {
        unsigned y=flc_u16(s),lines=flc_u16(s);if(y>h || lines>h-y)return 0;
        for(unsigned line=0;line<lines && s->ok;line++,y++) {
            unsigned packets=flc_u8(s),x=0;
            for(unsigned i=0;i<packets && s->ok;i++) {
                unsigned skip=flc_u8(s);int count=(int8_t)flc_u8(s);unsigned run=(unsigned)(count<0?-count:count);
                if(skip>w-x)return 0;x+=skip;if(run>w-x)return 0;
                if(count<0){uint8_t v=(uint8_t)flc_u8(s);memset(pixels+(size_t)y*w+x,v,run);}
                else for(unsigned j=0;j<run;j++)pixels[(size_t)y*w+x+j]=(uint8_t)flc_u8(s);
                x+=run;
            }
        }
    } else if(type==7) {
        unsigned lines=flc_u16(s),y=0;if(lines>h)return 0;
        while(lines && s->ok) {
            unsigned code=flc_u16(s);if(y>=h)return 0;
            if((code&0xC000)==0xC000){unsigned skip=(unsigned)-(int16_t)code;if(skip>h-y)return 0;y+=skip;continue;}
            if((code&0xC000)==0x8000){pixels[(size_t)y*w+w-1]=(uint8_t)code;continue;}
            if(code&0xC000)return 0;
            unsigned x=0;
            for(unsigned i=0;i<code && s->ok;i++) {
                unsigned skip=flc_u8(s);int count=(int8_t)flc_u8(s);unsigned run=2u*(unsigned)(count<0?-count:count);
                if(skip>w-x)return 0;x+=skip;if(run>w-x)return 0;
                if(count<0) {
                    uint8_t a=(uint8_t)flc_u8(s),b=(uint8_t)flc_u8(s);
                    for(unsigned j=0;j<run;j+=2){pixels[(size_t)y*w+x+j]=a;pixels[(size_t)y*w+x+j+1]=b;}
                }else for(unsigned j=0;j<run;j++)pixels[(size_t)y*w+x+j]=(uint8_t)flc_u8(s);
                x+=run;
            }
            y++;lines--;
        }
    } else if(type==18) { s->p=s->end; } /* bounded non-displayed thumbnail */
    else return 0;
    /* Some encoders pad an odd-sized packet to an even chunk boundary. */
    return s->ok && (s->p==s->end || (s->end-s->p==1 && *s->p==0));
}
static uint8_t *modfs_flc_decode(const uint8_t *d,size_t size,unsigned w,unsigned h,unsigned frames) {
    if(!d || size<128 || size>8u<<20 || !w || !h || !frames || frames>256 ||
       (uint64_t)w*h*frames*4>64u<<20 || flc_le32(d)!=size || flc_le16(d+4)!=0xAF12 ||
       flc_le16(d+6)!=frames || flc_le16(d+8)!=w || flc_le16(d+10)!=h || flc_le16(d+12)!=8)return NULL;
    size_t pixels_n=(size_t)w*h;uint8_t *pixels=calloc(pixels_n,1),*out=malloc(pixels_n*frames*4);
    if(!pixels || !out){free(pixels);free(out);return NULL;}
    uint8_t pal[256][3]={{0}};size_t pos=128;unsigned index=0;int good=1;
    while(pos<size && good) {
        if(size-pos<6){good=0;break;}
        uint32_t n=flc_le32(d+pos);unsigned type=flc_le16(d+pos+4);
        if(n<6 || n>size-pos){good=0;break;}
        if(type==0xF100 && !index){pos+=n;continue;}
        if(type!=0xF1FA || n<16 || index>frames){good=0;break;}
        unsigned chunks=flc_le16(d+pos+6);size_t p=pos+16,end=pos+n;
        for(unsigned c=0;c<chunks && good;c++) {
            if(end-p<6){good=0;break;}
            uint32_t z=flc_le32(d+p);if(z<6 || z>end-p){good=0;break;}
            ModFlcIn in={d+p+6,d+p+z,1};good=flc_image(flc_le16(d+p+4),&in,pixels,w,h,pal);p+=z;
        }
        if(p!=end)good=0;
        if(good && index<frames)for(size_t i=0;i<pixels_n;i++) {
            uint8_t *q=out+((size_t)index*pixels_n+i)*4;memcpy(q,pal[pixels[i]],3);q[3]=(q[0]|q[1]|q[2])?255:0;
        }
        index++;pos=end;
    }
    free(pixels);if(!good || pos!=size || index<frames){free(out);return NULL;}return out;
}
#endif
