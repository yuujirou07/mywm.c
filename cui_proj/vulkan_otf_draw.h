#ifndef VULKAN_OTF_DRAW_H
#define VULKAN_OTF_DRAW_H

#include "pty_make.h"

struct bmf_data {
    unsigned char* bitmap_buffer;
    unsigned int width;
    unsigned int height;
};

struct glyph_data {
    unsigned char *bitmap;  // グレースケール (0-255)、malloc済みコピーs
    int width, height;
    int bearing_x;   // FreeType の bitmap_left
    int bearing_y;   // FreeType の bitmap_top (ベースラインからグリフ上端まで)
    int advance_x;   // ピクセル単位の水平送り幅
};

int load_otf(char *file_path, struct pos font_size, struct bmf_data *bmf);
int load_otf_glyphs(const char *file_path, struct pos font_size,
                    struct glyph_data glyphs[128], int *ascender_out);
void free_otf_glyphs(struct glyph_data glyphs[128]);

#endif
