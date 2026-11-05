#ifndef BROWSER_H
#define BROWSER_H

#include <stdint.h>

#define BROWSER_MAX 256

typedef struct browser_entry {
    char name[80];
    uint32_t size;
    uint8_t is_dir;
} browser_entry_t;

/* List a directory (path is UTF-8, e.g. "/sd").  Returns entry count or
 * negative error.  Directories come first, then files, each sorted by name. */
int browser_list(const char *path);
int browser_count(void);
const browser_entry_t *browser_get(int index);

#endif
