gcc src/mybar.c src/wlr-layer-shell-unstable-v1-protocol.c src/xdg-shell-protocol.c -Iinclude -o mybar $(pkg-config --cflags --libs pango cairo pangocairo wayland-client)
