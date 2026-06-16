gcc mybar.c wlr-layer-shell-unstable-v1-protocol.c xdg-shell-protocol.c -o mybar $(pkg-config --cflags --libs pango cairo pangocairo) -lm $(pkg-config --cflags --libs wayland-client)
