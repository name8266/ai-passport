/* White-box tests bind the exact narrow-phase implementation, including its
 * integer edge axes and cache acceptance. They do not replace gameplay tests. */
#include "../main/pusher_physics.c"
#include <assert.h>
#include <stdio.h>
static cp_game_t game;
static int64_t length2(gv3 n){return (int64_t)n.x*n.x+(int64_t)n.y*n.y+(int64_t)n.z*n.z;}
static void compact_contacts(void){
 _Static_assert(sizeof(contact_t)==40,"Dense contacts must retain their 40-byte budget");
 cp_init(&game,917,40,0,1);memset(game.coins,0,sizeof(game.coins));
 const unsigned high[]={255,256,CP_MAX_COINS-1};uint32_t keys[4]={0};
 for(unsigned k=0;k<3;++k){contact_t c={.a=0,.b=(int16_t)high[k],.slot=3};keys[k]=contact_key(&c);}
 contact_t old_collision={.a=1,.b=-1,.slot=3};keys[3]=contact_key(&old_collision);
 for(unsigned i=0;i<4;++i)for(unsigned j=0;j<i;++j)assert(keys[i]!=keys[j]);
 cp_spawn(&game,CP_COIN_GOLD,cp_v(104,130,1.3f),(cp_q4){1,0,0,0});
 game.coins[255]=game.coins[287]=game.coins[0];game.coins[255].p.z=3.9f;game.coins[287].p.x=150;
 fixed_body(0,&game.coins[0],false);fixed_body(255,&game.coins[255],false);fixed_body(287,&game.coins[287],false);
 gv3 axis;int32_t depth;assert(pair_overlap(&game,0,255,&axis,&depth));assert(!pair_overlap(&game,0,287,&axis,&depth));
 uint32_t random=11728;
 for(unsigned k=0;k<10000;++k){random=random*1664525u+1013904223u;
  fv3 full_normal=unit(fv((int)(random&4095)-2048,(int)((random>>12)&4095)-2048,(int)(random>>24)-128));
  gv3 n=gv(full_normal.x>>4,full_normal.y>>4,full_normal.z>>4);
  if(k%5==0)n=gv(0,0,k&1?4096:-4096);
  fv3 normal=fv(n.x*16,n.y*16,n.z*16),t1,t2;
  if(!n.x&&!n.y){t1=fv(n.z>0?65536:-65536,0,0);t2=fv(0,65536,0);}
  else{t1=unit(giabs(n.z)>2867?fv(normal.z,0,-normal.x):fv(normal.y,-normal.x,0));t2=cross(normal,t1);}
  int32_t second=giabs(n.z)>2867?t1.z:t1.y;
  contact_t c={.n={(int16_t)(n.x*4),(int16_t)(n.y*4),(int16_t)(n.z*4)},.tangent_a=(int16_t)(t1.x>>2),.tangent_b=(int16_t)(second>>2),.tangent_bits=(uint8_t)((t1.x&3)|((second&3)<<2))};
  packed_v3 a,b,expected_a=pack_axis(t1),expected_b=pack_axis(t2);contact_basis(&c,&a,&b);
  assert(!memcmp(&a,&expected_a,sizeof(a))&&!memcmp(&b,&expected_b,sizeof(b)));
 }
 puts("40-byte contact budget / 10,000 exact tangent reconstructions / body indices 255..287: PASS");
}
static void streamed_broadphase(void){
 uint32_t random=71922;cp_init(&game,71922,40,0,1);
 for(unsigned trial=0;trial<500;++trial){
  for(unsigned i=0;i<CP_MAX_COINS;++i){
   random=random*1664525u+1013904223u;game.coins[i].active=(random&7)!=0;
   s_geometry[i].p.x=(int)(random%100000)-50000;
   s_geometry[i].bound.x=1+(random>>16)%8192;
   if(trial%4==0)s_geometry[i].p.x=0;
  }
  broadphase(&game);
  for(unsigned i=0;i<CP_MAX_COINS;++i)if(game.coins[i].active){
   broad_row(&game,i);
   for(unsigned j=0;j<CP_MAX_COINS;++j){
    bool expected=j>i&&game.coins[j].active&&
     giabs(s_geometry[i].p.x-s_geometry[j].p.x)<s_geometry[i].bound.x+s_geometry[j].bound.x+24;
    assert(!!(s_candidate[j/32]&(1u<<(j%32)))==expected);
   }
  }
 }
 puts("Streamed broadphase / 500 randomized populations versus exhaustive overlap: PASS");
}
int main(void){streamed_broadphase();compact_contacts();
 for(int x=1;x<64;++x)for(int z=-63;z<64;++z){gv3 normal=capsule_normal(x,z,root32((uint32_t)(x*x+z*z)));assert(length2(normal)>16700000&&length2(normal)<16900000);}
 uint32_t random=20278729;
 for(unsigned k=0;k<1000000;++k){random=random*1664525u+1013904223u;uint32_t den=1+random%33554432u;random=random*1664525u+1013904223u;int32_t n=(int32_t)(random%(den+1));if(k&1)n=-n;assert(axis_ratio(n,den,UINT32_MAX/den)==(int32_t)((int64_t)n*4096/den));}

 for(int x=1;x<80;++x)for(int y=1;y<80;++y){
  gv3 n=edge_cross(gv(4096,0,0),gv(4096,x,y));int64_t n2=length2(n);assert(n2>=16777216&&n2<=201326592);
  geometry_t a={.n=5,.half={2048,2048,333},.axis={{4096,0,0},{0,4096,0},{0,0,4096}}},b=a;
  gv3 normal={0};int32_t depth=INT32_MAX;
  assert(!local_separates(&a,&b,gv(0,0,128),n,n,-1,-1,false,&depth,&normal));
  assert(length2(normal)>16700000&&length2(normal)<16850000);
 }
 cp_init(&game,20278729,30,0,1);memset(game.coins,0,sizeof(game.coins));game.steps=729;
 cp_spawn(&game,CP_COIN_GOLD,cp_v(104,130,1.3f),(cp_q4){1,0,0,0});cp_spawn(&game,CP_COIN_GOLD,cp_v(104,130,3.5f),(cp_q4){1,0,0,0});
 fixed_body(0,&game.coins[0],false);fixed_body(1,&game.coins[1],false);
 uint16_t key=2;pair_cache_t *cache=&s_pair_cache[((uint32_t)key*2654435761u)>>23];
 *cache=(pair_cache_t){key,728,{2048,6144,2048},{0,0,563}};
 gv3 n;int32_t depth;assert(pair_overlap(&game,0,1,&n,&depth));assert(!game.cache_hits&&game.sat_calls==1);assert(length2(n)>16700000&&length2(n)<16850000&&depth<128);
 /* Even a valid unit axis cannot reuse a deep intersection as an MTD. */
 *cache=(pair_cache_t){key,728,{4096,0,0},{0,0,563}};assert(pair_overlap(&game,0,1,&n,&depth));assert(!game.cache_hits&&depth<128);
 /* Exercise the depth guard with a valid cap normal and unchanged pose. */
 game.coins[1].p.z=1.8f;fixed_body(1,&game.coins[1],false);
 *cache=(pair_cache_t){key,728,{0,0,4096},{0,0,128}};
 assert(pair_overlap(&game,0,1,&n,&depth));assert(!game.cache_hits&&depth>128);
 /* A cached separating direction must be re-proved for the current bodies. */
 game.coins[1].p=cp_v(130,130,1.3f);fixed_body(1,&game.coins[1],false);
 *cache=(pair_cache_t){(key|0x80000000u),0,{4096,0,0},{0,0,0}};
 unsigned calls=game.sat_calls,hits=game.cache_hits;
 assert(!pair_overlap(&game,0,1,&n,&depth));assert(game.sat_calls==calls&&game.cache_hits==hits+1);
 game.coins[1].p=cp_v(104,130,3.5f);fixed_body(1,&game.coins[1],false);
 assert(pair_overlap(&game,0,1,&n,&depth));assert(game.sat_calls==calls+1);
 /* Differential moving/rotating pairs exercise accepted and invalidated axes. */
 for(unsigned k=0;k<20000;++k){random=random*1664525u+1013904223u;
  game.coins[1].p=cp_v(104+(int)(random%1000)/40.0f,130+(int)((random>>10)%1000)/50.0f,1.3f+(random>>20)%12);
  game.coins[1].q=normalize((cp_q4){1,(int)(random%61)/300.0f,(int)((random>>8)%61)/300.0f,(int)((random>>16)%61)/100.0f});cp_body_refresh(&game.coins[1]);fixed_body(1,&game.coins[1],false);
  gv3 exact;int32_t exact_depth;bool expected=overlap(&s_geometry[0],&s_geometry[1],&exact,&exact_depth);
  assert(pair_overlap(&game,0,1,&n,&depth)==expected);++game.steps;
 }
 puts("Near-parallel SAT axes / non-unit cached normal / deep-cache rejection / current separation certificates: PASS");
}
