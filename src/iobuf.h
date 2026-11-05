#ifndef IOBUF_H
#define IOBUF_H

#include <stdint.h>

/* Shared 64 KiB transfer buffer (EWRAM). */
#define IOBUF_SIZE 0x10000u
extern uint8_t g_iobuf[IOBUF_SIZE];

#endif
