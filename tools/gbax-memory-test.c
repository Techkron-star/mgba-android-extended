/* SPDX-License-Identifier: MPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <mgba/internal/gba/gbax-memory.h>

int main(void) {
    uint8_t rom[0xC0] = {0};
    assert(!GBAXMarkedROM(rom, sizeof(rom)));
    memcpy(rom + 0xA0, "GBAX RAM64  GBAX", 16);
    rom[0xB2] = 0x96;
    assert(GBAXMarkedROM(rom, sizeof(rom)));
    assert(!GBAXMarkedROM(rom, sizeof(rom)-1));
    struct GBAXMemory* x = calloc(1, sizeof(*x));
    assert(x);
    assert(!GBAXContains(NULL, GBAX_RAM_BASE));
    assert(!GBAXContains(x, GBAX_RAM_BASE-1));
    assert(!GBAXContains(x, GBAX_RAM_BASE+GBAX_RAM_SIZE));
    assert(!GBAXWrite(x, GBAX_RAM_BASE, 3, 1));
    assert(!GBAXWrite(x, GBAX_RAM_BASE+GBAX_RAM_SIZE, 4, 1));
    assert(GBAXRead(x, GBAX_RAM_BASE, 4) == 0);
    assert(GBAXWrite(x, GBAX_RAM_BASE, 4, 0));
    assert(x->allocatedPages == 0);
    assert(!GBAXPageDirty(x, 0));
    for (uint32_t i = 0; i < GBAX_PAGE_COUNT; ++i) {
        uint32_t a = GBAX_RAM_BASE+i*GBAX_PAGE_SIZE;
        assert(GBAXWrite(x, a, 4, i+1));
        assert(GBAXWrite(x, a+GBAX_PAGE_SIZE-1, 1, 0xA5));
        assert(GBAXPageDirty(x, i));
    }
    assert(x->allocatedPages == GBAX_PAGE_COUNT);
    for (uint32_t i = 0; i < GBAX_PAGE_COUNT; ++i) {
        uint32_t a = GBAX_RAM_BASE+i*GBAX_PAGE_SIZE;
        assert(GBAXRead(x, a+3, 4) == i+1);
        assert(GBAXRead(x, a+GBAX_PAGE_SIZE-1, 1) == 0xA5);
        assert(GBAXWrite(x, a+7, 2, 0xABCD));
        assert(GBAXRead(x, a+6, 1) == 0xCD);
        assert(GBAXRead(x, a+7, 1) == 0xAB);
    }
    assert(GBAXWrite(x, GBAX_RAM_BASE+GBAX_RAM_SIZE-1, 4, 0x12345678));
    assert(GBAXRead(x, GBAX_RAM_BASE+GBAX_RAM_SIZE-4, 4) == 0x12345678);
    GBAXClear(x);
    assert(x->allocatedPages == 0);
    for (unsigned i = 0; i < GBAX_PAGE_COUNT; ++i) assert(GBAXPageDirty(x, i));
    assert(GBAXRead(x, GBAX_RAM_BASE+GBAX_RAM_SIZE-1, 4) == 0);
    GBAXClear(x);
    free(x);
    puts("PASS: GBAX 64 MiB sparse pages, bounds, byte order, alignment, marker, reset");
}
