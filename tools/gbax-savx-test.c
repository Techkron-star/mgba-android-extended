/* SPDX-License-Identifier: MPL-2.0 */
#include <mgba/internal/gba/gbax-savx.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Mock { uint8_t* p; size_t size; int failWrite; int failSync; };
static int64_t sz(void* v) { return ((struct Mock*)v)->size; }
static bool rd(void* v,uint32_t o,void* p,size_t n) {
    struct Mock* m=v; if (o>m->size || n>m->size-o) return false;
    memcpy(p,m->p+o,n); return true;
}
static bool wr(void* v,uint32_t o,const void* p,size_t n) {
    struct Mock* m=v; if (o>GBAX_SAVX_FILE_SIZE || n>GBAX_SAVX_FILE_SIZE-o) return false;
    if (m->failWrite>0 && !--m->failWrite) {
        size_t partial=n/2; memcpy(m->p+o,p,partial);
        if (o+partial>m->size) m->size=o+partial;
        return false;
    }
    memcpy(m->p+o,p,n); if(o+n>m->size)m->size=o+n; return true;
}
static bool syncFile(void* v) {
    struct Mock* m=v; return !(m->failSync>0 && !--m->failSync);
}
int main(void) {
    struct Mock m[2]={{0}};
    struct GBAXSaveIO io[2];
    for(int i=0;i<2;++i) {
        m[i].p=calloc(1,GBAX_SAVX_FILE_SIZE); assert(m[i].p);
        io[i]=(struct GBAXSaveIO){&m[i],sz,rd,wr,syncFile};
    }
    uint8_t* p=malloc(GBAX_SAVX_SIZE); assert(p);
    uint64_t g=999;
    memset(p,0xFF,GBAX_SAVX_SIZE);
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_EMPTY && g==0);
    for(size_t i=0;i<GBAX_SAVX_SIZE;++i)assert(p[i]==0);
    memset(p,0x12,GBAX_SAVX_SIZE); p[GBAX_SAVX_SIZE-1]=0xEF;
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==1);
    memset(p,0x34,GBAX_SAVX_SIZE);
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==2);
    memset(p,0x56,GBAX_SAVX_SIZE);
    m[0].failWrite=2;
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_IO_ERROR && g==2);
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==2);
    for(size_t i=0;i<GBAX_SAVX_SIZE;++i)assert(p[i]==0x34);
    m[0].failSync=2;
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_IO_ERROR && g==2);
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==2);
    memset(p,0x78,GBAX_SAVX_SIZE);
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==3);
    m[0].p[GBAX_SAVX_HEADER+GBAX_SAVX_SIZE-1]^=1;
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_OK && g==2);
    assert(p[0]==0x34 && p[GBAX_SAVX_SIZE-1]==0x34);
    assert(GBAXSaveLoad(io,43,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_CORRUPT);
    assert(GBAXSaveCommit(io,43,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_CORRUPT);
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE-1,&g)==GBAX_SAVE_INVALID);
    uint64_t stale=1;
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&stale)==GBAX_SAVE_INVALID);
    m[1].p[0]^=1;
    assert(GBAXSaveLoad(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_CORRUPT);
    assert(GBAXSaveCommit(io,42,p,GBAX_SAVX_SIZE,&g)==GBAX_SAVE_CORRUPT);
    free(p); free(m[0].p); free(m[1].p);
    puts("PASS: 16 MiB SAVX, two generations, interrupted writes, failed sync, CRC recovery, identity and stale generation guards");
}
