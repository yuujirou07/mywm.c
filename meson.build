project('mywm', 'c')

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')


# 依存関係
wlroots = dependency('wlroots-0.19')
wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
gdk_pixbuf = dependency('gdk-pixbuf-2.0')
libinput_dep = dependency('libinput')

# Wayland Scanner
wayland_scanner = find_program('wayland-scanner')
wayland_protocols = dependency('wayland-protocols')
protocols_dir = wayland_protocols.get_variable('pkgdatadir')
xdg_shell_xml = protocols_dir / 'stable/xdg-shell/xdg-shell.xml'

# xdg-shell ヘッダ・ソース生成
xdg_shell_h = custom_target(
  'xdg-shell-h',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

xdg_shell_c = custom_target(
  'xdg-shell-c',
  input: xdg_shell_xml,
  output: 'xdg-shell-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

wlr_protocols = dependency('wlr-protocols')
wlr_protocols_dir = wlr_protocols.get_variable('pkgdatadir')
layer_shell_xml = wlr_protocols_dir / 'unstable/wlr-layer-shell-unstable-v1.xml'

# layer-shell server-header (mywm 用)
layer_shell_server_h = custom_target(
  'layer-shell-server-h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

# layer-shell client-header・ソース生成 (mybar 用)
layer_shell_client_h = custom_target(
  'layer-shell-client-h',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-client-protocol.h',
  command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'],
)

layer_shell_c = custom_target(
  'layer-shell-c',
  input: layer_shell_xml,
  output: 'wlr-layer-shell-unstable-v1-protocol.c',
  command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
)

# ビルドディレクトリをインクルードパスに追加
xdg_shell_inc = include_directories('.')

# --- mybar (static library) ---
pango      = dependency('pango')
pangocairo = dependency('pangocairo')
cairo      = dependency('cairo')
wayland_client = dependency('wayland-client')

mybar_inc = include_directories('.', 'mybar_proj/include')

mybar_lib = static_library(
    'mybar',
    [
        'mybar_proj/src/mybar.c',
        xdg_shell_c,
        layer_shell_c,
        layer_shell_client_h,
    ],
    include_directories: mybar_inc,
    dependencies: [
        pango,
        pangocairo,
        cairo,
        wayland_client,
    ],
)

executable(
    'mybar',
    ['mybar_proj/src/mybar.c', xdg_shell_c, layer_shell_c, layer_shell_client_h],
    include_directories: mybar_inc,
    dependencies: [
        pango,
        pangocairo,
        cairo,
        wayland_client,
    ],
)

# --- mywm ---
executable(
    'mywm',
    ['mywm.c', xdg_shell_c, xdg_shell_h, layer_shell_server_h],
    include_directories: [xdg_shell_inc, mybar_inc],
    dependencies: [
        wlroots,
        wayland_server,
        xkbcommon,
        gdk_pixbuf,
        libinput_dep,
    ],
    link_with: mybar_lib,
)

# --- txt_editor ---
ncursesw_dep = dependency('ncursesw')
txt_editor_inc = include_directories('my_txt_editor/include')
executable(
    'txt_editor',
    [
        'my_txt_editor/src/main.c',
        'my_txt_editor/src/txt_editor_func.c',
        'my_txt_editor/src/my_cui_lib_linux.c',
    ],
    include_directories: txt_editor_inc,
    dependencies: [ncursesw_dep],
)
