# my_cui_lib / my_cui_lib_linux 説明書

Linux ターミナル向けの CUI ライブラリ。  
ANSIエスケープシーケンスを内部バッファに積み上げ、`screen_push()` で一括 `fwrite` することで画面のちらつきを抑える設計になっている。

---

## ファイル構成

| ファイル | 役割 |
|---|---|
| `include/my_cui_lib.h` | 公開 API（型・定数・関数宣言） |
| `src/my_cui_lib_linux.c` | Linux 実装（termios / ioctl 依存） |

---

## 基本的な使い方

```c
#include "my_cui_lib.h"

int main(void) {
    term_init();          // バッファをリセット
    change_buff_screen(); // オルタネートスクリーンへ切り替え
    hidden_cursor();      // カーソルを非表示

    erase_screen();
    move_cursor(1, 1);
    myprint("Hello, world!");

    screen_push();        // ここで初めて画面に反映される

    // ... イベントループ ...

    back_forward_screen(); // 元のスクリーンに戻す
    show_cursor();
    screen_push();
    return 0;
}
```

> **重要**: `screen_push()` を呼ぶまで出力はバッファにしか存在しない。

---

## 型定義

### `struct rgb`

```c
struct rgb {
    int r;  // 赤   0〜255
    int g;  // 緑   0〜255
    int b;  // 青   0〜255
};
```

24bit フルカラー (True Color) の RGB 値を保持する。

### `enum eight_bit_rgb`

```c
enum eight_bit_rgb {
    black, red, green, yellow, blue, magenta, cyan, white
};
```

ANSI 8色（標準色）を名前で指定するための列挙型。

---

## 定数

| 定数 | 値 | 意味 |
|---|---|---|
| `MY_BLACK`   | 0 | 黒 |
| `MY_RED`     | 1 | 赤 |
| `MY_GREEN`   | 2 | 緑 |
| `MY_YELLOW`  | 3 | 黄 |
| `MY_BLUE`    | 4 | 青 |
| `MY_MAGENTA` | 5 | マゼンタ |
| `MY_CYAN`    | 6 | シアン |
| `MY_WHITE`   | 7 | 白 |

これらは `enum eight_bit_rgb` の内部オフセット値であり、直接使う場面は少ない。

---

## 関数リファレンス

### バッファ管理

#### `int term_init(void)`

内部バッファのポインタを先頭にリセットする。  
`screen_push()` が内部で自動的に呼ぶため、通常は手動で呼ぶ必要はない。

#### `void screen_push(void)`

バッファに積んだ全エスケープシーケンス・文字列を `stdout` に一括出力し、バッファをリセットする。  
**画面への反映はこの関数でのみ行われる。**

---

### カーソル制御

| 関数 | エスケープシーケンス | 説明 |
|---|---|---|
| `move_cursor(x, y)` | `CSI y;x H` | カーソルを列 `x`・行 `y` へ移動（1始まり） |
| `reset_cursor()` | `CSI H` | カーソルを左上 (1,1) へ移動 |
| `cursor_up(count)` | `CSI n A` | カーソルを `count` 行上へ移動 |
| `cursor_down(count)` | `CSI n B` | カーソルを `count` 行下へ移動 |
| `cursor_move_right(count)` | `CSI n C` | カーソルを `count` 列右へ移動 |
| `cursor_move_left(count)` | `CSI n D` | カーソルを `count` 列左へ移動 |
| `cursor_down_line_start(count)` | `CSI n E` | `count` 行下の行頭へ移動 |
| `cursor_up_line_start(count)` | `CSI n F` | `count` 行上の行頭へ移動 |
| `cursor_line_move(count)` | `CSI n G` | カーソルを現在行の `count` 列目へ移動 |
| `rq_cursor_pos()` | `CSI 6n` | カーソル位置をターミナルに問い合わせる（レスポンスは stdin に来る） |
| `save_cursor_pos()` | `CSI s` | 現在のカーソル位置を保存 |
| `remove_cursor_pos()` | `CSI u` | 保存したカーソル位置を復元 |

---

### 画面・行消去

| 関数 | エスケープシーケンス | 消去範囲 |
|---|---|---|
| `erase_cursor_to_end_of_screen()` | `CSI J` | カーソル位置から画面末尾まで |
| `erase_cursor_to_start_of_screen()` | `CSI 1J` | 画面先頭からカーソル位置まで |
| `erase_screen()` | `CSI 2J` | 画面全体 |
| `erase_scroll_buff()` | `CSI 3J` | スクロールバッファを含む全消去 |
| `erase_cursor_pos_to_line_end()` | `CSI K` | カーソルから行末まで |
| `erase_cursor_pos_to_line_start()` | `CSI 1K` | 行頭からカーソルまで |
| `erase_cursor_line()` | `CSI 2K` | カーソルのいる行全体 |

---

### 色・書式設定 (SGR)

#### 8色（標準色）

```c
// 前景色（文字色）
void change_str_rgb_foreground_color_start(enum eight_bit_rgb color);
void change_str_rgb_foreground_color_end(void);

// 背景色
void change_str_rgb_background_color_start(enum eight_bit_rgb color);
void change_str_rgb_background_color_end(void);
```

`_start` と `_end` で文字列を囲む。`_end` は SGR リセット (`\x1b[0m`) または背景色リセット (`\x1b[49m`) を出力する。

**使用例:**

```c
change_str_rgb_foreground_color_start(red);
myprint("エラー");
change_str_rgb_foreground_color_end();
```

#### True Color (24bit)

```c
// 前景色
void change_str_rgb_true_foreground_color_start(struct rgb rgb);
void change_str_rgb_true_foreground_color_end(void);

// 背景色
void change_str_rgb_true_background_color_start(struct rgb rgb);
void change_str_rgb_true_background_color_end(void);
```

**使用例:**

```c
struct rgb orange = {255, 165, 0};
change_str_rgb_true_foreground_color_start(orange);
myprint("オレンジ色のテキスト");
change_str_rgb_true_foreground_color_end();
```

---

### 文字出力

#### `void myprint(char *str)`

文字列をバッファに追記する。カーソル位置は変化しない（カーソルは文字数分進む）。

#### `void myprint_at(int x, int y, const char *str)`

`move_cursor(x, y)` + `myprint(str)` の組み合わせ。指定座標に直接文字列を書く。

---

### ターミナルモード

| 関数 | エスケープシーケンス | 効果 |
|---|---|---|
| `hidden_cursor()` | `CSI ?25l` | カーソルを非表示 |
| `show_cursor()` | `CSI ?25h` | カーソルを表示 |
| `change_buff_screen()` | `CSI ?47h` | オルタネートスクリーンへ切り替え |
| `back_forward_screen()` | `CSI ?47l` | 通常スクリーンへ戻る |
| `report_focus_event()` | `CSI ?1004h` | フォーカスイベント報告を有効化 |
| `not_report_focus_event()` | `CSI ?1004l` | フォーカスイベント報告を無効化 |
| `on_bracket_paste_mode()` | `CSI ?2004h` | ブラケットペーストモード有効化 |
| `off_bracket_paste_mode()` | `CSI ?2004l` | ブラケットペーストモード無効化 |

#### `void set_echo_mode(int enable)`

stdin のエコーモードを切り替える（POSIX `termios` を直接操作）。  
- `enable = 1`: エコーオン（入力が画面に表示される）  
- `enable = 0`: エコーオフ（raw モード的な入力処理向け）  

> `screen_push()` を経由しない、即時反映の設定変更。

---

### OSCコマンド（ウィンドウ名）

#### `void change_window_name(char *w_name)`

ターミナルのウィンドウタイトルとタブ名を両方変更する (`OSC 0`)。

#### `void change_win_tab_name(char *w_t_name)`

ターミナルのタブ名のみ変更する (`OSC 2`)。

---

### ターミナルサイズ取得

#### `void get_terminal_size(int *width, int *height)`

`ioctl(TIOCGWINSZ)` でターミナルの列数・行数を取得する。  
`ioctl` が失敗した場合は `width=80, height=24` にフォールバックする。

```c
int w, h;
get_terminal_size(&w, &h);
// w: 列数（横）, h: 行数（縦）
```

---

## 内部バッファについて

```c
static char   big[4096];
static size_t pos = 0;
```

全関数はこの 4096 バイトの静的バッファに `snprintf` で書き込む。  
バッファが満杯に近づいても警告は出ないため、`screen_push()` の呼び出し頻度に注意すること。  
マルチスレッド環境では同期が必要。

---

## 依存ライブラリ

| ヘッダ | 用途 |
|---|---|
| `<stdio.h>` | `snprintf`, `fwrite`, `fflush` |
| `<stdlib.h>` | （将来拡張用） |
| `<string.h>` | （将来拡張用） |
| `<termios.h>` | `set_echo_mode` |
| `<sys/ioctl.h>` | `get_terminal_size` |
| `<unistd.h>` | `STDIN_FILENO`, `STDOUT_FILENO` |
