/* SPDX-License-Identifier: MPL-2.0 */
#ifndef GBAX_SAVX_BRIDGE_H
#define GBAX_SAVX_BRIDGE_H
#include <mgba/internal/gba/gbax-savx.h>

#define GBAX_SAVX_STAGE UINT32_C(0x43000000)
#define GBAX_SAVX_MMIO UINT32_C(0x44000000)
#define GBAX_SAVX_ID UINT32_C(0x58564153)
#define GBAX_SAVX_CMD_LOAD 1u
#define GBAX_SAVX_CMD_COMMIT 2u
enum {
    GBAX_SAVX_UNAVAILABLE, GBAX_SAVX_READY, GBAX_SAVX_LOADED,
    GBAX_SAVX_EMPTY, GBAX_SAVX_COMMITTED,
    GBAX_SAVX_ERROR_BASE = 0x100, GBAX_SAVX_NO_MEMORY = 0x110
};
struct GBAMemory;
struct VFile;
struct GBAXSaveSession;
/* Takes ownership of both files, even on failure. */
bool GBAXSaveAttach(struct GBAMemory*, struct VFile*, struct VFile*, uint32_t romId);
void GBAXSaveDetach(struct GBAMemory*);
void GBAXSaveReset(struct GBAMemory*);
static inline bool GBAXSaveRegister(uint32_t address) {
    return address - GBAX_SAVX_MMIO <= 0x10u && !(address & 3u);
}
uint32_t GBAXSaveReadRegister(const struct GBAMemory*, uint32_t address);
void GBAXSaveWriteRegister(struct GBAMemory*, uint32_t address, uint32_t value);
#endif
