#include <stdint.h>
#include <string.h>

#include "chislink/file.h"
#include "chislink/gba/dma.h"

#include "iobuf.h"
#include "save.h"
#include "savemode.h"

#define SAVE_WINDOW 0x0E000000u
#define FPGA_BUF 0x09E00000u
#define SAVE_DIR "/sd/saves"
#define PENDING "/sd/saves/pending.bin"
#define PENDING_MAGIC 0x4F4C5052u /* 'OLPR' */

typedef struct pending_rec {
    uint32_t magic;
    uint32_t size;
    char sav[64];
} pending_rec_t;

static uint8_t s_cfg[0x400] __attribute__((section(".sbss"), aligned(4)));

static void reg16(uint32_t addr, uint16_t v)
{
    *(volatile uint16_t *)addr = v;
}

static void reg8(uint32_t addr, uint8_t v)
{
    *(volatile uint8_t *)addr = v;
}

static void setbuffer_control(uint16_t control)
{
    reg16(0x09FE0000u, 0xD200);
    reg16(0x08000000u, 0x1500);
    reg16(0x08020000u, 0xD200);
    reg16(0x08040000u, 0x1500);
    reg16(0x09420000u, control);
    reg16(0x09FC0000u, 0x1500);
}

static void set_rampage(uint16_t page)
{
    reg16(0x09FE0000u, 0xD200);
    reg16(0x08000000u, 0x1500);
    reg16(0x08020000u, 0xD200);
    reg16(0x08040000u, 0x1500);
    reg16(0x09C00000u, page);
    reg16(0x09FC0000u, 0x1500);
}

static void send_fpga_config(uint32_t rom_size, uint8_t mode, uint32_t size)
{
    memset(s_cfg, 0, sizeof(s_cfg));
    /* layout matches the kernel's FAT_table_buffer tail */
    ((uint32_t *)s_cfg)[0x1F0 / 4] = rom_size;
    ((uint32_t *)s_cfg)[0x1F4 / 4] = 0x2; /* SET_PARAMETER_MODE */
    ((uint32_t *)s_cfg)[0x1F8 / 4] = 0x40;
    ((uint32_t *)s_cfg)[0x1FC / 4] = ((uint32_t)mode << 24) | size;

    setbuffer_control(1);
    cl_gba_dma_copy32((volatile void *)FPGA_BUF, s_cfg, (uint16_t)(sizeof(s_cfg) / 4));
    setbuffer_control(3);
    setbuffer_control(0);
}

/* Select the flash save bank (only meaningful for 1M flash saves). */
static void save_bank_switch(uint8_t bank)
{
    reg8(SAVE_WINDOW + 0x5555u, 0xAA);
    reg8(SAVE_WINDOW + 0x2AAAu, 0x55);
    reg8(SAVE_WINDOW + 0x5555u, 0xB0);
    reg8(SAVE_WINDOW, bank);
}

static void save_write_mem(uint32_t offset, const uint8_t *src, uint32_t len)
{
    while (len) {
        uint32_t page = offset >> 16;
        uint32_t in_page = offset & 0xFFFFu;
        uint32_t n = IOBUF_SIZE - in_page;
        uint32_t i;
        if (n > len) {
            n = len;
        }
        set_rampage((uint16_t)(page * 0x10u));
        for (i = 0; i < n; ++i) {
            reg8(SAVE_WINDOW + in_page + i, src[i]);
        }
        offset += n;
        src += n;
        len -= n;
    }
}

static void save_read_mem(uint32_t offset, uint8_t *dst, uint32_t len)
{
    while (len) {
        uint32_t page = offset >> 16;
        uint32_t in_page = offset & 0xFFFFu;
        uint32_t n = IOBUF_SIZE - in_page;
        uint32_t i;
        if (n > len) {
            n = len;
        }
        set_rampage((uint16_t)(page * 0x10u));
        for (i = 0; i < n; ++i) {
            dst[i] = *(volatile uint8_t *)(SAVE_WINDOW + in_page + i);
        }
        offset += n;
        dst += n;
        len -= n;
    }
}

static int write_all(cl_file_t file, const uint8_t *src, uint32_t len)
{
    while (len) {
        uint32_t written = 0;
        int ret = cl_file_write(file, src, len, &written);
        if (ret < 0 || written == 0) {
            return -1;
        }
        src += written;
        len -= written;
    }
    return 0;
}

static int read_all(cl_file_t file, uint8_t *dst, uint32_t len)
{
    while (len) {
        uint32_t got = 0;
        int ret = cl_file_read(file, dst, len, &got);
        if (ret < 0 || got == 0) {
            return -1;
        }
        dst += got;
        len -= got;
    }
    return 0;
}

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static void make_sav_path(char *dst, const char *rom_path)
{
    const char *base = base_name(rom_path);
    const char *dot = strrchr(base, '.');
    uint32_t n = dot ? (uint32_t)(dot - base) : (uint32_t)strlen(base);
    strcpy(dst, SAVE_DIR);
    strcat(dst, "/");
    if (n > 40) {
        n = 40;
    }
    strncat(dst, base, n);
    strcat(dst, ".sav");
}

static void write_pending(const char *sav, uint32_t size)
{
    pending_rec_t rec;
    cl_file_t file;
    uint32_t written = 0;

    memset(&rec, 0, sizeof(rec));
    rec.magic = PENDING_MAGIC;
    rec.size = size;
    strncpy(rec.sav, sav, sizeof(rec.sav) - 1);

    if (cl_file_open(PENDING, CLP_OPEN_WRITE | CLP_OPEN_CREATE | CLP_OPEN_TRUNCATE,
                     &file) < 0) {
        return;
    }
    (void)cl_file_write(file, &rec, sizeof(rec), &written);
    (void)cl_file_close(file);
}

int save_flush_pending(void)
{
    pending_rec_t rec;
    cl_file_t file;
    uint32_t got = 0;
    uint32_t done;
    uint32_t want;

    if (cl_file_open(PENDING, CLP_OPEN_READ, &file) < 0) {
        return 0;
    }
    if (cl_file_read(file, &rec, sizeof(rec), &got) < 0 || got != sizeof(rec) ||
        rec.magic != PENDING_MAGIC || rec.size == 0 || rec.size > 0x20000u) {
        (void)cl_file_close(file);
        (void)cl_file_remove(PENDING);
        return 0;
    }
    (void)cl_file_close(file);

    if (cl_file_open(rec.sav, CLP_OPEN_WRITE | CLP_OPEN_CREATE | CLP_OPEN_TRUNCATE,
                     &file) < 0) {
        return 0;
    }
    for (done = 0; done < rec.size; done += want) {
        want = IOBUF_SIZE;
        if (want > rec.size - done) {
            want = rec.size - done;
        }
        save_read_mem(done, g_iobuf, want);
        if (done == 0) {
        }
        if (write_all(file, g_iobuf, want) != 0) {
            (void)cl_file_close(file);
            return 0;
        }
    }
    (void)cl_file_close(file);
    set_rampage(0);
    (void)cl_file_remove(PENDING);
    return (int)rec.size;
}

int save_prepare(const char *rom_path, uint32_t rom_size,
                 const uint8_t gamecode[4], save_ctx_t *ctx)
{
    uint8_t mode;
    uint32_t size;
    char sav[160];
    cl_file_stat_t st;
    cl_file_t file;
    uint32_t done;
    int ret;

    ctx->mode = 0;
    ctx->size = 0;
    ctx->valid = 0;

    mode = savemode_lookup(gamecode);
    size = savemode_size(mode);
    if (size == 0) {
        return 0;
    }

    (void)cl_file_mkdir(SAVE_DIR);
    make_sav_path(sav, rom_path);

    if (cl_file_stat(sav, &st) < 0) {
        /* create a fresh, empty (0xFF) save file */
        uint32_t create_size = size;
        (void)cl_file_open(sav, CLP_OPEN_WRITE | CLP_OPEN_CREATE | CLP_OPEN_TRUNCATE,
                           &file);
        memset(g_iobuf, 0xFF, IOBUF_SIZE);
        while (create_size) {
            uint32_t n = (create_size > IOBUF_SIZE) ? IOBUF_SIZE : create_size;
            if (write_all(file, g_iobuf, n) != 0) {
                (void)cl_file_close(file);
                return -1;
            }
            create_size -= n;
        }
        (void)cl_file_close(file);
    } else {
        uint32_t actual = (uint32_t)st.size;
        if (actual > 0 && actual < size) {
            size = actual;
        }
    }

    if (cl_file_open(sav, CLP_OPEN_READ, &file) < 0) {
        return -2;
    }
    save_bank_switch(0);
    for (done = 0; done < size; done += IOBUF_SIZE) {
        uint32_t n = (size - done > IOBUF_SIZE) ? IOBUF_SIZE : (size - done);
        ret = read_all(file, g_iobuf, n);
        if (ret != 0) {
            (void)cl_file_close(file);
            return -3;
        }
        save_write_mem(done, g_iobuf, n);
    }
    (void)cl_file_close(file);
    set_rampage(0);

    send_fpga_config(rom_size, mode, size);
    write_pending(sav, size);

    ctx->mode = mode;
    ctx->size = size;
    ctx->valid = 1;
    return 0;
}
