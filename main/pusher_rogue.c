#include "pusher_rogue.h"
#include "pusher_physics.h"
#include <string.h>
#include <stdio.h>
#define COLD __attribute__((noinline))
const cp_relic_info_t cp_relic_info[CP_RELIC_COUNT]={
 {"磁力牵引","吸引金币向前汇聚","每级增强磁场牵引"},
 {"月球引力","盘内重力每级降低","搭配弹跳获得更高弹起"},
 {"冰霜滑道","降低币盘接触摩擦","雷霆在冰面扩散更久"},
 {"弹跳糖","落地碰撞真实回弹","月球引力增强弹跳"},
 {"追风摆杆","加快入口摆杆速度","改变钉阵中的落币轨迹"},
 {"巨浪推板","每级增加推板行程","磁力组合增强向前推力"},
 {"金币回声","投币概率追加免费币","每级提高回声概率"},
 {"涟漪连锁","入袋向附近发出冲击","连锁冲击带动周围金币"},
 {"虫洞回流","黑洞回收概率返还币","搭配炼金生成实体筹码"},
 {"恒星聚变","积累入袋触发小型聚变","奖励筹码并释放金币雨"},
 {"空间折叠","边槽损失概率回到入口","搭配流星可回收并锻造"},
 {"炼金术","入袋概率生成实体筹码","筹码入袋提供十二筹码"},
 {"共振导轨","额外点亮抽奖通道","三级时三个通道全亮"},
 {"幸运骰子","提高转盘超级大奖概率","每级增加中奖机会"},
 {"时间沙漏","延长事件并累加时长","同类事件叠加有上限"},
 {"连击节拍","连续入袋累积连击","连击提高筹码收益"},
 {"筹码利息","每次转盘按余额生息","每轮利息有独立收益上限"},
 {"节约券","每数次投币返还一次","等级越高返还越频繁"},
 {"筹码兑换","赚筹码另送投币不扣余额","升级降低兑换门槛"},
 {"应急保险","投币耗尽后补给五次","每次升级增加一次保险"},
 {"补给背包","奖品额外补给投币","选择时补给两次投币"},
 {"自动贩卖","降低筹码购买投币价格","升级增加购买次数上限"},
 {"藏宝图","升级加快空投奖品","镜像组合加快寻宝"},
 {"镜像宝箱","奖品概率重复特殊效果","搭配藏宝图更易连锁"},
 {"雷霆环","每三个事件激发雷霆","周期冲击弹起盘内金币"},
 {"反重力场","每次转盘短暂托起金币","时间结束后重新落下"},
 {"流星锻炉","边槽损失积累重型筹码","从高处砸入推动币盘"},
 {"命运改写","每次选牌获得免费重抽","候选偏向已有强化叠加"}
};
const char *const cp_synergy_names[8]={"月面弹珠","冰雷连锁","黑洞工厂","聚变风暴","引力潮汐","黄金资本","失落锻造","镜像藏宝"};
static uint32_t roll(cp_game_t *g){uint32_t x=g->rng;x^=x<<13;x^=x>>17;x^=x<<5;return g->rng=x?x:12345;}
static unsigned min_u(unsigned a,unsigned b){return a<b?a:b;}
static void coins(cp_game_t *g,unsigned n){g->bank=min_u(999,g->bank+n);}
static void bonus(cp_game_t *g,unsigned n){g->bonus_queue=(uint16_t)min_u(4096,g->bonus_queue+n);}
static void chips(cp_game_t *g,unsigned n){
 cp_rogue_t *r=&g->rogue;g->score=min_u(CP_CHIP_LIMIT,g->score+n);r->gross=min_u(CP_CHIP_LIMIT,r->gross+n);
 if(r->relic[CP_R_EXCHANGE]){unsigned goal=140-r->relic[CP_R_EXCHANGE]*20;r->exchange_progress=(uint16_t)(r->exchange_progress+n);coins(g,r->exchange_progress/goal);r->exchange_progress%=goal;}
 if(g->score>g->best)g->best=g->score;
}
static COLD void tune(cp_rogue_t *r){
 r->gravity=(uint8_t)(100-r->relic[CP_R_MOON]*7);r->friction=(uint8_t)(100-r->relic[CP_R_ICE]*12);
 r->bounce=(uint8_t)(20+r->relic[CP_R_BOUNCE]*18+(r->relic[CP_R_BOUNCE]&&r->relic[CP_R_MOON]?12:0));
 r->stroke=(uint8_t)(35+r->relic[CP_R_WAVE]*2);r->synergies=cp_relic_synergies(r->relic);
}
uint32_t cp_relic_synergies(const uint8_t levels[CP_RELIC_COUNT]){
 uint32_t mask=0;
 const uint8_t a[8]={CP_R_MOON,CP_R_ICE,CP_R_WORM,CP_R_FUSION,CP_R_MAGNET,CP_R_COMBO,CP_R_FOLD,CP_R_MAP};
 const uint8_t b[8]={CP_R_BOUNCE,CP_R_THUNDER,CP_R_ALCHEMY,CP_R_THUNDER,CP_R_WAVE,CP_R_INTEREST,CP_R_METEOR,CP_R_MIRROR};
 for(unsigned k=0;k<8;++k)if(levels[a[k]]&&levels[b[k]]&&levels[a[k]]+levels[b[k]]>=4)mask|=1u<<k;
 return mask;
}

static COLD void offers(cp_game_t *g){
 cp_rogue_t *r=&g->rogue;unsigned eligible=0;
 for(unsigned k=0;k<CP_RELIC_COUNT;++k)eligible+=r->relic[k]<CP_RELIC_MAX;
 if(!eligible){r->pending=0;cp_rogue_view(g,CP_VIEW_PLAY);return;}
 for(unsigned k=0;k<3;++k){unsigned candidates[CP_RELIC_COUNT],n=0;
  for(unsigned id=0;id<CP_RELIC_COUNT;++id){bool used=false;for(unsigned j=0;j<k;++j)used|=r->offer[j]==id;
   if(r->relic[id]<CP_RELIC_MAX&&!used)candidates[n++]=id;}
  if(!n){r->offer[k]=r->offer[0];continue;}
  unsigned chosen=candidates[roll(g)%n];
  if(k==0&&r->relic[CP_R_FATE]){unsigned owned=0;for(unsigned j=0;j<n;++j)if(r->relic[candidates[j]])candidates[owned++]=candidates[j];if(owned)chosen=candidates[roll(g)%owned];}
  r->offer[k]=(uint8_t)chosen;
 }
 r->cursor=0;r->rerolls=r->relic[CP_R_FATE];cp_rogue_view(g,CP_VIEW_DRAFT);
}
void cp_rogue_view(cp_game_t *g,cp_view_t view){g->rogue.view=(uint8_t)view;g->rogue.cursor=0;g->paused=view!=CP_VIEW_PLAY;g->accumulator=0;++g->rogue.revision;}
void cp_rogue_start(cp_game_t *g,uint32_t discovered){
 memset(&g->rogue,0,sizeof(g->rogue));cp_rogue_t *r=&g->rogue;r->active=true;r->seed=g->rng;r->discovered=discovered&0x0fffffffu;r->level=1;r->goal=20;r->pending=1;
 g->bank=CP_RUN_COINS;g->score=0;cp_cabinet_load(g);cp_tactics_start(g);tune(r);offers(g);
}
void cp_rogue_grant(cp_game_t *g,unsigned id){
 cp_rogue_t *r=&g->rogue;if(!r->active||id>=CP_RELIC_COUNT||r->relic[id]>=CP_RELIC_MAX)return;
 ++r->relic[id];r->discovered|=1u<<id;r->inspect=(uint8_t)id;r->toast_ticks=4*CP_HZ;++r->revision;
 if(id==CP_R_INSURANCE)++r->insurance;
 if(id==CP_R_SUPPLY)coins(g,2);
 tune(r);
}
void cp_rogue_choose(cp_game_t *g,unsigned choice){
 cp_rogue_t *r=&g->rogue;if(r->view!=CP_VIEW_DRAFT||choice>=3||!r->pending)return;
 unsigned chosen=r->offer[choice];cp_rogue_grant(g,chosen);cp_tactics_upgrade(g,chosen);--r->pending;
 if(r->pending)offers(g);else cp_rogue_view(g,CP_VIEW_PLAY);
}
bool cp_rogue_reroll(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(r->view!=CP_VIEW_DRAFT||!r->rerolls)return false;unsigned left=r->rerolls-1;offers(g);r->rerolls=(uint8_t)left;return true;}
void cp_rogue_finish(cp_game_t *g){if(!g->rogue.active||g->rogue.finished)return;g->rogue.finished=true;g->autoplay=false;cp_rogue_view(g,CP_VIEW_END);}
unsigned cp_rogue_price(const cp_game_t *g){return 32+g->rogue.shop_uses*8-g->rogue.relic[CP_R_SHOP]*3;}
bool cp_rogue_buy(cp_game_t *g){cp_rogue_t *r=&g->rogue;unsigned price=cp_rogue_price(g);
 if(!r->active||r->finished||r->shop_uses>=7+r->relic[CP_R_SHOP]||g->score<price)return false;
 g->score-=price;coins(g,5);++r->shop_uses;r->grace=0;++r->revision;return true;
}
void cp_rogue_paid(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(!r->active)return;
 r->grace=0;if(r->relic[CP_R_ECHO]&&roll(g)%100<7*r->relic[CP_R_ECHO])bonus(g,1);
 if(r->relic[CP_R_THRIFT]&&++r->paid_progress>=12-2*r->relic[CP_R_THRIFT]){r->paid_progress=0;coins(g,1);}
}
static void experience(cp_rogue_t *r,unsigned xp){if(r->level>=99){r->xp=r->goal;return;}r->xp=(uint16_t)(r->xp+xp);
 while(r->xp>=r->goal&&r->level<99){r->xp-=r->goal;++r->level;r->goal=(uint16_t)min_u(300,20+(r->level-1)*10);r->pending=(uint8_t)min_u(3,r->pending+1);}
}
void cp_rogue_collect(cp_game_t *g,unsigned kind,bool siphon,int x,int y,unsigned value){
 cp_rogue_t *r=&g->rogue;bool prize=cp_kind_prize(kind);if(!r->active)return;
 if(r->combo_ticks)r->combo=(uint8_t)min_u(20,r->combo+1);else r->combo=1;r->combo_ticks=3*CP_HZ;
 if(r->relic[CP_R_COMBO]&&r->combo>=4&&++r->combo_progress>=9-r->relic[CP_R_COMBO]){r->combo_progress=0;value+=1+((r->synergies&32)?1:0);}
 chips(g,value);experience(r,prize?7:kind==CP_CHIP?5:1);
 if(prize||kind==CP_CHIP){if(kind==CP_CHIP)coins(g,1);else if(++r->prize_credit>=2){r->prize_credit=0;coins(g,1);}if(prize&&r->relic[CP_R_SUPPLY]&&++r->supply_progress>=6-r->relic[CP_R_SUPPLY]){r->supply_progress=0;coins(g,1);}}
 else if(++r->gold_progress>=16){r->gold_progress=0;coins(g,1);}
 if(r->relic[CP_R_RIPPLE]&&!r->ripple_ticks){r->ripple_ticks=12;r->ripple_x=(int16_t)x;r->ripple_y=(int16_t)y;}
 if(siphon&&r->relic[CP_R_WORM]&&roll(g)%100<8*r->relic[CP_R_WORM])bonus(g,1);
 if(!prize&&kind!=CP_CHIP&&r->relic[CP_R_ALCHEMY]&&(roll(g)%100<3*r->relic[CP_R_ALCHEMY]||(siphon&&(r->synergies&4)&&roll(g)%4==0)))r->chip_queue=(uint8_t)min_u(4,r->chip_queue+1);
 if(!prize&&kind!=CP_CHIP&&r->relic[CP_R_FUSION]&&++r->fusion_progress>=18-2*r->relic[CP_R_FUSION]){r->fusion_progress=0;chips(g,r->relic[CP_R_FUSION]);bonus(g,1);if(r->synergies&8)r->storm_ticks=90;}
 if(!prize&&kind!=CP_CHIP&&r->relic[CP_R_MAP]&&++r->map_progress>=(unsigned)((r->synergies&128?42:48)-6*r->relic[CP_R_MAP])){r->map_progress=0;r->prize_queue=(uint8_t)min_u(2,r->prize_queue+1);}
 if(prize&&r->relic[CP_R_MIRROR]&&roll(g)%100<10*r->relic[CP_R_MIRROR])cp_reward_event(g,(cp_event_t)(1+kind-CP_PRIZE_RED));
}
void cp_rogue_lost(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(!r->active)return;
 if(r->relic[CP_R_FOLD]&&roll(g)%100<10*r->relic[CP_R_FOLD])bonus(g,1);
 if(r->relic[CP_R_METEOR]&&++r->meteor_progress>=14-2*r->relic[CP_R_METEOR]){r->meteor_progress=0;r->chip_queue=(uint8_t)min_u(4,r->chip_queue+1);}
}
void cp_rogue_jackpot(cp_game_t *g){coins(g,2);chips(g,30);}
void cp_rogue_event(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(!r->active)return;
 if(r->relic[CP_R_THUNDER]&&++r->event_progress>=5){r->event_progress=0;r->storm_ticks=(uint16_t)((1+r->relic[CP_R_THUNDER]+((r->synergies&2)?1:0))*CP_HZ);}
}
void cp_rogue_reel(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(!r->active)return;
 if(r->relic[CP_R_INTEREST])chips(g,min_u(4*r->relic[CP_R_INTEREST],g->score*r->relic[CP_R_INTEREST]/100));
 if(r->relic[CP_R_FLOAT])r->float_ticks=(uint16_t)((1+r->relic[CP_R_FLOAT])*CP_HZ/2);
}
unsigned cp_rogue_duration(const cp_game_t *g,unsigned old,unsigned base){
 if(!g->rogue.active){return base;}
 unsigned n=g->rogue.relic[CP_R_TIME];unsigned scaled=base*(100+12*n)/100;return old>scaled?old:scaled;
}
bool cp_rogue_lane(const cp_game_t *g,unsigned lane){if(lane>2)return false;if(lane==g->arcade.lane)return true;
 unsigned n=g->rogue.active?g->rogue.relic[CP_R_RAINBOW]:0;
 return n>=3||(lane==(g->arcade.lane+1)%3&&(n>=2||(n==1&&(g->steps/30)%2==0)));
}
static COLD void dispense(cp_game_t *g){
 cp_rogue_t *r=&g->rogue;if(r->spawn_clock){--r->spawn_clock;return;}if((!r->chip_queue&&!r->prize_queue)||cp_coin_count(g)>82)return;
 /* Each deferred spawn checks a real empty volume. Never insert mid-solver. */
 int x=60+(int)(g->steps%3)*44,y=100,z=68;bool clear=true;
 for(unsigned i=0;i<CP_MAX_COINS;++i){const cp_coin_t *b=&g->coins[i];if(b->active&&fabsf(b->p.x-x)<24&&fabsf(b->p.y-y)<24&&fabsf(b->p.z-z)<20){clear=false;break;}}
 if(!clear){return;}
 bool prize=r->prize_queue>0;cp_coin_t *b=cp_spawn(g,prize?(cp_coin_kind_t)(CP_PRIZE_RED+roll(g)%3):CP_CHIP,cp_v(x,y,z),(cp_q4){1,0,0,0});
 if(b){b->v.z=prize?-8:-60;b->w.y=.7f;if(prize)--r->prize_queue;else --r->chip_queue;r->spawn_clock=20;}
}
COLD void cp_rogue_tick(cp_game_t *g){cp_rogue_t *r=&g->rogue;if(!r->active||r->finished)return;
 if(r->combo_ticks&&! --r->combo_ticks)r->combo=0;
 if(r->storm_ticks){--r->storm_ticks;}
 if(r->float_ticks){--r->float_ticks;}
 if(r->ripple_ticks){--r->ripple_ticks;}
 if(r->toast_ticks){--r->toast_ticks;}
 dispense(g);
 if(g->bank||g->tactics.queue_count)r->grace=0;else if(++r->grace>=CP_RUN_GRACE){
  if(r->insurance){--r->insurance;r->grace=0;coins(g,5);r->inspect=CP_R_INSURANCE;r->toast_ticks=120;}else{cp_rogue_finish(g);return;}}
 if(r->pending)offers(g);
}
bool cp_rogue_valid(const cp_rogue_t *r){if(!r->active)return true;
 if(!r->seed||r->view>CP_VIEW_SUPPLY||!r->level||r->level>99||r->goal>300||r->pending>3||r->grace>CP_RUN_GRACE||r->chip_queue>4||r->prize_queue>2||r->cursor>27||r->rank>9||r->inspect>=CP_RELIC_COUNT)return false;
 for(unsigned k=0;k<CP_RELIC_COUNT;++k)if(r->relic[k]>CP_RELIC_MAX)return false;
 for(unsigned k=0;k<3;++k)if(r->offer[k]>=CP_RELIC_COUNT)return false;
 return true;
}

COLD cp_rogue_cmd_t cp_rogue_key(cp_game_t *g,unsigned key,bool hold){
 cp_rogue_t *r=&g->rogue;if(!r->active||key>2)return CP_CMD_NONE;
 unsigned view=r->view;
 if(view==CP_VIEW_PLAY){
  if(key==2&&hold)cp_rogue_view(g,CP_VIEW_PAUSE);
  else if(key==2)return CP_CMD_DROP;
  else if(key==0)g->autoplay=!g->autoplay;
  else if(key==1){if(hold)return CP_CMD_BURST;cp_rogue_view(g,CP_VIEW_TRAY);r->cursor=g->tactics.selected;}
 }else if(view==CP_VIEW_TRAY){
  if(key<2&&!hold){g->tactics.selected=(uint8_t)((g->tactics.selected+(key?1:CP_SPECIAL_COUNT-1))%CP_SPECIAL_COUNT);r->cursor=g->tactics.selected;}
  else if(key==2){if(hold)cp_rogue_view(g,CP_VIEW_PLAY);else cp_rogue_view(g,CP_VIEW_PLAY);}
 }else if(view==CP_VIEW_SUPPLY){
  if(key<2&&!hold)r->cursor=(uint8_t)((r->cursor+(key?1:2))%3);
  else if(key==2)cp_tactics_supply(g,r->cursor);
 }else if(view==CP_VIEW_DRAFT){
  if(key<2&&!hold)r->cursor=(uint8_t)((r->cursor+(key?1:2))%3);
  else if(key==2){if(hold)(void)cp_rogue_reroll(g);else cp_rogue_choose(g,r->cursor);}
 }else if(view==CP_VIEW_PAUSE){
  if(key<2&&!hold)r->cursor=(uint8_t)((r->cursor+(key?1:5))%6);
  else if(key==2){unsigned choice=r->cursor;
   if(hold||choice==0)cp_rogue_view(g,CP_VIEW_PLAY);
   else if(choice==1){r->back=CP_VIEW_PAUSE;r->inspect=0;cp_rogue_view(g,CP_VIEW_CODEX);}
   else if(choice==2){r->rank_back=CP_VIEW_PAUSE;cp_rogue_view(g,CP_VIEW_RANK);}
   else if(choice==3)cp_rogue_view(g,CP_VIEW_SHOP);
   else if(choice==4){r->muted=!r->muted;++r->revision;return CP_CMD_SOUND;}
   else cp_rogue_finish(g);
  }
 }else if(view==CP_VIEW_RANK){
  if(key<2&&!hold)r->rank=(uint8_t)((r->rank+(key?1:9))%10);
  else if(key==2){if(hold)cp_rogue_view(g,(cp_view_t)r->rank_back);
   else{r->back=CP_VIEW_RANK;r->inspect=0;cp_rogue_view(g,CP_VIEW_CODEX);}}
 }else if(view==CP_VIEW_CODEX){
  if(key<2&&!hold)r->inspect=(uint8_t)((r->inspect+(key?1:27))%28);
  else if(key==2)cp_rogue_view(g,(cp_view_t)r->back);
 }else if(view==CP_VIEW_SHOP){
  if(key<2&&!hold)r->cursor^=1;
  else if(key==2){if(hold||r->cursor)cp_rogue_view(g,CP_VIEW_PAUSE);else(void)cp_rogue_buy(g);}
 }else if(view==CP_VIEW_END){
  if(key==2)return CP_CMD_NEW;
  if(key==0){r->rank_back=CP_VIEW_END;cp_rogue_view(g,CP_VIEW_RANK);}
  else{r->back=CP_VIEW_END;r->inspect=0;cp_rogue_view(g,CP_VIEW_CODEX);}
 }
 ++r->revision;return CP_CMD_NONE;
}

void cp_rogue_effect(unsigned id,unsigned level,char *out,unsigned size){
 unsigned n=level<1?1:level>3?3:level;
 switch(id){
 case CP_R_MAGNET:snprintf(out,size,"磁场牵引强度 %u级",n);break;
 case CP_R_MOON:snprintf(out,size,"盘内重力降低 %u%%",7*n);break;
 case CP_R_ICE:snprintf(out,size,"下层盘面摩擦降低 %u%%",12*n);break;
 case CP_R_BOUNCE:snprintf(out,size,"碰撞回弹约 %u%%",(20+18*n)*100/256);break;
 case CP_R_ROD:snprintf(out,size,"入口摆杆速度增加 %u%%",25*n);break;
 case CP_R_WAVE:snprintf(out,size,"推板行程增加 %u毫米",2*n);break;
 case CP_R_ECHO:snprintf(out,size,"投币 %u%%概率追加免费币",7*n);break;
 case CP_R_RIPPLE:snprintf(out,size,"入袋冲击强度 %u级",n);break;
 case CP_R_WORM:snprintf(out,size,"黑洞回收 %u%%概率返币",8*n);break;
 case CP_R_FUSION:snprintf(out,size,"每%u币奖%u筹码和一枚币",18-2*n,n);break;
 case CP_R_FOLD:snprintf(out,size,"边槽损失 %u%%概率返币",10*n);break;
 case CP_R_ALCHEMY:snprintf(out,size,"入袋 %u%%概率生成蓝筹码",3*n);break;
 case CP_R_RAINBOW:snprintf(out,size,"%s",n==1?"第二通道间歇共振":n==2?"两个抽奖通道共振":"三个抽奖通道共振");break;
 case CP_R_LUCK:snprintf(out,size,"转盘大奖概率 1/%u",32-3*n);break;
 case CP_R_TIME:snprintf(out,size,"事件时间增加 %u%%",12*n);break;
 case CP_R_COMBO:snprintf(out,size,"连击每%u次额外一筹码",9-n);break;
 case CP_R_INTEREST:snprintf(out,size,"利息%u%% 单轮最多%u筹码",n,4*n);break;
 case CP_R_THRIFT:snprintf(out,size,"每投%u次返还一次",12-2*n);break;
 case CP_R_EXCHANGE:snprintf(out,size,"每赚%u筹码另送一次投币",140-20*n);break;
 case CP_R_INSURANCE:snprintf(out,size,"本次增加一份五币保险");break;
 case CP_R_SUPPLY:snprintf(out,size,"每%u奖品额外一次投币",6-n);break;
 case CP_R_SHOP:snprintf(out,size,"首购%u筹码 每购递增",32-3*n);break;
 case CP_R_MAP:snprintf(out,size,"每收%u金币补一件奖品",48-6*n);break;
 case CP_R_MIRROR:snprintf(out,size,"奖品 %u%%概率再触发事件",10*n);break;
 case CP_R_THUNDER:snprintf(out,size,"每五事件释放%u秒雷霆",1+n);break;
 case CP_R_FLOAT:snprintf(out,size,"短暂悬浮 %u毫秒",(1+n)*500);break;
 case CP_R_METEOR:snprintf(out,size,"每损失%u金币落一枚重锤",14-2*n);break;
 default:snprintf(out,size,"每次选牌可免费重抽%u次",n);break;
 }
}
