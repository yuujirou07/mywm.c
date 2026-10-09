#include <ncurses.h>
#include <stdint.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <wctype.h>
#include<wchar.h>

#include"txt_editor.h"
#include"txt_editor_syntax.h"

// Dark Modernの256色近似。色ペア1〜3は本文・選択・エラー表示が使用する。
static const struct {
    short color_256; // 256色以上の端末で使う前景色番号。
    short color_8; // 256色未満の端末で使う基本色番号。
} syntax_colors[] = {
    [reserved_word] = {176,COLOR_MAGENTA},
    [literal]       = {173,COLOR_YELLOW},
    [comment]       = {65,COLOR_GREEN},
    [Method]        = {187,COLOR_YELLOW},
    [type]          = {68,COLOR_BLUE},
    [operator]      = {188,COLOR_WHITE},
    [variable]      = {153,COLOR_CYAN},
    [declaration_keyword] = {68,COLOR_BLUE},
    [character_literal] = {173,COLOR_YELLOW},
    [header_name] = {173,COLOR_YELLOW},
    [member_method] = {187,COLOR_YELLOW},
};

// 登録されたsyntax_dataへの借用ポインタを保持する。要素自体は所有・解放しない。
syntax_data **syntax_data_collection = NULL;
static int garbage_collection_allocate_num = 256;
static int collection_count = 0;
static void color_text_cells(struct editor_state *state,int y,int x,int width,short color);


// ncursesとstart_color初期化後に、利用可能な構文色ペアを4番以降へ登録する。
// 引数・返り値: なし。色非対応なら何もせず、色登録の失敗は通知しない。
void init_syntax_colors(void){
    if(!has_colors())return;
    for(size_t i = 0;i < sizeof(syntax_colors) / sizeof(syntax_colors[0]);i++){
        short pair = (short)(4 + i);
        if(pair >= COLOR_PAIRS)break; // 端末のペア上限を超える分は登録しない(syntax_color_pairが本文色へ退避)。
        short foreground = (COLORS >= 256)
            ? syntax_colors[i].color_256 : syntax_colors[i].color_8;
        init_pair(pair,foreground,COLOR_BLACK);
    }
}

// word_typeに対応する構文色ペア番号を返す。事前にinit_syntax_colorsを呼ぶ。
// 返り値: 色非対応は0、分類不正・ペア不足は本文用の1、それ以外は4+word_type。
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
    L"#define",
    L"#ifndef",
    L"#endif",
};

static const wchar_t *const declaration_words[] = {
    L"static",
    L"struct",
    L"union",
    L"enum",
    L"typedef",
    L"const",
    L"extern",
    L"volatile",
    L"inline",
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

// set_syntax_language()が現在の言語に合わせて切り替える。文字列リテラルを借用する。
const wchar_t *comment_ev_str = L"//";



// chが現在のロケールで英数字、またはアンダースコアならtrueを返す。
// 引数: 判定するワイド文字。入力は変更せず、それ以外はfalse。
static bool is_word_char(wint_t ch){
    return iswalnum(ch) || ch == L'_';
}

// line_lenセルのlineのposから、NUL終端のkeywordが前後の単語境界を含め一致するか調べる。
// 返り値: 一致ならtrue、空語・行末超過・不一致はfalse。非NULL入力と範囲内のposが必要。
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

// lineのposからwordsのword_count語を照合し、最初に単語境界まで一致した語の長さを返す。
// line_lenはセル数。NULLの辞書要素を飛ばし、入力不正・不一致は0。入力の変更・解放はしない。
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

// 初期化済みsyntaxへ画面相対セル(x,y)からword_lenセルをword_typeで登録し、同じ始点は上書きする。
// 返り値: 成功0、再確保失敗-1。再確保で既存要素へのポインタは無効になり得る。
static int add_syntax_data(syntax *syntax,int x,int y,int word_len,syntax_type word_type){
    syntax_list_data *list = &syntax->syntax_list_data;
    int count = list->syntax_list_num;
    if(count >= list->syntax_list_allocate_num){
        syntax_data *data = realloc(list->syntax_data,((size_t)count + 1) * sizeof(*data));
        if(data == NULL)return -1;
        list->syntax_data = data;
        list->syntax_list_allocate_num = count + 1;
    }
    
    // 同じ始点の登録は後勝ちで上書きする。set_syntax_dataの呼び出し順が色の優先度になる。
    syntax_data *data = &list->syntax_data[count];
    bool use_new_arry = true; 
    for(int i = 0; i < count;i++){
        if(list->syntax_data[i].area.pos.x == x &&
            list->syntax_data[i].area.pos.y == y){
                data = &list->syntax_data[i];
                memset(data,0,sizeof(syntax_data));
                use_new_arry = false;
        }
    }
    data->area = (struct box){.pos = {x,y},.w = word_len,.h = 1};
    data->type = word_type;
    list->syntax_list_num = (use_new_arry)
        ?list->syntax_list_num+1:list->syntax_list_num;

    return 0;
}



// line_lenセルのlineからwordsのword_count語を探し、表示幅view_cols内を分類word_typeでsyntaxへ追加する。
// yは画面相対行。返り値: 成功0、追加失敗-1。失敗前に追加した情報は残る。
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

// ncurses初期化後、未確保のsyntaxに画面セル数分の配列を確保し、件数を0にする。langは保持する。
// 返り値: 成功0、NULL・画面寸法不正・確保失敗-1。配列は呼び出し側でfreeし、確保済み状態では再実行しない。
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

// syntaxへlangを保存し、行コメント記号をC/CPP/TSでは//、PYでは#にする。UNKNOWNは記号を保持する。
// 返り値: 成功0、syntaxがNULLまたは言語不正なら-1。既存の解析結果は更新しない。
int set_syntax_language(language lang,syntax *syntax){
    if(syntax == NULL)return -1;
    switch(lang){
        case C:
        case CPP:
        case TS:
            comment_ev_str = L"//";
            break;
        case PY:
            comment_ev_str = L"#";
            break;
        case UNKNOWN:
            break;
        default:
            return -1;
    }
    syntax->lang = lang;
    return 0;
}



// ctxの表示範囲を解析し、初期化済みsyntaxの着色情報を再構築する。複数行コメントの状態は追跡しない。
// 返り値: 登録件数、不正な状態・追加失敗は-1。途中失敗では部分結果が残り、再確保で要素ポインタが無効になり得る。
int set_syntax_data(syntax *syntax,struct editor_input_context *ctx){
    if(syntax == NULL || ctx == NULL || ctx->state == NULL)return -1;

    syntax->syntax_list_data.syntax_list_num = 0;
    if(syntax->syntax_list_data.syntax_data == NULL)return -1;

    // 色は画面に見えている行だけ計算する(hは画面相対、ファイル行はscr_start_numからのオフセット)。
    // 同じ始点は後のscanが上書きするため、literal・コメント等の優先したい種別を後ろに置く。
    for(int h = 0;h < ctx->state->write_area.h;h++){
        int now_file_line_num = ctx->state->scr.scr_start_num + h;
        if(now_file_line_num < 0 || now_file_line_num >= editor_line_limit(ctx->state))break;
        int line_str_len    = editor_line_len(ctx->state,now_file_line_num);
        wint_t *line_st_ptr = editor_line_cells(ctx->state,now_file_line_num);
        if(line_st_ptr == NULL)continue;
        int view_cols = editor_view_cols(ctx->state);

        if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
            reserved_words,
            sizeof(reserved_words) / sizeof(reserved_words[0]),reserved_word) < 0)return -1;
        if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
            type_words,
            sizeof(type_words) / sizeof(type_words[0]),type) < 0)return -1;
        if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
            declaration_words,sizeof(declaration_words) / sizeof(declaration_words[0]),
            declaration_keyword) < 0)return -1;
        if(scan_syntax_method(syntax,line_st_ptr,
            line_str_len,view_cols,h,Method) < 0)return -1;
        if(scan_syntax_comment(syntax,line_st_ptr,
            line_str_len,view_cols,h,comment) < 0)return -1;
        if(scan_syntax_variable(syntax,line_st_ptr,
            line_str_len,view_cols,h,variable) < 0)return -1;
        if(scan_syntax_literal(syntax,line_st_ptr,
            line_str_len,view_cols,h,literal) < 0)return -1;
        if(scan_syntax_header_name(syntax,line_st_ptr,
            line_str_len,view_cols,h,header_name) < 0)return -1;
    }
    return syntax->syntax_list_data.syntax_list_num;
}

// ctxの確保済み構文情報から画面相対行lineの旧情報を削除し、その行だけ再解析する。
// 返り値: 全登録件数、状態・対象行不正や追加失敗は-1。他行の情報は保持し、途中失敗は巻き戻さない。
int update_line_syntax_data(struct editor_input_context *ctx,int line){
    if(ctx == NULL || ctx->state == NULL)return -1;
    syntax *syntax = &ctx->syntax_data;
    if(syntax == NULL)return -1;
    if(syntax->syntax_list_data.syntax_data == NULL)return -1;

    // 1行編集のたびに全画面を再解析しないよう、対象行の旧データだけを詰め直して再計算する。
    int h = line;
    int now_file_line_num = ctx->state->scr.scr_start_num + h;
    if(now_file_line_num < 0 || now_file_line_num >= editor_line_limit(ctx->state))return -1;
    syntax_list_data *list = &syntax->syntax_list_data;
    int write_index = 0;
    for(int read_index = 0;read_index < list->syntax_list_num;read_index++){
        syntax_data *data = &list->syntax_data[read_index];
        if(data->area.pos.y == h)continue;
        if(write_index != read_index)list->syntax_data[write_index] = *data;
        write_index++;
    }
    list->syntax_list_num = write_index;

    int line_str_len = editor_line_len(ctx->state,now_file_line_num);
    wint_t *line_st_ptr = editor_line_cells(ctx->state,now_file_line_num);
    if(line_st_ptr == NULL)return list->syntax_list_num;
    int view_cols = editor_view_cols(ctx->state);
    if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
        reserved_words,sizeof(reserved_words) / sizeof(reserved_words[0]),reserved_word) < 0)return -1;
    if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
        type_words,sizeof(type_words) / sizeof(type_words[0]),type) < 0)return -1;
    if(scan_syntax_words(syntax,line_st_ptr,line_str_len,view_cols,h,
        declaration_words,sizeof(declaration_words) / sizeof(declaration_words[0]),declaration_keyword) < 0)return -1;
    if(scan_syntax_method(syntax,line_st_ptr,line_str_len,view_cols,h,Method) < 0)return -1;
    if(scan_syntax_comment(syntax,line_st_ptr,line_str_len,view_cols,h,comment) < 0)return -1;
    if(scan_syntax_variable(syntax,line_st_ptr,line_str_len,view_cols,h,variable) < 0)return -1;
    if(scan_syntax_literal(syntax,line_st_ptr,line_str_len,view_cols,h,literal) < 0)return -1;
    if(scan_syntax_header_name(syntax,line_st_ptr,line_str_len,view_cols,h,header_name) < 0)return -1;
    return syntax->syntax_list_data.syntax_list_num;
}



void set_syntax_color_line(struct editor_input_context *ctx,uint16_t line,uint16_t size){
    syntax *tmp_syntax = &ctx->syntax_data;    
    struct editor_state *state = ctx->state;

    for(int i = 0;i < tmp_syntax->syntax_list_data.syntax_list_num;i++){

        syntax_data tmp_syntax_data = tmp_syntax->syntax_list_data.syntax_data[i];
        struct box syntax_box = tmp_syntax_data.area;

        if(syntax_box.pos.y >= line && syntax_box.pos.y + syntax_box.h <= line + size){
            int view_cols = editor_view_cols(state);
            int col = syntax_box.pos.x;
            if(syntax_box.pos.y >= state->write_area.h ||
                col < 0 || col >= view_cols || syntax_box.w <= 0){
                    continue;
            }
            
            int end = col + (syntax_box.w < view_cols - col ? syntax_box.w : view_cols - col);
            int logical_line = state->scr.scr_start_num + syntax_box.pos.y;
            int len = editor_line_len(state,logical_line);
            wint_t *cells = editor_line_cells(state,logical_line);
            
            if(cells == NULL)continue;
            // 全角文字は2セル目が0で埋まっているため、着色範囲が文字の途中で切れないよう両端を広げる。
            while(col > 0 && col < len && cells[col] == 0)col--;
            while(end < len && end < view_cols && cells[end] == 0)end++;

            
            short syntax_color = syntax_color_pair(tmp_syntax_data.type);
            color_text_cells(state,
                syntax_box.pos.y + state->write_area.y_start,
                col + state->write_area.x_start,
                end - col,
                syntax_color
            );
        }
    }

}

// 画面座標(x,y)からwidthセルへ色ペアcolorを適用し、ACS罫線の属性は保持する。
// 返り値: なし。ncurses初期化済みで有効な画面範囲を渡す。描画失敗は通知しない。
static void color_text_cells(struct editor_state *state,int y,int x,int width,short color){
    int start = -1;
    for(int i = 0;i < width;i++){

        // 補完ウィンドウ上のセルは補完側の色を使うため、本文の構文色で上書きしない。
        // start--は、ここで打ち切る時点でstartが未設定(-1)でも末尾の着色が走らないようにするため。
        if(state->settings_data->auto_complete_settings_data.auto_complete_enabled &&
            state->edit_input_complete_data.show){
            struct box complete_box = complete_box_screen(state);
            if(box_contains_point(complete_box,(struct pos){x + i,y}) == true){
                start--;
                break;
            }
        }
        // 枠線(ACS文字)は属性を変えると崩れるので、その手前までで着色を区切る。
        if(mvinch(y,x + i) & A_ALTCHARSET){
            if(start >= 0){
                mvchgat(y,x + start,i - start,A_NORMAL,color,NULL);
                start = -1;
            }
        }
        else if(start < 0){
            start = i;
        }
    }

    if(start >= 0){
        mvchgat(y,x + start,width - start,A_NORMAL,color,NULL);
    }
}

// ctxの本文領域を通常色に戻し、借用したsyntaxの可視範囲へ構文色を適用する。
// 返り値: ctxがNULLなら-1、それ以外0。stateは必須で、配列の所有権は移動せず描画失敗は通知しない。
int apply_syntax_color(struct editor_input_context *ctx,syntax syntax){
    if(ctx == NULL)return -1;
    struct editor_state *state = ctx->state; 

    int syntax_num = syntax.syntax_list_data.syntax_list_num;
    
    // 本文描画の後に色だけを重ねる方式なので、まず色ペア1(通常色)に戻して前回の着色を消す。
    for(int h = 0;h < state->write_area.h;h++){
        if(state->write_area.w > 0){
            color_text_cells(state,state->write_area.y_start + h,state->write_area.x_start,
                state->write_area.w,1);
            }
    }
    if(syntax.lang == UNKNOWN)return 0;

    for(int i = 0;i < syntax_num;i++){
        syntax_data *data = &syntax.syntax_list_data.syntax_data[i];
        struct box *area = &data->area;
        if(area->pos.y < 0 || area->pos.y + area->h - 1 < 0)continue;

        else if(area->pos.y >= state->write_area.h || area->pos.y + area->h - 1 >= state->write_area.h)continue;

        short syntax_color = syntax_color_pair(data->type); 
        color_text_cells(state,state->write_area.y_start + area->pos.y,
            state->write_area.x_start + area->pos.x,
            area->w,syntax_color);
    }
    return 0;
}


// line_st_ptrのline_lenセルを調べ、括弧が続く識別子をsyntaxへ追加する。view_colsは表示幅、hは画面相対行。
// mthodは通常関数の分類、メンバーはmember_method。返り値: 成功0、追加失敗-1（追加済み情報は残る）。
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
        x--; // forのx++と合わせ、空白の次の文字から再走査する。
        if(next_x >= line_len || line_st_ptr[next_x] != L'(')continue;
        if(find_syntax_word(line_st_ptr,line_len,start_x,reserved_words,
            sizeof(reserved_words) / sizeof(reserved_words[0])) > 0)continue;
        if(find_syntax_word(line_st_ptr,line_len,start_x,type_words,
            sizeof(type_words) / sizeof(type_words[0])) > 0)continue;
        if(find_syntax_word(line_st_ptr,line_len,start_x,declaration_words,
            sizeof(declaration_words) / sizeof(declaration_words[0])) > 0)continue;

        if(word_len > view_cols - start_x)word_len = view_cols - start_x;
        int prev_x = start_x - 1;
        while(prev_x >= 0 && iswspace(line_st_ptr[prev_x]))prev_x--;
        syntax_type method_type = mthod;
        // 直前が「.」「->」なら関数ではなくメンバー呼び出しとして別色にする。
        if(prev_x >= 0 && (line_st_ptr[prev_x] == L'.' ||
            (prev_x > 0 && line_st_ptr[prev_x] == L'>' && line_st_ptr[prev_x - 1] == L'-'))){
            method_type = member_method;
        }
        if(add_syntax_data(syntax,start_x,h,word_len,method_type) < 0)return -1;

            
    }
    return 0;
}

// line_st_ptrのline_lenセルから行コメント記号を探し、h行の開始位置からview_colsまでを分類commentでsyntaxへ登録する。
// 返り値: 対象なし・成功0、追加失敗-1。syntaxは初期化済みとし、文字列内の記号も検出する。
int scan_syntax_comment(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment){

    int limit = line_len < view_cols ? line_len : view_cols;
    int comment_ev_str_len = (int)wcslen(comment_ev_str);
    for(int i = 0;i + comment_ev_str_len <= limit;i++){
        bool is_comment_start = true;
        for(int j = 0;j < comment_ev_str_len;j++){
            if(line_st_ptr[i + j] != (wint_t)comment_ev_str[j]){
                is_comment_start = false;
                break;
            }
        }
        if(is_comment_start){
            return add_syntax_data(
                syntax,
                i,
                h,
                (view_cols - i),
                comment);
        }
    }
    return 0;
}


// line_st_ptrの型名に続く識別子をsyntaxへ分類commentで追加する。line_len/view_colsはセル数、hは画面相対行。
// 関数形式は除外する。返り値: 成功0、追加失敗-1（追加済み情報は残る）。syntaxは初期化済みとする。
int scan_syntax_variable(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type comment){
    int limit = line_len < view_cols ? line_len : view_cols;
    for(int i = 0;i < limit;i++){
        int type_len = find_syntax_word(line_st_ptr,line_len,i,type_words,
            sizeof(type_words) / sizeof(type_words[0]));
        if(type_len == 0)continue;

        // 「int *p」「int a, b」の変数名を色付けするため、型名とポインタ記号・空白を読み飛ばす。
        i += type_len;
        while(i < limit && (iswspace(line_st_ptr[i]) || line_st_ptr[i] == L'*'))i++;
        if(i >= limit)break;
        if(find_syntax_word(line_st_ptr,line_len,i,type_words,
            sizeof(type_words) / sizeof(type_words[0])) > 0){
            i--; // 「unsigned int」のように型が続く場合は次ループで後ろの型から数え直す。
            continue;
        }
        if(!iswalpha(line_st_ptr[i]) && line_st_ptr[i] != L'_')continue;

        int st_pos_x = i;
        while(i < limit && is_word_char(line_st_ptr[i]))i++;
        int word_len = i - st_pos_x;
        while(i < limit && iswspace(line_st_ptr[i]))i++;
        if(i < limit && line_st_ptr[i] == L'(')continue; // 関数宣言は変数色にしない(Method側が着色する)。

        if(add_syntax_data(syntax,st_pos_x,h,word_len,comment) < 0)return -1;
        i--; // forのi++で名前の直後の文字(,など)を読み飛ばさないための補正。
    }
    return 0;
}

// syntaxの全着色範囲の行座標へyを加算する。正で下、負で上へ動かす。
// 返り値: 成功0、syntaxがNULLなら-1。画面外へ出た要素も保持する。
int move_syntax_pos_data(syntax *syntax,int y){
    if(syntax == NULL)return -1;
    syntax_list_data *syntax_list = &syntax->syntax_list_data;
    for(int i = 0;i <syntax_list->syntax_list_num;i++){
        syntax_list->syntax_data[i].area.pos.y += y;
    }
    return 0;
}

// syntax_data_ptrの借用ポインタを内部一覧へ登録する。登録中は呼び出し側で参照先を有効に保つ。
// 返り値: 成功0、NULL・一覧の確保失敗-1。現実装には登録解除や一覧の解放処理がない。
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

// syntax_ptrの全着色範囲へy行を加算し、表示行数view_rowsの外へ出た情報を削除する。
// 返り値: 成功0、syntax_ptrがNULLまたはview_rowsが0以下なら-1。配列の容量は保持する。
int scroll_syntax_pos_data(syntax *syntax_ptr,int y,int view_rows){
    if(syntax_ptr == NULL || view_rows <= 0)return -1;
    if(move_syntax_pos_data(syntax_ptr,y) < 0)return -1;

    syntax_list_data *list = &syntax_ptr->syntax_list_data;
    int write_index = 0;
    // スクロール後も再解析せず使い回し、画面外に出た分だけを前へ詰めて捨てる。
    for(int read_index = 0;read_index < list->syntax_list_num;read_index++){
        syntax_data *data = &list->syntax_data[read_index];
        if(data->area.pos.y + data->area.h - 1 < 0 || data->area.pos.y >= view_rows)continue;
        if(write_index != read_index)list->syntax_data[write_index] = *data;
        write_index++;
    }
    list->syntax_list_num = write_index;
    return 0;
}

// line_st_ptrの行頭#includeに続くヘッダー名を囲み文字ごとsyntaxへ分類str_typeで追加する。
// line_len/view_colsはセル数、hは画面相対行。返り値: 対象なし・成功0、追加失敗-1。syntaxは初期化済み。
int scan_syntax_header_name(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type str_type){
    const wchar_t *const words[] = {
        L"#include"
    };

    int include_len = find_syntax_word(
        line_st_ptr,
        line_len,
        0,
        words,
        sizeof(words) / sizeof(words[0])
    );
    if(include_len == 0)return 0;

    int limit = line_len < view_cols ? line_len : view_cols;
    int start_x = include_len;
    while(start_x < limit && iswspace(line_st_ptr[start_x]))start_x++;
    if(start_x >= limit)return 0;

    wint_t close_ch;
    if(line_st_ptr[start_x] == L'<')close_ch = L'>';
    else if(line_st_ptr[start_x] == L'"')close_ch = L'"';
    else return 0;

    int end_x = start_x + 1;
    while(end_x < limit && line_st_ptr[end_x] != close_ch)end_x++;
    if(end_x >= limit)return 0;

    return add_syntax_data(syntax,start_x,h,end_x - start_x + 1,str_type);
}

// line_st_ptrの引用符で閉じたリテラルをsyntaxへ追加する。line_len/view_colsはセル数、hは画面相対行。
// 文字列はliteral_type、文字はcharacter_literal。返り値: 成功0、追加失敗-1。未閉鎖は登録せず、syntaxは初期化済み。
int scan_syntax_literal(syntax *syntax,wint_t *line_st_ptr,
        int line_len,int view_cols,int h,syntax_type literal_type){
    int limit = line_len < view_cols ? line_len : view_cols;

    for(int i = 0;i < limit;i++){
        wint_t quote = line_st_ptr[i];
        if(quote != L'"' && quote != L'\'')continue;

        int start_x = i;
        bool escaped = false;
        for(i++;i < limit;i++){
            if(escaped){
                escaped = false;
                continue;
            }
            if(line_st_ptr[i] == L'\\'){
                escaped = true;
                continue;
            }
            if(line_st_ptr[i] == quote){
                if(add_syntax_data(
                    syntax,
                    start_x,
                    h,
                    i - start_x + 1,
                    quote == L'\'' ? character_literal : literal_type
                ) < 0)return -1;
                break;
            }
        }
    }
    return 0;
}
