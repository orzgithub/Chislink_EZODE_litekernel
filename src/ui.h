#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "browser.h"

#define UI_ROWS 8
#define UI_ROW_H 16
#define UI_LIST_Y 16

void ui_init(void);
void ui_list_view(const char *path, int count, int selected, int top);
void ui_loading(const char *name, int pct);
void ui_error(const char *title, int code);
void ui_present(void);
void ui_toast(const char *text);

#endif
