#ifndef SAVEMODE_H
#define SAVEMODE_H

#include <stdint.h>

/* gamecode (ROM 0xAC, 4 chars) -> EZ-Flash save type byte */
uint8_t savemode_lookup(const uint8_t gamecode[4]);
/* save type -> save file size in bytes */
uint32_t savemode_size(uint8_t mode);

#endif
