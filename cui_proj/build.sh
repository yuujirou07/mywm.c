#!/bin/bash
set -e

cd "$(dirname "$0")"

gcc src/pty_make_v1.c src/mouse_io.c  src/pty_drawing.c src/kbd_io.c src/error_log_opt.c src/vulkan_otf_draw.c src/codepoint_comb.c \
    -I include $(pkg-config --cflags freetype2) \
    -o pty_make_v1 \
    -lglfw -lvulkan -lGL -lm -lpthread -ldl -lrt -lX11 \
    -lwayland-client -lwayland-cursor -lwayland-egl -lxkbcommon \
    -lfreetype
