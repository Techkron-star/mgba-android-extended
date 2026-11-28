/* SPDX-License-Identifier: MPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/gbax-memory.h>
#include <assert.h>
#include <stdio.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec/1e9;
}
int main(int argc,char** argv) {
    assert(argc==2);
    struct mCore* core=GBACoreCreate();
    assert(core && core->init(core));
    mCoreInitConfig(core,NULL);
    core->opts.useBios=false; core->opts.skipBios=true;
    assert(mCoreLoadFile(core,argv[1]));
    core->reset(core);
    core->runFrame(core);
    struct GBA* gba=core->board;
    assert(gba->memory.gbax);
    const size_t budget=96u*1024u*1024u;
    const uint32_t base=GBAX_RAM_BASE;
    /* A populated read-only world cache: 32 MiB spread across sparse pages. */
    for (unsigned i=0;i<512;++i) {
        core->busWrite32(core,base+i*GBAX_PAGE_SIZE,i+1);
        core->busWrite32(core,base+i*GBAX_PAGE_SIZE+GBAX_PAGE_SIZE-4,0xA5A50000u+i);
    }
    GBAXRewindClear();
    GBAXRewindCapture(gba,budget);
    assert(GBAXRewindCount()==1);
    for (unsigned i=0;i<512;++i) assert(!GBAXPageDirty(gba->memory.gbax,i));
    double start=now();
    for (unsigned i=0;i<20;++i) GBAXRewindCapture(gba,budget);
    double clean=(now()-start)/20;
    start=now();
    for (unsigned i=0;i<20;++i) {
        /* Exact pre-optimization workload: conservatively compare all pages. */
        GBAXDirtyAll(gba->memory.gbax);
        GBAXRewindCapture(gba,budget);
    }
    double dirty=(now()-start)/20;
    core->busWrite32(core,base,22);
    assert(GBAXPageDirty(gba->memory.gbax,0));
    GBAXRewindCapture(gba,budget);
    core->busWrite32(core,base,33);
    GBAXRewindCapture(gba,budget);
    assert(GBAXRewindStep(gba) && core->busRead32(core,base)==33);
    assert(GBAXRewindStep(gba) && core->busRead32(core,base)==22);
    core->busWrite32(core,base,44);
    GBAXRewindCapture(gba,budget);
    core->busWrite32(core,base,55);
    assert(GBAXRewindStep(gba) && core->busRead32(core,base)==44);
    for (unsigned i=1;i<512;++i)
        assert(core->busRead32(core,base+i*GBAX_PAGE_SIZE+GBAX_PAGE_SIZE-4)==0xA5A50000u+i);
    GBAXRewindClear();
    assert(!GBAXRewindCount());
    printf("PASS dirty pages, 32 MiB immutable cache, writes, rewind then capture.\n"
           "Capture mean: clean %.3f ms, compare-all %.3f ms (%.1fx).\n",
           clean*1000,dirty*1000,dirty/clean);
    core->unloadROM(core);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return 0;
}
