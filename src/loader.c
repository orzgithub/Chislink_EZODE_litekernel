#include <stdint.h>
#include <string.h>

#include "chislink/file.h"
#include "chislink/gba/dma.h"

#include "iobuf.h"
#include "loader.h"
#include "save.h"

#define PSRAM_BASE 0x08800000u
#define CHUNK IOBUF_SIZE
#define READ_CHUNK 0x4000u


static void ez_write16(uint32_t addr, uint16_t v)
{
    *(volatile uint16_t *)addr = v;
}

static void ez_set_psram_page(uint32_t page)
{
    ez_write16(0x09FE0000u, 0xD200);
    ez_write16(0x08000000u, 0x1500);
    ez_write16(0x08020000u, 0xD200);
    ez_write16(0x08040000u, 0x1500);
    ez_write16(0x09860000u, (uint16_t)page);
    ez_write16(0x09FC0000u, 0x1500);
}

static void ez_set_rompage(uint32_t page)
{
    ez_write16(0x09FE0000u, 0xD200);
    ez_write16(0x08000000u, 0x1500);
    ez_write16(0x08020000u, 0xD200);
    ez_write16(0x08040000u, 0x1500);
    ez_write16(0x09880000u, (uint16_t)page);
    ez_write16(0x09FC0000u, 0x1500);
}

static void copy_chunk(const uint8_t *src, uint32_t offset, uint32_t length)
{
    uint32_t page = (offset / 0x800000u) * 0x1000u;
    uint32_t addr = PSRAM_BASE + (offset % 0x800000u);
    ez_set_psram_page(page);
    /* DMA in 32-bit units; the buffer is padded to 4 bytes by the caller. */
    cl_gba_dma_copy32((volatile void *)addr, src, (uint16_t)((length + 3u) >> 2));
}

static void jump_to_rom(void)
{
    *(volatile uint16_t *)0x04000208u = 0;        /* IME */
    *(volatile uint16_t *)0x04000200u = 0;        /* IE */
    *(volatile uint16_t *)0x04000202u = 0xFFFFu;  /* IF */

    ez_set_rompage(0x200u);

    *(volatile uint32_t *)0x04000304u =
        (1u << 5)   /* palette */ |
        (1u << 6)   /* vram    */ |
        (1u << 7)   /* oam     */ |
        (1u << 8)   /* sio     */ |
        (1u << 9);  /* sound   */

    __asm__ volatile(
        "mov r0, #0\n"
        "mov r1, #0\n"
        "mov r2, #0\n"
        "ldr r3, =0x03007F00\n"
        "mov sp, r3\n"
        "ldr r3, =0x08000000\n"
        "bx r3\n"
        ::: "r0", "r1", "r2", "r3", "memory");
}

int loader_load(const char *path, void (*progress)(int, const char *))
{
    cl_file_stat_t st;
    cl_file_t file;
    uint32_t size;
    uint32_t offset;
    int ret;

    if (cl_file_stat(path, &st) < 0 || st.type == CLP_FILE_DIRECTORY) {
        return -1;
    }
    size = (uint32_t)st.size;
    if (size == 0 || size > 0x2000000u) {
        return -2;
    }
    if (cl_file_open(path, CLP_OPEN_READ, &file) < 0) {
        return -3;
    }
    {
        /* only real GBA images: Nintendo logo + fixed 0x96 byte */
        uint32_t got = 0;
        if (cl_file_read(file, g_iobuf, 0xC0, &got) < 0 || got != 0xC0 ||
            g_iobuf[4] != 0x24u || g_iobuf[5] != 0xFFu || g_iobuf[6] != 0xAEu ||
            g_iobuf[7] != 0x51u || g_iobuf[0xB2] != 0x96u) {
            (void)cl_file_close(file);
            return -6;
        }
        if (cl_file_seek(file, 0) < 0) {
            (void)cl_file_close(file);
            return -7;
        }
    }

    if (progress) {
        progress(0, "LOADING");
    }
    for (offset = 0; offset < size; offset += CHUNK) {
        uint32_t want = (size - offset > CHUNK) ? CHUNK : (size - offset);
        uint32_t got = 0;
        uint32_t done = 0;
        int retries = 0;
        while (done < want) {
            uint32_t ask = want - done;
            if (ask > READ_CHUNK) {
                ask = READ_CHUNK;
            }
            ret = cl_file_read(file, g_iobuf + done, ask, &got);
            if (ret < 0) {
                if (++retries > 3) {
                    (void)cl_file_close(file);
                    return -4;
                }
                continue;
            }
            if (got == 0) {
                if (++retries > 3) {
                    (void)cl_file_close(file);
                    return -5;
                }
                continue;
            }
            retries = 0;
            done += got;
        }
        if (want & 3u) {
            uint32_t pad;
            for (pad = want; pad < ((want + 3u) & ~3u); ++pad) {
                g_iobuf[pad] = 0xFF;
            }
        }
        copy_chunk(g_iobuf, offset, want);
        if (progress) {
            progress((int)((offset + want) * 100u / size), "LOADING");
        }
    }
    (void)cl_file_close(file);
    ez_set_psram_page(0);

    /* Patch the save type: only for real GBA images (valid logo). */
    {
        const uint8_t *rom = (const uint8_t *)PSRAM_BASE;
        if (rom[4] == 0x24u && rom[5] == 0xFFu && rom[6] == 0xAEu &&
            rom[7] == 0x51u) {
            save_ctx_t ctx;
            if (progress) {
                progress(100, "SAVE");
            }
            (void)save_prepare(path, size, rom + 0xAC, &ctx);
        }
    }

    if (progress) {
        progress(100, "BOOTING");
    }
    jump_to_rom();
    return 0;
}
