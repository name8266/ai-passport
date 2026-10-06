#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "multi_slot_core.h"

static void empty_grid(uint8_t cells[15])
{
    /* No valid three-symbol pattern or leftmost pair. */
    for (int r=0;r<3;r++)for(int c=0;c<5;c++)cells[r*5+c]=(uint8_t)((r*2+c)%6);
}
int main(void)
{
    uint8_t cells[15], indices[3];
    uint32_t seen[27];
    empty_grid(cells);
    assert(multi_slot_evaluate(cells).score==0);
    for(uint8_t line=0;line<27;line++) {
        assert(multi_slot_line_cells(line,indices));
        uint32_t mask=0;
        for(int i=0;i<3;i++){assert(indices[i]<15);mask|=1u<<indices[i];}
        for(uint8_t j=0;j<line;j++)assert(seen[j]!=mask);
        seen[line]=mask;
        empty_grid(cells);
        for(int i=0;i<3;i++)cells[indices[i]]=SLOT_SEVEN;
        multi_slot_result_t result=multi_slot_evaluate(cells);
        assert(result.line_mask&(1u<<line));
        assert((result.cell_mask&mask)==mask);
        assert(result.score >= (line<9?100u:200u));
    }
    assert(!multi_slot_line_cells(27,indices));
    assert(!multi_slot_line_cells(0,NULL));
    memset(cells,SLOT_SEVEN,sizeof(cells));
    multi_slot_result_t full=multi_slot_evaluate(cells);
    assert(full.line_count==27 && full.line_mask==0x7ffffff && full.cell_mask==0x7fff);
    assert(full.score==4500 && full.pair_count==0);
    empty_grid(cells);cells[1]=cells[0];
    multi_slot_result_t pair=multi_slot_evaluate(cells);
    assert(pair.line_count==0 && pair.pair_count==1 && pair.score==5);
    assert(pair.cell_mask==3);
    cells[0]=6;assert(multi_slot_evaluate(cells).score==0);
    assert(multi_slot_evaluate(NULL).score==0);
    uint8_t reels[3]={0,0,0};assert(slot_classic_score(reels)==20);
    reels[2]=1;assert(slot_classic_score(reels)==5);
    reels[1]=2;assert(slot_classic_score(reels)==0);
    for(unsigned start=0;start<24;start++)for(unsigned column=0;column<5;column++) {
        uint8_t strip[24], before[24], target[3]={5,3,1};
        for(unsigned i=0;i<24;i++)strip[i]=(uint8_t)(i%6);
        memcpy(before,strip,sizeof(strip));
        multi_slot_plan_t plan=multi_slot_prepare_strip(strip,100000u*6144u+start*256u+91u,(uint8_t)column,target,start*777+column);
        assert(plan.start_q8==start*256u+91u && plan.end_q8>plan.start_q8 && plan.end_q8<256*256u);
        for(unsigned j=0;j<5;j++) {
            unsigned index=(start+24+2-j)%24;
            assert(strip[index]==before[index]);
        }
        for(unsigned r=0;r<3;r++)assert(strip[(plan.end_q8/256-r)%24]==target[r]);
    }
    unsigned wins=0,triples=0,pairs=0;uint64_t total=0;
    for(uint32_t seed=0;seed<100000;seed++) {
        multi_slot_result_t a=multi_slot_make_result(seed),b=multi_slot_make_result(seed);
        assert(!memcmp(&a,&b,sizeof(a)));
        multi_slot_result_t checked=multi_slot_evaluate(a.cells);
        assert(a.score==checked.score && a.line_mask==checked.line_mask && a.cell_mask==checked.cell_mask);
        assert(a.score<=4500);
        wins+=a.score>0;triples+=a.line_count>0;pairs+=a.pair_count>0;total+=a.score;
    }
    assert(wins>55000 && wins<85000);
    printf("Multi-slot 100000 seeds: wins=%u triples=%u pair_only=%u mean_score_x100=%llu\n",wins,triples,pairs,(unsigned long long)(total*100/100000));
    return 0;
}
