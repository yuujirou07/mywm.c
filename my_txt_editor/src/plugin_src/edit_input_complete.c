

#include<stdio.h>
#include "default_settings.h"
#include "editor_types.h"
#include"input_complete.h"
#include"txt_editor.h"


int init_edit_complete_data(struct editor_input_context *ctx){
    edit_input_complete_data *tmp_comp_data =
         &ctx->state->edit_input_complete_data;

    auto_complete_settings_data *tmp_settings_comp_data = 
        &ctx->state->settings_data->auto_complete_settings_data;

    tmp_comp_data->size = tmp_settings_comp_data->auto_complete_window_size;

    //念の為二倍に確保しておく
    tmp_comp_data->word_data.world_allocate_num = 
        tmp_settings_comp_data->auto_complete_window_size.y * 2;
    
    tmp_comp_data->word_data.world_num = 0;
    


}