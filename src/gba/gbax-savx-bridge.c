/* SPDX-License-Identifier: MPL-2.0 */
#include <mgba/internal/gba/gbax-savx-bridge.h>
#include <mgba/internal/gba/memory.h>
#include <mgba-util/vfs.h>

#define STAGE_FIRST ((GBAX_SAVX_STAGE-GBAX_RAM_BASE)/GBAX_PAGE_SIZE)
#define STAGE_PAGES (GBAX_SAVX_SIZE/GBAX_PAGE_SIZE)

struct GBAXSaveSession {
    struct VFile* file[2];
    struct GBAXSaveIO io[2];
    uint64_t generation;
    uint64_t epoch;
    uint32_t romId;
    uint32_t status;
    bool loaded;
};
static int64_t fileSize(void* v) {
    struct VFile* f=v;
    return f->size(f);
}
static bool fileRead(void* v,uint32_t offset,void* dest,size_t size) {
    struct VFile* f=v;
    uint8_t* p=dest;
    if (f->seek(f,offset,SEEK_SET)!=(off_t)offset) return false;
    while(size) {
        ssize_t n=f->read(f,p,size);
        if(n<=0 || (size_t)n>size) return false;
        p+=n; size-=(size_t)n;
    }
    return true;
}
static bool fileWrite(void* v,uint32_t offset,const void* src,size_t size) {
    struct VFile* f=v;
    const uint8_t* p=src;
    if (f->seek(f,offset,SEEK_SET)!=(off_t)offset) return false;
    while(size) {
        ssize_t n=f->write(f,p,size);
        if(n<=0 || (size_t)n>size) return false;
        p+=n; size-=(size_t)n;
    }
    return true;
}
static bool fileSync(void* v) {
    struct VFile* f=v;
    return f->sync(f,NULL,0);
}
void GBAXSaveDetach(struct GBAMemory* m) {
    struct GBAXSaveSession* s=m->savx;
    if(!s)return;
    s->file[0]->close(s->file[0]);
    s->file[1]->close(s->file[1]);
    free(s);
    m->savx=NULL;
}
bool GBAXSaveAttach(struct GBAMemory* m,struct VFile* a,struct VFile* b,uint32_t id) {
    GBAXSaveDetach(m);
    if(!a || !b || a==b || !m->gbax || !a->sync || !b->sync) {
        if(a)a->close(a);
        if(b && a!=b)b->close(b);
        return false;
    }
    struct GBAXSaveSession* s=calloc(1,sizeof(*s));
    if(!s) { a->close(a); b->close(b); return false; }
    s->file[0]=a; s->file[1]=b;
    for(unsigned i=0;i<2;++i)
        s->io[i]=(struct GBAXSaveIO){s->file[i],fileSize,fileRead,fileWrite,fileSync};
    s->romId=id; s->status=GBAX_SAVX_READY;
    m->savx=s;
    return true;
}
void GBAXSaveReset(struct GBAMemory* m) {
    if(!m->savx)return;
    ++m->savx->epoch;
    m->savx->loaded=false;
    m->savx->generation=0;
    m->savx->status=GBAX_SAVX_READY;
}
uint64_t GBAXSaveEpoch(const struct GBAMemory* m) {
    return m->savx ? m->savx->epoch : 0;
}
/* All-or-nothing replacement of the staging pages. Zero pages need no allocation. */
static bool replaceStage(struct GBAXMemory* ram,const uint8_t* p) {
    uint8_t* next[STAGE_PAGES]={0};
    for(unsigned i=0;i<STAGE_PAGES;++i) {
        const uint8_t* src=p+(size_t)i*GBAX_PAGE_SIZE;
        unsigned j=0;
        while(j<GBAX_PAGE_SIZE && !src[j])++j;
        if(j==GBAX_PAGE_SIZE)continue;
        next[i]=malloc(GBAX_PAGE_SIZE);
        if(!next[i]) {
            for(unsigned k=0;k<STAGE_PAGES;++k)free(next[k]);
            return false;
        }
        memcpy(next[i],src,GBAX_PAGE_SIZE);
    }
    for(unsigned i=0;i<STAGE_PAGES;++i) {
        unsigned page=STAGE_FIRST+i;
        if(ram->pages[page]) { free(ram->pages[page]); --ram->allocatedPages; }
        ram->pages[page]=next[i];
        if(next[i])++ram->allocatedPages;
    }
    return true;
}
static void copyStage(const struct GBAXMemory* ram,uint8_t* p) {
    for(unsigned i=0;i<STAGE_PAGES;++i) {
        uint8_t* dst=p+(size_t)i*GBAX_PAGE_SIZE;
        const uint8_t* src=ram->pages[STAGE_FIRST+i];
        if(src)memcpy(dst,src,GBAX_PAGE_SIZE);
        else memset(dst,0,GBAX_PAGE_SIZE);
    }
}
uint32_t GBAXSaveReadRegister(const struct GBAMemory* m,uint32_t address) {
    const struct GBAXSaveSession* s=m->savx;
    switch(address-GBAX_SAVX_MMIO) {
    case 0: return GBAX_SAVX_ID;
    case 4: return s?s->status:GBAX_SAVX_UNAVAILABLE;
    case 12: return s?(uint32_t)s->generation:0;
    case 16: return s?(uint32_t)(s->generation>>32):0;
    default: return 0;
    }
}
void GBAXSaveWriteRegister(struct GBAMemory* m,uint32_t address,uint32_t value) {
    struct GBAXSaveSession* s=m->savx;
    if(!s || !m->gbax || address!=GBAX_SAVX_MMIO+8) return;
    ++s->epoch; /* Includes failed/uncertain commands: never cross an I/O boundary. */
    if(value!=GBAX_SAVX_CMD_LOAD && value!=GBAX_SAVX_CMD_COMMIT) {
        s->status=GBAX_SAVX_ERROR_BASE+GBAX_SAVE_INVALID;
        return;
    }
    if(value==GBAX_SAVX_CMD_COMMIT && !s->loaded) {
        s->status=GBAX_SAVX_ERROR_BASE+GBAX_SAVE_INVALID;
        return;
    }
    uint8_t* buffer=malloc(GBAX_SAVX_SIZE);
    if(!buffer) { s->status=GBAX_SAVX_NO_MEMORY; return; }
    enum GBAXSaveResult result;
    if(value==GBAX_SAVX_CMD_LOAD) {
        s->loaded=false;
        uint64_t generation=0;
        result=GBAXSaveLoad(s->io,s->romId,buffer,GBAX_SAVX_SIZE,&generation);
        if(result==GBAX_SAVE_OK || result==GBAX_SAVE_EMPTY) {
            if(replaceStage(m->gbax,buffer)) {
                s->generation=generation;
                s->loaded=true;
                s->status=result==GBAX_SAVE_EMPTY?GBAX_SAVX_EMPTY:GBAX_SAVX_LOADED;
            } else s->status=GBAX_SAVX_NO_MEMORY;
        } else s->status=GBAX_SAVX_ERROR_BASE+result;
    } else {
        copyStage(m->gbax,buffer);
        result=GBAXSaveCommit(s->io,s->romId,buffer,GBAX_SAVX_SIZE,&s->generation);
        if(result==GBAX_SAVE_OK)s->status=GBAX_SAVX_COMMITTED;
        else {
            s->loaded=false; /* Re-read disk before retrying any uncertain commit. */
            s->status=GBAX_SAVX_ERROR_BASE+result;
        }
    }
    free(buffer);
}
