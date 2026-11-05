/* ODE lite kernel - ChisLink multiboot kernel.
 * Browse /sd, pick a .gba, copy it into EZ-Flash PSRAM and boot it. */
#include <stdint.h>
#include <string.h>

#include "chislink/gba/hw.h"
#include "chislink/gba/video.h"
#include "chislink/gba_sio_transport.h"
#include "chislink/client.h"
#include "chislink/file.h"
#include "chislink/proto.h"
#include "chislink/storage_client.h"

#include "browser.h"
#include "loader.h"
#include "save.h"
#include "lfont.h"
#include "ui.h"

#define KEY_A 0x0001u
#define KEY_B 0x0002u
#define KEY_UP 0x0040u
#define KEY_DOWN 0x0080u

static cl_gba_sio_transport_t s_transport;
static cl_client_t s_client;
static cl_storage_client_t s_storage;
static uint8_t s_scratch[640];

static char s_path[160];
static char s_loading_name[160];
static int s_sel;

static int name_is_gba(const char *name)
{
    size_t n = strlen(name);
    char c;
    if (n < 5) {
        return 0;
    }
    if (name[n - 4] != '.') {
        return 0;
    }
    c = name[n - 3];
    if (c != 'g' && c != 'G') {
        return 0;
    }
    c = name[n - 2];
    if (c != 'b' && c != 'B') {
        return 0;
    }
    c = name[n - 1];
    return (c == 'a' || c == 'A');
}

static uint16_t keys_read(void)
{
    return (uint16_t)(~*(volatile uint16_t *)0x04000130u & 0x03FFu);
}

static void wait_release(void)
{
    while (keys_read() != 0) {
    }
}

static void path_join(char *dst, const char *dir, const char *name)
{
    strcpy(dst, dir);
    if (dst[strlen(dst) - 1] != '/') {
        strcat(dst, "/");
    }
    strcat(dst, name);
}

static void path_parent(char *path)
{
    int n = (int)strlen(path);
    while (n > 0 && path[n - 1] != '/') {
        --n;
    }
    if (n > 1) {
        path[n - 1] = 0;
    }
}

static void redraw(void)
{
    int count = browser_count();
    int top;

    if (s_sel < 0) {
        s_sel = 0;
    }
    if (count > 0 && s_sel >= count) {
        s_sel = count - 1;
    }
    top = s_sel - (UI_ROWS - 1);
    if (top < 0) {
        top = 0;
    }
    ui_list_view(s_path, count, s_sel, top);
    ui_present();
}

static void on_progress(int pct, const char *stage)
{
    (void)stage;
    ui_loading(s_loading_name, pct);
    ui_present();
}

static void wait_a(void)
{
    while (!(keys_read() & KEY_A)) {
    }
    wait_release();
}

int main(void)
{
    int ret;
    int count;

    ui_init();
    cl_gba_time_init();

    if (!cl_gba_sio_transport_init_client(
            &s_transport, &s_client,
            clp_make_word(CLP_TYPE_NOP, CLP_CH_CONTROL, CLP_CTRL_NOP, 0),
            2 * CL_GBA_TIME_TICKS_PER_SECOND)) {
        ui_error("NO CHISLINK", 0);
        ui_present();
        wait_a();
        return 0;
    }
    cl_gba_sio_transport_install(&s_transport);
    cl_gba_sio_transport_set_timeout(
        &s_transport, CL_GBA_SIO_TRANSPORT_DEFAULT_TIMEOUT_TICKS);

    if (!cl_client_hello(&s_client)) {
        ui_error("HELLO FAILED", 0);
        ui_present();
        wait_a();
        return 0;
    }
    (void)cl_client_request_caps(&s_client);
    ret = cl_storage_client_init(&s_storage, &s_client, s_scratch,
                                 sizeof(s_scratch));
    if (ret < 0) {
        ui_error("STORAGE FAILED", -ret);
        ui_present();
        wait_a();
        return 0;
    }
    cl_file_reset_backends();
    (void)cl_file_register_remote_storage(&s_storage);

    lfont_init(&s_client);

    /* A game launched earlier may have left its save in the cartridge save
     * memory; flush it back to /sd/SAVER/<name>.sav now. */
    if (save_flush_pending() > 0) {
        ui_toast("SAVE SYNCED");
    }

    strcpy(s_path, "/sd");
    ret = browser_list(s_path);
    if (ret < 0) {
        ui_error("LIST FAILED", -ret);
        ui_present();
        wait_a();
        return 0;
    }
    s_sel = 0;
    redraw();

    for (;;) {
        uint16_t k = keys_read();
        if (k == 0) {
            continue;
        }
        count = browser_count();
        if (k & KEY_DOWN) {
            if (count > 0) {
                s_sel = (s_sel + 1) % count;
            }
            redraw();
        } else if (k & KEY_UP) {
            if (count > 0) {
                s_sel = (s_sel + count - 1) % count;
            }
            redraw();
        } else if (k & KEY_B) {
            if (strcmp(s_path, "/sd") != 0) {
                path_parent(s_path);
                ret = browser_list(s_path);
                s_sel = 0;
                redraw();
            }
        } else if (k & KEY_A) {
            const browser_entry_t *e = browser_get(s_sel);
            if (e) {
                if (e->is_dir) {
                    char next[160];
                    path_join(next, s_path, e->name);
                    if (browser_list(next) >= 0) {
                        strcpy(s_path, next);
                        s_sel = 0;
                        redraw();
                    }
                } else if (!name_is_gba(e->name)) {
                    ui_error("NOT A GAME (.GBA)", 0);
                    ui_present();
                    wait_a();
                    redraw();
                } else {
                    char path[160];
                    path_join(path, s_path, e->name);
                    strcpy(s_loading_name, e->name);
                    ui_loading(s_loading_name, 0);
                    ui_present();
                    ret = loader_load(path, on_progress);
                    ui_error("LOAD FAILED", -ret);
                    ui_present();
                    wait_a();
                    redraw();
                }
            }
        }
        wait_release();
    }
}
