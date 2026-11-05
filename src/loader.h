#ifndef LOADER_H
#define LOADER_H

#include <stdint.h>

/* Copies the ROM at path into EZ-Flash PSRAM and boots it.
 * Returns 0 if the file was copied (the boot call does not return),
 * negative on error.  progress is called with 0..100. */
int loader_load(const char *path, void (*progress)(int pct, const char *stage));

#endif
