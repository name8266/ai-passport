#include "pusher_photo.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define PHOTO_HOT IRAM_ATTR
#else
#define PHOTO_HOT
#endif
typedef uint32_t alias_u32 __attribute__((__may_alias__));
typedef uint16_t alias_u16 __attribute__((__may_alias__));
bool PHOTO_HOT cp_photo_decode(const uint8_t *source,size_t source_size,uint8_t *output,size_t output_size){
    const uint8_t *in=source,*end=source+source_size;
    uint8_t *out=output,*limit=output+output_size;
    while(in<end){unsigned token=*in++;size_t length=token>>4;
        if(length==15){unsigned byte;do{if(in==end)return false;byte=*in++;length+=byte;if(length>(size_t)(limit-out))return false;}while(byte==255);}
        if(length>(size_t)(end-in)||length>(size_t)(limit-out))return false;
        if(length>=16){memcpy(out,in,length);out+=length;in+=length;}
        else while(length--)*out++=*in++;
        if(in==end)return out==limit;
        if((size_t)(end-in)<2)return false;
        unsigned offset=in[0]|((unsigned)in[1]<<8);in+=2;
        if(!offset||offset>(size_t)(out-output))return false;
        length=(token&15)+4;
        if((token&15)==15){unsigned byte;do{if(in==end)return false;byte=*in++;length+=byte;if(length>(size_t)(limit-out))return false;}while(byte==255);}
        if(length>(size_t)(limit-out))return false;
        if(offset==1){memset(out,out[-1],length);out+=length;}
        else if(offset==2){
            if(((uintptr_t)out&1)&&length){*out=out[-2];++out;--length;}
            uint16_t pair=(uint16_t)(out[-2]|((uint16_t)out[-1]<<8));
            if(((uintptr_t)out&3)&&length>=2){*(alias_u16 *)out=pair;out+=2;length-=2;}
            uint32_t word=pair|((uint32_t)pair<<16);
            while(length>=8){((alias_u32 *)out)[0]=word;((alias_u32 *)out)[1]=word;out+=8;length-=8;}
            while(length>=2){*(alias_u16 *)out=pair;out+=2;length-=2;}
            if(length){*out=out[-2];++out;}
        }else{
            if(!(offset&3)){
                while(((uintptr_t)out&3)&&length){*out=out[-(int)offset];++out;--length;}
                while(length>=4){*(alias_u32 *)out=*(const alias_u32 *)(out-offset);out+=4;length-=4;}
            }
            while(length--){*out=out[-(int)offset];++out;}
        }
    }
    return out==limit;
}
