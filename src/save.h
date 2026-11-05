#ifndef SAVE_H
#define SAVE_H

#include <stdint.h>

typedef struct save_ctx {
    uint8_t mode;     /* EZ-Flash save type byte */
    uint32_t size;    /* save file size in bytes, 0 = no save */
    uint8_t valid;    /* 1 = save loaded into the cartridge save memory */
} save_ctx_t;

/* Flush a save that a previously launched game left in the cartridge save
 * memory (called once at kernel start, after the file system is ready). */
int save_flush_pending(void);

/* Prepare save memory for a game: load (or create) its .sav file into the
 * cartridge save memory and tell the FPGA about the save type. */
int save_prepare(const char *rom_path, uint32_t rom_size,
                 const uint8_t gamecode[4], save_ctx_t *ctx);

#endif
