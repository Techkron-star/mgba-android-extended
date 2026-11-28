/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba/internal/gba/serialize.h>

#include <mgba/internal/arm/macros.h>
#include <mgba/internal/gba/bios.h>
#include <mgba/internal/gba/io.h>
#include <mgba/internal/gba/gbax-savx-bridge.h>
#include <mgba-util/crc32.h>

#include <mgba-util/memory.h>
#include <mgba-util/vfs.h>

#include <fcntl.h>

MGBA_EXPORT const uint32_t GBASavestateMagic = 0x01000000;
MGBA_EXPORT const uint32_t GBASavestateVersion = 0x0000000B;

mLOG_DEFINE_CATEGORY(GBA_STATE, "GBA Savestate", "gba.serialize");

struct GBABundledState {
	struct GBASerializedState* state;
	struct mStateExtdata* extdata;
};

static void _GBASerializeNative(struct GBA* gba, struct GBASerializedState* state) {
	STORE_32(GBASavestateMagic + GBASavestateVersion, 0, &state->versionMagic);
	STORE_32(gba->biosChecksum, 0, &state->biosChecksum);
	STORE_32(gba->romCrc32, 0, &state->romCrc32);
	STORE_32(gba->timing.masterCycles, 0, &state->masterCycles);
	STORE_64LE(gba->timing.globalCycles, 0, &state->globalCycles);

	if (gba->memory.rom) {
		switch (gba->memory.unl.type) {
		case GBA_UNL_CART_NONE:
		case GBA_UNL_CART_VFAME:
			state->id = ((struct GBACartridge*) gba->memory.rom)->id;
			memcpy(state->title, ((struct GBACartridge*) gba->memory.rom)->title, sizeof(state->title));
			break;
		case GBA_UNL_CART_MULTICART:
			state->id = ((struct GBACartridge*) gba->memory.unl.multi.rom)->id;
			memcpy(state->title, ((struct GBACartridge*) gba->memory.unl.multi.rom)->title, sizeof(state->title));
			break;
		}
	} else {
		state->id = 0;
		memset(state->title, 0, sizeof(state->title));
	}

	int i;
	for (i = 0; i < 16; ++i) {
		STORE_32(gba->cpu->gprs[i], i * sizeof(state->cpu.gprs[0]), state->cpu.gprs);
	}
	STORE_32(gba->cpu->cpsr.packed, 0, &state->cpu.cpsr.packed);
	STORE_32(gba->cpu->spsr.packed, 0, &state->cpu.spsr.packed);
	STORE_32(gba->cpu->cycles, 0, &state->cpu.cycles);
	STORE_32(gba->cpu->nextEvent, 0, &state->cpu.nextEvent);
	for (i = 0; i < 6; ++i) {
		int j;
		for (j = 0; j < 7; ++j) {
			STORE_32(gba->cpu->bankedRegisters[i][j], (i * 7 + j) * sizeof(gba->cpu->bankedRegisters[0][0]), state->cpu.bankedRegisters);
		}
		STORE_32(gba->cpu->bankedSPSRs[i], i * sizeof(gba->cpu->bankedSPSRs[0]), state->cpu.bankedSPSRs);
	}

	STORE_32(gba->memory.biosPrefetch, 0, &state->biosPrefetch);
	STORE_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);
	STORE_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
	STORE_32(gba->memory.lastPrefetchedPc, 0, &state->lastPrefetchedPc);

	GBASerializedMiscFlags miscFlags = 0;
	miscFlags = GBASerializedMiscFlagsSetHalted(miscFlags, gba->cpu->halted);
	miscFlags = GBASerializedMiscFlagsSetPOSTFLG(miscFlags, gba->memory.io[GBA_REG(POSTFLG)] & 1);
	if (mTimingIsScheduled(&gba->timing, &gba->irqEvent)) {
		miscFlags = GBASerializedMiscFlagsFillIrqPending(miscFlags);
		STORE_32(gba->irqEvent.when - mTimingCurrentTime(&gba->timing), 0, &state->nextIrq);
	}
	miscFlags = GBASerializedMiscFlagsSetBlocked(miscFlags, gba->cpuBlocked);
	miscFlags = GBASerializedMiscFlagsSetKeyIRQKeys(miscFlags, gba->keysLast);
	STORE_32(miscFlags, 0, &state->miscFlags);
	STORE_32(gba->biosStall, 0, &state->biosStall);

	GBAMemorySerialize(&gba->memory, state);
	GBAIOSerialize(gba, state);
	GBAUnlCartSerialize(gba, state);
	GBAVideoSerialize(&gba->video, state);
	GBAAudioSerialize(&gba->audio, state);
	GBASavedataSerialize(&gba->memory.savedata, state);

	if (gba->memory.matrix.size) {
		GBAMatrixSerialize(gba, state);
	}
}

static bool _GBADeserializeNative(struct GBA* gba, const struct GBASerializedState* state) {
	bool error = false;
	int32_t check;
	uint32_t ucheck;
	LOAD_32(ucheck, 0, &state->versionMagic);
	if (ucheck > GBASavestateMagic + GBASavestateVersion) {
		mLOG(GBA_STATE, WARN, "Invalid or too new savestate: expected %08X, got %08X", GBASavestateMagic + GBASavestateVersion, ucheck);
		error = true;
	} else if (ucheck < GBASavestateMagic) {
		mLOG(GBA_STATE, WARN, "Invalid savestate: expected %08X, got %08X", GBASavestateMagic + GBASavestateVersion, ucheck);
		error = true;
	} else if (ucheck < GBASavestateMagic + GBASavestateVersion) {
		mLOG(GBA_STATE, WARN, "Old savestate: expected %08X, got %08X, continuing anyway", GBASavestateMagic + GBASavestateVersion, ucheck);
	}
	LOAD_32(ucheck, 0, &state->biosChecksum);
	if (ucheck != gba->biosChecksum) {
		mLOG(GBA_STATE, WARN, "Savestate created using a different version of the BIOS: expected %08X, got %08X", gba->biosChecksum, ucheck);
		uint32_t pc;
		LOAD_32(pc, ARM_PC * sizeof(state->cpu.gprs[0]), state->cpu.gprs);
		if ((ucheck == GBA_BIOS_CHECKSUM || gba->biosChecksum == GBA_BIOS_CHECKSUM) && pc < GBA_SIZE_BIOS && pc >= 0x20) {
			error = true;
		}
	}
	if (gba->memory.rom) {
		struct GBACartridge* cart;
		switch (gba->memory.unl.type) {
		case GBA_UNL_CART_NONE:
		case GBA_UNL_CART_VFAME:
		default:
			cart = (struct GBACartridge*) gba->memory.rom;
			break;
		case GBA_UNL_CART_MULTICART:
			cart = (struct GBACartridge*) gba->memory.unl.multi.rom;
			break;
		}
		if (state->id != cart->id || memcmp(state->title, cart->title, sizeof(state->title))) {
			mLOG(GBA_STATE, WARN, "Savestate is for a different game");
			error = true;
		}
	} else if (!gba->memory.rom && state->id != 0) {
		mLOG(GBA_STATE, WARN, "Savestate is for a game, but no game loaded");
		error = true;
	}
	LOAD_32(ucheck, 0, &state->romCrc32);
	if (ucheck != gba->romCrc32) {
		mLOG(GBA_STATE, WARN, "Savestate is for a different version of the game");
	}
	LOAD_32(check, 0, &state->cpu.cycles);
	if (check < 0) {
		mLOG(GBA_STATE, WARN, "Savestate is corrupted: CPU cycles are negative");
		error = true;
	}
	if (check >= (int32_t) GBA_ARM7TDMI_FREQUENCY) {
		mLOG(GBA_STATE, WARN, "Savestate is corrupted: CPU cycles are too high");
		error = true;
	}
	LOAD_32(check, ARM_PC * sizeof(state->cpu.gprs[0]), state->cpu.gprs);
	int region = (check >> BASE_OFFSET);
	if ((region == GBA_REGION_ROM0 || region == GBA_REGION_ROM1 || region == GBA_REGION_ROM2) && ((check - WORD_SIZE_ARM) & GBA_SIZE_ROM0) >= gba->memory.romSize - WORD_SIZE_ARM) {
		mLOG(GBA_STATE, WARN, "Savestate created using a differently sized version of the ROM");
		error = true;
	}
	if (error) {
		return false;
	}
	mTimingClear(&gba->timing);
	LOAD_32(gba->timing.masterCycles, 0, &state->masterCycles);
	LOAD_64LE(gba->timing.globalCycles, 0, &state->globalCycles);

	size_t i;
	for (i = 0; i < 16; ++i) {
		LOAD_32(gba->cpu->gprs[i], i * sizeof(gba->cpu->gprs[0]), state->cpu.gprs);
	}
	LOAD_32(gba->cpu->cpsr.packed, 0, &state->cpu.cpsr.packed);
	LOAD_32(gba->cpu->spsr.packed, 0, &state->cpu.spsr.packed);
	LOAD_32(gba->cpu->cycles, 0, &state->cpu.cycles);
	LOAD_32(gba->cpu->nextEvent, 0, &state->cpu.nextEvent);
	for (i = 0; i < 6; ++i) {
		int j;
		for (j = 0; j < 7; ++j) {
			LOAD_32(gba->cpu->bankedRegisters[i][j], (i * 7 + j) * sizeof(gba->cpu->bankedRegisters[0][0]), state->cpu.bankedRegisters);
		}
		LOAD_32(gba->cpu->bankedSPSRs[i], i * sizeof(gba->cpu->bankedSPSRs[0]), state->cpu.bankedSPSRs);
	}
	gba->cpu->privilegeMode = gba->cpu->cpsr.priv;
	if (gba->cpu->gprs[ARM_PC] & 1) {
		mLOG(GBA_STATE, WARN, "Savestate has unaligned PC and is probably corrupted");
		gba->cpu->gprs[ARM_PC] &= ~1;
	}

	// Since this can remap the ROM, we need to do this before we reset the pipeline
	GBAUnlCartDeserialize(gba, state);
	gba->memory.activeRegion = -1;
	gba->cpu->memory.setActiveRegion(gba->cpu, gba->cpu->gprs[ARM_PC]);
	if (state->biosPrefetch) {
		LOAD_32(gba->memory.biosPrefetch, 0, &state->biosPrefetch);
	}
	LOAD_32(gba->memory.lastPrefetchedPc, 0, &state->lastPrefetchedPc);
	if (gba->cpu->cpsr.t) {
		gba->cpu->executionMode = MODE_THUMB;
		if (state->cpuPrefetch[0] && state->cpuPrefetch[1]) {
			LOAD_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);
			LOAD_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
			gba->cpu->prefetch[0] &= 0xFFFF;
			gba->cpu->prefetch[1] &= 0xFFFF;
		} else {
			// Maintain backwards compat
			LOAD_16(gba->cpu->prefetch[0], (gba->cpu->gprs[ARM_PC] - WORD_SIZE_THUMB) & gba->cpu->memory.activeMask, gba->cpu->memory.activeRegion);
			LOAD_16(gba->cpu->prefetch[1], (gba->cpu->gprs[ARM_PC]) & gba->cpu->memory.activeMask, gba->cpu->memory.activeRegion);
		}
	} else {
		gba->cpu->executionMode = MODE_ARM;
		if (state->cpuPrefetch[0] && state->cpuPrefetch[1]) {
			LOAD_32(gba->cpu->prefetch[0], 0, state->cpuPrefetch);
			LOAD_32(gba->cpu->prefetch[1], 4, state->cpuPrefetch);
		} else {
			// Maintain backwards compat
			LOAD_32(gba->cpu->prefetch[0], (gba->cpu->gprs[ARM_PC] - WORD_SIZE_ARM) & gba->cpu->memory.activeMask, gba->cpu->memory.activeRegion);
			LOAD_32(gba->cpu->prefetch[1], (gba->cpu->gprs[ARM_PC]) & gba->cpu->memory.activeMask, gba->cpu->memory.activeRegion);
		}
	}
	GBASerializedMiscFlags miscFlags = 0;
	LOAD_32(miscFlags, 0, &state->miscFlags);
	gba->cpu->halted = GBASerializedMiscFlagsGetHalted(miscFlags);
	gba->memory.io[GBA_REG(POSTFLG)] = GBASerializedMiscFlagsGetPOSTFLG(miscFlags);
	if (GBASerializedMiscFlagsIsIrqPending(miscFlags)) {
		int32_t when;
		LOAD_32(when, 0, &state->nextIrq);
		mTimingSchedule(&gba->timing, &gba->irqEvent, when);
	}
	gba->cpuBlocked = GBASerializedMiscFlagsGetBlocked(miscFlags);
	gba->keysLast = GBASerializedMiscFlagsGetKeyIRQKeys(miscFlags);
	LOAD_32(gba->biosStall, 0, &state->biosStall);

	GBAVideoDeserialize(&gba->video, state);
	GBAMemoryDeserialize(&gba->memory, state);
	GBAIODeserialize(gba, state);
	GBAAudioDeserialize(&gba->audio, state);
	GBASavedataDeserialize(&gba->memory.savedata, state);

	if (gba->memory.matrix.size) {
		GBAMatrixDeserialize(gba, state);
	}

	mTimingInterrupt(&gba->timing);

	return true;
}

/* Keep the legacy fixed-size raw API isolated. GBAX named states have a
 * separate envelope and sparse pages; raw rewind/netplay callers cannot
 * accidentally serialize only the native part. */
void GBASerialize(struct GBA* gba, struct GBASerializedState* state) {
	if (gba->memory.gbax) {
		memset(state, 0, sizeof(*state));
		return;
	}
	_GBASerializeNative(gba, state);
}
bool GBADeserialize(struct GBA* gba, const struct GBASerializedState* state) {
	return !gba->memory.gbax && _GBADeserializeNative(gba, state);
}

#define GBAX_STATE_BITMAP (GBAX_PAGE_COUNT / 8)
#define GBAX_STATE_HEADER (36u + GBAX_STATE_BITMAP)
static const uint8_t _gbaxStateMagic[8]={'G','B','A','X','S','T','0','1'};

static uint32_t _xsGet(const uint8_t* p) {
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void _xsPut(uint8_t* p,uint32_t v) {
	for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));
}
static bool _xsRead(struct VFile* f,void* data,size_t n) {
	uint8_t* p=data;
	while(n) {
		ssize_t r=f->read(f,p,n);
		if(r<=0 || (size_t)r>n)return false;
		p+=r; n-=(size_t)r;
	}
	return true;
}
static bool _xsWrite(struct VFile* f,const void* data,size_t n) {
	const uint8_t* p=data;
	while(n) {
		ssize_t r=f->write(f,p,n);
		if(r<=0 || (size_t)r>n)return false;
		p+=r; n-=(size_t)r;
	}
	return true;
}
static bool _xsNonzero(const uint8_t* p) {
	if(!p)return false;
	for(unsigned i=0;i<GBAX_PAGE_SIZE;++i)if(p[i])return true;
	return false;
}
bool GBAXStateActive(const struct mCore* core) {
	return core && core->board && core->platform(core)==mPLATFORM_GBA &&
		((const struct GBA*)core->board)->memory.gbax;
}
bool GBAXStateFile(struct VFile* f) {
	uint8_t magic[8];
	if(!f || f->seek(f,0,SEEK_SET)!=0)return false;
	bool yes=_xsRead(f,magic,sizeof(magic)) && !memcmp(magic,_gbaxStateMagic,8);
	f->seek(f,0,SEEK_SET);
	return yes;
}
bool GBAXStateSave(struct GBA* gba,struct VFile* f) {
	if(!gba->memory.gbax || !f)return false;
	struct GBASerializedState* native=calloc(1,sizeof(*native));
	if(!native)return false;
	_GBASerializeNative(gba,native);
	STORE_32(0x58544350u,0,native->reserved);
	STORE_32(gba->cpu->gbaxTurbo,4,native->reserved);
	STORE_32(gba->cpu->gbaxFraction,8,native->reserved);
	uint8_t head[GBAX_STATE_HEADER]={0};
	uint8_t invalid[GBAX_STATE_HEADER]={0};
	uint32_t count=0;
	for(unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
		if(_xsNonzero(gba->memory.gbax->pages[i])) {
			head[36+i/8]|=(uint8_t)(1u<<(i%8)); ++count;
		}
	}
	memcpy(head,_gbaxStateMagic,8);
	_xsPut(head+8,1); _xsPut(head+12,gba->romCrc32);
	_xsPut(head+16,sizeof(*native)); _xsPut(head+20,GBAX_PAGE_SIZE);
	_xsPut(head+24,count); _xsPut(head+28,doCrc32(native,sizeof(*native)));
	/* Header checksum covers the bitmap too, with checksum field zero. */
	_xsPut(head+32,doCrc32(head,sizeof(head)));
	size_t size=sizeof(head)+sizeof(*native)+(size_t)count*(GBAX_PAGE_SIZE+4);
	bool ok=f->seek(f,0,SEEK_SET)==0 && _xsWrite(f,invalid,sizeof(invalid)) &&
		_xsWrite(f,native,sizeof(*native));
	for(unsigned i=0;ok && i<GBAX_PAGE_COUNT;++i) {
		if(!(head[36+i/8]&(1u<<(i%8))))continue;
		uint8_t crc[4];
		_xsPut(crc,doCrc32(gba->memory.gbax->pages[i],GBAX_PAGE_SIZE));
		ok=_xsWrite(f,crc,4) && _xsWrite(f,gba->memory.gbax->pages[i],GBAX_PAGE_SIZE);
	}
	if(ok) {
		f->truncate(f,size);
		ok=f->size(f)==(ssize_t)size && f->seek(f,0,SEEK_SET)==0 && _xsWrite(f,head,sizeof(head));
	}
	/* Named states are snapshots, not the durable dual-slot SAVX journal. */
	free(native);
	return ok;
}
bool GBAXStateLoad(struct GBA* gba,struct VFile* f) {
	if(!gba->memory.gbax || !f)return false;
	uint8_t head[GBAX_STATE_HEADER];
	if(f->seek(f,0,SEEK_SET)!=0 || !_xsRead(f,head,sizeof(head)) ||
		memcmp(head,_gbaxStateMagic,8) || _xsGet(head+8)!=1 ||
		_xsGet(head+12)!=gba->romCrc32 || _xsGet(head+16)!=sizeof(struct GBASerializedState) ||
		_xsGet(head+20)!=GBAX_PAGE_SIZE)return false;
	uint32_t expected=_xsGet(head+32); _xsPut(head+32,0);
	if(expected!=doCrc32(head,sizeof(head)))return false;
	unsigned count=0;
	for(unsigned i=0;i<GBAX_PAGE_COUNT;++i)if(head[36+i/8]&(1u<<(i%8)))++count;
	if(count!=_xsGet(head+24) || f->size(f)!=(ssize_t)(sizeof(head)+sizeof(struct GBASerializedState)+(size_t)count*(GBAX_PAGE_SIZE+4)))return false;
	struct GBASerializedState* native=malloc(sizeof(*native));
	struct GBAXMemory* next=calloc(1,sizeof(*next));
	bool ok=native && next && _xsRead(f,native,sizeof(*native)) &&
		doCrc32(native,sizeof(*native))==_xsGet(head+28);
	unsigned turbo=1, fraction=0;
	if(ok) {
		uint32_t pc, rom, version, bios, tag;
		LOAD_32(pc,ARM_PC*sizeof(native->cpu.gprs[0]),native->cpu.gprs);
		LOAD_32(rom,0,&native->romCrc32);
		LOAD_32(version,0,&native->versionMagic);
		LOAD_32(bios,0,&native->biosChecksum);
		/* GBAX is data-only: reject execution from extended or unknown regions.
		 * Require exact native version/ROM/BIOS for this experimental format. */
		unsigned region=pc>>24;
		ok=rom==gba->romCrc32 && version==GBASavestateMagic+GBASavestateVersion &&
			bios==gba->biosChecksum && !(pc&1) &&
			(region==0 || region==2 || region==3 || (region>=8 && region<=13));
		LOAD_32(tag,0,native->reserved);
		if(tag==0x58544350u) {
			LOAD_32(turbo,4,native->reserved);
			LOAD_32(fraction,8,native->reserved);
			ok=ok && (turbo==1 || turbo==2 || turbo==4 || turbo==8) && fraction<8 &&
				(turbo!=1 || fraction==0);
		} else if(tag!=0) ok=false;
	}
	for(unsigned i=0;ok && i<GBAX_PAGE_COUNT;++i) {
		if(!(head[36+i/8]&(1u<<(i%8))))continue;
		uint8_t crc[4];
		next->pages[i]=malloc(GBAX_PAGE_SIZE);
		ok=next->pages[i] && _xsRead(f,crc,4) && _xsRead(f,next->pages[i],GBAX_PAGE_SIZE) &&
			_xsGet(crc)==doCrc32(next->pages[i],GBAX_PAGE_SIZE);
		if(ok)++next->allocatedPages;
	}
	/* Do not mutate live memory until the complete envelope is validated. */
	if(ok)ok=_GBADeserializeNative(gba,native);
	if(ok) {
		struct GBAXMemory* old=gba->memory.gbax;
		gba->memory.gbax=next;
		next=old;
		ARMSetComputeTurbo(gba->cpu,turbo);
		gba->cpu->gbaxFraction=fraction;
		/* Never roll back persistent storage. LOAD is required before COMMIT. */
		GBAXSaveReset(&gba->memory);
	}
	GBAXClear(next);
	free(next); free(native);
	return ok;
}

/* Native rewind, bounded immutable pages shared between adjacent snapshots.
 * Only the Libretro emulation thread uses this history. No disk writes,
 * no 64 MiB Java envelopes and no persistent SAVX session rollback. */
#define GBAX_REWIND_LIMIT 60u
struct GBAXRewindPage {
    unsigned refs;
    uint8_t data[GBAX_PAGE_SIZE];
};
struct GBAXRewindSnapshot {
    struct GBASerializedState native;
    struct GBAXRewindPage* pages[GBAX_PAGE_COUNT];
    uint64_t saveEpoch;
    uint32_t rom;
    unsigned turbo, fraction;
};
static struct GBAXRewindSnapshot* _rwHistory[GBAX_REWIND_LIMIT];
static size_t _rwCount, _rwBytes;

static void _rwFree(struct GBAXRewindSnapshot* s) {
    if (!s) return;
    for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
        struct GBAXRewindPage* p=s->pages[i];
        if (p && !--p->refs) { _rwBytes-=sizeof(*p); free(p); }
    }
    _rwBytes-=sizeof(*s);
    free(s);
}
static void _rwEvict(void) {
    if (!_rwCount) return;
    _rwFree(_rwHistory[0]);
    --_rwCount;
    memmove(_rwHistory,_rwHistory+1,_rwCount*sizeof(*_rwHistory));
    _rwHistory[_rwCount]=NULL;
}
void GBAXRewindClear(void) {
    while (_rwCount) _rwEvict();
}
size_t GBAXRewindCount(void) { return _rwCount; }
void GBAXRewindCapture(struct GBA* gba,size_t budget) {
    if (!gba || !gba->memory.gbax || budget<sizeof(struct GBAXRewindSnapshot)) {
        GBAXRewindClear(); return;
    }
    uint64_t epoch=GBAXSaveEpoch(&gba->memory);
    if (_rwCount && (_rwHistory[_rwCount-1]->rom!=gba->romCrc32 ||
            _rwHistory[_rwCount-1]->saveEpoch!=epoch)) GBAXRewindClear();
    /* Keep the newest snapshot alive while comparing pages. If necessary
     * abandon sharing and restart with a fresh baseline. */
    while (_rwCount>1 && (_rwBytes+sizeof(struct GBAXRewindSnapshot)>budget || _rwCount>=GBAX_REWIND_LIMIT)) _rwEvict();
    if (_rwBytes+sizeof(struct GBAXRewindSnapshot)>budget) GBAXRewindClear();
    struct GBAXRewindSnapshot* prev=_rwCount?_rwHistory[_rwCount-1]:NULL;
    struct GBAXRewindSnapshot* s=calloc(1,sizeof(*s));
    if (!s) { GBAXRewindClear(); return; }
    _rwBytes+=sizeof(*s);
    s->saveEpoch=epoch; s->rom=gba->romCrc32;
    s->turbo=gba->cpu->gbaxTurbo; s->fraction=gba->cpu->gbaxFraction;
    _GBASerializeNative(gba,&s->native);
    /* First retain every unchanged page before any history eviction. */
    for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
        const uint8_t* live=gba->memory.gbax->pages[i];
        struct GBAXRewindPage* old=prev?prev->pages[i]:NULL;
        if (live && old && !memcmp(live,old->data,GBAX_PAGE_SIZE)) {
            ++old->refs; s->pages[i]=old;
        }
    }
    for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
        const uint8_t* live=gba->memory.gbax->pages[i];
        if (s->pages[i] || !_xsNonzero(live)) continue;
        while (_rwCount && _rwBytes+sizeof(struct GBAXRewindPage)>budget) _rwEvict();
        if (_rwBytes+sizeof(struct GBAXRewindPage)>budget) { _rwFree(s); GBAXRewindClear(); return; }
        struct GBAXRewindPage* p=malloc(sizeof(*p));
        if (!p) { _rwFree(s); GBAXRewindClear(); return; }
        p->refs=1; memcpy(p->data,live,GBAX_PAGE_SIZE);
        s->pages[i]=p; _rwBytes+=sizeof(*p);
    }
    while (_rwCount>=GBAX_REWIND_LIMIT) _rwEvict();
    _rwHistory[_rwCount++]=s;
}
bool GBAXRewindStep(struct GBA* gba) {
    if (!gba || !gba->memory.gbax || !_rwCount) return false;
    struct GBAXRewindSnapshot* s=_rwHistory[_rwCount-1];
    if (s->rom!=gba->romCrc32 || s->saveEpoch!=GBAXSaveEpoch(&gba->memory)) {
        GBAXRewindClear(); return false;
    }
    /* Allocate only newly needed live pages, never a second full RAM copy.
     * Preflight all allocations before changing CPU or RAM. */
    struct GBAXMemory* ram=gba->memory.gbax;
    uint8_t* created[GBAX_PAGE_COUNT]={0};
    for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
        if (s->pages[i] && !ram->pages[i]) {
            created[i]=malloc(GBAX_PAGE_SIZE);
            if (!created[i]) {
                for (unsigned j=0;j<GBAX_PAGE_COUNT;++j) free(created[j]);
                GBAXRewindClear(); return false;
            }
        }
    }
    if (!_GBADeserializeNative(gba,&s->native)) {
        for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) free(created[i]);
        GBAXRewindClear(); return false;
    }
    for (unsigned i=0;i<GBAX_PAGE_COUNT;++i) {
        if (!s->pages[i]) {
            if (ram->pages[i]) { free(ram->pages[i]); ram->pages[i]=NULL; --ram->allocatedPages; }
        } else {
            if (!ram->pages[i]) { ram->pages[i]=created[i]; ++ram->allocatedPages; }
            memcpy(ram->pages[i],s->pages[i]->data,GBAX_PAGE_SIZE);
        }
    }
    ARMSetComputeTurbo(gba->cpu,s->turbo);
    gba->cpu->gbaxFraction=s->fraction;
    /* Same SAVX epoch: the host session is still valid. Unlike named states,
     * do not reset or change the persistent session here. */
    _rwHistory[--_rwCount]=NULL; _rwFree(s);
    return true;
}
