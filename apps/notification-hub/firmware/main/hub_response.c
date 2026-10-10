#include "hub_response.h"
#include <stdlib.h>
#include <string.h>
bool hub_response_append(hub_response_t *r,const void *data,size_t length) {
    if(!r || r->overflow) return false;
    if(!length) return true;
    if(!data || r->used>=r->cap || length>=r->cap-r->used) {
        r->overflow=true;return false;
    }
    if(!r->data) {
        r->data=calloc(1,r->cap);
        if(!r->data) {r->overflow=true;return false;}
    }
    memcpy(r->data+r->used,data,length);r->used+=length;
    r->data[r->used]=0;return true;
}
void hub_response_release(hub_response_t *r) {
    if(!r) return;
    if(r->data) {
        volatile unsigned char *bytes=(volatile unsigned char *)r->data;
        for(size_t i=0;i<r->cap;i++)bytes[i]=0;
        free(r->data);
    }
    r->data=NULL;r->used=0;r->overflow=false;
}
