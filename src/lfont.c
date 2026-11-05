#include <string.h>

#include "lfont.h"
#include "font_lookup.h"
#include "chislink/gba/video.h"

typedef struct {
    uint32_t cp;
    uint8_t valid;
    uint8_t tile[LFONT_TILE_BYTES];
} lfont_entry_t;

static cl_client_t *s_client;
static lfont_entry_t s_cache[LFONT_CACHE_SIZE] __attribute__((section(".sbss")));
static uint32_t s_cache_used;
static uint32_t s_cache_next;
static uint8_t s_layout;
static uint8_t s_scale = 1;
static int s_last_ret;
static uint32_t s_last_count;

void lfont_init(cl_client_t *client)
{
    s_client = client;
    s_cache_used = 0;
    s_cache_next = 0;
    s_layout = 0;
    s_last_ret = 0;
    s_last_count = 0;
    memset(s_cache, 0, sizeof(s_cache));
}

void lfont_set_layout(uint8_t layout) { s_layout = layout; }
void lfont_set_scale(uint8_t scale) { s_scale = (scale == 0) ? 1 : scale; }
uint8_t lfont_get_layout(void) { return s_layout; }
int lfont_last_ret(void) { return s_last_ret; }
uint32_t lfont_last_count(void) { return s_last_count; }

static lfont_entry_t *cache_find(uint32_t cp)
{
    uint32_t i;
    for (i = 0; i < s_cache_used; ++i) {
        if (s_cache[i].valid && s_cache[i].cp == cp) {
            return &s_cache[i];
        }
    }
    return 0;
}

static lfont_entry_t *cache_add(uint32_t cp)
{
    lfont_entry_t *e;
    if (s_cache_used < LFONT_CACHE_SIZE) {
        e = &s_cache[s_cache_used++];
    } else {
        e = &s_cache[s_cache_next];
        s_cache_next = (s_cache_next + 1) % LFONT_CACHE_SIZE;
    }
    e->cp = cp;
    e->valid = 1;
    memset(e->tile, 0, sizeof(e->tile));
    return e;
}

static uint32_t utf8_encode(uint32_t cp, uint8_t out[4])
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

static const char *utf8_next(const char *text, uint32_t *cp)
{
    const uint8_t *s = (const uint8_t *)text;
    uint32_t c = s[0];
    uint32_t len;
    uint32_t v;

    if (c < 0x80u) {
        *cp = c;
        return text + 1;
    }
    if ((c & 0xE0u) == 0xC0u) {
        len = 2; v = c & 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
        len = 3; v = c & 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
        len = 4; v = c & 0x07u;
    } else {
        *cp = 0xFFFDu;
        return text + 1;
    }
    if ((s[1] & 0xC0u) != 0x80u) {
        *cp = 0xFFFDu;
        return text + 1;
    }
    v = (v << 6) | (s[1] & 0x3Fu);
    if (len >= 3) {
        if ((s[2] & 0xC0u) != 0x80u) {
            *cp = 0xFFFDu;
            return text + 1;
        }
        v = (v << 6) | (s[2] & 0x3Fu);
    }
    if (len == 4) {
        if ((s[3] & 0xC0u) != 0x80u) {
            *cp = 0xFFFDu;
            return text + 1;
        }
        v = (v << 6) | (s[3] & 0x3Fu);
    }
    *cp = v;
    return text + len;
}

static void put_block(int x, int y, uint8_t color)
{
    int i;
    int j;
    for (j = 0; j < (int)s_scale; ++j) {
        for (i = 0; i < (int)s_scale; ++i) {
            cl_gba_video_put_pixel((uint16_t)(x + i), (uint16_t)(y + j),
                                   color);
        }
    }
}

static uint8_t row_nibble(const uint8_t *tile, int row, int col)
{
    uint8_t b = tile[row * 4 + (col >> 1)];
    return (uint8_t)((col & 1) ? (b >> 4) : (b & 0x0Fu));
}

void lfont_blit_tile(int x, int y, uint8_t color, const uint8_t *tile)
{
    int px;
    int py;

    if (s_layout == 0) {
        /* 16x16 cell stored as four 8x8 4bpp quadrants:
         * TL 0..31, TR 32..63, BL 64..95, BR 96..127.
         * Low nibble is the left pixel. CJK glyphs occupy rows 3..12,
         * cols 0..10; ASCII glyphs rows 4..11, cols 0..5. */
        for (py = 0; py < 16; ++py) {
            int qy = (py >= 8) ? 2 : 0;
            int rr = py & 7;
            for (px = 0; px < 16; ++px) {
                int qx = (px >= 8) ? 1 : 0;
                const uint8_t *quad =
                    tile + (qy + qx) * 32 + rr * 4 + ((px & 7) >> 1);
                uint8_t v = *quad;
                v = (uint8_t)((px & 1) ? (v >> 4) : (v & 0x0Fu));
                if (v) {
                    put_block(x + px * (int)s_scale,
                              y + py * (int)s_scale, color);
                }
            }
        }
    } else if (s_layout == 1) {
        for (py = 0; py < 16; ++py) {
            for (px = 0; px < 16; ++px) {
                uint8_t b = tile[py * 8 + (px >> 1)];
                uint8_t v = (uint8_t)((px & 1) ? (b >> 4) : (b & 0x0Fu));
                if (v) {
                    put_block(x + px * (int)s_scale, y + py * (int)s_scale,
                              color);
                }
            }
        }
    } else if (s_layout == 2) {
        for (py = 0; py < 16; ++py) {
            for (px = 0; px < 16; ++px) {
                uint8_t b = tile[py * 8 + (px >> 1)];
                uint8_t v = (uint8_t)((px & 1) ? (b & 0x0Fu) : (b >> 4));
                if (v) {
                    put_block(x + px * (int)s_scale, y + py * (int)s_scale,
                              color);
                }
            }
        }
    } else {
        for (py = 0; py < 8; ++py) {
            for (px = 0; px < 8; ++px) {
                if (row_nibble(tile, py + 4, px)) {
                    put_block(x + px * (int)s_scale, y + py * (int)s_scale,
                              color);
                }
            }
        }
    }
}

/* Fetch up to `want` glyphs in one lookup request.  The manager echoes the
 * 4-byte UTF-8 key for every record, so records are matched by key. */
static void fetch_batch(const uint32_t *cps, uint32_t want)
{
    clf_glyph_t got[CLF_MAX_RECORDS];
    uint32_t count = 0;
    uint32_t n = 0;
    uint32_t i;
    int ret;

    while (n < want) {
        uint32_t batch = want - n;
        if (batch > CLF_MAX_RECORDS) {
            batch = CLF_MAX_RECORDS;
        }
        ret = clf_lookup(s_client, cps + n, batch, got, CLF_MAX_RECORDS,
                         &count);
        s_last_ret = ret;
        s_last_count = count;
        if (ret < 0 || count == 0) {
            return;
        }
        for (i = 0; i < count; ++i) {
            uint32_t k;
            uint32_t m;
            uint32_t cp = 0;
            for (k = 0; k < batch; ++k) {
                uint8_t enc[4];
                uint32_t len;
                memset(enc, 0, sizeof(enc));
                len = utf8_encode(cps[n + k], enc);
                if (memcmp(enc, got[i].utf8, 4) == 0) {
                    cp = cps[n + k];
                    break;
                }
                (void)len;
            }
            if (!cp) {
                continue;
            }
            if (cache_find(cp)) {
                continue;
            }
            m = 0;
            {
                lfont_entry_t *e = cache_add(cp);
                memcpy(e->tile, got[i].tile, LFONT_TILE_BYTES);
            }
            (void)m;
        }
        n += batch;
    }
}

int lfont_draw(int x, int y, uint8_t color, const char *text)
{
    uint32_t cps[64];
    uint32_t total = 0;
    uint32_t need[64];
    uint32_t need_count = 0;
    uint32_t i;
    const char *p = text;

    if (!text || !s_client) {
        return x;
    }
    while (*p && total < 64) {
        p = utf8_next(p, &cps[total]);
        ++total;
    }
    for (i = 0; i < total; ++i) {
        if (!cache_find(cps[i])) {
            need[need_count++] = cps[i];
        }
    }
    if (need_count) {
        fetch_batch(need, need_count);
    }
    for (i = 0; i < total; ++i) {
        lfont_entry_t *e = cache_find(cps[i]);
        if (e) {
            /* ASCII glyphs sit one row higher than CJK in the compact font;
             * push them down so the baselines line up. */
            int dy = (cps[i] < 0x80u) ? (int)s_scale : 0;
            lfont_blit_tile(x, y + dy, color, e->tile);
        }
        x += (int)(((cps[i] < 0x80u) ? 8u : 12u) * s_scale);
    }
    return x;
}
