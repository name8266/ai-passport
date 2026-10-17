#include "pusher_physics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static cp_game_t g;
static unsigned ctl;
static unsigned next(void){unsigned x=ctl;x^=x<<13;x^=x>>17;x^=x<<5;return ctl=x?x:1721;}
static const int preference[6][7]={{0,2,5,7,24,-1,-1},{1,3,4,25,14,-1,-1},{8,11,13,14,27,-1,-1},{15,16,17,18,21,-1,-1},{9,10,20,22,23,26,-1},{-1,-1,-1,-1,-1,-1,-1}};
static const unsigned pairs[8][2]={{1,3},{2,24},{8,11},{9,24},{0,5},{15,16},{10,26},{22,23}};
static unsigned pick(unsigned policy){unsigned best=0;int highest=-9999;
 for(unsigned k=0;k<3;++k){unsigned id=g.rogue.offer[k];int score=0;
  if(policy==5)score=(int)(next()%10000);
  else{for(unsigned j=0;j<7;++j)if(preference[policy][j]==(int)id)score+=2000;
   score+=g.rogue.relic[id]*300;
   for(unsigned j=0;j<8;++j)if((pairs[j][0]==id&&g.rogue.relic[pairs[j][1]])||(pairs[j][1]==id&&g.rogue.relic[pairs[j][0]]))score+=1200;
   score+=(int)(next()%200);}
  if(score>highest){highest=score;best=k;}
 }return best;
}
int main(int argc,char **argv){unsigned runs=argc>1?(unsigned)atoi(argv[1]):50;int solo=argc>2?atoi(argv[2]):-2;
 for(unsigned run=0;run<runs;++run){unsigned policy=run%6,seed=20261016u+(run+1)*17713u;ctl=seed^0x81e05aa1u;
  cp_init(&g,seed,30,0,1);cp_rogue_start(&g,0);g.rogue.practice=true;g.autoplay=true;
  unsigned shops=0,choices=0,steps=0,seen=0;float penetration=0;
  while(steps<1220*CP_HZ&&!g.rogue.finished){
   if(g.rogue.view==CP_VIEW_DRAFT){if(solo>=-1){if(g.rogue.level==1){memset(g.rogue.relic,0,sizeof(g.rogue.relic));if(solo>=0)for(unsigned n=0;n<3;++n)cp_rogue_grant(&g,(unsigned)solo);}g.rogue.pending=0;cp_rogue_view(&g,CP_VIEW_PLAY);continue;}unsigned k=pick(policy);seen|=1u<<g.rogue.offer[k];cp_rogue_choose(&g,k);++choices;continue;}
   if(g.rogue.view==CP_VIEW_SUPPLY){cp_tactics_supply(&g,run%3);continue;}
   if(policy>=2&&g.bank<=5&&shops<7+g.rogue.relic[CP_R_SHOP]&&g.score>=cp_rogue_price(&g)){if(!cp_rogue_buy(&g))return 3;++shops;}
   cp_step(&g,CP_DT);++steps;
   if(!cp_valid(&g)||g.overflow_steps){fprintf(stderr,"Invalid seed %u step %u\n",seed,steps);return 2;}
   if(g.max_penetration>penetration)penetration=g.max_penetration;
  }
  printf("{\"run\":%u,\"seed\":%u,\"policy\":%u,\"seconds\":%.2f,\"natural_end\":%s,\"chips\":%u,\"coins\":%u,\"paid\":%u,\"collected\":%u,\"prizes\":%u,\"lost\":%u,\"level\":%u,\"choices\":%u,\"seen\":%u,\"synergy\":%u,\"shops\":%u,\"peak_contacts\":%u,\"capacity_waits\":%u,\"max_penetration_mm\":%.3f,\"overflow\":%u,\"levels\":[",run+1,seed,policy,(double)g.game_time,g.rogue.finished?"true":"false",g.score,g.bank,g.drops,g.collected,g.prizes,g.lost,g.rogue.level,choices,seen,g.rogue.synergies,shops,g.peak_contacts,g.capacity_waits,(double)penetration,g.overflow_steps);
  for(unsigned k=0;k<28;++k)printf("%s%u",k?",":"",g.rogue.relic[k]);printf("],\"stage\":%u,\"won\":%s,\"casts\":%u,\"links\":%u,\"grown\":%u,\"end\":%u}\n",g.tactics.stage+1,g.tactics.won?"true":"false",g.tactics.casts,g.tactics.links,g.tactics.grown,g.tactics.end_reason);fflush(stdout);
 }
 return 0;
}
