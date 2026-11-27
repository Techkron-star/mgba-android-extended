/* SPDX-License-Identifier: MPL-2.0 */
#ifndef GBAX_MEMORY_H
#define GBAX_MEMORY_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GBAX_RAM_BASE UINT32_C(0x40000000)
#define GBAX_RAM_SIZE UINT32_C(0x04000000)
#define GBAX_PAGE_SIZE UINT32_C(0x10000)
#define GBAX_PAGE_COUNT (GBAX_RAM_SIZE / GBAX_PAGE_SIZE)

/* Only marked test ROMs allocate a page table; backing pages are zeroed on
 * first write, not on read. Little-endian, data-only guest memory. */
struct GBAXMemory {
    uint8_t* pages[GBAX_PAGE_COUNT];
    uint32_t allocatedPages;
};

static inline bool GBAXMarkedROM(const void* rom, size_t size) {
    const uint8_t* bytes = (const uint8_t*) rom;
    return bytes && size >= 0xC0 && bytes[0xB2] == 0x96 &&
        !memcmp(bytes + 0xA0, "GBAX RAM64  GBAX", 16);
}

static inline bool GBAXContains(const struct GBAXMemory* x, uint32_t address) {
    return x && address - GBAX_RAM_BASE < GBAX_RAM_SIZE;
}

static inline void GBAXClear(struct GBAXMemory* x) {
    if (!x) return;
    for (uint32_t i = 0; i < GBAX_PAGE_COUNT; ++i) {
        free(x->pages[i]);
        x->pages[i] = NULL;
    }
    x->allocatedPages = 0;
}

static inline bool GBAXWidthValid(unsigned width) {
    return width == 1 || width == 2 || width == 4;
}

static inline uint32_t GBAXRead(const struct GBAXMemory* x, uint32_t address, unsigned width) {
    if (!GBAXContains(x, address) || !GBAXWidthValid(width)) return 0;
    uint32_t offset = (address - GBAX_RAM_BASE) & ~(uint32_t)(width - 1);
    const uint8_t* page = x->pages[offset / GBAX_PAGE_SIZE];
    if (!page) return 0;
    offset %= GBAX_PAGE_SIZE;
    uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i)
        value |= (uint32_t) page[offset + i] << (8 * i);
    return value;
}

static inline bool GBAXWrite(struct GBAXMemory* x, uint32_t address, unsigned width, uint32_t value) {
    if (!GBAXContains(x, address) || !GBAXWidthValid(width)) return false;
    uint32_t offset = (address - GBAX_RAM_BASE) & ~(uint32_t)(width - 1);
    uint32_t index = offset / GBAX_PAGE_SIZE;
    if (!x->pages[index]) {
        if (!value) return true;
        x->pages[index] = (uint8_t*) calloc(1, GBAX_PAGE_SIZE);
        if (!x->pages[index]) return false;
        ++x->allocatedPages;
    }
    offset %= GBAX_PAGE_SIZE;
    for (unsigned i = 0; i < width; ++i)
        x->pages[index][offset + i] = (uint8_t)(value >> (8 * i));
    return true;
}
#endif
