/* ChisLink remote glyph rendering for the lite kernel.
 *
 * Glyphs come from CLP_CH_MANAGER / CLP_MANAGER_FONT_LOOKUP as 132 byte
 * records: 4 byte UTF-8 echo + one 128 byte tile.  The tile is an 8x16 cell
 * at 4bpp, low nibble = left pixel, 4 bytes per row: rows 0..7 live at bytes
 * 0..31 and rows 8..15 at bytes 64..95 (the two 32 byte gaps are unused).
 * The compact glyph itself occupies rows 4..11 of the cell.  Layout variants are selectable
 * so a probe build can confirm the exact geometry on hardware.
 */
#ifndef LITE_LFONT_H
#define LITE_LFONT_H

#include <stdint.h>

#include "chislink/client.h"

#define LFONT_CELL        16
#define LFONT_TILE_BYTES  128
#define LFONT_CACHE_SIZE  192

void lfont_init(cl_client_t *client);
/* Draw `text` (UTF-8) at (x,y) with palette index `color`. Returns the x after
 * the last glyph. */
int lfont_draw(int x, int y, uint8_t color, const char *text);
/* Blit a raw tile using the currently selected layout. */
void lfont_blit_tile(int x, int y, uint8_t color, const uint8_t *tile);
void lfont_set_layout(uint8_t layout);
void lfont_set_scale(uint8_t scale);
uint8_t lfont_get_layout(void);
int lfont_last_ret(void);
uint32_t lfont_last_count(void);

#endif
