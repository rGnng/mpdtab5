#pragma once
#include <stddef.h>
#include <stdint.h>

/* Fixed-size metadata strings must remain valid UTF-8 for both LVGL and JSON.
 * Copy complete code points; replace malformed source bytes and never leave a
 * partial character at the end of a small title/artist/album buffer. */
static inline void text_copy(char *dst,size_t cap,const char *source)
{
    if(!cap) return;
    const unsigned char *s=(const unsigned char *)(source ? source : "");
    size_t used=0;
    while(*s && used+1<cap) {
        unsigned n=*s<0x80 ? 1 : *s>=0xc2 && *s<=0xdf ? 2 :
            *s>=0xe0 && *s<=0xef ? 3 : *s>=0xf0 && *s<=0xf4 ? 4 : 0;
        for(unsigned i=1;i<n;i++) if(!s[i] || (s[i]&0xc0)!=0x80) { n=0; break; }
        if(n==3 && ((*s==0xe0 && s[1]<0xa0) || (*s==0xed && s[1]>=0xa0))) n=0;
        if(n==4 && ((*s==0xf0 && s[1]<0x90) || (*s==0xf4 && s[1]>=0x90))) n=0;
        if(!n) { dst[used++]='?';s++;continue; }
        if(used+n>=cap) break;
        for(unsigned i=0;i<n;i++) dst[used++]=(char)*s++;
    }
    dst[used]=0;
}
