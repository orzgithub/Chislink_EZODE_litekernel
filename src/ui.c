#include <stdint.h>
#include <string.h>

#include "chislink/gba/video.h"

#include "browser.h"
#include "lfont.h"
#include "ui.h"

#define COL_BG 0
#define COL_TEXT 1
#define COL_SEL 2
#define COL_DIM 3

static void fill_rect(int x, int y, int w, int h, uint8_t color)
{
    int i;
    int j;
    for (j = 0; j < h; ++j) {
        for (i = 0; i < w; ++i) {
            cl_gba_video_put_pixel((uint16_t)(x + i), (uint16_t)(y + j), color);
        }
    }
}

/* decode one UTF-8 char, returns bytes consumed (>=1) */
static int utf8_next(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    if (p[0] < 0x80u) {
        *cp = p[0];
        return 1;
    }
    if ((p[0] & 0xE0u) == 0xC0u && (p[1] & 0xC0u) == 0x80u) {
        *cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
        return 2;
    }
    if ((p[0] & 0xF0u) == 0xE0u && (p[1] & 0xC0u) == 0x80u &&
        (p[2] & 0xC0u) == 0x80u) {
        *cp = ((uint32_t)(p[0] & 0x0Fu) << 12) |
              ((uint32_t)(p[1] & 0x3Fu) << 6) | (uint32_t)(p[2] & 0x3Fu);
        return 3;
    }
    *cp = '?';
    return 1;
}

static int glyph_advance(uint32_t cp, int scale)
{
    return ((cp < 0x80u) ? 8 : 12) * scale;
}

static int draw_clipped(int x, int y, uint8_t color, const char *s, int max_x)
{
    while (*s) {
        uint32_t cp;
        int n = utf8_next(s, &cp);
        char tmp[6];
        int adv = glyph_advance(cp, 1);
        if (x + adv > max_x) {
            break;
        }
        memcpy(tmp, s, (size_t)n);
        tmp[n] = 0;
        lfont_draw(x, y, color, tmp);
        x += adv;
        s += n;
    }
    return x;
}

/* Breadcrumbs: draw the tail of the path that fits, prefixed with "..". */
static void draw_path(int x, int y, uint8_t color, const char *path, int max_x)
{
    int avail = max_x - x;
    int len = (int)strlen(path);
    int start = len;
    int width = 0;

    while (start > 0) {
        const char *p = path + start - 1;
        int n;
        uint32_t cp;
        if ((uint8_t)*p >= 0x80u) {
            /* step back to a UTF-8 lead byte */
            int k = 0;
            while (k < 3 && start - 2 - k >= 0 &&
                   ((uint8_t)path[start - 2 - k] & 0xC0u) == 0x80u) {
                ++k;
            }
            n = 1 + k;
        } else {
            n = 1;
        }
        if (start - n < 0) {
            break;
        }
        utf8_next(path + start - n, &cp);
        if (width + glyph_advance(cp, 1) > avail - 2 * 8) {
            break;
        }
        width += glyph_advance(cp, 1);
        start -= n;
    }
    if (start > 0) {
        lfont_draw(x, y, COL_DIM, "..");
        draw_clipped(x + 16, y, color, path + start, max_x);
    } else {
        draw_clipped(x, y, color, path, max_x);
    }
}

void ui_init(void)
{
    cl_gba_video_mode4_init(0);
    cl_gba_video_set_palette(COL_TEXT, 0x7FFF);
    cl_gba_video_set_palette(COL_SEL, 0x001F);
    cl_gba_video_set_palette(COL_DIM, 0x03E0);
    cl_gba_video_clear(COL_BG);
}

static void fill_row(int y, int selected)
{
    if (selected) {
        fill_rect(0, y, 240, UI_ROW_H, COL_SEL);
    }
}

void ui_list_view(const char *path, int count, int selected, int top)
{
    int i;
    char line[96];

    cl_gba_video_clear(COL_BG);
    lfont_draw(2, 0, COL_DIM, "ODE LITE");
    draw_path(68, 0, COL_TEXT, path, 238);

    for (i = 0; i < UI_ROWS; ++i) {
        int index = top + i;
        int y = UI_LIST_Y + i * UI_ROW_H;
        const browser_entry_t *e;
        if (index >= count) {
            break;
        }
        e = browser_get(index);
        if (!e) {
            break;
        }
        fill_row(y, index == selected);
        line[0] = 0;
        strcpy(line, e->name);
        if (e->is_dir) {
            strcat(line, "/");
        }
        lfont_draw(6, y, COL_TEXT, line);
    }

    {
        char foot[40];
        int n = 0;
        foot[n++] = 'A';
        foot[n++] = ':';
        foot[n++] = 'O';
        foot[n++] = 'K';
        foot[n++] = ' ';
        foot[n++] = 'B';
        foot[n++] = ':';
        foot[n++] = 'U';
        foot[n++] = 'P';
        foot[n++] = ' ';
        foot[n++] = 'L';
        foot[n++] = '+';
        foot[n++] = 'R';
        foot[n++] = '?';
        foot[n] = 0;
        lfont_draw(2, UI_LIST_Y + UI_ROWS * UI_ROW_H, COL_DIM, foot);
    }
}

void ui_loading(const char *name, int pct)
{
    int x;
    int y = 92;

    cl_gba_video_clear(COL_BG);
    lfont_draw(2, 20, COL_DIM, "LOADING");
    draw_path(2, 40, COL_TEXT, name, 238);

    /* bar */
    fill_rect(20, y, 200, 10, COL_DIM);
    fill_rect(22, y + 2, 196, 6, COL_BG);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    x = 22 + 196 * pct / 100;
    if (x > 22) {
        fill_rect(22, y + 2, x - 22, 6, COL_TEXT);
    }
}

void ui_error(const char *title, int code)
{
    char buf[48];
    int n = 0;
    cl_gba_video_clear(COL_BG);
    lfont_draw(2, 30, COL_SEL, title);
    if (code != 0) {
        buf[n++] = 'E';
        buf[n++] = 'R';
        buf[n++] = 'R';
        buf[n++] = ' ';
        buf[n++] = '0' + (code / 10) % 10;
        buf[n++] = '0' + code % 10;
        buf[n] = 0;
        lfont_draw(2, 50, COL_TEXT, buf);
    }
    lfont_draw(2, 120, COL_DIM, "PRESS A");
}

void ui_toast(const char *text)
{
    lfont_draw(2, 140, COL_DIM, text);
    ui_present();
}

void ui_present(void)
{
    cl_gba_video_present();
}
