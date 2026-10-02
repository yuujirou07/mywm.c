#ifndef START_MENU_H
#define START_MENU_H
#include <stdbool.h>
#include <time.h>
#include "ascii_art_comb.h"

// スタートメニュープラグインの呼出し規約。返り値は下記の操作コード。
typedef int (*Start_Menu)(int screen_w, int screen_h, struct ascii_data *ascii_data,
                          const struct timespec *startup_start_time,
                          const char *startup_log_path);

// スタートメニュー画面の入力とプラグイン呼出しに必要な借用データ。
struct start_menu_screen_context {
    bool *open; // メインループが所有する表示継続フラグへの借用ポインタ。
    bool has_plugin; // pluginが呼出し可能ならtrue。
    Start_Menu plugin; // dlopenした共有ライブラリ内の関数。ここでは解放しない。
    struct ascii_data *ascii_data; // メイン処理が所有するASCIIアートへの借用ポインタ。
    const struct timespec *startup_start_time; // 起動時間計測の開始値への借用ポインタ。
    const char *startup_log_path; // 起動時間ログのパス。文字列を所有しない。
};

#define my_txt_editor_var 0.0 // スタートメニューへ表示するエディタの版番号。
#define new_file 0 // 新規ファイル作成を選択した返り値。
#define quit 1 // エディタ終了を選択した返り値。
#define select_folder 3 // フォルダ選択を選択した返り値。
#define settings 2 // 設定画面を選択した返り値。
#define none 4 // 操作が確定していない状態を表す値。
#define resize_request 5 // 端末リサイズによる再描画要求を表す返り値。
#define option_list_max 8 // プラグイン内の選択肢配列へ確保する要素数。

int draw_start_menu(int screen_max_w,int screen_max_h,struct ascii_data *ascii_data_ptr,
                    const struct timespec *startup_start_time,
                    const char *startup_log_path);

#endif
