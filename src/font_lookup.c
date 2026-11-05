#include <string.h>

#include "font_lookup.h"
#include "chislink/wire.h"

static uint32_t clf_utf8_encode(uint32_t cp, uint8_t out[4])
{
    if (cp < 0x80u) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (uint8_t)(0xF0u | (cp >> 18));
    out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4;
}


void clf_debug_exchange(cl_client_t *client,
                        const uint8_t *request,
                        uint32_t request_len,
                        uint32_t *out_words,
                        uint32_t out_word_count)
{
    clp_header_t header;
    uint32_t words[4];
    uint32_t i;

    if (!client || !client->config.xfer32 || !request || !out_words) {
        return;
    }

    header.type = CLP_TYPE_COMMAND;
    header.channel = (uint8_t)CLP_CH_MANAGER;
    header.opcode = (uint8_t)CLP_MANAGER_FONT_LOOKUP;
    header.imm = 0;
    header.length = request_len;
    header.seq = cl_client_next_seq(client);
    header.flags = CLP_FLAG_NEEDS_ACK;
    header.crc32 = 0;

    clp_encode_header_words(&header, words);
    for (i = 0; i < 4u; ++i) {
        (void)client->config.xfer32(words[i], client->config.transport_user);
    }
    for (i = 0; i < request_len; i += 4u) {
        uint32_t value = 0;
        uint32_t b;
        for (b = 0; b < 4u && (i + b) < request_len; ++b) {
            value |= (uint32_t)request[i + b] << (8u * b);
        }
        (void)client->config.xfer32(value, client->config.transport_user);
    }
    for (i = 0; i < out_word_count; ++i) {
        out_words[i] = client->config.xfer32(
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            client->config.transport_user);
    }
}

int clf_request_raw(cl_client_t *client,
                    const uint8_t *request,
                    uint32_t request_len,
                    uint8_t *response,
                    uint32_t response_cap,
                    uint32_t *response_len)
{
    cl_wire_chunk_t chunk;
    clp_header_t header;
    uint32_t length;
    uint32_t copy;
    int ret;

    if (!client || !request || !request_len || !response || !response_len) {
        return -1;
    }
    *response_len = 0;

    chunk.data = request;
    chunk.length = request_len;

    ret = cl_wire_command_chunks(client,
                                 (uint8_t)CLP_CH_MANAGER,
                                 (uint8_t)CLP_MANAGER_FONT_LOOKUP,
                                 &chunk, 1, &header);
    if (ret < 0) {
        return ret;
    }

    length = header.length;
    copy = (length > response_cap) ? response_cap : length;
    if (copy) {
        cl_wire_read_payload(client, response, copy);
    }
    if (length > copy) {
        cl_wire_discard_payload(client, length - copy);
    }
    *response_len = copy;
    return 0;
}

int clf_lookup(cl_client_t *client,
               const uint32_t *codepoints,
               uint32_t count,
               clf_glyph_t *out,
               uint32_t max_records,
               uint32_t *out_count)
{
    static uint8_t request[CLF_MAX_KEYS * 4];
    static uint8_t response[CLF_MAX_RESPONSE_BYTES];
    uint32_t length = 0;
    uint32_t records;
    uint32_t i;
    int ret;

    if (!client || !codepoints || !out || !out_count) {
        return -1;
    }
    *out_count = 0;
    if (!count || count > CLF_MAX_KEYS || !max_records) {
        return -1;
    }

    memset(request, 0, sizeof(request));
    for (i = 0; i < count; ++i) {
        clf_utf8_encode(codepoints[i], &request[i * 4]);
    }

    ret = clf_request_raw(client, request, count * 4, response,
                          sizeof(response), &length);
    if (ret < 0) {
        return ret;
    }

    records = length / CLF_RECORD_BYTES;
    if (records > max_records) {
        records = max_records;
    }
    for (i = 0; i < records; ++i) {
        memcpy(out[i].utf8, &response[i * CLF_RECORD_BYTES], 4);
        memcpy(out[i].tile, &response[i * CLF_RECORD_BYTES + 4],
               CLF_TILE_BYTES);
    }
    *out_count = records;
    return 0;
}
