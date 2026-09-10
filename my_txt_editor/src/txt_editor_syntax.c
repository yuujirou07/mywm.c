#include <ncurses.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include <time.h>
#include <wctype.h>
#include<wchar.h>

#include"txt_editor.h"
#include"txt_editor_syntax.h"

// Dark+の近似色。色ペア1〜3は本文・選択・エラー表示が使用する。
static const struct {
    short color_256; // 256色以上の端末で使う前景色番号。
    short color_8; // 256色未満の端末で使う基本色番号。
} syntax_colors[] = {
    [reserved_word] = {176,COLOR_MAGENTA},
    [literal]       = {173,COLOR_YELLOW},
    [comment]       = {65,COLOR_GREEN},
    [Method]        = {187,COLOR_YELLOW},
    [type]          = {74,COLOR_BLUE},
    [operator]      = {188,COLOR_WHITE},
    [variable]      = {73,COLOR_BLUE},
};

syntax_data **syntax_data_collection = NULL;
static int garbage_collection_allocate_num = 256;
static int collection_count = 0;


/* 構文用の色ペア4以降を黒背景で登録する。引数・返り値なし。
 * ncursesとstart_color()の初期化後に呼ぶ。色非対応なら何もしない。
 * COLOR_PAIRSの範囲内だけ登録し、init_pairの失敗は通知しない。
 */
void init_syntax_colors(void){
    if(!has_colors())return;
    for(size_t i = 0;i < sizeof(syntax_colors) / sizeof(syntax_colors[0]);i++){
        short pair = (short)(4 + i);
        if(pair >= COLOR_PAIRS)break;
        short foreground = (COLORS >= 256)
            ? syntax_colors[i].color_256 : syntax_colors[i].color_8;
        init_pair(pair,foreground,COLOR_BLACK);
    }
}

/* 引数: word_typeは着色の分類。使用前にinit_syntax_colors()を呼ぶ。
 * 返り値: 対応する色ペア番号。色非対応なら0、分類不正・ペア不足なら本文用の1。
 */
short syntax_color_pair(syntax_type word_type){
    if(!has_colors())return 0;
    //suntax_typeが要素数より上の値かの条件分岐
    if((unsigned int)word_type >= sizeof(syntax_colors) / sizeof(syntax_colors[0]))return 1;
    short pair = (short)(4 + word_type);
    return (pair < COLOR_PAIRS) ? pair : 1;
}

static const wchar_t *const reserved_words[] = {
    L"if",
    L"while",
    L"for",
    L"return",
    L"continue",
    L"else",
    L"#include",
};

static const wchar_t *const type_words[] = {
    L"void",
    L"char",
    L"short",
    L"int",
    L"long",
    L"float",
    L"double",
    L"signed",
    L"unsigned",
    L"bool",
    L"_Bool",
};



/* 引数: chは判定するワイド文字。
 * 返り値: 現在のロケールで英数字に分類される文字、または'_'ならtrue。
 */
static bool is_word_char(wint_t ch){
    return iswalnum(ch) || ch == L'_';
}

/* lineのposからkeywordが単語単位で一致するかを返す。
 * 引数: lineはline_len要素以上の配列（NUL終端不要）、0 <= pos < line_len。
 * keywordは非NULLのNUL終端ワイド文字列。前後の英数字・'_'への連結は不一致。
 * 返り値: 一致ならtrue。空のkeywordや行末を越える場合はfalse。
 */
static bool keyword_at(const wint_t *line,int line_len,int pos,const wchar_t *keyword){
    int keyword_len = (int)wcslen(keyword);
    if(keyword_len == 0 || keyword_len > line_len - pos)return false;
    if(pos > 0 && is_word_char(line[pos - 1]))return false;
    if(pos + keyword_len < line_len && is_word_char(line[pos + keyword_len]))return false;

    for(int i = 0;i < keyword_len;i++){
        if(line[pos + i] != (wint_t)keyword[i])return false;
    }
    return true;
}

/* 引数: lineはline_len要素の行配列（NUL終端不要）、posは照合開始の添字。
 * wordsはword_count要素の辞書で、各要素はNUL終端ワイド文字列またはNULL。
 * 返り値: 単語境界を含めて最初に一致した語の文字数。不一致、line/wordsがNULL、
 * posが範囲外なら0。入力は変更せず、NULLの辞書要素は読み飛ばす。
 */
int find_syntax_word(const wint_t *line,int line_len,int pos,
                     const wchar_t *const words[],size_t word_count){
    if(line == NULL || words == NULL || pos < 0 || pos >= line_len)return 0;

    for(size_t i = 0;i < word_count;i++){
        if(words[i] != NULL && keyword_at(line,line_len,pos,words[i])){
            return (int)wcslen(words[i]);
        }
    }
    return 0;
}

/* 引数: syntaxは初期化済みの管理情報、x/yは表示領域内の開始位置、
 * word_lenは正の着色文字数、word_typeは分類。同一行の範囲を末尾へ追加する。
 * 返り値: 成功0、realloc失敗-1（既存の配列・件数は維持）。
 * 再確保すると既存要素へのポインタは無効になるため、呼び出し後に取得し直す。
 */
static int add_syntax_data(syntax *syntax,int x,int y,int word_len,syntax_type word_type){
    syntax_list_data *list = &syntax->syntax_list_data;
    int count = list->syntax_list_num;
    if(count >= list->syntax_list_allocate_num){
        syntax_data *data = realloc(list->syntax_data,((size_t)count + 1) * sizeof(*data));
        if(data == NULL)return -1;
        list->syntax_data = data;
        list->syntax_list_allocate_num = count + 1;
    }
    
    syntax_data *data = &list->syntax_data[count];
    bool use_new_arry = true; 
    for(int i = 0; i < count;i++){
        if(list->syntax_data[i].area.st_x == x &&
            list->syntax_data[i].area.st_y == y){
                data = &list->syntax_data[i];
                memset(data,0,sizeof(syntax_data));
                use_new_arry = false;
        }
    }
    data->area = (syntax_area){.st_x = x,.st_y = y,.end_x = x + word_len - 1,.end_y = y};
    data->type = word_type;
    list->syntax_list_num = (use_new_arry)
        ?list->syntax_list_num+1:list->syntax_list_num;

    return 0;
}



/* 引数: syntaxは初期化済み、line/line_lenは行配列と要素数、view_colsは表示列数、
 * yは表示行。words/word_countは辞書と要素数、word_typeは追加する分類。
 * 行全体で単語境界を調べ、表示列内の一致範囲だけをsyntaxへ追加する。
 * 返り値: 成功0、追加失敗-1。失敗前に追加した要素は残す。
 */
static int scan_syntax_words(syntax *syntax,const wint_t *line,int line_len,int view_cols,
                            int y,const wchar_t *const words[],size_t word_count,
                                syntax_type word_type){

    for(int x = 0;x < line_len && x < view_cols;x++){
        int word_len = find_syntax_word(line,line_len,x,words,word_count);
        if(word_len == 0)continue;

        // 単語の判定には行全体を使い、着色範囲だけ表示幅に収める。
        int draw_len = word_len;
        if(draw_len > view_cols - x)draw_len = view_cols - x;
        if(add_syntax_data(syntax,x,y,draw_len,word_type) < 0)return -1;
        x += word_len - 1;
    }
    return 0;
}

/* 引数: syntaxは未確保の管理情報（0初期化推奨）。ncurses初期化後に呼ぶ。
 * stdscrの行数×列数の要素を確保し、有効件数を0にする。langは変更しない。
 * 返り値: 成功0、NULL・画面寸法不正・確保失敗なら-1。
 * 成功後の配列は呼び出し側がfreeする。確保済みのまま再度呼ぶとリークする。
 */
int init_syntax(syntax *syntax){
    if(syntax == NULL)return -1;
    int x;
    int y;
    getmaxyx(stdscr, y, x);
    if(x <= 0 || y <= 0)return -1;
    
    syntax->syntax_list_data.syntax_data = malloc((y * x) * sizeof(syntax_data));
    if(syntax->syntax_list_data.syntax_data == NULL)return -1;
    syntax->syntax_list_data.syntax_list_allocate_num = y * x;
    syntax->syntax_list_data.syntax_list_num = 0;
    return 0;
}

/* 引数: langは保存する言語、syntaxは設定先。langの値の妥当性は検証しない。
 * 返り値: 成功0、syntaxがNULLなら-1。既存の解析結果は変更しない。
 */
int set_syntax_language(language lang,syntax *syntax){
    if(syntax == NULL)return -1;
    syntax->lang = lang;
    return 0;
}



/* 引数: syntaxはinit_syntax成功済み、ctxは有効なstateを持つ入力コンテキスト。
 * スクロール開始行から表示領域内の予約語と型名を再解析し、着色情報を置き換える。
 * langは参照せず、文字列・コメント内かどうかの区別もしない。
 * 返り値: 登録件数（0以上）。NULL引数・state不在・未確保・追加失敗なら-1。
 * 再解析開始後の失敗では件数が0または途中までの結果になる。配列は保持する。
 * ctxは借用して変更しない。配列の再確保により以前の要素ポインタは無効になり得る。
 */
int set_syntax_data(syntax *syntax,struct editor_input_context *ctx){
    if(syntax == NULL || ctx == NULL || ctx->state == NULL)return -1;

    syntax->syntax_list_data.syntax_list_num = 0;
    if(syntax->syntax_list_data.syntax_data == NULL)return -1;

    for(int h = 0;h < ctx->state->write_area.h;h++){
        int now_file_line_num = ctx->state->scr.scr_start_num + h;
        if(now_file_line_num < 0 || now_file_line_num >= editor_line_limit(ctx->state))break;
        int line_str_len = editor_line_len(ctx->state,now_file_line_num);
        wint_t *line_st_ptr = editor_line_cells(ctx->state,now_file_line_num);
        if(line_st_ptr == NULL)continue;
        int view_cols = editor_view_cols(ctx->state);
        if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
            reserved_words,sizeof(reserved_words) / sizeof(reserved_words[0]),reserved_word) < 0)return -1;
        if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
            type_words,sizeof(type_words) / sizeof(type_words[0]),type) < 0)return -1;
        if(scan_syntax_method(syntax,line_st_ptr,line_str_len,view_cols,h,Method) < 0)return -1;
        if(scan_syntax_comment(syntax,line_st_ptr,line_str_len,view_cols,h,comment) < 0)return -1;
        if(scan_syntax_variable(syntax,line_st_ptr,line_str_len,view_cols,h,variable) < 0)return -1;
    }
    return syntax->syntax_list_data.syntax_list_num;
}

int update_line_syntax_data(struct editor_input_context *ctx,int line){
    if(ctx == NULL || ctx->state == NULL)return -1;
    syntax *syntax = now_usint_syntax_ptr_ctl(NULL,get);
    if(syntax == NULL)return -1;
    if(syntax->syntax_list_data.syntax_data == NULL)return -1;

    int h = line;
    int now_file_line_num = ctx->state->scr.scr_start_num + h;
    if(now_file_line_num < 0 || now_file_line_num >= editor_line_limit(ctx->state))return -1;
    int line_str_len = editor_line_len(ctx->state,now_file_line_num);
    wint_t *line_st_ptr = editor_line_cells(ctx->state,now_file_line_num);
    int view_cols = editor_view_cols(ctx->state);
    if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
        reserved_words,sizeof(reserved_words) / sizeof(reserved_words[0]),reserved_word) < 0)return -1;
    if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
        type_words,sizeof(type_words) / sizeof(type_words[0]),type) < 0)return -1;
    if(scan_syntax_method(syntax,line_st_ptr,line_str_len,view_cols,h,Method) < 0)return -1;
    if(scan_syntax_comment(syntax,line_st_ptr,line_str_len,view_cols,h,comment) < 0)return -1;
    if(scan_syntax_variable(syntax,line_st_ptr,line_str_len,view_cols,h,variable) < 0)return -1;
    return syntax->syntax_list_data.syntax_list_num;
}

int apply_syntax_color(struct editor_input_context *ctx,syntax syntax){
    if(ctx == NULL)return -1;
    struct editor_state *state = ctx->state; 

    int syntax_num = syntax.syntax_list_data.syntax_list_num;
    
    // 前回の着色を戻してから、本文描画後の画面へ適用する。
    for(int h = 0;h < state->write_area.h;h++){
        if(state->write_area.w > 0)
            mvchgat(state->write_area.y_start + h,state->write_area.x_start,
                state->write_area.w,A_NORMAL,1,NULL);
    }
    for(int i = 0;i < syntax_num;i++){
        syntax_data *data = &syntax.syntax_list_data.syntax_data[i];
        syntax_area *area = &data->area;
        if(area->st_y < 0 || area->end_y < 0)continue;
        else if(area->st_y > state->write_area.h || area->end_y > state->write_area.h)continue;

        short syntax_color = syntax_color_pair(data->type); 
        mvchgat(state->write_area.y_start + area->st_y,
            state->write_area.x_start + area->st_x,
            area->end_x - area->st_x + 1,A_NORMAL,syntax_color,NULL);
    }
    refresh();
    return 0;
}



int scan_syntax_method(syntax *syntax,wint_t *line_st_ptr,
    int line_len,int view_cols,int h,syntax_type mthod){

    for(int x = 0;x < line_len && x < view_cols;x++){
        if(x > 0 && is_word_char(line_st_ptr[x - 1]))continue;
        if(!iswalpha(line_st_ptr[x]) && line_st_ptr[x] != L'_')continue;

        int start_x = x;
        while(x < line_len && is_word_char(line_st_ptr[x]))x++;
        int word_len = x - start_x;
        while(x < line_len && iswspace(line_st_ptr[x]))x++;
        int next_x = x;
        x--;
        if(next_x >= line_len || line_st_ptr[next_x] != L'(')continue;
        if(find_syntax_word(line_st_ptr,line_len,start_x,reserved_words,
            sizeof(reserved_words) / sizeof(reserved_words[0])) > 0)continue;
        if(find_syntax_word(line_st_ptr,line_len,start_x,type_words,
            sizeof(type_words) / sizeof(type_words[0])) > 0)continue;

        if(word_len > view_cols - start_x)word_len = view_cols - start_x;
        if(add_syntax_data(syntax,start_x,h,word_len,mthod) < 0)return -1;

            
    }
    return 0;
}

int scan_syntax_comment(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment){

    wint_t comment_ev_str[2] = {L'/',L'/'};

    int is_find_command_str = 0;
    for(int i = 0; i < view_cols;i++){
        if(*(line_st_ptr + i) == comment_ev_str[0] &&
            is_find_command_str == 0){
            is_find_command_str++;
        }
        else if(*(line_st_ptr + i) == comment_ev_str[0] &&
            is_find_command_str == 1){
            add_syntax_data(syntax,i - 1,h,(view_cols - i),comment);
            break;
        }
    }
    
    return 0;
}


int scan_syntax_variable(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment){
    int limit = line_len < view_cols ? line_len : view_cols;
    for(int i = 0;i < limit;i++){
        int type_len = find_syntax_word(line_st_ptr,line_len,i,type_words,
            sizeof(type_words) / sizeof(type_words[0]));
        if(type_len == 0)continue;

        i += type_len;
        while(i < limit && (iswspace(line_st_ptr[i]) || line_st_ptr[i] == L'*'))i++;
        if(i >= limit)break;
        if(find_syntax_word(line_st_ptr,line_len,i,type_words,
            sizeof(type_words) / sizeof(type_words[0])) > 0){
            i--;
            continue;
        }
        if(!iswalpha(line_st_ptr[i]) && line_st_ptr[i] != L'_')continue;

        int st_pos_x = i;
        while(i < limit && is_word_char(line_st_ptr[i]))i++;
        int word_len = i - st_pos_x;
        while(i < limit && iswspace(line_st_ptr[i]))i++;
        if(i < limit && line_st_ptr[i] == L'(')continue;

        if(add_syntax_data(syntax,st_pos_x,h,word_len,comment) < 0)return -1;
        i--;
    }
    return 0;
}

int move_syntax_pos_data(syntax *syntax,int y){
    if(syntax == NULL)return -1;
    syntax_list_data *syntax_list = &syntax->syntax_list_data;
    for(int i = 0;i <syntax_list->syntax_list_num;i++){
        syntax_list->syntax_data[i].area.st_y +=y;
        syntax_list->syntax_data[i].area.end_y += y;
    }
    return 0;
}

int add_garbage_collection(syntax_data *syntax_data_ptr){
    if(syntax_data_ptr == NULL)return -1;
    if(syntax_data_collection == NULL){
        syntax_data_collection = 
            malloc(sizeof(struct syntax_data *) * garbage_collection_allocate_num);
        if(syntax_data_collection == NULL)return -1;
    }
    else if(garbage_collection_allocate_num <= collection_count){
        syntax_data **tmp_syntax_data_collection = 
            realloc(syntax_data_collection,
                sizeof(syntax_data *) * (collection_count * 2));
        if(tmp_syntax_data_collection == NULL)return -1;
        syntax_data_collection = tmp_syntax_data_collection;
        garbage_collection_allocate_num = collection_count * 2;
    }
    syntax_data_collection[collection_count] = syntax_data_ptr;
    collection_count++;
    return 0;
}

syntax* now_usint_syntax_ptr_ctl(syntax *now_using_syntax,enum flags flags){
    if(now_using_syntax == NULL && flags == set)return NULL;
    static syntax *now_using_syntax_ptr = NULL;
    if(flags == set){
        now_using_syntax_ptr = now_using_syntax;
        return now_using_syntax_ptr;
    }
    else if(flags == get){
        return now_using_syntax_ptr;
    }
    return NULL;
}

int scroll_syntax_pos_data(int y){
    syntax *syntax_ptr = now_usint_syntax_ptr_ctl(NULL,get);
    if(syntax_ptr == NULL)return -1;
    move_syntax_pos_data(syntax_ptr,y);
    return 0;
}