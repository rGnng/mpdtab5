#pragma once
#include "coloplay_ble_protocol.h"

/* Fixed-size, bounded stream envelope. Arbitrary console output is ignored.
 * USB has no BLE MTU limit, but the existing 20-byte payload stays identical. */
enum { CP_USB_VERSION=1, CP_USB_FRAME_BYTES=32,
       CP_USB_PROBE=1, CP_USB_OFFER=2, CP_USB_HELLO=3,
       CP_USB_READY=4, CP_USB_DATA=5, CP_USB_PING=6 };
static inline uint32_t cp_usb_crc(const uint8_t *p,size_t n){
    uint32_t crc=0xffffffffu;
    while(n--){crc^=*p++;for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return ~crc;
}
static inline void cp_usb_encode(uint8_t *out,uint8_t kind,const uint8_t *payload){
    const uint8_t magic[4]={0xc7,0x50,0x35,0x55};
    memcpy(out,magic,4);out[4]=CP_USB_VERSION;out[5]=kind;out[6]=CP_BLE_BYTES;out[7]=0;
    if(payload)memcpy(out+8,payload,CP_BLE_BYTES);else memset(out+8,0,CP_BLE_BYTES);
    cp_put32(out+28,cp_usb_crc(out+4,24));
}
typedef struct {uint8_t bytes[CP_USB_FRAME_BYTES];unsigned used;uint32_t invalid;} cp_usb_parser_t;
/* Sliding window also recovers from truncated frames followed by a valid one. */
static inline bool cp_usb_feed(cp_usb_parser_t *s,uint8_t byte,uint8_t *kind,uint8_t *payload){
    const uint8_t magic[4]={0xc7,0x50,0x35,0x55};
    if(s->used<4){
        if(byte==magic[s->used])s->bytes[s->used++]=byte;
        else{s->used=byte==magic[0]?1:0;if(s->used)s->bytes[0]=byte;}
        return false;
    }
    s->bytes[s->used++]=byte;
    if(s->used<CP_USB_FRAME_BYTES)return false;
    if(s->bytes[4]==CP_USB_VERSION&&s->bytes[5]>=CP_USB_PROBE&&s->bytes[5]<=CP_USB_PING&&
       s->bytes[6]==CP_BLE_BYTES&&!s->bytes[7]&&cp_u32(s->bytes+28)==cp_usb_crc(s->bytes+4,24)){
        *kind=s->bytes[5];memcpy(payload,s->bytes+8,CP_BLE_BYTES);s->used=0;return true;
    }
    ++s->invalid;
    /* Keep the earliest complete marker or partial suffix already received. */
    unsigned at=1;
    for(;at<CP_USB_FRAME_BYTES;++at){unsigned n=CP_USB_FRAME_BYTES-at;if(n>4)n=4;if(!memcmp(s->bytes+at,magic,n))break;}
    s->used=CP_USB_FRAME_BYTES-at;if(s->used)memmove(s->bytes,s->bytes+at,s->used);
    return false;
}
