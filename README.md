# mywm_proj

wlroots 0.19 を使った自作 Wayland compositor と、その上で動かすための bar / terminal / text editor をまとめたリポジトリです。

個人学習とポートフォリオ目的の実験プロジェクトです。API や内部構造は安定していません。

## 構成

```text
.
├── mywm.c              # wlroots ベースの Wayland compositor
├── meson.build         # mywm / mybar / txt_editor の Meson build
├── mybar_proj/         # layer-shell client の bar
├── cui_proj/           # GLFW + Vulkan + PTY の terminal
└── my_txt_editor/      # ncursesw ベースの text editor
```

## 主な機能

- `mywm`
  - xdg-shell の通常ウィンドウ管理
  - layer-shell による wallpaper / top bar の管理
  - server-side decoration
  - マウスによる移動・リサイズ
  - `Super + Enter` で `cui_proj/pty_make_v1` を起動
  - `Escape` で compositor を終了
- `mybar`
  - layer-shell top bar
  - Cairo / Pango による描画
- `cui_proj/pty_make_v1`
  - PTY 上で `bash -i` を起動
  - GLFW + Vulkan による文字描画
  - ANSI escape sequence の一部を解釈
- `my_txt_editor`
  - ncursesw ベースのテキストエディタ
  - ファイルブラウザ、保存、行ジャンプ、status bar
  - `my_txt_editor/my_txt_editor_settings.json` による一部設定変更

## 依存

開発環境には少なくとも次のライブラリとツールが必要です。

- Meson / Ninja
- wlroots 0.19
- wayland-server / wayland-client / wayland-protocols
- wlr-protocols
- xkbcommon
- libinput
- gdk-pixbuf-2.0 / glib
- cairo / pango / pangocairo
- ncursesw
- cJSON
- GLFW
- Vulkan
- OpenGL
- freetype2
- X11
- swaybg

## ビルド

トップレベルの Meson build で `mywm`、`mybar`、`txt_editor` をまとめてビルドします。

```sh
meson setup build
meson compile -C build
```

すでに `build/` がある場合は次だけで十分です。

```sh
meson compile -C build
```

## 起動

`mywm` は Wayland compositor なので、通常の Wayland client としてではなく compositor として起動します。

```sh
cd build
./mywm
```

`mywm` は起動後、出力検出時に `mybar` と `swaybg -i wp.jpg -m fill` を起動します。`Super + Enter` で `cui_proj/pty_make_v1` を起動します。

## 単体ビルド

### text editor

```sh
cd my_txt_editor
./build.sh
./main
```

### terminal

```sh
cd cui_proj
./build.sh
./run.sh
```

## 注意

- 絶対パスで起動している箇所が残っています。別環境へ移す場合は `mywm.c` 内の `pty_make_v1` fallback path や `mybar` fallback path を確認してください。
- `cui_proj/run.sh` は HiDPI 環境向けにいくつかの scale 系環境変数を固定してから起動します。
- `my_txt_editor/build.sh` は単体確認用です。トップレベルの Meson build では出力名が `txt_editor` になります。

## License & Copyright

Copyright (c) 2026 meridith
All rights reserved.

現在、このプロジェクトは個人的な学習およびポートフォリオ目的で公開しています。
作者の許可なくソースコードの改変、および改変したものの再配布を禁止します。
