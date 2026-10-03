#ifndef DEFALT_SETTINGS_H
#define DEFALT_SETTINGS_H

#include "txt_editor_icon.h"
#include "input_complete.h"
#include<ncurses.h>
#include<editor_types.h>

#define MAX_LINES    1000 // 初期設定で扱う最大行数。
#define MAX_LINE_SIZE 1024 // ファイル読込み時の1行バッファ長。
#define LINE_NUMBER_SPACE 4 // 行番号表示へ確保する端末セル数。
#define INDENT_RANGE 8 // Tab入力1回で挿入する空白数。
#define JMP_SET_CUR_POS 10 // 行ジャンプ後のカーソル位置として保持する既定値。
#define DEFAULT_LOAD_LINE_SIZE 9999 // 読み込める論理行数の既定上限。
#define LOAD_BUFFER_LINES 100 // 編集バッファへ最初に確保する行数。
#define SHOW_STATUS_BAR true // ステータスバーを表示する既定値。
#define DEFAULT_DRAW_SPLIT_LINE 1 // 行番号欄と本文の境界線を描く既定値。
#define DEFAULT_STATUS_BAR_SIDE top // ステータスバーを置く既定の画面端。
#define JUMP_LINE_NUM_DIGITS 4 // 行ジャンプ欄へ入力できる最大桁数。
#define DEFAULT_ASK_MAKE_FILE 1 // 未作成ファイルを開く際に作成確認を出す既定値。
#define DEFAULT_PATH_NAME_MAX_SIZE PATH_MAX // ファイルパス配列の要素数。
#define DEFAULT_FILE_SELECT_SCENE_LIGHTING true // ファイル選択行を反転表示する既定値。
#define DEFAULT_SHOW_START_MENU true // 起動時にスタートメニューを表示する既定値。
#define DEFAULT_LSP_PROCESS_LAUNCH_STARTUP_EDITOR true // 起動時にLSPを開始する既定値。
#define DEFAULT_EPOLL_TIME_OUT_MS 16 // LSPイベント待機の既定時間（ミリ秒）。
#define DEFAULT_LSP_USE true // LSP連携を有効にする既定値。
#define DEFAULT_USE_ICON false // ファイル種別アイコンを表示する既定値。
#define DEFAULT_BUILT_IN_SYNTAX true // 組込み構文着色を有効にする既定値。

#define DEFAULT_AUTO_COMPLETE true // 自動補完を有効にする既定値。
#define DEFAULT_AUTO_COMPLETE_WINDOW true // 補完候補ウィンドウを表示する既定値。
#define DEFAULT_AUTO_COMPLETE_WINDOW_WIDTH 12 // 候補ウィンドウの既定幅（端末セル数）。
#define DEFAULT_AUTO_COMPLETE_WINDOW_HEIGHT 7 // 候補ウィンドウの既定高さ（端末行数）。
#define DEFAULT_AUTO_COMPLETE_POSITION_MODE EDIT_COMP_TRACKING // 候補ウィンドウを入力位置に追従させる既定値。

#define DEFAULT_SETTINGS_LANGUAGE 2





typedef enum{
    c   = 0,
    lua = 1,
    unknown = 2,
}settings_lang;

const char *const SETTINGS_LANGUAGE_JSON_KEY_STR[] = 
    {
        "c",
        "lua",
        "unknown",
    };



// LSPの有効状態、起動方法、ポーリング条件を保持する設定。
struct lsp_settings_data{
    bool lsp_use; // LSPとの送受信を行うならtrue。
    bool lsp_launch_startup_editor; // エディタ起動時に言語サーバーを開始するならtrue。
    int lsp_epoll_timeout_ms; // epoll_wait()の待機時間（ミリ秒）。
    char lsp_language[32]; // 起動対象の言語サーバー名を保持するNUL終端文字列。
};

// 自動補完と候補ウィンドウの表示設定。
typedef struct{
    bool auto_complete_enabled; // 自動補完機能を有効にするならtrue。
    bool auto_complete_window_enable; // 候補ウィンドウを表示するならtrue。
    struct pos auto_complete_window_size; // x=幅、y=高さ。どちらも枠を含む端末セル数。
    EDIT_COMPLETE_POSITION_MODE auto_complete_position_mode; // JSONのwindow.position_modeを列挙値で保持する。

}auto_complete_settings_data;

// 既定値と設定JSONを統合した、実行中のエディタ設定。
struct editor_settings{
    int max_lines; // 扱う最大行数として保持する設定値。
    int max_line_size; // ファイル読込み時の1行バッファ長。
    int line_number_space; // 行番号欄へ確保する端末セル数。
    int indent_range; // Tab入力1回で挿入する空白数。
    int jmp_set_cur_pos; // 行ジャンプ後のカーソル位置として保持する設定値。
    int default_load_line_size; // 読込みと行管理配列に使う論理行数の上限。
    int load_buffer_lines; // 編集バッファへ最初に確保する行数。
    int bar_side_state; // enum status_bar_sideの値。
    bool show_status_bar; // ステータスバーを表示するならtrue。
    bool draw_split_line; // 行番号欄と本文の境界線を描くならtrue。
    bool ask_make_file; // 未作成ファイルを開く際に作成確認を出すならtrue。
    bool show_start_menu; // 起動時にスタートメニューを表示するならtrue。
    bool file_select_scene_lighting; // ファイルブラウザの選択行を反転表示するならtrue。
    bool use_icon; // ファイル種別アイコンを表示するならtrue。
    bool built_in_syntax; // 組込み構文着色を使うならtrue。
    auto_complete_settings_data auto_complete_settings_data; // 自動補完の設定一式。
    struct lsp_settings_data lsp; // LSP連携の設定一式。
    icon_data icon_data; // 読み込んだ拡張子別アイコン配列を所有する。
    settings_lang settings_lang;
};


#endif 
