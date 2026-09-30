#include "battery_protocol.h"
#include <string.h>

bool battery_payload_safe(const char *data,size_t length) {
    if (!length || length>1536 || memchr(data,0,length)) return false;
    bool string=false,escape=false; unsigned depth=0;
    for (size_t i=0;i<length;++i) {
        unsigned char c=(unsigned char)data[i];
        if (string) {
            if (escape) {
                /* cJSON stores strings as NUL-terminated data, so a decoded NUL
                 * cannot be represented safely. Reject it before conversion. */
                if (c=='u' && i+4<length && !memcmp(data+i+1,"0000",4)) return false;
                escape=false;
            } else if (c=='\\') escape=true;
            else if (c=='"') string=false;
            else if (c<32) return false;
        } else if (c=='"') string=true;
        else if (c=='{' || c=='[') { if (++depth>4) return false; }
        else if (c=='}' || c==']') { if (!depth) return false; --depth; }
    }
    return !string && !escape && !depth;
}
