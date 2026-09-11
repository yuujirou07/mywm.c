
#ifndef TXT_EDITOR_SYNTAX_H
#define TXT_EDITOR_SYNTAX_H


#include "txt_editor.h"

// 解析対象の言語。現在はコメント開始文字列の切り替えに使用する。
typedef enum{
    C,
    CPP,
    PY,
    TS,
}language;

// 着色の分類。現在は予約語、リテラル、ヘッダー名、コメント、メソッド、型、変数を解析で生成する。
typedef enum{
    reserved_word,
    literal,
    comment,
    Method,
    type,
    operator,
    variable,
    declaration_keyword,
    character_literal,
    header_name,
    member_method,
}syntax_type;


// 本文表示領域の左上を原点とする0始まりの範囲。終端も範囲に含む。
// xは行のwint_t配列の添字で、文字の端末表示幅を換算した座標ではない。
typedef struct{
    int st_x; // 開始列。
    int st_y; // 開始行。ファイル行番号ではなく表示領域内の行番号。
    int end_x; // 終了列。
    int end_y; // 終了行。現在の解析ではst_yと同じ。
}syntax_area;

// 1つの着色範囲と、その分類。
typedef struct{
    syntax_area area; // 表示幅に切り詰めた着色範囲。
    syntax_type type; // 使用する色ペアを決める分類。
}syntax_data;

// 動的配列の所有者。利用終了時に呼び出し側がsyntax_dataをfreeする。
typedef struct{
    syntax_data *syntax_data; // 確保済み配列。解析時のreallocでアドレスが変わり得る。
    int syntax_list_num; // 有効な要素数。全体解析では再構築し、行解析では既存要素を部分更新する。
    int syntax_list_allocate_num; // 確保済みの要素数（バイト数ではない）。
}syntax_list_data;

// 言語設定と、表示領域を基準にした着色情報を保持する。
typedef struct{
    language lang; // set_syntax_languageで設定する言語。init_syntaxでは変更しない。
    syntax_list_data syntax_list_data; // この構造体が所有する着色情報の配列と件数。
}syntax;

// 現在の言語で使用する行コメント開始文字列。文字列リテラルを借用し、解放しない。
extern const wchar_t *comment_ev_str;


// 各関数の引数・戻り値・前提条件は実装側の定義直前に記載。
int set_syntax_language(language lang,syntax *syntax);
// posで単語が一致すればその長さ、非一致なら0。lineはNUL終端不要。
int find_syntax_word(const wint_t *line,int line_len,int pos,
                     const wchar_t *const words[],size_t word_count);
int set_syntax_data(syntax *syntax,struct editor_input_context *ctx);


int init_syntax(syntax *syntax);
void init_syntax_colors(void);
short syntax_color_pair(syntax_type word_type);

int apply_syntax_color(struct editor_input_context *ctx,syntax syntax);


int scan_syntax_method(syntax *syntax,wint_t *line_st_ptr,int line_len,int view_cols,int h,syntax_type ethod);

int scan_syntax_comment(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment);


int scan_syntax_variable(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment);

int scan_syntax_header_name(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type str_type);

int scan_syntax_literal(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type literal_type);

int move_syntax_pos_data(syntax *syntax,int y);
int scroll_syntax_pos_data(int y);

int update_line_syntax_data(struct editor_input_context *ctx,int line);
syntax* now_usint_syntax_ptr_ctl(syntax *now_using_syntax,enum flags flags);
#endif
