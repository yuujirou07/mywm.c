#!/bin/bash
set -e

cd "$(dirname "$0")"

# HiDPI環境でのスケール挙動を固定してから起動する
export WAYLAND_DISPLAY_SCALE=1
export GDK_SCALE=1
export QT_AUTO_SCREEN_SCALE_FACTOR=0

./pty_make_v1
