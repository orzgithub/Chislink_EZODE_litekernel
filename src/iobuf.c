#include <stdint.h>

#include "iobuf.h"

uint8_t g_iobuf[IOBUF_SIZE] __attribute__((section(".sbss"), aligned(4)));
