#include <stdint.h>
#include <string.h>

#include "chislink/file.h"

#include "browser.h"

static browser_entry_t s_entries[BROWSER_MAX] __attribute__((section(".sbss")));
static int s_count;

static int add_entry(const cl_file_dir_entry_t *entry, void *user)
{
    (void)user;
    if (s_count >= BROWSER_MAX) {
        return 0;
    }
    if (entry->name[0] == '.') {
        return 0;
    }
    {
        browser_entry_t *dst = &s_entries[s_count];
        uint32_t i = 0;
        while (entry->name[i] && i < sizeof(dst->name) - 1) {
            dst->name[i] = entry->name[i];
            ++i;
        }
        dst->name[i] = 0;
        dst->size = (uint32_t)entry->size;
        dst->is_dir = (entry->type == CLP_FILE_DIRECTORY) ? 1 : 0;
    }
    ++s_count;
    return 1;
}

int browser_list(const char *path)
{
    uint32_t next = 0;
    int ret;

    s_count = 0;
    do {
        uint32_t count = 0;
        ret = cl_file_list_dir_page(path, next, BROWSER_MAX - s_count,
                                    add_entry, 0, &next, &count);
        if (ret < 0) {
            return ret;
        }
        if (count == 0) {
            break;
        }
    } while (next != 0xFFFFFFFFu && s_count < BROWSER_MAX);

    /* insertion sort: directories first, then by name */
    {
        int i;
        for (i = 1; i < s_count; ++i) {
            browser_entry_t tmp = s_entries[i];
            int j = i - 1;
            while (j >= 0) {
                int less;
                if (s_entries[j].is_dir != tmp.is_dir) {
                    less = s_entries[j].is_dir < tmp.is_dir;
                } else {
                    less = strcmp(s_entries[j].name, tmp.name) > 0;
                }
                if (!less) {
                    break;
                }
                s_entries[j + 1] = s_entries[j];
                --j;
            }
            s_entries[j + 1] = tmp;
        }
    }
    return s_count;
}

int browser_count(void) { return s_count; }

const browser_entry_t *browser_get(int index)
{
    if (index < 0 || index >= s_count) {
        return 0;
    }
    return &s_entries[index];
}
