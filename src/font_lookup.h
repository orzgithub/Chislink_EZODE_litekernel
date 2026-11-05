/* ChisLink remote font lookup (CLP_CH_MANAGER / CLP_MANAGER_FONT_LOOKUP).
 *
 * The public SDK has no client wrapper for this opcode, so the wire format is
 * reverse engineered from the protocol header:
 *   request : N * 4 bytes, one UTF-8 encoded codepoint per record (zero padded)
 *   response: N * (4 + 128) bytes, the UTF-8 echo + one 128-byte glyph tile
 * A probe build is used to confirm the exact encoding against real hardware.
 */
#ifndef LITE_FONT_LOOKUP_H
#define LITE_FONT_LOOKUP_H

#include <stdint.h>

#include "chislink/client.h"
#include "chislink/proto.h"

#define CLF_MAX_KEYS           CLP_MANAGER_FONT_LOOKUP_MAX_KEYS
#define CLF_MAX_RECORDS        CLP_MANAGER_FONT_LOOKUP_MAX_RECORDS
#define CLF_TILE_BYTES         CLP_MANAGER_FONT_TILE_BYTES
#define CLF_RECORD_BYTES       CLP_MANAGER_FONT_RECORD_BYTES
#define CLF_MAX_RESPONSE_BYTES CLP_MANAGER_FONT_LOOKUP_MAX_RESPONSE_BYTES

typedef struct clf_glyph {
    uint8_t utf8[4];
    uint8_t tile[CLF_TILE_BYTES];
} clf_glyph_t;

/* Send a raw lookup request and copy the response payload into `response`.
 * `response_len` receives the number of bytes actually stored. Returns 0 on a
 * protocol level success (the response header was received), negative on
 * transport/wire errors. */
int clf_request_raw(cl_client_t *client,
                    const uint8_t *request,
                    uint32_t request_len,
                    uint8_t *response,
                    uint32_t response_cap,
                    uint32_t *response_len);

/* Look up up to CLF_MAX_KEYS codepoints. Uses the UTF-8 record encoding. */
void clf_debug_exchange(cl_client_t *client, const uint8_t *request, uint32_t request_len, uint32_t *out_words, uint32_t out_word_count);
int clf_lookup(cl_client_t *client,
               const uint32_t *codepoints,
               uint32_t count,
               clf_glyph_t *out,
               uint32_t max_records,
               uint32_t *out_count);

#endif
