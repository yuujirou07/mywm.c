
#ifndef TXT_EDITOR_SYNTAX_H
#define TXT_EDITOR_SYNTAX_H


#include "txt_editor.h"

// 解析対象の言語。現在はコメント開始文字列の切り替えに使用する。
typedef enum{
    C, // C。
    CPP, // C++。
    PY, // Python。
    TS, // TypeScript。
    UNKNOWN, // 未対応言語。構文着色を適用しない。
}language;

// 着色の分類。現在は予約語、リテラル、ヘッダー名、コメント、メソッド、型、変数を解析で生成する。
typedef enum{
    reserved_word, // 制御構文などの予約語。
    literal, // 二重引用符で囲まれた文字列。
    comment, // 行コメント。
    Method, // 通常の関数・メソッド呼出し。
    type, // 組込み型または登録済みの型名。
    operator, // 演算子。
    variable, // 変数として検出した識別子。
    declaration_keyword, // 宣言を開始するキーワード。
    character_literal, // 単一引用符で囲まれた文字。
    header_name, // include対象のヘッダー名。
    member_method, // '.'または'->'に続くメソッド。
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
typedef struct syntax{
    language lang; // set_syntax_languageで設定する言語。init_syntaxでは変更しない。
    syntax_list_data syntax_list_data; // この構造体が所有する着色情報の配列と件数。
}syntax;

// 現在の言語で使用する行コメント開始文字列。文字列リテラルを借用し、解放しない。
extern const wchar_t *comment_ev_str;


// 解析言語と行コメント開始文字列を切り替える。
// 引数: langはC/CPP/PY/TS/UNKNOWN、syntaxは設定先。
// 戻り値: 成功時0、NULLまたは範囲外の言語なら-1。既存の解析結果は変更しない。
int set_syntax_language(language lang,syntax *syntax);

// 行のposから辞書内の単語を単語境界付きで照合する。
// 引数: lineはline_len要素のNUL終端不要な配列、posは開始位置、wordsはword_count要素の辞書。
// 戻り値: 最初に一致した単語の文字数。不一致または無効な引数なら0。入力の所有権は移動しない。
int find_syntax_word(const wint_t *line,int line_len,int pos,
                     const wchar_t *const words[],size_t word_count);

// 現在表示している本文を再解析し、syntaxの着色情報を再構築する。
// 引数: syntaxはinit_syntax()成功済み、ctxは有効なstateを持つ入力コンテキスト。どちらも借用する。
// 戻り値: 登録件数。無効な引数・未確保・追加失敗なら-1。失敗時は途中までの結果が残る。
int set_syntax_data(syntax *syntax,struct editor_input_context *ctx);


// 画面寸法分の着色情報配列を確保し、有効件数を0にする。langは変更しない。
// 引数: syntaxは未確保の管理情報。ncurses初期化後に呼び出す。
// 戻り値: 成功時0、NULL・画面寸法不正・確保失敗なら-1。成功後の配列は呼び出し側がfreeする。
int init_syntax(syntax *syntax);

// 構文分類用のncurses色ペアを登録する。
// 引数: なし。start_color()後に呼び出す。
// 戻り値: なし。色非対応なら何もしない。
void init_syntax_colors(void);

// 構文分類に対応するncurses色ペア番号を返す。
// 引数: word_typeは着色分類。使用前にinit_syntax_colors()を呼び出す。
// 戻り値: 対応する色ペア。色非対応なら0、分類不正またはペア不足なら1。
short syntax_color_pair(syntax_type word_type);

// 本文領域を通常色に戻し、登録済みの構文色を画面へ適用する。
// 引数: ctxは有効なstateを持つコンテキスト、syntaxは着色配列を借用する浅いコピー。
// 戻り値: 成功時0、ctxがNULLなら-1。ncursesの失敗は返り値に反映しない。
int apply_syntax_color(struct editor_input_context *ctx,syntax syntax);


// 識別子の後ろに空白と'('が続く箇所をメソッドとして登録する。
// 引数: syntaxは初期化済み、line_st_ptrはline_len要素の行、view_colsは表示列数、hは画面相対行、ethodは通常メソッドの分類。
// 戻り値: 成功時0、着色情報の追加失敗なら-1。予約語と型名は除外し、'.'または'->'の後ろはmember_methodにする。
int scan_syntax_method(syntax *syntax,wint_t *line_st_ptr,int line_len,int view_cols,int h,syntax_type ethod);

// 行コメント開始文字列から表示行末までを着色情報へ登録する。
// 引数: syntaxは初期化済み、line_st_ptrはline_len要素の行、view_colsは表示列数、hは画面相対行、commentは登録分類。
// 戻り値: コメントなしまたは追加成功なら0、追加失敗なら-1。
int scan_syntax_comment(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment);


// 型名の後ろにある識別子を変数候補として登録する。
// 引数: syntaxは初期化済み、line_st_ptrはline_len要素の行、view_colsは表示列数、hは画面相対行、commentは登録分類。
// 戻り値: 成功時0、追加失敗なら-1。空白と'*'を読み飛ばし、関数形式は除外する。
int scan_syntax_variable(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment);

// 行頭の#includeに続く山括弧または二重引用符形式のヘッダー名を登録する。
// 引数: syntaxは初期化済み、line_st_ptrはline_len要素の行、view_colsは表示列数、hは画面相対行、str_typeは登録分類。
// 戻り値: 対象なしまたは追加成功なら0、追加失敗なら-1。囲み文字も着色範囲に含む。
int scan_syntax_header_name(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type str_type);

// 二重引用符の文字列と単一引用符の文字を、囲み文字ごと登録する。
// 引数: syntaxは初期化済み、line_st_ptrはline_len要素の行、view_colsは表示列数、hは画面相対行、literal_typeは文字列の分類。
// 戻り値: 成功時0、追加失敗なら-1。文字はcharacter_literalに分類し、閉じていないリテラルは登録しない。
int scan_syntax_literal(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type literal_type);

// 全着色範囲の画面相対行を同じ量だけ移動する。
// 引数: syntaxは移動対象、yは加算行数。正なら下、負なら上へ移動する。
// 戻り値: 成功時0、syntaxがNULLなら-1。画面外の範囲も削除しない。
int move_syntax_pos_data(syntax *syntax,int y);

// 現在使用中のsyntaxをスクロールし、画面外の着色範囲を削除する。
// 引数: syntaxは更新対象、yは加算行数、view_rowsは表示行数。
// 戻り値: 成功時0、syntaxがNULLまたはview_rowsが0以下なら-1。
int scroll_syntax_pos_data(syntax *syntax,int y,int view_rows);

// 指定した表示行の旧着色情報を削除し、その1行だけを再解析する。
// 引数: ctxは有効なstateとsyntax_dataを持つコンテキスト、lineは表示領域先頭を0とする行。配列確保が必要。
// 戻り値: 更新後の登録件数。無効な引数・未確保・追加失敗なら-1。
int update_line_syntax_data(struct editor_input_context *ctx,int line);
#endif
