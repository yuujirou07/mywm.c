project('mywm', 'c')

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

# 依存関係
wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')

# Wayland Scanner
wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
protocols_dir = wayland_protocols.get_variable('pkgdatadir')
xdg_shell_xml = protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

# 1. xdg-shell ヘッダ生成
xdg_shell_h = custom_target(
  'xdg-shell-h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

# 2. xdg-shell ソース生成
xdg_shell_c = custom_target(
  'xdg-shell-c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# ビルドディレクトリをインクルードパスに追加
xdg_shell_inc = include_directories('.')

dependency('gdk-pixbuf-2.0')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')

# 実行ファイルの設定
# 依存関係の定義（これらはキーワード引数を持つ関数）
wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')

# 実行ファイルの定義
executable(
    'mywm',
    ['mywm.c', xdg_shell_c, xdg_shell_h],  # ← xdg_shell_c と xdg_shell_h を追加
    include_directories: xdg_shell_inc,
    dependencies: [
        wlroots,
        wayland_server,
        xkbcommon,
        gdk_pixbuf
    ],
)