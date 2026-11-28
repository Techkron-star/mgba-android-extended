/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

M_TEST_DEFINE(create) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	core->deinit(core);
}

M_TEST_DEFINE(platform) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->platform(core) == mPLATFORM_GBA);
	assert_true(core->init(core));
	core->deinit(core);
}

M_TEST_DEFINE(reset) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->reset(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

M_TEST_DEFINE(loadNullROM) {
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	assert_false(core->loadROM(core, NULL));
	mCoreInitConfig(core, NULL);
	core->reset(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

/* Guest integration regression. The ROM is built from original assembly in CI. */
M_TEST_DEFINE(gbaxGuest) {
	const char* path = getenv("GBAX_TEST_ROM");
	if (!path) {
		skip();
	}
	struct mCore* core = GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core, NULL);
	core->opts.useBios = false;
	core->opts.skipBios = true;
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	assert_non_null(vf);
	assert_true(core->loadROM(core, vf));
	core->reset(core);
	assert_int_equal(core->rawRead32(core, 0x40000000, -1), 0);
	for (unsigned i = 0; i < 60000; ++i) {
		core->step(core);
		if (core->busRead32(core, 0x02000000)) break;
	}
	assert_int_equal(core->busRead32(core, 0x02000000), 0x600d600d);
	assert_int_equal(core->rawRead32(core, 0x40000000, -1), 0x5a5a0000);
	assert_int_equal(core->rawRead32(core, 0x43fffffc, -1), 0xa5a503ff);
	assert_int_equal(core->rawRead32(core, 0x43fffff0, -1), 0xdeadbeef);
	assert_int_equal(core->rawRead32(core, 0x43fffff4, -1), 0x10203040);
	assert_int_equal(core->busRead32(core, 0x40000100), 0xcdefab78);
	core->rawWrite32(core, 0x41001000, -1, 0x42abc123);
	assert_int_equal(core->busRead32(core, 0x41001000), 0x42abc123);
	void* snapshot = calloc(1, core->stateSize(core));
	assert_non_null(snapshot);
	assert_false(core->saveState(core, snapshot));
	assert_false(core->loadState(core, snapshot));
	core->reset(core);
	assert_int_equal(core->rawRead32(core, 0x40000000, -1), 0);
	assert_int_equal(core->rawRead32(core, 0x43fffffc, -1), 0);
	core->unloadROM(core);
	/* Same original program, marker removed: legacy bus and state behavior. */
	FILE* f = fopen(path, "rb");
	assert_non_null(f);
	uint8_t rom[32768];
	assert_int_equal(fread(rom, 1, sizeof(rom), f), sizeof(rom));
	fclose(f);
	rom[0xA0] = 'N';
	vf = VFileMemChunk(rom, sizeof(rom));
	assert_non_null(vf);
	assert_true(core->loadROM(core, vf));
	core->reset(core);
	core->busWrite32(core, 0x02000000, 0x12345678);
	assert_int_equal(core->busRead32(core, 0x02000000), 0x12345678);
	core->rawWrite32(core, 0x40000000, -1, 0xffffffff);
	assert_int_equal(core->rawRead32(core, 0x40000000, -1), 0);
	assert_true(core->saveState(core, snapshot));
	assert_true(core->loadState(core, snapshot));
	free(snapshot);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

#if defined(ENABLE_VFS) && defined(ENABLE_DIRECTORIES)
M_TEST_DEFINE(gbaxSavxBridge) {
    const char* path=getenv("GBAX_SAVX_TEST_ROM");
    if(!path)skip();
    const uint32_t reg=0x44000000, stage=0x43000000;
    struct mCore* core=GBACoreCreate();
    assert_non_null(core);
    assert_true(core->init(core));
    mCoreInitConfig(core,NULL);
    core->opts.useBios=false; core->opts.skipBios=true;
    assert_true(mCoreLoadFile(core,path));
    assert_true(mCoreAutoloadSave(core));
    core->reset(core);
    assert_int_equal(core->busRead32(core,reg),0x58564153);
    assert_int_equal(core->busRead32(core,reg+4),1);
    core->busWrite32(core,reg+8,2);
    assert_int_equal(core->busRead32(core,reg+4),0x104);
    core->busWrite32(core,reg+8,1);
    assert_int_equal(core->busRead32(core,reg+4),3);
    core->busWrite32(core,stage,0x1234abcd);
    core->busWrite32(core,0x43fffffc,0xdeadbeef);
    core->busWrite32(core,0x40000000,0x98765432);
    core->busWrite32(core,reg+8,2);
    assert_int_equal(core->busRead32(core,reg+4),4);
    assert_int_equal(core->busRead32(core,reg+12),1);
    core->busWrite32(core,stage,0x87654321);
    core->busWrite32(core,reg+8,2);
    assert_int_equal(core->busRead32(core,reg+4),4);
    assert_int_equal(core->busRead32(core,reg+12),2);
    core->reset(core);
    assert_int_equal(core->busRead32(core,stage),0);
    assert_int_equal(core->busRead32(core,reg+4),1);
    core->busWrite32(core,reg+8,1);
    assert_int_equal(core->busRead32(core,reg+4),2);
    assert_int_equal(core->busRead32(core,stage),0x87654321);
    assert_int_equal(core->busRead32(core,0x43fffffc),0xdeadbeef);
    assert_int_equal(core->busRead32(core,0x40000000),0);
    core->unloadROM(core);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    core=GBACoreCreate();
    assert_non_null(core);
    assert_true(core->init(core));
    mCoreInitConfig(core,NULL);
    core->opts.useBios=false; core->opts.skipBios=true;
    assert_true(mCoreLoadFile(core,path));
    assert_true(mCoreAutoloadSave(core));
    core->reset(core);
    core->busWrite32(core,reg+8,1);
    assert_int_equal(core->busRead32(core,reg+4),2);
    assert_int_equal(core->rawRead32(core,reg+12,-1),2);
    assert_int_equal(core->busRead32(core,stage),0x87654321);
    assert_int_equal(core->busRead32(core,0x43fffffc),0xdeadbeef);
    core->unloadROM(core);
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
}
#endif

M_TEST_DEFINE(gbaxNamedState) {
	const char* path=getenv("GBAX_TEST_ROM");
	if(!path)skip();
	struct mCore* core=GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core,NULL);
	core->opts.useBios=false; core->opts.skipBios=true;
	struct VFile* rom=VFileOpen(path,O_RDONLY);
	assert_non_null(rom);
	assert_true(core->loadROM(core,rom));
	core->reset(core);
	struct VFile* snapshot=VFileMemChunk(NULL,0);
	assert_non_null(snapshot);
	core->busWrite32(core,0x02000000,0xabcdef01);
	core->busWrite32(core,0x40000000,0x11112222);
	core->busWrite32(core,0x43fffffc,0x33334444);
	core->busWrite32(core,0x42000000,1);
	core->busWrite32(core,0x42000000,0);
	assert_true(mCoreSaveStateNamed(core,snapshot,0));
	/* 164-byte envelope + native state + two nonzero pages, no zero page. */
	assert_int_equal(snapshot->size(snapshot),164+0x61000+2*(65536+4));
	core->busWrite32(core,0x02000000,0);
	core->busWrite32(core,0x40000000,0);
	core->busWrite32(core,0x41000000,0x55556666);
	assert_true(mCoreLoadStateNamed(core,snapshot,0));
	assert_int_equal(core->busRead32(core,0x02000000),0xabcdef01);
	assert_int_equal(core->busRead32(core,0x40000000),0x11112222);
	assert_int_equal(core->busRead32(core,0x43fffffc),0x33334444);
	assert_int_equal(core->busRead32(core,0x41000000),0);
	/* Damage the final page, then prove that the live state is unchanged. */
	off_t last=snapshot->size(snapshot)-1;
	assert_int_equal(snapshot->seek(snapshot,last,SEEK_SET),last);
	uint8_t bad=0xFF;
	assert_int_equal(snapshot->write(snapshot,&bad,1),1);
	core->busWrite32(core,0x02000000,0x77778888);
	core->busWrite32(core,0x40000000,0x9999aaaa);
	assert_false(mCoreLoadStateNamed(core,snapshot,0));
	assert_int_equal(core->busRead32(core,0x02000000),0x77778888);
	assert_int_equal(core->busRead32(core,0x40000000),0x9999aaaa);
	snapshot->truncate(snapshot,20);
	assert_false(mCoreLoadStateNamed(core,snapshot,0));
	snapshot->close(snapshot);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static void _gbaxDMA3(struct mCore* core,uint32_t src,uint32_t dst,uint16_t count,uint16_t control) {
	core->busWrite32(core,0x040000d4,src);
	core->busWrite32(core,0x040000d8,dst);
	core->busWrite16(core,0x040000dc,count);
	core->busWrite16(core,0x040000de,control);
}
M_TEST_DEFINE(gbaxDMA3) {
	const char* path=getenv("GBAX_TEST_ROM");
	if(!path)skip();
	struct mCore* core=GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core,NULL);
	core->opts.useBios=false; core->opts.skipBios=true;
	struct VFile* rom=VFileOpen(path,O_RDONLY);
	assert_non_null(rom);
	assert_true(core->loadROM(core,rom));
	core->reset(core);
	/* Run a safe branch-to-self in native IWRAM instead of the diagnostic.
	 * Use the core register API to set PC, with ARM mode left unchanged. */
	core->busWrite32(core,0x03000000,0xeafffffe);
	assert_true(core->writeRegister(core,"pc",0x03000000));
	for(unsigned i=0;i<32;++i)core->busWrite32(core,0x02001000+4*i,0x12340000+i);
	_gbaxDMA3(core,0x02001000,0x40ffffe0,32,0x8400);
	for(unsigned i=0;i<2048;++i)core->step(core);
	for(unsigned i=0;i<32;++i)assert_int_equal(core->busRead32(core,0x40ffffe0+4*i),0x12340000+i);
	_gbaxDMA3(core,0x40ffffe0,0x43ffff80,32,0x8400);
	for(unsigned i=0;i<2048;++i)core->step(core);
	for(unsigned i=0;i<32;++i)assert_int_equal(core->busRead32(core,0x43ffff80+4*i),0x12340000+i);
	_gbaxDMA3(core,0x43ffff80,0x02002000,64,0x8000);
	for(unsigned i=0;i<2048;++i)core->step(core);
	for(unsigned i=0;i<32;++i)assert_int_equal(core->busRead32(core,0x02002000+4*i),0x12340000+i);
	/* Fixed source fill and decrement copy. */
	_gbaxDMA3(core,0x02001000,0x42000000,16,0x8500);
	for(unsigned i=0;i<2048;++i)core->step(core);
	for(unsigned i=0;i<16;++i)assert_int_equal(core->busRead32(core,0x42000000+4*i),0x12340000);
	_gbaxDMA3(core,0x0200107c,0x4200017c,32,0x84a0);
	for(unsigned i=0;i<2048;++i)core->step(core);
	for(unsigned i=0;i<32;++i)assert_int_equal(core->busRead32(core,0x42000100+4*i),0x12340000+i);
	/* Crossing the aperture is rejected before any word is written. */
	core->busWrite32(core,0x43fffffc,0xabcdef01);
	_gbaxDMA3(core,0x02001000,0x43fffffc,2,0x8400);
	for(unsigned i=0;i<64;++i)core->step(core);
	assert_int_equal(core->busRead32(core,0x43fffffc),0xabcdef01);
	assert_int_equal(core->busRead16(core,0x040000de)&0x8000,0);
	/* The SAVX control register must not be reachable via DMA. */
	_gbaxDMA3(core,0x02001000,0x44000008,1,0x8400);
	assert_int_equal(core->busRead16(core,0x040000de)&0x8000,0);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

static unsigned _gbaxTurboFrame(struct mCore* core,unsigned mult,bool thumb) {
	mCoreConfigSetUIntValue(&core->config,"gbax.cpuMultiplier",mult);
	core->reset(core);
	/* Pure register loop in IWRAM; avoid idle-loop removal via changing r0. */
	if(thumb) {
		core->busWrite16(core,0x03000000,0x3001); /* adds r0,#1 */
		core->busWrite16(core,0x03000002,0xe7fd); /* b 0x03000000 */
		assert_true(core->writeRegister(core,"cpsr",0x3f));
	} else {
		core->busWrite32(core,0x03000000,0xe2800001); /* add r0,r0,#1 */
		core->busWrite32(core,0x03000004,0xeafffffd); /* b 0x03000000 */
	}
	assert_true(core->writeRegister(core,"r0",0));
	assert_true(core->writeRegister(core,"pc",0x03000000));
	core->busWrite16(core,0x04000100,0);
	core->busWrite16(core,0x04000102,0x80);
	core->runFrame(core);
	int32_t counter=0;
	assert_true(core->readRegister(core,"r0",&counter));
	assert_true(counter>0);
	return (unsigned)counter;
}
M_TEST_DEFINE(gbaxComputeTurbo) {
	const char* path=getenv("GBAX_TEST_ROM");
	if(!path)skip();
	struct mCore* core=GBACoreCreate();
	assert_non_null(core);
	assert_true(core->init(core));
	mCoreInitConfig(core,NULL);
	core->opts.useBios=false;core->opts.skipBios=true;
	struct VFile* rom=VFileOpen(path,O_RDONLY);
	assert_non_null(rom);
	assert_true(core->loadROM(core,rom));
	for(unsigned thumb=0;thumb<2;++thumb) {
		unsigned one=_gbaxTurboFrame(core,1,thumb);
		unsigned timer=core->busRead16(core,0x04000100);
		for(unsigned mult=2;mult<=8;mult*=2) {
			unsigned fast=_gbaxTurboFrame(core,mult,thumb);
			unsigned tick=core->busRead16(core,0x04000100);
			assert_true(fast>one*(mult-1));
			/* Native timer remains on the master clock, minor event overshoot allowed. */
			int diff=(int16_t)(tick-timer);
			assert_true(diff>=-32 && diff<=32);
		}
	}
	/* Invalid config falls back to native. */
	unsigned standard=_gbaxTurboFrame(core,1,false);
	assert_int_equal(_gbaxTurboFrame(core,3,false),standard);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

M_TEST_SUITE_DEFINE(GBACore,
	cmocka_unit_test(create),
	cmocka_unit_test(platform),
	cmocka_unit_test(reset),
	cmocka_unit_test(loadNullROM),
	cmocka_unit_test(gbaxGuest),
	cmocka_unit_test(gbaxNamedState),
	cmocka_unit_test(gbaxDMA3),
	cmocka_unit_test(gbaxComputeTurbo)
#if defined(ENABLE_VFS) && defined(ENABLE_DIRECTORIES)
	, cmocka_unit_test(gbaxSavxBridge)
#endif
)
