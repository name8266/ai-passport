#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../firmware/main/hub_protocol.h"

int main(void) {
    hub_event_t e={0};
    uint8_t event[]={0,0,4,2,0x78,0x56,0x34,0x12};
    assert(hub_event_parse(event,8,&e));
    assert(e.uid==0x12345678 && e.category==4 && e.event==0);
    assert(!hub_event_parse(event,7,&e));
    event[0]=7;assert(!hub_event_parse(event,8,&e));
    event[0]=0;
    const uint8_t p[]={
        0,0x78,0x56,0x34,0x12,
        0,6,0,'W','e','C','h','a','t',
        1,6,0,0xE5,0xBE,0xAE,0xE4,0xBF,0xA1,
        3,5,0,'h','e','l','l','o'
    };
    hub_decoder_t d;hub_notice_t n={0};
    hub_decoder_begin(&d,e.uid);
    for(size_t i=0;i<sizeof(p)-1;i++)
        assert(!hub_decoder_feed(&d,p+i,1,&n));
    assert(hub_decoder_feed(&d,p+sizeof(p)-1,1,&n));
    assert(strcmp(n.app,"WeChat")==0 && strcmp(n.body,"hello")==0);
    assert(strcmp(n.title,"微信")==0);
    hub_decoder_begin(&d,9);assert(!hub_decoder_feed(&d,p,sizeof(p),&n));
    char small[4];
    hub_utf8_copy(small,sizeof(small),(const uint8_t *)"微信",6);
    assert(strcmp(small,"微")==0);
    hub_decoder_begin(&d,e.uid);
    assert(!hub_decoder_feed(&d,p,1,&n));
    assert(!hub_decoder_feed(&d,p,SIZE_MAX,&n));
    assert(d.used==0);
    assert(hub_decoder_feed(&d,p,sizeof(p),&n));
    const uint8_t invalid[]={0xED,0xA0,0x80};
    hub_utf8_copy(small,sizeof(small),invalid,sizeof(invalid));
    assert(small[0]==0);
    for(size_t split=1;split<sizeof(p);split++) {
        hub_decoder_begin(&d,e.uid);
        assert(!hub_decoder_feed(&d,p,split,&n));
        assert(hub_decoder_feed(&d,p+split,sizeof(p)-split,&n));
    }
    puts("ANCS parser fragmentation, UTF-8 and overflow: PASS");
    return 0;
}
