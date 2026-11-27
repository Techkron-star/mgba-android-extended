/* SPDX-License-Identifier: MPL-2.0 */
#ifndef GBAX_SAVX_H
#define GBAX_SAVX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBAX_SAVX_SIZE UINT32_C(0x01000000)
#define GBAX_SAVX_HEADER 36u
#define GBAX_SAVX_FILE_SIZE (GBAX_SAVX_HEADER + GBAX_SAVX_SIZE)

/* Two independent files, each containing one complete generation.
 * All callbacks must return exact success/failure. sync must be durable.
 * The caller owns the files, payload buffer and any necessary locking. */
struct GBAXSaveIO {
    void* context;
    int64_t (*size)(void*);
    bool (*read)(void*, uint32_t, void*, size_t);
    bool (*write)(void*, uint32_t, const void*, size_t);
    bool (*sync)(void*);
};
enum GBAXSaveResult {
    GBAX_SAVE_OK, GBAX_SAVE_EMPTY, GBAX_SAVE_CORRUPT,
    GBAX_SAVE_IO_ERROR, GBAX_SAVE_INVALID, GBAX_SAVE_EXHAUSTED
};
/* Exactly 16 MiB. Empty initializes payload to zero; corruption never does.
 * Save must be called with an independently initialized/loaded payload. */
enum GBAXSaveResult GBAXSaveLoad(const struct GBAXSaveIO slots[2],
    uint32_t romId, uint8_t* payload, size_t size, uint64_t* generation);
enum GBAXSaveResult GBAXSaveCommit(const struct GBAXSaveIO slots[2],
    uint32_t romId, const uint8_t* payload, size_t size, uint64_t* generation);
#endif
