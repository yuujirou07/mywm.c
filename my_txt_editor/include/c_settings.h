#ifndef C_SETTINGS_H
#define C_SETTINGS_H

#include "public_data/c_settings/c_settings_setting.h"

struct editor_input_context;

void connect_api_mem_data(MY_TXT_EDITOR_API *api, struct editor_input_context *ctx);
void init_settings_src(MY_TXT_EDITOR_API *api, struct editor_input_context *ctx);

void check_key_mapps_entry(struct editor_input_context *ctx,wchar_t chr[2],MY_TXT_EDITOR_API *api);
#endif
