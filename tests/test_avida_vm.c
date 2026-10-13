// Research regression suite: prove offspring arise from executed h-copy/h-divide
// and deterministic, versioned snapshots continue with identical CPU trajectories.
#include "avida_vm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void run(av_world_t *w,unsigned rounds)
{
    for(unsigned i=0;i<rounds;++i)av_step(w,512);
}

int main(void)
{
    static av_world_t a,b,restarted;
    static av_snapshot_t snapshot;
    uint8_t ancestor[AV_ANCESTOR_LEN];
    assert(av_get_ancestor(ancestor));
    assert(ancestor[0]==AV_H_ALLOC&&ancestor[7]==AV_H_COPY&&
           ancestor[8]==AV_IF_LABEL&&ancestor[11]==AV_H_DIVIDE);
    av_reset(&a,0x12344321u);
    av_reset(&b,0x12344321u);
    assert(memcmp(&a,&b,sizeof(a))==0);
    assert(a.next_id==17&&a.next_lineage==2);

    // Without VM execution, no replication or mutation should occur.
    av_step(&a,0);
    assert(a.births==0 && a.copy_attempts==0);
    a.mutation_level=0;
    b.mutation_level=0;
    run(&a,120);
    run(&b,120);
    assert(memcmp(&a,&b,sizeof(a))==0);
    assert(a.births>16);
    assert(a.copy_attempts>=a.births*AV_ANCESTOR_LEN);
    assert(a.failed_divides==0);
    assert(a.mutations==0 && a.insertion_deletions==0);
    av_stats_t stats;
    av_stats(&a,&stats);
    assert(stats.population>16&&stats.population<=AV_SITES);
    assert(stats.genotype_count==1);
    assert(stats.dominant_count==stats.population);
    for(unsigned i=0;i<AV_SITES;++i){
        const av_organism_t *o=&a.organisms[i];
        if(!o->length)continue;
        assert(o->length==AV_ANCESTOR_LEN);
        assert(memcmp(o->code,ancestor,AV_ANCESTOR_LEN)==0);
        assert(o->ip<o->length);
    }

    // Save all partial in-flight copy buffers, registers, and PRNG state.
    av_snapshot_encode(&a,9,&snapshot);
    assert(snapshot.sequence==9);
    assert(av_snapshot_decode(&restarted,&snapshot));
    assert(memcmp(&restarted,&a,sizeof(a))==0);
    run(&a,12);
    run(&restarted,12);
    assert(memcmp(&restarted,&a,sizeof(a))==0);

    // Tampering/version change must fail without modifying running world.
    av_snapshot_encode(&a,10,&snapshot);
    snapshot.world.organisms[0].code[0] ^= 1u;
    assert(!av_snapshot_decode(&restarted,&snapshot));
    assert(memcmp(&restarted,&a,sizeof(a))==0);
    av_snapshot_encode(&a,11,&snapshot);
    snapshot.version++;
    assert(!av_snapshot_decode(&restarted,&snapshot));

    // Mutation is applied at instruction copy, not automatically on birth.
    av_reset(&a,0x12344321u);
    a.mutation_level=3;
    run(&a,200);
    av_stats(&a,&stats);
    assert(a.copy_attempts>0);
    assert(a.mutations>0);
    assert(stats.genotype_count>1);
    assert(a.births>0);
    // Paused world is bit-for-bit stable.
    av_snapshot_encode(&a,12,&snapshot);
    a.paused=1;
    restarted=a;
    av_step(&a,4096);
    assert(memcmp(&restarted,&a,sizeof(a))==0);
    // Every opcode has an explicit name.
    char name[32];
    for(unsigned i=0;i<AV_OPCODE_COUNT;++i){
        av_instruction_name(i,name,sizeof(name));
        assert(name[0]&&strcmp(name,"invalid")!=0);
    }
    puts("Avida-inspired host tests: PASS");
    return 0;
}
