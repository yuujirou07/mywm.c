#include "kbd_input_log.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <wchar.h>
#include "editor_types.h"
#include "error_log.h"
#include "txt_editor.h"

FILE *key_log_file = NULL;


// stateのキー履歴へ文字ch・get_wch結果result・入力時のscreen_stateを追加する。
// 履歴は設定上限までで打ち止めにし、長時間使ってもメモリが増え続けないようにしている。
// 返り値: 追加できたら0。記録数が設定上限(key_log_buffer_size)に達している場合と、確保・再確保に失敗した場合は-1。
int KL_add(struct editor_state *state,wchar_t ch,int result,screen_state screen_state){
    if(state->key_bord_data.Key_log_data.key_count >= 
            state->settings_data->key_log_settings.key_log_buffer_size){
        return -1; 
    }

    if(state->key_bord_data.Key_log_data.key_log == NULL){
        state->key_bord_data.Key_log_data.key_allocate_num = KEY_LOG_ALLOCATE_NUM;
        state->key_bord_data.Key_log_data.key_count = 0;
        state->key_bord_data.Key_log_data.key_log = 
            malloc(sizeof(key_data) * 
                state->key_bord_data.Key_log_data.key_allocate_num);

        if(state->key_bord_data.Key_log_data.key_log == NULL){
            error_log("malloc");
            return -1;
        }
    }
    else if(state->key_bord_data.Key_log_data.key_allocate_num <= 
        state->key_bord_data.Key_log_data.key_count){
        size_t tmp_realloc_num = 
            (state->key_bord_data.Key_log_data.key_count * 2 >= 
                state->settings_data->key_log_settings.key_log_buffer_size)?
                state->settings_data->key_log_settings.key_log_buffer_size:
                state->key_bord_data.Key_log_data.key_count * 2;
        

        key_data *tmp_key_data = 
            realloc(
                state->key_bord_data.Key_log_data.key_log,
                sizeof(key_data) * tmp_realloc_num
            );
        if(tmp_key_data == NULL){
            error_log("malloc");
            return -1;
        }
        state->key_bord_data.Key_log_data.key_log = tmp_key_data;
        state->key_bord_data.Key_log_data.key_allocate_num = tmp_realloc_num;
    }

    key_data *tmp_key_log = 
        &state->key_bord_data.Key_log_data.key_log[state->key_bord_data.Key_log_data.key_count];
    tmp_key_log->screen_state = screen_state;
    tmp_key_log->input_result = result;
    tmp_key_log->key = ch;
    state->key_bord_data.Key_log_data.key_count++;
    return 0;
}


// 記録済みのキー履歴からscreen_stateの画面で入力された分を取り出す。2回呼びで使う。
// 1回目: 該当件数を*nへ入れて0を返す。2回目: *nを上限に該当要素のポインタをdataへ詰め、件数を返す。dataがNULLなら*nを0にして-1を返す。
int KL_GetScreenLog(screen_state screen_state,struct editor_state *state,key_data **data,size_t *n){
    
    static bool IsGetNum = false;
    key_log_data tmp_log_data = state->key_bord_data.Key_log_data;
    *n = 0;

    if(!IsGetNum){
        for(size_t i = 0;i < tmp_log_data.key_count;i++){
            if(tmp_log_data.key_log[i].screen_state == screen_state)(*n)++;
        }
        IsGetNum = true;
        return 0;
    }
    else{
        if(data == NULL){
            *n = 0;
            IsGetNum = false;
            return -1;
        }
        size_t count = 0;
        size_t loop_count = (tmp_log_data.key_count - 1 < *n)?tmp_log_data.key_count - 1:*n;
        for(size_t i = tmp_log_data.key_count - 1;i < loop_count;i--){
            if(tmp_log_data.key_log[i].screen_state == screen_state){
                data[count++] = &tmp_log_data.key_log[i];
            }
        }
        IsGetNum = false;
        return count;
    }
    return -1;
}



// キー履歴の出力先ファイルpathを追記モードで開く。既に開いていれば何もしない。
// 返り値: 成功0、開けなかった場合-1(error_logへ記録する)。
int KL_open_key_log_file(const char *path){
    if(key_log_file == NULL){
        key_log_file = fopen(path,"a+");
        if(key_log_file == NULL){
            error_log("can not open key log file");
            return -1;
        }
    }
    

    return 0;
}
