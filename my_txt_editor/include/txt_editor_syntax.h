
#ifndef TXT_EDITOR_SYNTAX_H
#define TXT_EDITOR_SYNTAX_H


#include "txt_editor.h"
typedef enum{
    C,
    CPP,
    PY,
    TS,
}language;

typedef enum{
    reserved_word,
    literal,
    comment,
    Method,
    type,
    operator,
}syntax_type;


typedef struct{
    int st_x;
    int st_y;
    int end_x;
    int end_y;
}syntax_area;

typedef struct{
    syntax_area area;
    syntax_type type;
}syntax_data;

typedef struct{
    syntax_data *syntax_data;
    int syntax_list_num;
    int syntax_list_allocate_num;
}syntax_list_data;

typedef struct{
    language lang;
    syntax_list_data syntax_list_data;
}syntax;


int set_syntax_language(language lang,syntax *syntax);
// posで単語が一致すればその長さ、非一致なら0。lineはNUL終端不要。
int find_syntax_word(const wint_t *line,int line_len,int pos,
                     const wchar_t *const words[],size_t word_count);
int set_syntax_data(syntax *syntax,struct editor_input_context *ctx);


int init_syntax(syntax *syntax);
void init_syntax_colors(void);
short syntax_color_pair(syntax_type word_type);


#endif
