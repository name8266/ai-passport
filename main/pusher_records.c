#include "pusher_records.h"
#include <string.h>
static uint32_t crc(const void *data,size_t n){const uint8_t *p=data;uint32_t c=~0u;
 while(n--){c^=*p++;for(unsigned k=0;k<8;++k)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
void cp_records_seal(cp_records_t *b){b->checksum=crc(b,offsetof(cp_records_t,checksum));}
void cp_records_init(cp_records_t *b){memset(b,0,sizeof(*b));b->magic=CP_RECORD_MAGIC;b->next_sequence=1;cp_records_seal(b);}
static bool ahead(const cp_record_t *a,const cp_record_t *b){
 if(!b->sequence){return a->sequence!=0;}
 if(!a->sequence){return false;}
 if(a->chips!=b->chips)return a->chips>b->chips;
 if(a->level!=b->level)return a->level>b->level;
 if(a->paid!=b->paid){return a->paid<b->paid;}
 return a->sequence<b->sequence;
}
bool cp_records_valid(const cp_records_t *b){
 if(b->magic!=CP_RECORD_MAGIC||!b->next_sequence||b->discovered&0xf0000000u||b->checksum!=crc(b,offsetof(cp_records_t,checksum)))return false;
 for(unsigned k=0;k<CP_RANK_COUNT;++k){const cp_record_t *r=&b->rows[k];if(r->chips>CP_CHIP_LIMIT||r->level>99||r->sequence>=b->next_sequence)return false;if(k&&ahead(r,&b->rows[k-1]))return false;}
 return true;
}
unsigned cp_record_level(const cp_record_t *r,unsigned id){return id<CP_RELIC_COUNT?(r->build[id/4]>>((id%4)*2))&3:0;}
bool cp_records_add(cp_records_t *b,const cp_game_t *g){
 if(!g->rogue.active||!g->rogue.finished||g->rogue.practice)return false;
 cp_record_t row={0};row.seed=g->rogue.seed;row.chips=g->score;row.sequence=b->next_sequence++;if(!b->next_sequence)b->next_sequence=1;
 row.paid=(uint16_t)(g->drops>65535?65535:g->drops);row.seconds=(uint16_t)(g->game_time>65535?65535:g->game_time);row.level=(uint8_t)g->rogue.level;
 for(unsigned k=0;k<CP_RELIC_COUNT;++k)row.build[k/4]|=(uint8_t)(g->rogue.relic[k]<<((k%4)*2));
 bool added=false;for(unsigned k=0;k<CP_RANK_COUNT;++k)if(ahead(&row,&b->rows[k])){memmove(&b->rows[k+1],&b->rows[k],(CP_RANK_COUNT-k-1)*sizeof(row));b->rows[k]=row;added=true;break;}
 b->discovered|=g->rogue.discovered;++b->generation;cp_records_seal(b);return added;
}
