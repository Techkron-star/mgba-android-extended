/* SPDX-License-Identifier: MPL-2.0 */
#include <mgba/internal/gba/gbax-savx.h>
#include <string.h>

#define CHUNK 4096u
static const uint8_t magic[8] = {'G','B','A','X','S','A','V','1'};
enum SlotResult { SLOT_EMPTY, SLOT_VALID, SLOT_BAD, SLOT_IO };
struct SlotInfo { enum SlotResult result; uint64_t generation; uint32_t crc; };

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put32(uint8_t* p, uint32_t n) {
    for (unsigned i=0;i<4;++i) p[i]=(uint8_t)(n>>(i*8));
}
static uint32_t crcUpdate(uint32_t crc, const uint8_t* p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (unsigned b=0;b<8;++b) crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xEDB88320));
    }
    return crc;
}
static uint32_t crc32(const uint8_t* p,size_t n) {
    return ~crcUpdate(UINT32_MAX,p,n);
}
static bool validIO(const struct GBAXSaveIO* io) {
    return io && io->size && io->read && io->write && io->sync;
}
static struct SlotInfo inspect(const struct GBAXSaveIO* io, uint32_t romId) {
    struct SlotInfo r={SLOT_BAD,0,0};
    uint8_t head[GBAX_SAVX_HEADER], buf[CHUNK];
    int64_t size=io->size(io->context);
    if (size<0) { r.result=SLOT_IO; return r; }
    if (size==0) { r.result=SLOT_EMPTY; return r; }
    if (size!=GBAX_SAVX_FILE_SIZE) return r;
    if (!io->read(io->context,0,head,sizeof(head))) { r.result=SLOT_IO; return r; }
    if (memcmp(head,magic,8) || get32(head+8)!=1 || get32(head+12)!=romId ||
        get32(head+16)!=GBAX_SAVX_SIZE || get32(head+32)!=crc32(head,32)) return r;
    r.generation=(uint64_t)get32(head+20) | (uint64_t)get32(head+24)<<32;
    r.crc=get32(head+28);
    if (!r.generation) return r;
    uint32_t crc=UINT32_MAX;
    for (uint32_t off=0;off<GBAX_SAVX_SIZE;off+=CHUNK) {
        if (!io->read(io->context,GBAX_SAVX_HEADER+off,buf,sizeof(buf))) {
            r.result=SLOT_IO; return r;
        }
        crc=crcUpdate(crc,buf,sizeof(buf));
    }
    if (~crc==r.crc) r.result=SLOT_VALID;
    return r;
}
static int newest(const struct SlotInfo r[2]) {
    if (r[0].result==SLOT_VALID && r[1].result==SLOT_VALID)
        return r[1].generation>r[0].generation ? 1:0;
    if (r[0].result==SLOT_VALID) return 0;
    if (r[1].result==SLOT_VALID) return 1;
    return -1;
}
static enum GBAXSaveResult noValid(const struct SlotInfo r[2]) {
    if (r[0].result==SLOT_IO || r[1].result==SLOT_IO) return GBAX_SAVE_IO_ERROR;
    if (r[0].result==SLOT_EMPTY && r[1].result==SLOT_EMPTY) return GBAX_SAVE_EMPTY;
    return GBAX_SAVE_CORRUPT;
}
enum GBAXSaveResult GBAXSaveLoad(const struct GBAXSaveIO slots[2], uint32_t romId,
    uint8_t* payload, size_t size, uint64_t* generation) {
    if (!slots || !validIO(&slots[0]) || !validIO(&slots[1]) || !payload ||
        size!=GBAX_SAVX_SIZE || !generation) return GBAX_SAVE_INVALID;
    struct SlotInfo r[2]={inspect(&slots[0],romId),inspect(&slots[1],romId)};
    int n=newest(r);
    if (n<0) {
        enum GBAXSaveResult result=noValid(r);
        if (result==GBAX_SAVE_EMPTY) { memset(payload,0,size); *generation=0; }
        return result;
    }
    if (!slots[n].read(slots[n].context,GBAX_SAVX_HEADER,payload,size)) return GBAX_SAVE_IO_ERROR;
    if (crc32(payload,size)!=r[n].crc) return GBAX_SAVE_CORRUPT;
    *generation=r[n].generation;
    return GBAX_SAVE_OK;
}
enum GBAXSaveResult GBAXSaveCommit(const struct GBAXSaveIO slots[2], uint32_t romId,
    const uint8_t* payload, size_t size, uint64_t* generation) {
    if (!slots || !validIO(&slots[0]) || !validIO(&slots[1]) || !payload ||
        size!=GBAX_SAVX_SIZE || !generation) return GBAX_SAVE_INVALID;
    struct SlotInfo r[2]={inspect(&slots[0],romId),inspect(&slots[1],romId)};
    int n=newest(r);
    if (r[0].result==SLOT_IO || r[1].result==SLOT_IO) return GBAX_SAVE_IO_ERROR;
    if (n<0 && noValid(r)!=GBAX_SAVE_EMPTY) return GBAX_SAVE_CORRUPT;
    uint64_t current=n<0?0:r[n].generation;
    /* Reject stale callers; a recovered generation must be loaded first. */
    if (*generation!=current) return GBAX_SAVE_INVALID;
    if (current==UINT64_MAX) return GBAX_SAVE_EXHAUSTED;
    const struct GBAXSaveIO* target=&slots[n==0?1:0];
    uint8_t head[GBAX_SAVX_HEADER]={0};
    /* Invalidate target, sync payload, then publish its verified header.
     * Never modify the current valid slot. */
    if (!target->write(target->context,0,head,sizeof(head)) ||
        !target->sync(target->context) ||
        !target->write(target->context,GBAX_SAVX_HEADER,payload,size) ||
        !target->sync(target->context)) return GBAX_SAVE_IO_ERROR;
    memcpy(head,magic,8);
    put32(head+8,1); put32(head+12,romId); put32(head+16,GBAX_SAVX_SIZE);
    ++current;
    put32(head+20,(uint32_t)current); put32(head+24,(uint32_t)(current>>32));
    put32(head+28,crc32(payload,size)); put32(head+32,crc32(head,32));
    if (!target->write(target->context,0,head,sizeof(head)) ||
        !target->sync(target->context)) return GBAX_SAVE_IO_ERROR;
    struct SlotInfo verify=inspect(target,romId);
    if (verify.result!=SLOT_VALID || verify.generation!=current) return GBAX_SAVE_IO_ERROR;
    *generation=current;
    return GBAX_SAVE_OK;
}
