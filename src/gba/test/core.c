/* Copyright (c) 2013-2016 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "util/test/suite.h"

#include <mgba/core/core.h>
#include <mgba/gba/core.h>
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
	void* state = calloc(1, core->stateSize(core));
	assert_non_null(state);
	assert_false(core->saveState(core, state));
	assert_false(core->loadState(core, state));
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
	assert_true(core->saveState(core, state));
	assert_true(core->loadState(core, state));
	free(state);
	core->unloadROM(core);
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

M_TEST_SUITE_DEFINE(GBACore,
	cmocka_unit_test(create),
	cmocka_unit_test(platform),
	cmocka_unit_test(reset),
	cmocka_unit_test(loadNullROM),
	cmocka_unit_test(gbaxGuest))
