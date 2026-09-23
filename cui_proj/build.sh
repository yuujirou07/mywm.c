#!/bin/bash
set -e

cd "$(dirname "$0")"

entry=src/pty_make_v1.c
output=pty_make_v1
if [ "${1:-}" = "--test" ]; then
    entry=test_raylib.c
    output=$(mktemp /tmp/cui-raylib-test.XXXXXX)
    trap 'rm -f "$output"' EXIT
fi

gcc "$entry" src/escape_sequence_parser.c src/mouse_io.c src/pty_drawing.c src/kbd_io.c src/error_log_opt.c \
    -I include $(pkg-config --cflags raylib) \
    -o "$output" \
    $(pkg-config --libs raylib) -lm

if [ "${1:-}" = "--test" ]; then
    "$output"
fi
