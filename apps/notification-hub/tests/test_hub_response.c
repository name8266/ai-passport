#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int allocations,fail;
static void *test_calloc(size_t n,size_t size) {allocations++;return fail?NULL:calloc(n,size);}
#define calloc test_calloc
#include "../firmware/main/hub_response.c"
#undef calloc
int main(void) {
    hub_response_t r={.cap=16};
    assert(hub_response_append(&r,NULL,0) && allocations==0 && !r.data);
    assert(hub_response_append(&r,"hello",5) && allocations==1);
    assert(hub_response_append(&r," world",6) && allocations==1);
    assert(r.used==11 && !strcmp(r.data,"hello world"));
    assert(!hub_response_append(&r,"12345",5) && r.overflow && r.used==11);
    assert(!hub_response_append(&r,"a",1));hub_response_release(&r);
    assert(!r.data && !r.used && !r.overflow);
    fail=1;assert(!hub_response_append(&r,"x",1) && r.overflow && !r.data);
    hub_response_release(&r);fail=0;
    assert(!hub_response_append(&r,"x",SIZE_MAX) && r.overflow);
    hub_response_release(&r);r.cap=0;
    assert(!hub_response_append(&r,"x",1));hub_response_release(&r);
    assert(!hub_response_append(NULL,"x",1));hub_response_release(NULL);
    puts("Deferred response allocation and overflow tests: PASS");return 0;
}
