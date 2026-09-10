#include <ncurses.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include <wctype.h>
#include<wchar.h>

#include"txt_editor.h"
#include"txt_editor_syntax.h"

// Dark+の近似色。色ペア1〜3は本文・選択・エラー表示が使用する。
static const struct {
    short color_256;
    short color_8;
} syntax_colors[] = {
    [reserved_word] = {176,COLOR_MAGENTA},
    [literal]       = {173,COLOR_YELLOW},
    [comment]       = {65,COLOR_GREEN},
    [Method]        = {187,COLOR_YELLOW},
    [type]          = {74,COLOR_BLUE},
    [operator]      = {188,COLOR_WHITE},
};

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

short syntax_color_pair(syntax_type word_type){
    if(!has_colors())return 0;
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

static bool is_word_char(wint_t ch){
    return iswalnum(ch) || ch == L'_';
}

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
    data->area = (syntax_area){.st_x = x,.st_y = y,.end_x = x + word_len - 1,.end_y = y};
    data->type = word_type;
    list->syntax_list_num++;
    return 0;
}

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

int set_syntax_language(language lang,syntax *syntax){
    if(syntax == NULL)return -1;
    syntax->lang = lang;
    return 0;
}



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
    }
    return syntax->syntax_list_data.syntax_list_num;
}
